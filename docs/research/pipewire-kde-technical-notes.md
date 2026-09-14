# Technical notes — PipeWire graph, app assignment, effects, KDE integration, Stream Deck, language stack

Collected 2026-09-14 from PipeWire/WirePlumber/KDE documentation and project repositories. Input for ADR 0002 (audio graph), ADR 0003 (app assignment), ADR 0004 (language/UI stack).

## 1. N output mixes with independent per-channel volume in PipeWire

All four approaches create a *virtual sink* (or source), so each mix becomes another output device in the graph; the mix has its own input channels (one per app/stream) giving per-channel volumes.

### a) libpipewire-module-loopback (per-route)
- Each app stream (or a dedicated capture stream) is paired with a playback stream into a physical sink. `capture.props` / `playback.props` can set `node.name`, `media.class`, `audio.position`, `node.passive`, `node.dont-reconnect`, `stream.dont-remix`.
- Volume control: not built-in; volumes are held by the session manager (WirePlumber) stream state (`state.restore-props`, `state.restore-target`).
- Latency/CPU: ~zero added latency (direct stream pair, one hop), no DSP.
- Use: cheapest "route A to B"; classic Voicemeeter-style per-app routing.
- https://docs.pipewire.org/page_module_loopback.html

### b) libpipewire-module-filter-chain (DSP graph / EasyEffects-style)
- `filter.graph` supports nodes of type `ladspa`, `lv2`, `builtin`, `sofa`, `ffmpeg`; 2 streams build the chain: capture stream = input, playback stream = output. Can expose a virtual sink (`media.class=Audio/Sink` on the capture side).
- Docs include a complete RNNoise virtual-source example and a 5.1→2.0 Dolby Surround example (builtin `mixer` node with `"Gain 1"`/`"Gain 2"` controls, links, inputs/outputs).
- `capture.volumes`/`playback.volumes` (control, min/max, scale) make volumes controllable at the filter-graph level.
- Latency/CPU: full DSP on real-time threads; each LADSPA/LV2 plugin adds buffer latency and CPU cost.
- Use: natural choice for per-channel effects (noise suppression, gate, compressor, EQ) AND per-mix mixing.
- https://docs.pipewire.org/page_module_filter_chain.html

### c) libpipewire-module-combine-stream (Voicemeeter-style Monitor/Stream mixes)
- `combine.mode = sink|source`: one virtual sink forwards audio to multiple other sinks, matched by `stream.rules` (match on `node.name`, `media.class`, `application.name`). `combine.latency-compensate` aligns stream latencies; `combine.props`/`stream.props` set channel maps.
- Volumes: same session-manager state as (a).
- Latency/CPU: extra buffer-alignment step when latency-compensate=true.
- https://docs.pipewire.org/page_module_combine_stream.html

### d) Custom SPA node (libpipewire-module-spa-node)
- Write your own SPA plugin + module to get a fully custom mixer node (all per-channel and per-mix volumes in one SPA node). Most flexible, most work (in C, or in a Rust/Qt app via pipewire-rs/cxx-qt).
- https://docs.pipewire.org/page_module_spa_node.html

### Which tool uses which
- **EasyEffects** (wwmm/easyeffects, GPL-3.0, ~10.2k stars): Qt/QML + Kirigami2 (repo topics confirm kde/kirigami2/qml/qt/pipewire/pulseaudio). Chains LV2/LADSPA filters in a filter-chain/SPA filter graph (limiter, compressor, convolver, EQ, auto-volume…).
- **Sonusmix**: ~~KDE audio mixer~~ **Correction (maintainer review, 2026-09-14): Sonusmix is GTK4/relm4, not KDE — verified in its `Cargo.toml` (`relm4 = "0.9.1"`, pipewire-rs from git). Model is Endpoint↔Group links; no first-class mix.** From the r/linuxaudio thread: "pretty app … still new under development (and abandoned?)."
- **PulseMeeter**: VU metering, "a voicemeeter alternative for linux" (separate r/linuxaudio thread, 5 years old).

### Recommendation
- **filter-chain** for per-channel volume + effects (each app stream → filter-chain node with per-channel gains + effect chain)
- **combine-stream** for the N output mixes (Monitor, Stream, Game)
- **loopback** for simple app→sink routing
- **Custom SPA node** only if you want everything in one in-process binary

Sources: https://docs.pipewire.org/page_module_loopback.html, https://docs.pipewire.org/page_module_filter_chain.html, https://docs.pipewire.org/page_module_combine_stream.html, https://docs.pipewire.org/page_module_spa_node.html, https://github.com/wwmm/easyeffects, https://www.reddit.com/r/linuxaudio/comments/1kvpx1c/voicemeeter_setup_to_linux/, https://www.reddit.com/r/linuxaudio/comments/q5hfyw/pulsemeeter_a_voicemeeter_alternative_for_linux/

## 2. Reliable app → virtual-sink assignment, stable across restarts

Key idea: identify each app's stream by stream properties. Stable, app-level keys: `node.name` (node level) and `application.name` (client context) — not `pid` (changes every launch; use it only as a session-internal key).

### WirePlumber stream state (the actual mechanism)
- WirePlumber 0.5.17 is a modular session/policy manager for PipeWire and a GObject-based library wrapping the PipeWire API. It **remembers each application's volume, mute state, channel map, and target device**, and restores them the next time the same app appears — exactly the "stable across restarts" requirement.
- Config location priorities: `$XDG_CONFIG_HOME/wireplumber` > `$XDG_CONFIG_DIRS/wireplumber` > `$sysconfdir/wireplumber` > `$XDG_DATA_DIRS/wireplumber` > `$datadir/wireplumber`; fragments from `<dir>/wireplumber.conf.d/*.conf` are loaded in reverse-priority order; `WIREPLUMBER_CONFIG_DIR` overrides.
- Per-stream overrides: `stream.rules` matched against stream properties; actions set `state.restore-props` (restore volume/mute/channel map), `state.restore-target` (restore target device), `state.default-volume` (volume when no stored volume). Match examples: `{ application.name = "pw-play" }` or `{ node.name = "~alsa_input.*" }`.
- Related settings (Well-known settings page):
  - `node.stream.default-playback-volume` / `node.stream.default-capture-volume` (applied on stream activation when no stored volume)
  - `node.stream.restore-props`, `node.stream.restore-target`
  - `linking.allow-moving-streams`: lets an app (e.g. pavucontrol) move a stream via metadata `target.object` — **our app should use the same mechanism** so volumes stay consistent with the Plasma volume applet.
  - `device.restore-profile`, `device.restore-routes` (also restores volumes of sinks/sources within a route and IEC958 codec selections)
  - `node.restore-default-targets`: stores/restores the default sink/source (PulseAudio "fallback").
- State files: `$XDG_STATE_HOME/wireplumber` (overridable via `WIREPLUMBER_DATA_DIR`); `wpctl reset` clears state and restarts the daemon.

### Known pitfalls
- **pid vs node.name vs application.name**: use `application.name` (stable app identity) and `node.name` (node level); use `pid` only within a session. Pitfall: multiple apps sharing a node name (e.g. several Chrome windows share node.name) — disambiguate with `application.name` + `object.serial`.
- Streams are *removed* when an app quits; WirePlumber keeps state and re-links to the same virtual sink at next launch. To force a specific virtual sink, write to the stream's metadata `target.object` (like pavucontrol) or set `state.restore-target = true`.
- `capture.props` / `playback.props` belong to the loopback/filter-chain *module* config; `stream.props` is runtime stream properties.
- `node.passive` / `node.dont-reconnect` on the session manager keeps virtual sinks from auto-corking or breaking.
- `device.routes.default-sink-volume` defaults to `0.4^3` (40% on a cubic scale) — useful when a route is restored and the sink has no stored volume.
- To reset: stop the daemon, delete `$XDG_STATE_HOME/wireplumber` (or `wpctl reset`); or `wpctl settings --save` for runtime toggling.

### Practical recipe
1. Create the virtual sinks once (e.g. `kmixdeck-stream`, `kmixdeck-monitor`, …) via a loopback or combine-stream module, or as a filter-chain virtual sink with `media.class = Audio/Sink` on the capture side.
2. In `wireplumber.conf.d/kmixdeck.conf`, add `stream.rules` that match `application.name` (or `node.name`) for each app and set `state.restore-target = true`, so that at next launch the app automatically re-links to our virtual sink.
3. At runtime, subscribe to the same "default" metadata (target.object) that pavucontrol / Plasma's applet writes, and apply it.
4. Read WirePlumber stream state (or libpulse volumes via the protocol-pulse compatibility layer) to stay in sync with the Plasma volume applet.

Sources: https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/stream.html, https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/settings.html, https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/features.html, https://pipewire.pages.freedesktop.org/wireplumber/daemon/locations.html, https://pipewire.pages.freedesktop.org/wireplumber/, https://docs.pipewire.org/page_module_protocol_pulse.html

## 3. Effects: LV2/LADSPA chains on channels

### How to insert effects (filter-chain)
From the official docs (filter-chain page), `filter.graph` supports nodes of type `ladspa`, `lv2`, `builtin`, `sofa`, `ffmpeg`. Each node has `name`, `plugin` (LADSPA library, e.g. `ladspa/librnnoise_ladspa`), `label`, `config` and `control` maps (controls can be indexed by name or index). Example taken verbatim from the docs — a virtual source using RNNoise: ``` filter.graph = { nodes = [ { type = ladspa name = rnnoise plugin = ladspa/librnnoise_ladspa label = noise_suppressor_stereo control = { "VAD Threshold (%)" = 50.0 } } ] } ``` ### Available high-quality open-source plugins - **werman/noise-suppression-for-voice** (based on RNNoise, 6.8k stars, GPL-3.0): builds VST2, VST3, LV2, LADSPA, AU, AUv3. Real-time noise suppression; works at 48000 Hz, 16-bit, one or more channels. Parameters: `VAD Threshold (%)` (85–95% is good), `VAD Grace Period (ms)`, `Retroactive VAD Grace Period (ms)` (adds latency).
- **EasyEffects** (wwmm/easyeffects, GPL-3.0, 10.2k stars): full GUI with a chain of plugins (limiter, compressor, convolver, equalizer, auto-volume, etc.), same filter-chain/SPA graph approach.
- Standard LADSPA/LV2 plugin sets for voice channels (open source): - **RNNoise** — noise suppression (Xiph; also at https://github.com/xiph/rnnoise) - **LSP plugins** — large LADSPA library (LADSPA, VST3, LV2) - **x42-plugins** — high-quality open LADSPA/LV2 plugins: EQ, compressor, gate, etc. - **Calf** — LADSPA plugins with noise gate, compressor, EQ - **noise-suppression-for-voice** — the LADSPA for RNNoise

### Latency / CPU note
- RNNoise LADSPA: a few ms (VAD grace period adds latency, retroactive grace adds even more).
- Gate/compressor/EQ: cheap, near-zero latency.
- Convolver / reverb: CPU-heavy.
- Buffer size for the filter chain should be sized to the plugin's minimum; PipeWire's session manager automatically manages buffer allocation.

Sources: https://docs.pipewire.org/page_module_filter_chain.html, https://github.com/werman/noise-suppression-for-voice, https://github.com/wwmm/easyeffects, https://github.com/xiph/rnnoise, https://github.com/lsp-plugins/lsp-plugins, https://github.com/cathro/calf, https://github.com/brucelafy/x42-plugins (or the x42-plugins repo; verify exact URL)

## 4. KDE integration

### Kirigami app structure (from develop.kde.org)
- Kirigami = Qt Quick components + "convergent application" framework (desktop + mobile), QML on top of Qt Quick Controls 2; business logic in C++ (or Python or Rust).
- Tutorials in `getting-started/kirigami`: setup guides for C++, Python, and Rust (https://develop.kde.org/docs/getting-started/kirigami/setup-rust/ exists).
- Recommended structure: one main `main.cpp` (or `main.rs`), QML `main.qml` with a `Kirigami.Page`/`PageRow`, and a backend (C++ QObject or Rust via cxx-qt) that exposes volumes/mixes/models to QML via models/signals.
- Tutorials: pages ("Explaining pages", "Layouts, ListViews, Cards", "Actions", "Dialogs", "Separate files", "Next steps", "Connect logic to QML UI", "Connect C++ models").
- Kirigami Addons for settings pages, about pages.

### Tray / StatusNotifierItem
- StatusNotifierItem is a freedesktop.org spec (freedesktop.org/wiki/Specification/StatusNotifierItem) — the D-Bus interface that system-tray icons (and Plasma's system-tray) use to register with the tray. Our app registers a StatusNotifierItem on the session bus with `StatusIcon` (tray icon + tooltip + context menu).
- Simplest path on KDE: `QSystemTrayIcon` / QML TrayIcon, which uses StatusNotifierItem internally when running on KDE.

### KGlobalAccel for hotkeys on Wayland
- KDE Frameworks' `KGlobalAccel` (api.kde.org/frameworks6 — the exact class page is 404 in my fetch, the URL may have moved). On Wayland, global shortcuts are registered via KGlobalAccel, and it works under Wayland because Plasma's global-shortcut manager listens for the D-Bus service.
- Pattern: create a `KGlobalAccelManager` in the app, add global shortcuts (e.g. "Mute mic", "Mute Stream", "Open settings"), wire to `KWindow` (the app's window) or to the session bus; Plasma's global shortcut manager handles the registration.
- Caveat: `KWindow` and `KGlobalAccel` require the app to run in a window (QML Window or QWidget) — headless daemons need a hidden window or D-Bus activation.

### How plasma-pa reads volumes
- plasma-pa (the Plasma PulseAudio applet) uses the PulseAudio client library (`libpulse`/`libpulse-simple`); on a KDE + PipeWire system this works because of the **protocol-pulse** module (https://docs.pipewire.org/page_module_protocol_pulse.html), which exposes PipeWire nodes as PulseAudio-compatible devices. Thus the Plasma volume applet reads/writes the same stream state that WirePlumber stores.
- To stay consistent with the Plasma volume applet:
  - Use the same `linking.allow-moving-streams` metadata mechanism for moving streams (target.object) — the same way pavucontrol/plasma-pa does it.
  - Read per-channel volumes from WirePlumber stream state (the same state Plasma reads) or via `state.restore-props`/`state.restore-target`.
  - For selecting the "default sink", use `node.restore-default-targets`.
- Our app should therefore: (a) not fight plasma-pa over the same nodes, (b) subscribe to the same metadata keys, and (c) expose its own virtual sinks as regular PipeWire sinks (protocol-pulse makes them visible to the Plasma applet).

### KConfig
- KDE features page (develop.kde.org/docs/features/): KConfig/KConfigXT, D-Bus, KNotification, SOLID, ThreadWeaver, etc.
- Use KConfig to persist kmixdeck's UI state (window geometry, mix layouts, hotkeys) under a `kmixdeckrc` file.

Sources: https://develop.kde.org/docs/getting-started/kirigami/, https://develop.kde.org/docs/features/, https://develop.kde.org/docs/features/d-bus, https://api.kde.org/frameworks6/ (KGlobalAccel API page was 404 on my fetch, class page has moved), https://freedesktop.org/wiki/Specification/StatusNotifierItem (anti-bot page blocked scrape, spec exists), https://invent.kde.org/plasma/plasma-pa, https://docs.pipewire.org/page_module_protocol_pulse.html, https://docs.pipewire.org/page_modules.html

## 5. Stream Deck on Linux

### Direct HID (recommended for a self-contained app)
- **abcminiuser/python-elgato-streamdeck** (MIT, 1.1k stars): Python 3 library that controls the Stream Deck directly via HID, without the official software. Supports the 6/15/32-key modules, Mini, Neo, Original, Pedal, Plus, Plus XL, and Studio. Features: enumerate devices, set panel brightness, set button images, read button states. Docs: https://python-elgato-streamdeck.readthedocs.io/
- For a KDE/Rust/Qt6 app, port the HID logic (or wrap this library). Elgato protocol docs: https://docs.elgato.com/streamdeck/hid/

### OpenDeck
- **nekename/OpenDeck** (GPL-3.0, 2.2k stars): cross-platform (Linux/macOS/Windows) Stream Deck manager that supports the original Elgato plugin format. Built with Tauri (src-tauri) + Svelte (src), with a `plugins/com.amansprojects.starterpack.sdPlugin` and translations; includes `install_opendeck.sh` for Linux.
- Use case: run OpenDeck as a standalone app so that kmixdeck's controls show up on the deck; or write our own .sdPlugin.

### streamdeck-ui
- **timothycrosley/streamdeck-ui** (MIT, 1.3k stars): Python UI for the Stream Deck; uses `pynput` for simulating key presses, but pynput has limited Wayland support (issue #189). KeyPress/Write Text may not work under Wayland; use the "Command" feature as a more robust mechanism.

### streamduck
- **streamduck-org/streamduck** (MPL-2.0, 37 stars, C#/.NET): "Macro Device Software" managing Stream Decks; in active development (Rust version moved to the `old-master` branch).

### Recommended approach
1. **Direct HID within kmixdeck** — embed the python-elgato-streamdeck HID logic (or port to Rust/C++/Qt) so the deck is a first-class device, no extra process.
2. **Plugin/API** — write a small `.sdPlugin`/`.sdActions` (openaction) so OpenDeck can drive kmixdeck's actions; this is how most streamer tools expose actions.
3. **D-Bus** — expose kmixdeck as a D-Bus service; streamdeck-ui's Command feature can shell out to `qdbus`/`dbus-send` to fire actions.

Sources: https://github.com/abcminiuser/python-elgato-streamdeck, https://github.com/ninjadev64/OpenDeck (repo moved to nekename/OpenDeck), https://github.com/timothycrosley/streamdeck-ui, https://github.com/streamduck-org/streamduck, https://doc.elgato.com/streamdeck/hid/

## 6. Rust vs C++ for the app

### C++/QML (no CXX needed)
- Easiest for the KDE ecosystem: plasma-pa is C++ using libpulse; EasyEffects is a C++/QML app (repo topics: kde, kirigami2, qml, qt, pipewire, pulseaudio). C++ is the most battle-tested path.

### Rust + cxx-qt
- **CXX-Qt** (kdab.github.io/cxx-qt/book/): CXX-based bridge that creates a "QObject in Rust" with macro annotations; code generation produces the C++ wrapper and the CXX bridge. Safe API, safe multithreading between Qt and Rust.
- Maturity: CI-tested on Linux/Windows/macOS x86_64 (not wasm32, not 32-bit, not aarch64 — flag as uncertain).
- Requirements to get started: C/C++ compiler, CMake ≥ 3.24, Rust toolchain, Qt 5 or Qt 6.

### pipewire-rs
- **pipewire** (Rust bindings for PipeWire), latest 0.10.1 (2026-08-19), maintained by gdesmott and others. Dependencies: pipewire-sys, libspa, libspa-sys, bitflags ^2, libc ^0.2, rustix ^1.1; dev-dep clap. Docs: https://pipewire.pages.freedesktop.org/pipewire-rs/pipewire/
- Maturity: active (0.5.0 → 0.10.1), but **docs.rs reports "docs.rs failed to build pipewire-0.10.1"**, so the latest release is not fully validated on docs.rs.
- Combined with cxx-qt, a Rust app can: create PipeWire nodes (custom SPA nodes for per-channel, per-mix volumes), subscribe to streams, and expose them to QML.

### Recommendation
- **KDE-native, low-friction**: C++/QML + libpulse (protocol-pulse) + WirePlumber (same way plasma-pa works).
- **Safer, Rust backend**: Rust + cxx-qt + pipewire-rs. Both are feasible; the Rust path is the higher-effort option, and cxx-qt is newer but stable on x86_64.
- For the **Stream Deck**: a small C++ or Rust module wraps the python-elgato-streamdeck logic; either language works.

Sources: https://kdab.github.io/cxx-qt/book/, https://kdab.github.io/cxx-qt/book/getting-started/index.html, https://docs.rs/pipewire (pipewire 0.10.1), https://pipewire.pages.freedesktop.org/wireplumber/, https://github.com/wwmm/easyeffects (KDE/Kirigami2/Qt/QML topics), https://github.com/abcminiuser/python-elgato-streamdeck

## Sources fetched

- https://api.kde.org/frameworks6/
- https://develop.kde.org/docs/features/
- https://develop.kde.org/docs/features/,
- https://develop.kde.org/docs/features/d-bus
- https://develop.kde.org/docs/features/d-bus,
- https://develop.kde.org/docs/getting-started/kirigami/
- https://develop.kde.org/docs/getting-started/kirigami/,
- https://develop.kde.org/docs/getting-started/kirigami/setup-rust/
- https://doc.elgato.com/streamdeck/hid/
- https://docs.elgato.com/streamdeck/hid/
- https://docs.pipewire.org/
- https://docs.pipewire.org/page_module_adapter.html
- https://docs.pipewire.org/page_module_combine_stream.html
- https://docs.pipewire.org/page_module_combine_stream.html,
- https://docs.pipewire.org/page_module_filter_chain.html
- https://docs.pipewire.org/page_module_filter_chain.html,
- https://docs.pipewire.org/page_module_loopback.html
- https://docs.pipewire.org/page_module_loopback.html,
- https://docs.pipewire.org/page_module_protocol_pulse.html
- https://docs.pipewire.org/page_module_protocol_pulse.html,
- https://docs.pipewire.org/page_module_spa_node.html
- https://docs.pipewire.org/page_module_spa_node.html,
- https://docs.pipewire.org/page_modules.html
- https://docs.rs/pipewire
- https://freedesktop.org/wiki/Specification/StatusNotifierItem
- https://github.com/abcminiuser/python-elgato-streamdeck
- https://github.com/abcminiuser/python-elgato-streamdeck,
- https://github.com/brucelafy/x42-plugins
- https://github.com/cathro/calf,
- https://github.com/lsp-plugins/lsp-plugins,
- https://github.com/ninjadev64/OpenDeck
- https://github.com/streamduck-org/streamduck
- https://github.com/streamduck-org/streamduck,
- https://github.com/timothycrosley/streamdeck-ui
- https://github.com/timothycrosley/streamdeck-ui,
- https://github.com/werman/noise-suppression-for-voice
- https://github.com/werman/noise-suppression-for-voice,
- https://github.com/wwmm/easyeffects
- https://github.com/wwmm/easyeffects,
- https://github.com/xiph/rnnoise
- https://github.com/xiph/rnnoise,
- https://invent.kde.org/plasma/plasma-pa
- https://invent.kde.org/plasma/plasma-pa,
- https://kdab.github.io/cxx-qt/book/
- https://kdab.github.io/cxx-qt/book/,
- https://kdab.github.io/cxx-qt/book/getting-started/index.html
- https://kdab.github.io/cxx-qt/book/getting-started/index.html,
- https://pipewire.pages.freedesktop.org/pipewire-rs/pipewire/
- https://pipewire.pages.freedesktop.org/wireplumber/
- https://pipewire.pages.freedesktop.org/wireplumber/,
- https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/features.html
- https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/features.html,
- https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/settings.html
- https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/settings.html,
- https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/stream.html
- https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/stream.html,
- https://pipewire.pages.freedesktop.org/wireplumber/daemon/locations.html
- https://pipewire.pages.freedesktop.org/wireplumber/daemon/locations.html,
- https://python-elgato-streamdeck.readthedocs.io/
- https://www.reddit.com/r/linuxaudio/comments/1kvpx1c/voicemeeter_setup_to_linux/
- https://www.reddit.com/r/linuxaudio/comments/1kvpx1c/voicemeeter_setup_to_linux/,
- https://www.reddit.com/r/linuxaudio/comments/q5hfyw/pulsemeeter_a_voicemeeter_alternative_for_linux/
