// Measure: N peak-detect capture streams (resample.peaks, 25 Hz) against the prototype graph.
// Reports: CPU of this process, samples/s received, and whether values track a known tone.
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/latency-utils.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <chrono>
#include <sys/resource.h>

struct Meter { pw_stream *stream; std::string target; float peak = 0; unsigned frames = 0; unsigned callbacks = 0; };
static void on_process(void *ud) {
    auto *m = static_cast<Meter *>(ud);
    pw_buffer *b = pw_stream_dequeue_buffer(m->stream); if (!b) return;
    auto *d = &b->buffer->datas[0];
    m->callbacks++;
    if (d->data) {
        unsigned n = d->chunk->size / sizeof(float); auto *f = static_cast<float *>(d->data);
        for (unsigned i = 0; i < n; i++) m->peak = std::max(m->peak, std::fabs(f[i]));
        m->frames += n;
    }
    pw_stream_queue_buffer(m->stream, b);
}
static pw_stream_events make_events() { pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS; e.process = on_process; return e; }
static const pw_stream_events events = make_events();

int main(int argc, char **argv) {
    pw_init(&argc, &argv);
    int rate = argc > 1 ? atoi(argv[1]) : 25; int seconds = argc > 2 ? atoi(argv[2]) : 5;
    std::vector<std::string> targets;
    for (const char *t : {"kmixdeck.channel.game", "kmixdeck.channel.system", "kmixdeck.channel.voice", "kmixdeck.mix.monitor", "kmixdeck.mix.stream"}) targets.push_back(t);
    // pad to N with repeats to simulate a big layout
    int N = argc > 3 ? atoi(argv[3]) : (int)targets.size();
    while ((int)targets.size() < N) targets.push_back(targets[targets.size() % 5]);
    auto *loop = pw_thread_loop_new("meter", nullptr); pw_thread_loop_lock(loop);
    auto *ctx = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    auto *core = pw_context_connect(ctx, nullptr, 0);
    std::vector<Meter *> meters;
    for (auto &t : targets) {
        auto *m = new Meter; m->target = t;
        auto *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Music",
            PW_KEY_STREAM_MONITOR, "true", PW_KEY_STREAM_CAPTURE_SINK, "true", PW_KEY_TARGET_OBJECT, t.c_str(),
            "resample.peaks", "true", PW_KEY_NODE_NAME, "kmixdeck.meter", PW_KEY_NODE_PASSIVE, "true", nullptr);
        pw_properties_setf(props, PW_KEY_NODE_RATE, "1/%d", rate);
        pw_properties_setf(props, PW_KEY_NODE_LATENCY, "1/%d", rate);
        m->stream = pw_stream_new(core, "meter", props);
        pw_stream_add_listener(m->stream, new spa_hook{}, &events, m);
        uint8_t buf[1024]; spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        spa_audio_info_raw info{}; info.format = SPA_AUDIO_FORMAT_F32; info.rate = rate; info.channels = 1;
        const spa_pod *params[1] = { spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info) };
        pw_stream_connect(m->stream, PW_DIRECTION_INPUT, PW_ID_ANY, (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), params, 1);
        meters.push_back(m);
    }
    pw_thread_loop_unlock(loop); pw_thread_loop_start(loop);
    rusage r0; getrusage(RUSAGE_SELF, &r0); auto t0 = std::chrono::steady_clock::now();
    sleep(seconds);
    rusage r1; getrusage(RUSAGE_SELF, &r1); double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double cpu = (r1.ru_utime.tv_sec - r0.ru_utime.tv_sec) + (r1.ru_utime.tv_usec - r0.ru_utime.tv_usec) / 1e6 + (r1.ru_stime.tv_sec - r0.ru_stime.tv_sec) + (r1.ru_stime.tv_usec - r0.ru_stime.tv_usec) / 1e6;
    pw_thread_loop_lock(loop);
    unsigned total = 0; for (auto *m : meters) total += m->frames;
    unsigned cbs = 0; for (auto *m : meters) cbs += m->callbacks;
    printf("streams=%zu rate=%d Hz wall=%.1fs cpu=%.3fs (%.2f%% of one core) callbacks=%u (%.1f/s/stream) samples=%u (%.1f/s/stream)\n", meters.size(), rate, wall, cpu, 100 * cpu / wall, cbs, cbs / wall / meters.size(), total, total / wall / meters.size());
    for (size_t i = 0; i < std::min<size_t>(5, meters.size()); i++) printf("  %-24s peak %.4f = %6.1f dBFS\n", meters[i]->target.c_str(), meters[i]->peak, meters[i]->peak > 0 ? 20 * log10(meters[i]->peak) : -INFINITY);
    pw_thread_loop_unlock(loop);
    return 0;
}
