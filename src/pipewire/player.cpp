// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "player.h"
#include "graph.h"
#include "../logging.h"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/ringbuffer.h>
#include <QDateTime>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace kmixdeck::pw {

namespace {
// 2^20 bytes = 131072 interleaved stereo float frames ≈ 2.7 s at 48 kHz. Power of two: spa_ringbuffer masks indices.
constexpr uint32_t kRingBytes = 1u << 20;
constexpr uint32_t kFrameBytes = 2 * sizeof(float);
constexpr int kIoBuffer = 32 * 1024;
// Plain audio containers only. Every demuxer that can follow a reference to another resource (hls, dash, concat,
// the image sequence demuxers, ...) is absent, and io_open below refuses any nested open anyway — the daemon must
// never fetch a URL or open a path because a client's bytes asked it to (ADR 0011, ADR 0016).
constexpr const char *kFormats = "wav,w64,aiff,flac,mp3,ogg,aac,mov,matroska";
constexpr qint64 kNegotiateTimeoutMs = 10000;   // linked + format negotiated, or the track fails
constexpr qint64 kDrainTimeoutMs = 4000;        // `drained` not seen → treat as played (never hang a queue)
}

struct Player::Track {
    QString channel, title;
    quint32 id = 0;
    int fd = -1;                       // owned
    bool seekable = false;
    bool running = false;              // Qt thread: stream created, worker started
    uint32_t movedNode = 0;            // node id whose target.object metadata we set (moveTo), cleared at the end
    qint64 startedMs = 0, drainSeenMs = 0;
    pw_stream *stream = nullptr;
    spa_hook listener{};
    std::thread worker;
    std::atomic<bool> stop{false};             // Qt → worker / read callback
    std::atomic<int> rate{0};                  // negotiated rate (param_changed), 0 = not yet
    std::atomic<bool> eof{false};              // worker: everything is in the ring
    std::atomic<bool> drainRequested{false};   // RT: ring empty at EOF, pw_stream_flush(drain) issued
    std::atomic<bool> drained{false};          // `drained` event seen
    std::atomic<bool> workerFailed{false};
    std::atomic<bool> streamFailed{false};
    std::mutex errMutex;
    QString error;                             // guarded by errMutex
    spa_ringbuffer rb{};
    std::vector<uint8_t> ring;

    void setError(const QString &why, std::atomic<bool> &flag) {
        { std::lock_guard<std::mutex> g(errMutex); if (error.isEmpty()) error = why; }
        flag.store(true, std::memory_order_release);
    }
    QString errorText() { std::lock_guard<std::mutex> g(errMutex); return error; }
};

namespace {
using Track = Player::Track;

// The custom read callback: poll() with a 100 ms timeout so a stalled pipe never blocks a stop for longer than that.
int readCb(void *opaque, uint8_t *buf, int size) {
    auto *t = static_cast<Track *>(opaque);
    for (;;) {
        if (t->stop.load(std::memory_order_relaxed)) return AVERROR_EXIT;
        pollfd p{t->fd, POLLIN, 0};
        const int r = ::poll(&p, 1, 100);
        if (r < 0) { if (errno == EINTR) continue; return AVERROR(errno); }
        if (r == 0) continue;
        const ssize_t n = ::read(t->fd, buf, size_t(size));
        if (n > 0) return int(n);
        if (n == 0) return AVERROR_EOF;
        if (errno == EINTR || errno == EAGAIN) continue;
        return AVERROR(errno);
    }
}

int64_t seekCb(void *opaque, int64_t offset, int whence) {
    auto *t = static_cast<Track *>(opaque);
    if (whence & AVSEEK_SIZE) { struct stat st {}; return ::fstat(t->fd, &st) == 0 ? int64_t(st.st_size) : AVERROR(errno); }
    whence &= ~AVSEEK_FORCE;
    const off_t r = ::lseek(t->fd, off_t(offset), whence);
    return r < 0 ? AVERROR(errno) : int64_t(r);
}

int refuseNestedOpen(AVFormatContext *, AVIOContext **, const char *url, int, AVDictionary **) {
    qCWarning(lcPipewire) << "playback: refused a nested open requested by the input:" << (url ? url : "");
    return AVERROR(EPERM);
}

QString avError(int e) { char b[AV_ERROR_MAX_STRING_SIZE] = {}; av_strerror(e, b, sizeof b); return QString::fromUtf8(b); }

/// Worker thread: demux + decode + convert into the ring, never touching the stream. Ends on EOF, error or stop.
void decodeWorker(Track *t) {
    auto fail = [t](const QString &why) { t->setError(why, t->workerFailed); };
    auto *iobuf = static_cast<unsigned char *>(av_malloc(kIoBuffer));
    AVIOContext *io = iobuf ? avio_alloc_context(iobuf, kIoBuffer, 0, t, readCb, nullptr, t->seekable ? seekCb : nullptr) : nullptr;
    if (!io) { av_free(iobuf); fail(QStringLiteral("out of memory")); return; }
    io->seekable = t->seekable ? AVIO_SEEKABLE_NORMAL : 0;
    struct IoGuard { AVIOContext **p; ~IoGuard() { if (*p) { av_freep(&(*p)->buffer); avio_context_free(p); } } } iog{&io};

    AVFormatContext *fc = avformat_alloc_context();
    if (!fc) { fail(QStringLiteral("out of memory")); return; }
    fc->pb = io;
    fc->flags |= AVFMT_FLAG_CUSTOM_IO;
    fc->io_open = refuseNestedOpen;
    fc->probesize = 1 << 20;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "format_whitelist", kFormats, 0);
    av_dict_set(&opts, "protocol_whitelist", "none", 0);
    const int op = avformat_open_input(&fc, "", nullptr, &opts);   // frees fc on failure
    av_dict_free(&opts);
    if (op < 0) {
        if (op != AVERROR_EXIT) fail(QStringLiteral("cannot decode the input (%1) — formats: wav, w64, aiff, flac, mp3, ogg, aac, mp4/m4a, matroska/webm").arg(avError(op)));
        return;
    }
    struct FcGuard { AVFormatContext **p; ~FcGuard() { if (*p) avformat_close_input(p); } } fcg{&fc};
    if (const int r = avformat_find_stream_info(fc, nullptr); r < 0) { if (r != AVERROR_EXIT) fail(QStringLiteral("no stream info (%1)").arg(avError(r))); return; }
    const int si = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (si < 0) { fail(QStringLiteral("the input has no audio stream")); return; }
    AVCodecParameters *par = fc->streams[si]->codecpar;
    const AVCodec *dec = avcodec_find_decoder(par->codec_id);
    if (!dec) { fail(QStringLiteral("no decoder for %1").arg(QString::fromUtf8(avcodec_get_name(par->codec_id)))); return; }
    AVCodecContext *cc = avcodec_alloc_context3(dec);
    if (!cc) { fail(QStringLiteral("out of memory")); return; }
    struct CcGuard { AVCodecContext **p; ~CcGuard() { if (*p) avcodec_free_context(p); } } ccg{&cc};
    if (avcodec_parameters_to_context(cc, par) < 0 || avcodec_open2(cc, dec, nullptr) < 0) { fail(QStringLiteral("cannot open the decoder")); return; }

    // Wait for the rate the graph negotiated (param_changed). The Qt side fails the track if it never comes.
    while (t->rate.load(std::memory_order_acquire) == 0) {
        if (t->stop.load(std::memory_order_relaxed)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    SwrContext *swr = nullptr;
    int outRate = 0;
    struct SwrGuard { SwrContext **p; ~SwrGuard() { if (*p) swr_free(p); } } swrg{&swr};
    AVChannelLayout stereo; av_channel_layout_default(&stereo, 2);
    auto buildSwr = [&]() -> bool {
        if (swr) swr_free(&swr);
        outRate = t->rate.load(std::memory_order_acquire);
        if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, outRate, &cc->ch_layout, cc->sample_fmt, cc->sample_rate, 0, nullptr) < 0
            || swr_init(swr) < 0) { fail(QStringLiteral("cannot convert %1 Hz to %2 Hz").arg(cc->sample_rate).arg(outRate)); return false; }
        return true;
    };
    if (!buildSwr()) return;

    quint64 total = 0;
    auto push = [&](const uint8_t *src, size_t bytes) -> bool {   // false = stopped
        while (bytes > 0) {
            if (t->stop.load(std::memory_order_relaxed)) return false;
            uint32_t widx;
            const int32_t filled = spa_ringbuffer_get_write_index(&t->rb, &widx);
            uint32_t n = std::min<uint32_t>(kRingBytes - uint32_t(std::max(filled, 0)), uint32_t(bytes));
            n -= n % kFrameBytes;
            if (n == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
            spa_ringbuffer_write_data(&t->rb, t->ring.data(), kRingBytes, widx & (kRingBytes - 1), src, n);
            spa_ringbuffer_write_update(&t->rb, widx + n);
            src += n; bytes -= n;
        }
        return true;
    };
    std::vector<float> chunk;
    auto convert = [&](AVFrame *f) -> bool {
        const int want = swr_get_out_samples(swr, f ? f->nb_samples : 0);
        if (want <= 0) return true;
        chunk.resize(size_t(want) * 2);
        uint8_t *dst[1] = { reinterpret_cast<uint8_t *>(chunk.data()) };
        const int got = swr_convert(swr, dst, want, f ? const_cast<const uint8_t **>(f->data) : nullptr, f ? f->nb_samples : 0);
        if (got <= 0) return true;
        total += quint64(got);
        return push(reinterpret_cast<const uint8_t *>(chunk.data()), size_t(got) * kFrameBytes);
    };

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frm = av_frame_alloc();
    struct PfGuard { AVPacket **p; AVFrame **f; ~PfGuard() { if (*p) av_packet_free(p); if (*f) av_frame_free(f); } } pfg{&pkt, &frm};
    if (!pkt || !frm) { fail(QStringLiteral("out of memory")); return; }
    for (;;) {
        const int r = av_read_frame(fc, pkt);
        if (r == AVERROR_EXIT) return;   // stopped
        if (r == AVERROR_EOF) break;
        if (r < 0) { fail(QStringLiteral("read error (%1)").arg(avError(r))); return; }
        if (outRate != t->rate.load(std::memory_order_acquire) && !buildSwr()) { av_packet_unref(pkt); return; }   // graph rate changed
        if (pkt->stream_index == si && avcodec_send_packet(cc, pkt) >= 0)
            while (avcodec_receive_frame(cc, frm) >= 0) if (!convert(frm)) { av_packet_unref(pkt); return; }
        av_packet_unref(pkt);
    }
    avcodec_send_packet(cc, nullptr);
    while (avcodec_receive_frame(cc, frm) >= 0) if (!convert(frm)) return;
    if (!convert(nullptr)) return;
    if (total == 0) { fail(QStringLiteral("the input decoded to no audio")); return; }
    t->eof.store(true, std::memory_order_release);
}

// ---- stream callbacks (PipeWire threads) ---------------------------------------------------------------------------
void onProcess(void *ud) {   // RT: copy from the ring, no allocation, no lock
    auto *t = static_cast<Track *>(ud);
    if (t->drainRequested.load(std::memory_order_relaxed)) return;
    // eof BEFORE the fill level: the worker writes its last bytes and then sets eof (release), so once eof reads true
    // the fill level read after it is final. The other order could see an old empty ring next to a fresh eof and cut
    // the tail.
    const bool eof = t->eof.load(std::memory_order_acquire);
    uint32_t ridx;
    const int32_t avail = spa_ringbuffer_get_read_index(&t->rb, &ridx);
    if (avail <= 0 && eof) {
        // Everything was handed over in earlier cycles, and the graph asking again means it took the last buffer.
        // Drain now, without queueing anything: `drained` then fires once that audio has left the stream.
        t->drainRequested.store(true, std::memory_order_release);
        pw_stream_flush(t->stream, true);
        return;
    }
    pw_buffer *b = pw_stream_dequeue_buffer(t->stream);
    if (!b) return;
    spa_data *d = &b->buffer->datas[0];
    uint32_t room = d->maxsize / kFrameBytes;
    if (b->requested) room = std::min<uint32_t>(room, uint32_t(b->requested));
    auto *dst = static_cast<uint8_t *>(d->data);
    if (dst) {
        uint32_t n = std::min<uint32_t>(avail > 0 ? uint32_t(avail) : 0u, room * kFrameBytes);
        n -= n % kFrameBytes;
        if (n) {
            spa_ringbuffer_read_data(&t->rb, t->ring.data(), kRingBytes, ridx & (kRingBytes - 1), dst, n);
            spa_ringbuffer_read_update(&t->rb, ridx + n);
        }
        std::memset(dst + n, 0, room * kFrameBytes - n);   // underrun (a stalled pipe) or the end: silence, never stale memory
    }
    d->chunk->offset = 0;
    d->chunk->stride = int32_t(kFrameBytes);
    d->chunk->size = room * kFrameBytes;
    pw_stream_queue_buffer(t->stream, b);
}

/// Same rule as the Sampler (DV-4): no fixed rate in EnumFormat, the graph tells us what it runs at.
void onParamChanged(void *ud, uint32_t id, const spa_pod *param) {
    auto *t = static_cast<Track *>(ud);
    if (id != SPA_PARAM_Format || !param) return;
    spa_audio_info_raw info{};
    if (spa_format_audio_raw_parse(param, &info) < 0 || info.rate == 0) return;
    t->rate.store(int(info.rate), std::memory_order_release);
}

void onStateChanged(void *ud, pw_stream_state, pw_stream_state state, const char *error) {
    auto *t = static_cast<Track *>(ud);
    if (state == PW_STREAM_STATE_ERROR) t->setError(QString::fromUtf8(error ? error : "stream error"), t->streamFailed);
}

void onDrained(void *ud) { static_cast<Track *>(ud)->drained.store(true, std::memory_order_release); }

pw_stream_events streamEvents() {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.process = onProcess;
    e.param_changed = onParamChanged;
    e.state_changed = onStateChanged;
    e.drained = onDrained;
    return e;
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
} // namespace

Player::Player(Graph *graph, QObject *parent) : QObject(parent), m_graph(graph) {
    // A garbage stream would otherwise log one line per broken packet to the journal — a write loop (disk wear).
    // The reason a track failed reaches the client through its result instead.
    av_log_set_level(AV_LOG_FATAL);
    m_reaper.setInterval(50);
    connect(&m_reaper, &QTimer::timeout, this, &Player::reap);
    // Same trap as Sampler/Meters: on EPIPE the core is still alive when disconnected() arrives, and a stream freed
    // without spa_hook_remove leaves a dangling listener. Tear everything down while the loop still exists.
    connect(graph, &Graph::disconnected, this, [this] { stopAll(QStringLiteral("error: PipeWire went away")); });
}

Player::~Player() { blockSignals(true); stopAll(); }

quint32 Player::enqueue(const QString &channel, int fd, const QString &title, QString *error) {
    auto fail = [&](const QString &why) -> quint32 { if (fd >= 0) ::close(fd); if (error) *error = why; return 0; };
    if (fd < 0) return fail(QStringLiteral("bad file descriptor"));
    if (!m_graph->isConnected()) return fail(QStringLiteral("not connected to PipeWire"));
    auto &q = m_queues[channel];
    if (q.size() >= kQueueLimit) {
        if (q.isEmpty()) m_queues.remove(channel);
        return fail(QStringLiteral("queue full: '%1' already holds %2 tracks (limit %2)").arg(channel).arg(kQueueLimit));
    }
    auto *t = new Track;
    t->channel = channel;
    t->id = m_nextId++;
    if (m_nextId == 0) m_nextId = 1;
    t->title = title.isEmpty() ? QStringLiteral("track %1").arg(t->id) : title;
    t->fd = fd;
    struct stat st {};
    t->seekable = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode);   // regular files and memfds; pipes are not
    t->ring.assign(kRingBytes, 0);
    spa_ringbuffer_init(&t->rb);
    q.append(t);
    if (q.size() == 1) startNext(channel);
    if (!m_reaper.isActive()) m_reaper.start();
    Q_EMIT changed(channel);
    return t->id;
}

void Player::startNext(const QString &channel) {
    auto it = m_queues.find(channel);
    if (it == m_queues.end()) return;
    if (it->isEmpty()) { m_queues.erase(it); return; }
    Track *t = it->first();
    if (t->running) return;
    t->running = true;
    t->startedMs = nowMs();
    const QString target = m_target ? m_target(channel) : QString();
    if (target.isEmpty() || !m_graph->isConnected() || !m_graph->threadLoop()) {
        t->setError(QStringLiteral("not connected to PipeWire"), t->streamFailed);   // the reaper ends it, never synchronously
        if (!m_reaper.isActive()) m_reaper.start();
        return;
    }
    const QByteArray node = QStringLiteral("kmixdeck.playback.%1").arg(channel).toUtf8();
    auto *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Production",
        PW_KEY_TARGET_OBJECT, target.toUtf8().constData(),
        PW_KEY_NODE_NAME, node.constData(),
        // Never the default sink: no fallback. NOT dont-reconnect — Mixer moves this stream onto a new FX entry
        // while it plays. linger: when the old entry vanishes during a chain swap, WirePlumber waits for the new
        // target instead of destroying the node ("defined target not found", find-defined-target.lua).
        "node.dont-fallback", "true", "node.linger", "true",
        // WirePlumber keys stream state by media.role first: without these, the target of THIS stream would be
        // remembered for every "Production" stream on the machine (state-stream.lua formKey).
        "state.restore-target", "false", "state.restore-props", "false",
        nullptr);
    pw_properties_set(props, PW_KEY_NODE_DESCRIPTION, QStringLiteral("kmixdeck playback: %1").arg(t->title).toUtf8().constData());
    pw_properties_set(props, PW_KEY_MEDIA_NAME, t->title.toUtf8().constData());

    pw_thread_loop_lock(m_graph->threadLoop());
    t->stream = pw_stream_new(m_graph->core(), "kmixdeck playback", props);
    if (!t->stream) {
        pw_thread_loop_unlock(m_graph->threadLoop());
        t->setError(QStringLiteral("cannot create a PipeWire stream"), t->streamFailed);
        if (!m_reaper.isActive()) m_reaper.start();
        return;
    }
    static const pw_stream_events ev = streamEvents();
    pw_stream_add_listener(t->stream, &t->listener, &ev, t);
    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.channels = 2;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = { spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info) };
    pw_stream_connect(t->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
                      params, 1);
    pw_thread_loop_unlock(m_graph->threadLoop());
    t->worker = std::thread(decodeWorker, t);
    if (!m_reaper.isActive()) m_reaper.start();
    qCInfo(lcPipewire).noquote() << QStringLiteral("playback: track %1 '%2' starts on %3 into %4").arg(t->id).arg(t->title, channel, target);
    Q_EMIT started(channel, t->id);
}

void Player::destroyStream(Track *t) {
    if (!t->stream) return;
    if (t->movedNode && m_graph->isConnected()) m_graph->clearStreamTarget(t->movedNode);
    if (auto *loop = m_graph->threadLoop()) {
        pw_thread_loop_lock(loop);
        spa_hook_remove(&t->listener);
        pw_stream_destroy(t->stream);
        pw_thread_loop_unlock(loop);
    }
    t->stream = nullptr;
}

void Player::finish(Track *t, const QString &result, bool startFollowing) {
    t->stop.store(true, std::memory_order_relaxed);
    destroyStream(t);                                   // no callback can run after this
    if (t->worker.joinable()) t->worker.join();         // ≤ ~100 ms: the read callback polls in 100 ms steps
    if (t->fd >= 0) { ::close(t->fd); t->fd = -1; }
    const QString ch = t->channel;
    const quint32 id = t->id;
    auto it = m_queues.find(ch);
    if (it != m_queues.end()) { it->removeOne(t); if (it->isEmpty()) m_queues.erase(it); }
    qCInfo(lcPipewire).noquote() << QStringLiteral("playback: track %1 '%2' on %3 ended: %4").arg(id).arg(t->title, ch, result);
    delete t;
    Q_EMIT ended(ch, id, result);
    if (startFollowing) startNext(ch);
    Q_EMIT changed(ch);
}

void Player::reap() {
    const qint64 now = nowMs();
    QList<QPair<Track *, QString>> done;
    for (auto it = m_queues.begin(); it != m_queues.end(); ++it) {
        if (it->isEmpty()) continue;
        Track *t = it->first();
        if (!t->running) continue;
        if (t->workerFailed.load(std::memory_order_acquire) || t->streamFailed.load(std::memory_order_acquire))
            done.append({t, QStringLiteral("error: ") + t->errorText()});
        else if (t->drained.load(std::memory_order_acquire))
            done.append({t, QStringLiteral("played")});
        else if (t->drainRequested.load(std::memory_order_acquire)) {
            if (!t->drainSeenMs) t->drainSeenMs = now;
            else if (now - t->drainSeenMs > kDrainTimeoutMs) done.append({t, QStringLiteral("played")});
        }
        else if (t->rate.load(std::memory_order_acquire) == 0 && now - t->startedMs > kNegotiateTimeoutMs)
            done.append({t, QStringLiteral("error: the channel did not accept the stream within %1 s").arg(kNegotiateTimeoutMs / 1000)});
    }
    for (const auto &d : done) finish(d.first, d.second, true);
    if (m_queues.isEmpty()) m_reaper.stop();
}

int Player::stop(const QString &channel, quint32 id) {
    auto it = m_queues.find(channel);
    if (it == m_queues.end()) return 0;
    if (id == 0) {
        const QList<Track *> all = *it;
        // waiting ones first, so ending the sounding one never starts a track that is about to be dropped
        for (int i = all.size() - 1; i >= 0; --i) finish(all[i], QStringLiteral("stopped"), false);
        return int(all.size());
    }
    for (Track *t : *it) if (t->id == id) { finish(t, QStringLiteral("stopped"), t->running); return 1; }
    return 0;
}

void Player::stopAll(const QString &result) {
    const auto keys = m_queues.keys();
    for (const auto &ch : keys) {
        const QList<Track *> all = m_queues.value(ch);
        for (int i = all.size() - 1; i >= 0; --i) finish(all[i], result, false);
    }
    m_reaper.stop();
}

QString Player::nowPlaying(const QString &channel) const {
    const auto it = m_queues.constFind(channel);
    return it == m_queues.constEnd() || it->isEmpty() ? QString() : it->first()->title;
}
QStringList Player::queue(const QString &channel) const {
    QStringList out;
    const auto it = m_queues.constFind(channel);
    if (it != m_queues.constEnd()) for (int i = 1; i < it->size(); ++i) out << it->at(i)->title;
    return out;
}
int Player::count(const QString &channel) const { return int(m_queues.value(channel).size()); }
bool Player::hasTrack(const QString &channel, quint32 id) const {
    for (const Track *t : m_queues.value(channel)) if (t->id == id) return true;
    return false;
}
uint32_t Player::streamNodeId(const QString &channel) const {
    const auto it = m_queues.constFind(channel);
    if (it == m_queues.constEnd() || it->isEmpty() || !it->first()->stream || !m_graph->threadLoop()) return 0;
    pw_thread_loop_lock(m_graph->threadLoop());
    const uint32_t id = pw_stream_get_node_id(it->first()->stream);
    pw_thread_loop_unlock(m_graph->threadLoop());
    return id == SPA_ID_INVALID ? 0 : id;
}
bool Player::moveTo(const QString &channel, const QString &targetNode) {
    const uint32_t id = streamNodeId(channel);
    if (!id || !m_graph->moveStream(id, targetNode)) return false;
    m_queues.value(channel).first()->movedNode = id;
    return true;
}
QStringList Player::channels() const { return m_queues.keys(); }

} // namespace kmixdeck::pw
