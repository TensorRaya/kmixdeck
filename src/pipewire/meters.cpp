// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "meters.h"
#include "graph.h"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <QMutex>
#include <QMutexLocker>
#include <cmath>

namespace kmixdeck::pw {

struct MeterStream {
    pw_stream *stream = nullptr;
    spa_hook listener{};
    QString target;
    float peak = 0.f;        // written on the RT/data thread, read+reset on publish (guarded by mutex)
};

struct Meters::Impl {
    Graph *graph;
    QMutex mutex;
    QHash<QString, MeterStream *> streams;

    static void onProcess(void *ud) {
        auto *m = static_cast<MeterStream *>(ud);
        pw_buffer *b = pw_stream_dequeue_buffer(m->stream);
        if (!b) return;
        const auto *d = &b->buffer->datas[0];
        if (d->data) {
            const unsigned n = d->chunk->size / sizeof(float);
            const auto *f = static_cast<const float *>(d->data);
            float p = m->peak;
            for (unsigned i = 0; i < n; i++) p = std::max(p, std::fabs(f[i]));
            m->peak = p;   // single writer; the publisher only reads+resets under the mutex, a lost update is one tick
        }
        pw_stream_queue_buffer(m->stream, b);
    }
    static pw_stream_events events() { pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS; e.process = onProcess; return e; }

    MeterStream *create(const QString &target) {
        auto *m = new MeterStream; m->target = target;
        auto *props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Music",
            PW_KEY_STREAM_MONITOR, "true", PW_KEY_STREAM_CAPTURE_SINK, "true",
            PW_KEY_TARGET_OBJECT, target.toUtf8().constData(),
            "resample.peaks", "true",
            PW_KEY_NODE_NAME, "kmixdeck.meter", PW_KEY_NODE_DESCRIPTION, "kmixdeck level meter",
            PW_KEY_NODE_PASSIVE, "true", PW_KEY_NODE_DONT_RECONNECT, "true", "node.dont-fallback", "true",
            nullptr);
        pw_properties_setf(props, PW_KEY_NODE_RATE, "1/%d", kRateHz);
        pw_properties_setf(props, PW_KEY_NODE_LATENCY, "1/%d", kRateHz);
        m->stream = pw_stream_new(graph->core(), "kmixdeck meter", props);
        static const pw_stream_events ev = events();
        pw_stream_add_listener(m->stream, &m->listener, &ev, m);
        uint8_t buf[1024]; spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        spa_audio_info_raw info{}; info.format = SPA_AUDIO_FORMAT_F32; info.rate = kRateHz; info.channels = 1;
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

Meters::Meters(Graph *graph, QObject *parent) : QObject(parent), d(new Impl{graph, {}, {}}) {
    m_tick.setInterval(1000 / kRateHz);
    connect(&m_tick, &QTimer::timeout, this, &Meters::publish);
    connect(graph, &Graph::disconnected, this, [this] {
        // streams died with the core; forget them (Graph recreates the core, setTargets() recreates streams)
        QMutexLocker l(&d->mutex);
        for (auto *m : d->streams) delete m;   // pw objects are gone with the core
        d->streams.clear(); m_tick.stop();
    });
}

Meters::~Meters() { setTargets({}); }

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

QStringList Meters::targets() const { QMutexLocker l(&d->mutex); return d->streams.keys(); }

void Meters::publish() {
    QHash<QString, float> out;
    {
        QMutexLocker l(&d->mutex);
        for (auto it = d->streams.cbegin(); it != d->streams.cend(); ++it) { out.insert(it.key(), it.value()->peak); it.value()->peak = 0.f; }
    }
    Q_EMIT peaks(out);
}

} // namespace kmixdeck::pw
