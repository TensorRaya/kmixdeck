# ADR 0006 — Level meters: peak streams in the daemon, one `Peaks` signal per tick, on demand

- Status: accepted
- Date: 2026-09-15
- Resolves: ADR 0005 open question Q-6; requirement UX-6

## Context

Frontends want VU meters on every channel and mix (UX-6 — the top Linux wish in the user research).
Meters need audio samples; ADR 0005 says frontends must **not** be PipeWire clients (AR-6). So either the
daemon measures and ships numbers over the bus, or the rule gets an exception. Decide by measurement, not
taste.

## Measurements (2026-09-15, headless VM, PipeWire 1.6.2, prototype graph, test tone −41.1 dBFS in `game`)

**Getting peaks out of PipeWire.** Same mechanism pipewire-pulse uses for `PA_STREAM_PEAK_DETECT`
(`pulse-server.c`: `stream.monitor = true` + the `resample.peaks` converter mode, `resample-peaks.c`): a
capture stream at *rate 25 Hz, 1 channel, F32* whose resampler outputs the peak of each window instead of a
resampled signal. One tiny buffer per tick, no audio copied out of the graph.

| meter streams | tick | our process CPU | PipeWire daemon CPU | callbacks/s/stream | tracks tone |
|---|---|---|---|---|---|
| 5 | 25 Hz | 0.74 % of one core | 1 % (= idle baseline 1 %) | 24.6 | yes: −41.1 dBFS on game, monitor, stream; −∞ on system, voice |
| 24 | 25 Hz | 2.0 % | 1 % | 24.0 | yes |
| 24 | 60 Hz | 2.2 % | 1 % | 44.4 | yes |

Rate limiting works (a stream asked for 60 Hz gets ~44 — quantum-bound), peaks are exact (input −41.1 dBFS,
measured −41.1 dBFS, not smeared by resampling).

**Shipping them over D-Bus.** One `Peaks(a{sd})` signal per tick, 25 Hz, private bus, 1 emitter,
2 subscribed receivers (Python/GLib — a pessimistic stand-in for C++):

| nodes in the dict | emitter CPU | each receiver CPU | dbus-daemon CPU |
|---|---|---|---|
| 5 | 1.9 % | 0.74 % | 0.1 % |
| 24 | 3.8 % | 0.75 % | 0.3 % |

So a full 24-node layout metered at 25 Hz costs roughly **2 % (PipeWire side) + 4 % (bus, Python emitter) of one
core** in the daemon and under 1 % per frontend. A C++ emitter will be well below the Python figure. This is
the same order as pavucontrol's meters and far from a reason to break the architecture.

## Decision

1. **The daemon meters.** One peak stream per channel and per mix (`stream.monitor`, `resample.peaks`,
   1 ch F32 at the meter rate), attached only while at least one client has called `Levels.Subscribe()`;
   torn down a few seconds after the last `Unsubscribe()` or when the subscriber leaves the bus (watched via
   `NameOwnerChanged`). Idle daemon = zero meter cost.
2. **One signal per tick, not one per node.** `org.kmixdeck1.Levels.Peaks(a{sd} peaks)` on `/org/kmixdeck1`:
   node slug → peak in linear 0…1 (frontends convert to dB; −∞ stays 0). One dictionary per tick keeps the bus
   at 25 messages/s regardless of layout size. Slugs: `channel/<slug>`, `mix/<slug>`.
3. **Fixed 25 Hz** (`Levels.Rate` read-only property). Same as PulseAudio's meters; enough for a fader UI,
   cheap, and the measurement shows PipeWire will not deliver much more at the default quantum anyway.
4. **Frontends stay off PipeWire** (AR-6 holds). A Waybar module or a Stream Deck plugin gets meters with the
   same subscribe call as the Kirigami app.

## Consequences

- `kmixdeckd` gains a `Levels` interface (XML in `interfaces/`), a meter manager owning N `pw_stream`s, and a
  subscriber set keyed by unique bus name.
- Peaks are *per node*, not per cell. A cell meter is the channel's meter scaled by the cell volume — the
  frontend can draw that without more streams. If real per-cell metering is ever wanted, the same mechanism
  works on `kmixdeck.link.<c>.<m>` nodes (24 more streams ≈ +2 %).
- Test: subscribe → tone into `game` → signal carries ≈ −41 dB on `channel/game`, `mix/stream`, `mix/monitor`
  and 0 on the others; unsubscribe → meter nodes disappear from the graph.

## Rejected

- *Frontend opens its own PipeWire peak streams* (what plasma-pa does via PulseAudio). Cheapest per hop,
  but every frontend re-implements it, and a non-Qt frontend needs libpipewire — exactly what ADR 0005 avoids.
- *Properties + PropertiesChanged per node.* 24 signals per tick instead of 1; the standard property
  machinery is not designed for 600 changes/s.
- *Shared memory / a side socket for meter data.* Faster, but a second transport for a second-tier feature;
  the D-Bus figures do not justify it.
