// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "meters.h"
#include "graph.h"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <cmath>
#include <ebur128.h>

namespace kmixdeck::pw {

struct MeterStream {
    pw_stream *stream = nullptr;
    spa_hook listener{};
    QString target;
    std::atomic<float> peak{0.f};   // RT thread: max-accumulate; publisher: exchange(0) — no lost tick either way
    std::atomic<double> sumSq{0.0}; // RT thread: sum of squares since the last tick; publisher: exchange(0) → RMS (CH-7)
    std::atomic<unsigned> samples{0};
    std::atomic<unsigned> clipped{0}; // RT thread: samples at or above full scale since the last tick (CH-7 clip)
    int clipTicks = 0;              // publisher: clip indicator stays lit ≈ 1.5 s after the last clipped sample
    float shown = 0.f;              // UX-16 ballistics (publisher thread only): instant rise, hold, then ≈ 20 dB/s fall
    int holdTicks = 0;
    std::atomic<bool> seen{false};  // RT thread sets once the first buffer arrived; until then the key is NOT published
                                    // (a fresh stream reads 0 for a tick or two while PipeWire links it — that 0 is
                                    // "no data yet", not "silence", and showed up as a dropout under load)
};

// UX-18 (EBU R128 / ITU-R BS.1770): its own capture stream per mix, because a loudness meter needs the real
// channel layout — the peak meters fold to mono, and BS.1770 weights L/R separately before summing. libebur128
// keeps the 400 ms / 3 s windows and the gated integration itself; we only feed frames and read back.
// The analyser runs on the RT thread, so the state pointer is only ever touched there and under the mutex.
struct LoudnessStream {
    pw_stream *stream = nullptr;
    spa_hook listener{};
    QString target;
    ebur128_state *st = nullptr;
    std::atomic<bool> seen{false};
    uint32_t rate = 0;                 // DV-4: the rate the graph negotiated, not one we asked for
    QMutex feed;                       // RT thread feeds, publisher reads — ebur128 is not thread-safe
};

struct Meters::Impl {
    Graph *graph;
    QMutex mutex;
    QHash<QString, MeterStream *> streams;
    QHash<QString, LoudnessStream *> loud;   // UX-18
    QStringList wantedLoud;                  // UX-18: what SHOULD run, survives a core restart

    static void onLoudProcess(void *ud) {
        auto *l = static_cast<LoudnessStream *>(ud);
        pw_buffer *b = pw_stream_dequeue_buffer(l->stream);
        if (!b) return;
        spa_data *d = &b->buffer->datas[0];
        if (d->data && d->chunk->size) {
            const size_t frames = d->chunk->size / (sizeof(float) * 2);   // interleaved stereo
            QMutexLocker fl(&l->feed);
            if (l->st && frames) {
                ebur128_add_frames_float(l->st, static_cast<const float *>(d->data), frames);
                l->seen.store(true, std::memory_order_relaxed);
            }
        }
        pw_stream_queue_buffer(l->stream, b);
    }
    // DV-4: ebur128 is created here, not in createLoud — only now do we know the rate the graph actually
    // negotiated. BS.1770's K-weighting filters are rate-dependent, so the analyser MUST be built with the
    // real rate; hard-coding 48 kHz in EnumFormat forced a resampler into a 44.1 kHz session and broke the
    // daemon's "follows the session, forces nothing" contract (DV-4 red: "negotiated 48000 Hz in a 44.1 kHz
    // session"). Asking for the graph's rate instead means the state has to be built on first param_changed.
    static void onLoudParamChanged(void *ud, uint32_t id, const spa_pod *param) {
        auto *l = static_cast<LoudnessStream *>(ud);
        if (id != SPA_PARAM_Format || !param) return;
        spa_audio_info_raw info{};
        if (spa_format_audio_raw_parse(param, &info) < 0) return;
        if (info.rate == 0 || info.channels == 0) return;
        QMutexLocker fl(&l->feed);
        if (l->st && l->rate == info.rate) return;          // renegotiation to the same rate: keep the windows
        if (l->st) ebur128_destroy(&l->st);                  // rate really changed: a stale state would misread
        l->rate = info.rate;
        l->st = ebur128_init(info.channels, info.rate,
                             EBUR128_MODE_I | EBUR128_MODE_S | EBUR128_MODE_M | EBUR128_MODE_TRUE_PEAK);
    }
    static pw_stream_events loudEvents() {
        pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS;
        e.process = onLoudProcess; e.param_changed = onLoudParamChanged; return e;
    }

    LoudnessStream *createLoud(const QString &target) {
        auto *l = new LoudnessStream; l->target = target;
        const auto ni = graph->node(target);
        const bool isSink = ni && ni->mediaClass == QLatin1String("Audio/Sink");
        auto *props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Music",
            PW_KEY_STREAM_MONITOR, "true",
            PW_KEY_TARGET_OBJECT, target.toUtf8().constData(),
            PW_KEY_NODE_NAME, "kmixdeck.loudness", PW_KEY_NODE_DESCRIPTION, "kmixdeck loudness meter",
            PW_KEY_NODE_PASSIVE, "true", PW_KEY_NODE_DONT_RECONNECT, "true", "node.dont-fallback", "true",
            nullptr);
        if (isSink) pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
        // NO node.latency here, unlike the peak meters. Their 1920/48000 asks for one buffer per publish tick,
        // which is right for a peak meter: a missed buffer just means the ballistics hold. An R128 analyser
        // integrates over sliding windows and needs EVERY sample — with the forced quantum the readings came out
        // 20 LU low and jittery (measured 2026-09-19: M -29.7 / S -38.5 / I -70 for a verified -20 LUFS tone,
        // while the true peak, which does not integrate, was already spot on at -20.02 dBTP).
        l->stream = pw_stream_new(graph->core(), "kmixdeck loudness", props);
        static const pw_stream_events ev = loudEvents();
        pw_stream_add_listener(l->stream, &l->listener, &ev, l);
        uint8_t buf[1024]; spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        // DV-4: stereo (BS.1770 weights L/R separately before summing, so no mono fold here), but NO rate —
        // leaving info.rate at 0 means "the graph's rate" and keeps a resampler out of the path. The analyser
        // is created in onLoudParamChanged once the real rate is known; see the note there.
        spa_audio_info_raw info{}; info.format = SPA_AUDIO_FORMAT_F32; info.channels = 2;
        info.position[0] = SPA_AUDIO_CHANNEL_FL; info.position[1] = SPA_AUDIO_CHANNEL_FR;
        const spa_pod *params[1] = { spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info) };
        pw_stream_connect(l->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                          static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
                          params, 1);
        return l;
    }
    void destroyLoud(LoudnessStream *l) {
        spa_hook_remove(&l->listener);
        pw_stream_destroy(l->stream);
        { QMutexLocker fl(&l->feed); if (l->st) { ebur128_destroy(&l->st); l->st = nullptr; } }
        delete l;
    }

    static void onProcess(void *ud) {
        auto *m = static_cast<MeterStream *>(ud);
        pw_buffer *b = pw_stream_dequeue_buffer(m->stream);
        if (!b) return;
        const auto *d = &b->buffer->datas[0];
        if (d->data) {
            const unsigned n = d->chunk->size / sizeof(float);
            const auto *f = static_cast<const float *>(d->data);
            float p = 0.f; double sq = 0.0; unsigned clip = 0;
            for (unsigned i = 0; i < n; i++) { const float a = std::fabs(f[i]); p = std::max(p, a); sq += double(f[i]) * f[i]; if (a >= 1.0f) ++clip; }
            m->sumSq.fetch_add(sq, std::memory_order_relaxed); m->samples.fetch_add(n, std::memory_order_relaxed);
            if (clip) m->clipped.fetch_add(clip, std::memory_order_relaxed);
            // max-accumulate against whatever the publisher has not consumed yet: two buffers in one tick keep the
            // louder one, a tick with no buffer keeps nothing (the publisher's exchange(0) then reads 0 and the
            // ballistics below hold the last value instead of dropping to silence)
            m->seen.store(true, std::memory_order_relaxed);
            float cur = m->peak.load(std::memory_order_relaxed);
            while (p > cur && !m->peak.compare_exchange_weak(cur, p, std::memory_order_relaxed)) {}
        }
        pw_stream_queue_buffer(m->stream, b);
    }
    static pw_stream_events events() { pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS; e.process = onProcess; return e; }

    MeterStream *create(const QString &target) {
        auto *m = new MeterStream; m->target = target;
        // Sinks (channels, mixes) are metered on their monitor ports (capture.sink). Everything that is itself an
        // output stream — cells, edges, app nodes (UX-13) — is captured directly: no capture.sink, or WirePlumber
        // looks for a sink of that name and finds none.
        const auto ni = graph->node(target);
        const bool isSink = ni && ni->mediaClass == QLatin1String("Audio/Sink");
        auto *props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Music",
            PW_KEY_STREAM_MONITOR, "true",
            PW_KEY_TARGET_OBJECT, target.toUtf8().constData(),
            // No resample.peaks: that mode hands us ONE peak sample per tick — fine for a peak meter, useless for RMS and
            // clip counting (CH-7: RMS came out equal to peak). We take the graph's own rate, mono
            // (channelmix folds to one), and reduce in onProcess: ~48k float multiply-adds per meter per second.
            PW_KEY_NODE_NAME, "kmixdeck.meter", PW_KEY_NODE_DESCRIPTION, "kmixdeck level meter",
            PW_KEY_NODE_PASSIVE, "true", PW_KEY_NODE_DONT_RECONNECT, "true", "node.dont-fallback", "true",
            nullptr);
        if (isSink) pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
        // quantum = one publish tick worth of samples at the graph rate (48000/25 = 1920); PipeWire may split it
        pw_properties_set(props, PW_KEY_NODE_LATENCY, "1920/48000");
        m->stream = pw_stream_new(graph->core(), "kmixdeck meter", props);
        static const pw_stream_events ev = events();
        pw_stream_add_listener(m->stream, &m->listener, &ev, m);
        uint8_t buf[1024]; spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        spa_audio_info_raw info{}; info.format = SPA_AUDIO_FORMAT_F32; info.channels = 1;   // rate: the graph's
        const spa_pod *params[1] = { spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info) };
        pw_stream_connect(m->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                          static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
                          params, 1);
        return m;
    }
    void destroy(MeterStream *m) {
        spa_hook_remove(&m->listener);
        pw_stream_destroy(m->stream);
        delete m;
    }
};

Meters::Meters(Graph *graph, QObject *parent) : QObject(parent), d(new Impl{graph, {}, {}, {}, {}}) {
    m_tick.setInterval(1000 / kRateHz);
    connect(&m_tick, &QTimer::timeout, this, &Meters::publish);
    connect(graph, &Graph::disconnected, this, [this] {
        // CH-4 SIGSEGV (root cause, measured 2026-09-19): this handler used to `delete` the stream objects
        // WITHOUT spa_hook_remove() + pw_stream_destroy(). "pw objects are gone with the core" is wrong about
        // the ORDER: Graph emits disconnected() from a QueuedConnection on the EPIPE, and Graph::teardown()
        // — which actually calls pw_core_disconnect() — runs afterwards. So at this point the core is still
        // there and each freed stream leaves its spa_hook registered in the core's listener list, pointing at
        // released memory. pw_core_disconnect() then walks that list and the daemon dies with SIGSEGV before
        // it can release its bus name, which is why test_ch4 read "service not reachable" in ten places for
        // one fault. Proven by markers: TD-4 reached, TD-5 (right after pw_core_disconnect) never — and with
        // no analyser present (loudness default off) the very same daemon walks through TD-5/TD-6 and recovers.
        //
        // Destroy them properly instead. The thread loop is alive here (teardown drops the core, not the loop),
        // so pw_stream_destroy() is legal and is what unregisters the hook.
        if (auto *loop = d->graph->threadLoop()) pw_thread_loop_lock(loop);
        {
            QMutexLocker lock(&d->mutex);
            for (auto *m : d->streams) { spa_hook_remove(&m->listener); pw_stream_destroy(m->stream); delete m; }
            // UX-18: the analyser state goes with it. `wantedLoud` is kept so a reconnect can rebuild without
            // the daemon having to notice — a PipeWire restart must not silently leave a mix without its meter.
            for (auto *ls : d->loud) {
                spa_hook_remove(&ls->listener);
                pw_stream_destroy(ls->stream);
                { QMutexLocker fl(&ls->feed); if (ls->st) { ebur128_destroy(&ls->st); ls->st = nullptr; } }
                delete ls;
            }
            d->streams.clear(); d->loud.clear();
        }
        if (auto *loop = d->graph->threadLoop()) pw_thread_loop_unlock(loop);
        m_tick.stop();
    });
}

Meters::~Meters() { setLoudnessTargets({}); setTargets({}); }

void Meters::setTargets(const QStringList &nodeNames) {
    if (!d->graph->isConnected()) return;
    pw_thread_loop_lock(d->graph->threadLoop());
    {
        QMutexLocker l(&d->mutex);
        for (auto it = d->streams.begin(); it != d->streams.end();) {
            if (!nodeNames.contains(it.key())) { d->destroy(it.value()); it = d->streams.erase(it); } else ++it;
        }
        for (const auto &n : nodeNames)
            if (!d->streams.contains(n)) d->streams.insert(n, d->create(n));
    }
    pw_thread_loop_unlock(d->graph->threadLoop());
    if (d->streams.isEmpty()) m_tick.stop(); else if (!m_tick.isActive()) m_tick.start();
}

void Meters::setLoudnessTargets(const QStringList &nodeNames) {
    { QMutexLocker lock(&d->mutex); d->wantedLoud = nodeNames; }
    // Not connected yet (or the core just died): remember the wish and rebuild on Graph::connected. Creating
    // streams against a core that is coming up took the daemon down with it — CH-4 restarts PipeWire under a
    // running daemon, and layoutChanged fires while the graph is still gone (found 2026-09-19 by test_ch4).
    if (!d->graph->isConnected()) return;
    // An R128 analyser carries STATE: the 400 ms / 3 s windows and the gated integration are built up over
    // seconds. Recreating one resets all of that, so an unchanged target list must be a no-op — otherwise every
    // Subscribe() (which calls syncTargets()) wipes the readings of everyone already listening. Measured
    // 2026-09-19: a verified -20 LUFS tone read -20.0 while a second subscriber kept the analyser alive, and
    // -29.7 / -38.5 / -70 the moment `loudness --once` subscribed and rebuilt it mid-integration.
    {
        QMutexLocker l(&d->mutex);
        QStringList have = d->loud.keys(); have.sort();
        QStringList want = nodeNames; want.sort();
        if (have == want) return;
    }
    pw_thread_loop_lock(d->graph->threadLoop());
    {
        QMutexLocker l(&d->mutex);
        for (auto it = d->loud.begin(); it != d->loud.end();) {
            if (!nodeNames.contains(it.key())) { d->destroyLoud(it.value()); it = d->loud.erase(it); } else ++it;
        }
        for (const auto &n : nodeNames)
            if (!d->loud.contains(n)) d->loud.insert(n, d->createLoud(n));
    }
    pw_thread_loop_unlock(d->graph->threadLoop());
    // the loudness tick rides on the peak tick; a loudness-only setup still needs it running
    if (!d->loud.isEmpty() && !m_tick.isActive()) m_tick.start();
}

QStringList Meters::targets() const { QMutexLocker l(&d->mutex); return d->streams.keys(); }

// UX-16: meter ballistics live HERE so every frontend shows the same thing (CLI, KDE, tray).
//  - rise: instant to the new peak
//  - hold: kHoldTicks (≈ 320 ms) at the peak
//  - fall: kFallDbPerTick (20 dB/s at 25 Hz = 0.8 dB per tick), like a PPM (IEC 60268-18)
//  - a tick whose stream delivered no buffer (timer/graph phase drift) is NOT silence: the hold/fall continues.
void Meters::publish() {
    static constexpr int kHoldTicks = 8;
    static constexpr float kFallFactor = 0.912f;     // 10^(-0.8/20)
    static constexpr float kFloor = 1e-4f;           // −80 dBFS → 0
    static constexpr int kClipTicks = 38;            // ≈ 1.5 s at 25 Hz
    QHash<QString, float> out;
    {
        QMutexLocker l(&d->mutex);
        for (auto it = d->streams.cbegin(); it != d->streams.cend(); ++it) {
            MeterStream *m = it.value();
            if (!m->seen.load(std::memory_order_relaxed)) continue;
            const float raw = m->peak.exchange(0.f, std::memory_order_relaxed);
            if (raw >= m->shown) { m->shown = raw; m->holdTicks = kHoldTicks; }
            else if (m->holdTicks > 0) { m->holdTicks--; }
            else { m->shown *= kFallFactor; if (m->shown < kFloor) m->shown = 0.f; }
            out.insert(it.key(), m->shown);
            // CH-7: RMS over the tick's samples (a tick with no buffer repeats nothing — key absent), clip held
            const unsigned n = m->samples.exchange(0, std::memory_order_relaxed);
            const double sq = m->sumSq.exchange(0.0, std::memory_order_relaxed);
            if (n) out.insert(QStringLiteral("rms/") + it.key(), float(std::sqrt(sq / n)));
            if (m->clipped.exchange(0, std::memory_order_relaxed)) m->clipTicks = kClipTicks;
            if (m->clipTicks > 0) { out.insert(QStringLiteral("clip/") + it.key(), 1.f); m->clipTicks--; }
        }
    }
    Q_EMIT peaks(out);

    // UX-18: read the analysers back at the same rate. Momentary/short-term come straight from libebur128's
    // sliding windows; the integrated value is gated per BS.1770 and only becomes meaningful after ~1 s, which
    // ebur128 signals by returning -HUGE_VAL — that is "not yet", not "silence", so the key stays absent.
    QHash<QString, QVector<float>> lu;
    {
        QMutexLocker l(&d->mutex);
        for (auto it = d->loud.cbegin(); it != d->loud.cend(); ++it) {
            LoudnessStream *ls = it.value();
            if (!ls->seen.load(std::memory_order_relaxed)) continue;
            double m = 0, st = 0, i = 0, tpL = 0, tpR = 0;
            QMutexLocker fl(&ls->feed);
            if (!ls->st) continue;
            const bool okM  = ebur128_loudness_momentary(ls->st, &m) == EBUR128_SUCCESS && std::isfinite(m);
            const bool okS  = ebur128_loudness_shortterm(ls->st, &st) == EBUR128_SUCCESS && std::isfinite(st);
            const bool okI  = ebur128_loudness_global(ls->st, &i) == EBUR128_SUCCESS && std::isfinite(i);
            ebur128_prev_true_peak(ls->st, 0, &tpL);
            ebur128_prev_true_peak(ls->st, 1, &tpR);
            const double tp = std::max(tpL, tpR);
            // Clamp at the -70 LUFS floor instead of publishing whatever the filter computes below it.
            // isfinite() alone is NOT enough: libebur128 only promises -HUGE_VAL for true negative infinity, and
            // for near-silence it returns perfectly finite nonsense — measured 2026-09-22, the moment a 12 s test
            // tone ended: M went to -253.54, then -2432.19 dB, and those numbers travelled through the bus into
            // all four frontends. ffmpeg's ebur128 (the reference tool) does exactly this clamp: it prints
            // M:-163.2 for digital silence but reports I: -70.0 LUFS, the BS.1770 gate's absolute threshold.
            // -70 is already this codebase's "nothing yet" everywhere else, so one floor covers both cases.
            constexpr float kFloorLufs = -70.f;
            const auto floorAt = [](bool ok, double v) { return ok && v > kFloorLufs ? float(v) : kFloorLufs; };
            lu.insert(it.key(), QVector<float>{floorAt(okM, m), floorAt(okS, st), floorAt(okI, i),
                                               tp > 0 ? std::max(kFloorLufs, float(20.0 * std::log10(tp))) : kFloorLufs});
        }
    }
    if (!lu.isEmpty()) Q_EMIT loudness(lu);
}

} // namespace kmixdeck::pw
