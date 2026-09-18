# kmixdeck

**A streamer's audio mixer for Linux, built on PipeWire — with a KDE window, a web UI, a CLI and a Stream Deck plugin
on one service.**

Route every application into its own channel, build as many output mixes as you need (what *you* hear vs. what the *stream*
hears vs. …), set each channel's level independently per mix, put effects on anything, and drive it from whatever is in
reach — the desktop, a phone on the LAN, a shell script, a Stream Deck. Think Elgato Wave Link, for Linux, without the
hardware lock-in and without the artificial limits.

![kmixdeck — six mixes at 1280 px](docs/screenshots/six-mixes-1280.png)

*Qt 6 / Kirigami · PipeWire-native · GPL-3.0-or-later · v0.1*

## Why

There is no Wave Link / VoiceMeeter / Sonar equivalent on Linux. PipeWire has every primitive needed (virtual sinks,
loopbacks, per-app routing), but the tooling stops at patchbays (qpwgraph, Helvum) and volume controls (pavucontrol). The
one project that tried to fill the gap, [Sonusmix](https://codeberg.org/sonusmix/sonusmix), is unmaintained since 2025 and
never solved independent per-mix levels ([#72](https://codeberg.org/sonusmix/sonusmix/issues/72)).
[ADR 0001](docs/adr/0001-new-project-not-a-fork.md) says why this is a new project rather than a fork.

## What you get

- **Channels × mixes.** Channels (*Game*, *System*, *Voice*, …) as rows, mixes (*Monitor*, *Stream*, any number) as
  columns; one fader per intersection, levels that never leak between mixes. Measured on the wire, not assumed
  (−12.0 / +6.0 / −∞ dB, [ADR 0002](docs/adr/0002-audio-graph-loopback-matrix.md)).
- **Per-mix outputs and capture sources.** Each mix plays to one or several hardware outputs *and* is exposed as
  `kmixdeck.source.<mix>` for OBS, Discord, anything that records. A fallback output covers the unplugged headset.
- **Ports, not just devices.** A 32-in/32-out interface is 32 addressable ports: any port or pair can be a channel, a
  mix can go to `AUX3,AUX4`, a side can be bound left-only ([ADR 0009](docs/adr/0009-port-based-virtual-devices.md)).
- **Effects.** Ordered insert chains on channels and mixes — builtin plus LADSPA (noise suppression, gate, compressor),
  presets, bypass, live controls ([ADR 0008](docs/adr/0008-effects-filter-chain.md)).
- **Devices that survive real life.** Identified by stable `node.name`; unplug, replug, sleep, wake — routing and levels
  come back without a restart. Tested with every device vanishing at once.
- **Live meters everywhere.** Cells, apps, inputs, outputs — 25 Hz over one D-Bus signal that only runs while somebody
  looks ([ADR 0006](docs/adr/0006-level-meters-daemon-peaks.md)).
- **Kill every kmixdeck process, audio keeps flowing.** The graph is plain PipeWire objects from a config the service
  renders; PipeWire builds it at login on its own. The service is only the control plane.
- **One service, N frontends.** Window, tray, web UI, CLI and Stream Deck speak the same D-Bus contract; a change in one is
  in all the others within a frame. A feature counts as done only when every frontend has it
  ([ADR 0010](docs/adr/0010-one-backend-n-frontends.md)) — and a machine-checked requirements table enforces that.

## Install

Build dependencies: CMake ≥ 3.20, a C++20 compiler, Qt ≥ 6.6 (Core, Gui, Widgets, Qml, Quick, QuickControls2, Svg,
DBus), KDE Frameworks ≥ 6.0 (CoreAddons, Config, I18n, Kirigami, KirigamiAddons, QQC2DesktopStyle, IconThemes,
GlobalAccel, StatusNotifierItem, Notifications, DBusAddons, Crash), `libpipewire-0.3 ≥ 1.0`.
Runtime: PipeWire + WirePlumber (any distro of 2024 or later). Optional: `swh-plugins`/`rnnoise` for LADSPA effects;
`python3-gi` + `python3-websockets` for the web UI.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
sudo ninja -C build install
systemctl --user daemon-reload && systemctl --user enable --now kmixdeckd   # or let D-Bus start it on first use
kmixdeck-kde
```

The first start offers to set up defaults (*Monitor* → your speakers, *Voice* ← your mic, running apps by role). From then on
PipeWire builds the graph at every login from `~/.config/pipewire/pipewire.conf.d/kmixdeck.conf`, which the service keeps
in sync with your layout.

## Use it

| Frontend | Start | Docs |
|---|---|---|
| **Window + tray** (Plasma) | `kmixdeck-kde` | [docs/window.md](docs/window.md) |
| **Web UI** (phone, tablet, headless box) | `kmixdeck-web` / `kmixdeck-web --lan` / `systemctl --user enable --now kmixdeck-web` | [docs/web.md](docs/web.md) |
| **CLI** (scripts, tests) | `kmixdeck status`, `kmixdeck --json …` | [docs/cli.md](docs/cli.md) |
| **Stream Deck** (OpenDeck) | `kmixdeck streamdeck install` | [streamdeck/README.md](streamdeck/README.md) |
| **Your own** | D-Bus `org.kmixdeck1` | [docs/frontend-guide.md](docs/frontend-guide.md), contract in [`interfaces/`](interfaces/) |

Thirty seconds of CLI:

```sh
kmixdeck cell set game stream -12dB          # game quieter in the stream only
kmixdeck mix output stream alsa_output.usb-Focusrite_Scarlett-00.analog-stereo
kmixdeck app move firefox browser            # remembered for next time firefox starts
kmixdeck export > backup.json                # layout + every level
```

## How it fits together

```
applications ──▶ channel sinks ──(loopback = one fader per cell)──▶ mix sinks ──▶ headphones / USB / …
hardware inputs ─┘                                                            └▶ kmixdeck.source.<mix> → OBS
kmixdeckd ── owns the graph, renders it to PipeWire config, exports org.kmixdeck1 on the session bus
     ▲            ▲             ▲            ▲              ▲
  kmixdeck    kmixdeck-kde   kmixdeck-web   Stream Deck   your frontend
```

The long version with diagrams: [docs/architecture.md](docs/architecture.md). What it must do and how we know it does:
[docs/spec/requirements.md](docs/spec/requirements.md) — 102 numbered requirements, 101 proven by a named test, the count
written by the audit that fails the build otherwise. Why it is built this way: [docs/adr/](docs/adr/).

## Developing

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build
pip install pytest pulsectl                     # integration tests
sudo apt install pipewire wireplumber pipewire-pulse ffmpeg gettext chromium swh-plugins   # what the suites need
ctest --test-dir build --output-on-failure      # ~18 min serial, ~6 min with -j4: 140 integration tests against a
                                                # private PipeWire per suite, real audio measured, four frontends driven
ruff check .                                    # Python; the C++ build is -Werror
```

Every requirement row names its test; `ctest -R sot-audit` checks that and fails on a ✅ without proof. Read
[CONTRIBUTING.md](CONTRIBUTING.md) for the two rules (backend/API first; a feature is done when every frontend has it) and
the review that shaped v0.1: [docs/review/2026-09-18-v0.1-review.md](docs/review/2026-09-18-v0.1-review.md).

## Repository layout

```
src/                 kmixdeckd (service) · kmixdeck (CLI) · kmixdeck-kde (window, tray) · frontend/ (MixerClient lib) · qml/
web/                 kmixdeck-web (bridge) · static/ (the browser UI, plain ES modules)
streamdeck/          OpenAction plugin for OpenDeck
interfaces/          the D-Bus introspection XML — the API contract
docs/                window.md · web.md · cli.md · architecture.md · frontend-guide.md · spec/ · adr/ · review/
tests/               QTest units · pytest integration (private PipeWire sandbox per suite) · tools/
po/                  translations (English source, German)
data/                systemd user units, D-Bus service, desktop/metainfo/notifyrc
```

## Translations

English is the source language; German ships in `po/de/`. `sh tools/extract-messages.sh` refreshes the catalogs; `ctest`
fails on a stale `.pot` or an incomplete German one.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
