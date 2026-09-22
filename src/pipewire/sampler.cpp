// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "sampler.h"
#include "graph.h"
#include "../logging.h"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <atomic>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace kmixdeck::pw {

namespace {

/// Decoded sample: interleaved stereo float at `rate`. Stereo because the graph side is stereo and folding
/// or duplicating once here is cheaper than deciding per buffer on the RT thread.
struct Decoded {
    std::vector<float> frames;   // interleaved L,R,L,R…
    int rate = 0;
    QString error;
    size_t frameCount() const { return frames.size() / 2; }
};

/// Full decode into memory. A soundboard sample is a jingle, not an album — the longest thing anybody puts on
/// a stream deck button is a few seconds, and holding it as float stereo costs 8 bytes per frame (48 kHz ≈
/// 384 KB/s). Streaming from disk on the RT thread would mean file I/O in the audio callback, which is the one
/// thing you must not do there.
Decoded decodeAll(const QString &path, int limitSeconds = 600) {
    Decoded out;
    AVFormatContext *fc = nullptr;
    if (avformat_open_input(&fc, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        out.error = QStringLiteral("cannot open '%1' (unsupported format or unreadable)").arg(path);
        return out;
    }
    struct FcGuard { AVFormatContext **p; ~FcGuard() { if (*p) avformat_close_input(p); } } fcg{&fc};
    if (avformat_find_stream_info(fc, nullptr) < 0) { out.error = QStringLiteral("no stream info in '%1'").arg(path); return out; }
    const int si = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (si < 0) { out.error = QStringLiteral("'%1' has no audio stream").arg(path); return out; }

    AVCodecParameters *par = fc->streams[si]->codecpar;
    const AVCodec *dec = avcodec_find_decoder(par->codec_id);
    if (!dec) { out.error = QStringLiteral("no decoder for %1").arg(QString::fromUtf8(avcodec_get_name(par->codec_id))); return out; }
    AVCodecContext *cc = avcodec_alloc_context3(dec);
    if (!cc) { out.error = QStringLiteral("out of memory"); return out; }
    struct CcGuard { AVCodecContext **p; ~CcGuard() { if (*p) avcodec_free_context(p); } } ccg{&cc};
    if (avcodec_parameters_to_context(cc, par) < 0 || avcodec_open2(cc, dec, nullptr) < 0) {
        out.error = QStringLiteral("cannot open decoder for '%1'").arg(path); return out;
    }

    // Resample to stereo float at the FILE's rate. The graph's rate is not known here (it comes from
    // param_changed, and asking for a fixed rate is what broke DV-4) — the rate conversion happens later.
    AVChannelLayout stereo;
    av_channel_layout_default(&stereo, 2);
    SwrContext *swr = nullptr;
    if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, cc->sample_rate,
                            &cc->ch_layout, cc->sample_fmt, cc->sample_rate, 0, nullptr) < 0 || swr_init(swr) < 0) {
        if (swr) swr_free(&swr);
        out.error = QStringLiteral("cannot build resampler for '%1'").arg(path); return out;
    }
    struct SwrGuard { SwrContext **p; ~SwrGuard() { if (*p) swr_free(p); } } swrg{&swr};

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frm = av_frame_alloc();
    struct PfGuard { AVPacket **p; AVFrame **f; ~PfGuard() { if (*p) av_packet_free(p); if (*f) av_frame_free(f); } } pfg{&pkt, &frm};
    if (!pkt || !frm) { out.error = QStringLiteral("out of memory"); return out; }

    const size_t maxFrames = size_t(limitSeconds) * size_t(cc->sample_rate);
    std::vector<float> chunk;
    auto drain = [&](AVFrame *f) {
        const int want = swr_get_out_samples(swr, f ? f->nb_samples : 0);
        if (want <= 0) return;
        chunk.resize(size_t(want) * 2);
        uint8_t *dst[1] = { reinterpret_cast<uint8_t *>(chunk.data()) };
        const int got = swr_convert(swr, dst, want,
                                   f ? const_cast<const uint8_t **>(f->data) : nullptr, f ? f->nb_samples : 0);
        if (got <= 0) return;
        const size_t have = out.frames.size();
        const size_t add = size_t(got) * 2;
        if (out.frameCount() + size_t(got) > maxFrames) return;   // refuse to eat all of RAM on a mis-registered file
        out.frames.resize(have + add);
        std::copy(chunk.begin(), chunk.begin() + add, out.frames.begin() + have);
    };

    while (av_read_frame(fc, pkt) >= 0) {
        if (pkt->stream_index == si && avcodec_send_packet(cc, pkt) >= 0)
            while (avcodec_receive_frame(cc, frm) >= 0) drain(frm);
        av_packet_unref(pkt);
    }
    avcodec_send_packet(cc, nullptr);                     // flush the decoder
    while (avcodec_receive_frame(cc, frm) >= 0) drain(frm);
    drain(nullptr);                                       // flush the resampler

    out.rate = cc->sample_rate;
    if (out.frames.empty()) out.error = QStringLiteral("'%1' decoded to zero samples").arg(path);
    return out;
}

} // namespace

/// One sounding voice. `data` is only written before the stream is connected and after it is disconnected,
/// so the RT thread reads it without a lock; `pos` is the only thing both sides touch.
struct Voice {
    pw_stream *stream = nullptr;
    spa_hook listener{};
    QString name;
    quint32 id = 0;
    float gain = 1.0f;
    std::vector<float> data;            // interleaved stereo at `rate`
    int rate = 0;                       // rate of `data`
    std::atomic<size_t> pos{0};         // next frame index (RT thread advances)
    std::atomic<bool> done{false};      // RT thread sets on end of data; the reaper destroys it
};

struct Sampler::Impl {
    Graph *graph = nullptr;
    QMutex mutex;
    QHash<quint32, Voice *> voices;
    quint32 nextId = 1;
    QTimer reaper;

    static void onProcess(void *ud) {
        auto *v = static_cast<Voice *>(ud);
        pw_buffer *b = pw_stream_dequeue_buffer(v->stream);
        if (!b) return;
        spa_data *d = &b->buffer->datas[0];
        const uint32_t stride = sizeof(float) * 2;
        uint32_t room = d->maxsize / stride;
        if (b->requested) room = std::min<uint32_t>(room, uint32_t(b->requested));
        auto *dst = static_cast<float *>(d->data);
        const size_t total = v->data.size() / 2;
        size_t at = v->pos.load(std::memory_order_relaxed);
        uint32_t wrote = 0;
        if (dst) {
            for (; wrote < room && at < total; ++wrote, ++at) {
                dst[wrote * 2]     = v->data[at * 2]     * v->gain;
                dst[wrote * 2 + 1] = v->data[at * 2 + 1] * v->gain;
            }
            // Pad the rest of the buffer with silence rather than leaving stale memory audible.
            for (uint32_t i = wrote; i < room; ++i) { dst[i * 2] = 0.f; dst[i * 2 + 1] = 0.f; }
        }
        v->pos.store(at, std::memory_order_relaxed);
        d->chunk->offset = 0;
        d->chunk->stride = int32_t(stride);
        d->chunk->size = room * stride;
        pw_stream_queue_buffer(v->stream, b);
        if (at >= total) v->done.store(true, std::memory_order_release);
    }

    /// The graph tells us the rate it negotiated — only now can the sample be brought to it. Asking for a
    /// fixed rate instead is exactly what DV-4 punished in the loudness meter: a hard 48 kHz forces a
    /// resampler into a 44.1 kHz session and breaks the daemon's "follows the session" contract.
    static void onParamChanged(void *ud, uint32_t id, const spa_pod *param) {
        auto *v = static_cast<Voice *>(ud);
        if (id != SPA_PARAM_Format || !param) return;
        spa_audio_info_raw info{};
        if (spa_format_audio_raw_parse(param, &info) < 0) return;
        if (info.rate == 0 || int(info.rate) == v->rate || v->data.empty()) return;
        // Resample the whole voice once, here on the PipeWire thread but BEFORE any process() callback can
        // run for this stream (param_changed precedes the first buffer), so no lock is needed against the RT
        // thread — and no allocation happens in the audio callback either.
        AVChannelLayout stereo; av_channel_layout_default(&stereo, 2);
        SwrContext *swr = nullptr;
        if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, int(info.rate),
                                &stereo, AV_SAMPLE_FMT_FLT, v->rate, 0, nullptr) < 0 || swr_init(swr) < 0) {
            if (swr) swr_free(&swr);
            qCWarning(lcPipewire) << "sampler: cannot resample" << v->name << v->rate << "->" << info.rate;
            return;
        }
        const size_t inFrames = v->data.size() / 2;
        const size_t outMax = size_t(swr_get_out_samples(swr, int(inFrames))) + 64;
        std::vector<float> out(outMax * 2);
        const uint8_t *src[1] = { reinterpret_cast<const uint8_t *>(v->data.data()) };
        uint8_t *dst[1] = { reinterpret_cast<uint8_t *>(out.data()) };
        int got = swr_convert(swr, dst, int(outMax), src, int(inFrames));
        if (got > 0) {
            out.resize(size_t(got) * 2);
            v->data.swap(out);
            v->rate = int(info.rate);
        }
        swr_free(&swr);
    }

    static pw_stream_events events() {
        pw_stream_events e{};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.process = onProcess;
        e.param_changed = onParamChanged;
        return e;
    }

    /// Caller holds the thread loop lock.
    void destroy(Voice *v) {
        spa_hook_remove(&v->listener);
        pw_stream_destroy(v->stream);
        delete v;
    }
};

Sampler::Sampler(Graph *graph, QObject *parent) : QObject(parent), d(new Impl) {
    d->graph = graph;
    // The RT thread cannot tear down its own stream, so it only sets `done` and a timer in the Qt thread
    // reaps. 100 ms is inaudible for "the jingle ended" and keeps this off the audio path.
    d->reaper.setInterval(100);
    connect(&d->reaper, &QTimer::timeout, this, [this] {
        QVector<QPair<QString, quint32>> ended;
        bool idle = false;
        if (auto *loop = d->graph->threadLoop()) pw_thread_loop_lock(loop);
        {
            QMutexLocker l(&d->mutex);
            for (auto it = d->voices.begin(); it != d->voices.end();) {
                Voice *v = it.value();
                if (v->done.load(std::memory_order_acquire)) {
                    ended.append({v->name, v->id});
                    d->destroy(v);
                    it = d->voices.erase(it);
                } else ++it;
            }
            idle = d->voices.isEmpty();
        }
        if (auto *loop = d->graph->threadLoop()) pw_thread_loop_unlock(loop);
        for (const auto &e : ended) Q_EMIT finished(e.first, e.second);
        if (idle) d->reaper.stop();
    });
    // Same trap as in meters.cpp: on EPIPE the core is still alive when disconnected() arrives, and a stream
    // freed without spa_hook_remove leaves a dangling listener that kills the daemon inside
    // pw_core_disconnect() — SIGSEGV before it can release its bus name.
    connect(graph, &Graph::disconnected, this, [this] {
        if (auto *loop = d->graph->threadLoop()) pw_thread_loop_lock(loop);
        {
            QMutexLocker l(&d->mutex);
            for (auto *v : d->voices) d->destroy(v);
            d->voices.clear();
        }
        if (auto *loop = d->graph->threadLoop()) pw_thread_loop_unlock(loop);
        d->reaper.stop();
    });
}

Sampler::~Sampler() { stopAll(); delete d; }

Sampler::Probe Sampler::probe(const QString &path) {
    Probe p;
    if (!QFileInfo::exists(path)) { p.error = QStringLiteral("no such file: %1").arg(path); return p; }
    AVFormatContext *fc = nullptr;
    if (avformat_open_input(&fc, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        p.error = QStringLiteral("cannot open '%1' (unsupported format or unreadable)").arg(path);
        return p;
    }
    struct G { AVFormatContext **p; ~G() { if (*p) avformat_close_input(p); } } g{&fc};
    if (avformat_find_stream_info(fc, nullptr) < 0) { p.error = QStringLiteral("no stream info in '%1'").arg(path); return p; }
    const int si = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (si < 0) { p.error = QStringLiteral("'%1' has no audio stream").arg(path); return p; }
    AVCodecParameters *par = fc->streams[si]->codecpar;
    if (!avcodec_find_decoder(par->codec_id)) {
        p.error = QStringLiteral("no decoder for %1 — install the distro's codec package")
                      .arg(QString::fromUtf8(avcodec_get_name(par->codec_id)));
        return p;
    }
    p.rate = par->sample_rate;
    p.channels = par->ch_layout.nb_channels;
    // Prefer the stream's own duration; container duration can be absent for a raw stream.
    const AVStream *st = fc->streams[si];
    if (st->duration > 0 && st->time_base.den > 0) p.length = double(st->duration) * av_q2d(st->time_base);
    else if (fc->duration > 0) p.length = double(fc->duration) / AV_TIME_BASE;
    return p;
}

quint32 Sampler::play(const QString &name, const QString &path, const QString &targetNode, float gain, QString *error) {
    auto fail = [&](const QString &why) -> quint32 { if (error) *error = why; return 0; };
    if (!d->graph->isConnected()) return fail(QStringLiteral("not connected to PipeWire"));
    if (targetNode.isEmpty()) return fail(QStringLiteral("no target node"));

    Decoded dec = decodeAll(path);
    if (!dec.error.isEmpty()) return fail(dec.error);

    auto *v = new Voice;
    v->name = name;
    v->gain = gain;
    v->data = std::move(dec.frames);
    v->rate = dec.rate;

    auto *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Production",
        PW_KEY_TARGET_OBJECT, targetNode.toUtf8().constData(),
        PW_KEY_NODE_NAME, "kmixdeck.sample", PW_KEY_NODE_DESCRIPTION, "kmixdeck sample",
        PW_KEY_NODE_DONT_RECONNECT, "true", "node.dont-fallback", "true",
        nullptr);
    // The description carries the sample name so `pw-top`/`pw-cli` show which button is sounding.
    pw_properties_set(props, PW_KEY_NODE_DESCRIPTION, QStringLiteral("kmixdeck sample: %1").arg(name).toUtf8().constData());

    pw_thread_loop_lock(d->graph->threadLoop());
    v->stream = pw_stream_new(d->graph->core(), "kmixdeck sample", props);
    if (!v->stream) {
        pw_thread_loop_unlock(d->graph->threadLoop());
        delete v;
        return fail(QStringLiteral("cannot create stream"));
    }
    static const pw_stream_events ev = Impl::events();
    pw_stream_add_listener(v->stream, &v->listener, &ev, v);
    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    // No rate here on purpose (info.rate stays 0 = "the graph's rate"): the sample is converted to whatever
    // the session runs at, in onParamChanged. Stereo, matching the decode.
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.channels = 2;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = { spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info) };
    pw_stream_connect(v->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
                      params, 1);
    quint32 id = 0;
    {
        QMutexLocker l(&d->mutex);
        id = v->id = d->nextId++;
        d->voices.insert(id, v);
    }
    pw_thread_loop_unlock(d->graph->threadLoop());
    if (!d->reaper.isActive()) d->reaper.start();
    qCDebug(lcPipewire) << "sampler: playing" << name << "into" << targetNode << "voice" << id
                        << "frames" << (v->data.size() / 2) << "rate" << v->rate;
    return id;
}

int Sampler::stop(const QString &name, quint32 id) {
    QList<QPair<QString, quint32>> beendet;
    if (auto *loop = d->graph->threadLoop()) pw_thread_loop_lock(loop);
    {
        QMutexLocker l(&d->mutex);
        for (auto it = d->voices.begin(); it != d->voices.end();) {
            Voice *v = it.value();
            const bool hit = id ? (v->id == id) : (name.isEmpty() || v->name == name);
            if (hit) { beendet.append({v->name, v->id}); d->destroy(v); it = d->voices.erase(it); } else ++it;
        }
    }
    if (auto *loop = d->graph->threadLoop()) pw_thread_loop_unlock(loop);
    // Announce a stop exactly like the reaper announces a natural end — outside both locks, same as line 232.
    // Without this every UI kept showing "playing" forever after a manual stop: the daemon was silent, so
    // PropertiesChanged never fired and the web pad, the QML pad and the tray all stayed lit. Measured
    // 2026-09-22: CLI reported sounding=false while the web pad still carried the "sounding" class.
    for (const auto &e : beendet) Q_EMIT finished(e.first, e.second);
    return int(beendet.size());
}

void Sampler::stopAll() { stop(QString(), 0); }

QStringList Sampler::sounding() const {
    QMutexLocker l(&d->mutex);
    QStringList out;
    for (auto *v : d->voices) out << v->name;
    return out;
}

} // namespace kmixdeck::pw
