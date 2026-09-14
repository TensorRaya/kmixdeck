# kmixdeck

**A streamer's audio mixer for KDE Plasma, built on PipeWire.**

Route every application into its own channel, build as many output mixes as
you need (what *you* hear vs. what the *stream* hears vs. …), and set each
channel's level independently per mix. Think Elgato Wave Link — for Linux,
without the hardware lock-in and without the artificial limits.

> Status: **requirements & design phase.** No code yet. The specs live in
> [`docs/spec/`](docs/spec/) and are developed in the open — issues and PRs
> against the spec are as welcome as against code.

## Why

There is no Wave Link / VoiceMeeter / Sonar equivalent on Linux. PipeWire has
every primitive needed (virtual sinks, loopbacks, filter chains, per-app
routing), but the tooling stops at patchbays (qpwgraph, Helvum) and plain
volume controls (pavucontrol). The one project that tried to fill the gap,
[Sonusmix](https://codeberg.org/sonusmix/sonusmix), is unmaintained since 2025
and never solved independent per-mix levels
([#72](https://codeberg.org/sonusmix/sonusmix/issues/72)).

See [ADR 0001](docs/adr/0001-new-project-not-a-fork.md) for why this is a new
project rather than a fork, and [ADR 0002](docs/adr/0002-audio-graph-loopback-matrix.md)
for the proposed audio graph (to be validated with a prototype).

## Core ideas (short version — full spec in `docs/spec/`)

- **Channels**: virtual devices such as *Game*, *System*, *Voice*, *Music*,
  *Mic*. Applications are assigned to a channel once and stay there.
- **Mixes**: *any number* of output mixes, added with a `+`. Typical: *Monitor*
  (your headphones) and *Stream* (what OBS captures). Each mix has its own
  fader per channel. No "max 5 mixes".
- **Native KDE**: Qt 6 / Kirigami, tray integration, global shortcuts via
  KGlobalAccel (works on Wayland), consistent with the Plasma volume applet.
- **PipeWire-native**: no daemon of our own in the audio path; kmixdeck
  configures PipeWire graph objects and gets out of the way. Kill the app,
  the audio keeps flowing.
- **Effects** per channel (noise suppression, gate, compressor, EQ) via
  PipeWire filter-chain and open LV2 plugins.
- **Stream Deck / external control** through a documented local API.

## Repository layout

```
docs/spec/        requirements (what), numbered, testable
docs/adr/         architecture decision records (why)
docs/research/    source material: competitor feature inventories, user feedback
```

## License

GPL-3.0-or-later (KDE ecosystem standard). See [LICENSE](LICENSE).

## Status

- **Architecture validated** (ADR 0002): channel × mix matrix as PipeWire null sinks + loopbacks, one
  fader per cell, persisted by WirePlumber — proven acoustically (−12.0 dB / +6.0 dB / −∞ measured).
- **Stack decided** (ADR 0004): C++20, Qt 6, Kirigami, KDE Frameworks 6, `libpipewire` directly.
- **Builds and runs**: matrix UI with per-cell faders/mute, add channel / add mix at runtime.
- **Tested**: QTest units + acoustic integration tests against a private PipeWire daemon
  (`docs/spec/testing.md`). `ctest` is the merge gate; CI runs it on every push.

## Build

```sh
# Ubuntu 26.04 / Debian: see .github/workflows/ci.yml for the package list
cmake -S . -B build -G Ninja && ninja -C build
ctest --test-dir build --output-on-failure
./build/bin/kmixdeck
```

Try the audio graph **without the app** — it is plain PipeWire config:

```sh
cp prototype/kmixdeck-prototype.conf ~/.config/pipewire/pipewire.conf.d/
systemctl --user restart pipewire wireplumber
pactl list short sinks | grep kmixdeck     # 3 channels, 2 mixes; "kmixdeck.source.stream" for OBS
```
