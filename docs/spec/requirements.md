# Requirements — kmixdeck

Numbered, testable requirements. `MUST` / `SHOULD` / `MAY` as in RFC 2119.
Each requirement carries a source: `[owner]` = project owner's stated need,
`[wavelink #n]` = feature n in `docs/research/wavelink-feature-inventory.md` (Ln = documented limitation), `[users]` = recurring user feedback in `docs/research/user-feedback.md`,
`[platform]` = required by PipeWire/KDE integration.

Status legend: 📝 draft · 🔶 partly covered · ✅ **verified by an automated test in this repo** (VF-2; "agreed" alone is not a status — either it is tested or it is a draft)

---

## 1. Channels (inputs)

| ID | Requirement | Source | Status |
|---|---|---|---|
| CH-1 | The app MUST provide virtual audio channels (e.g. *Game*, *System*, *Voice*, *Music*, *Browser*) that appear to the desktop as ordinary output devices, selectable in any application and in the Plasma volume applet. | owner, wavelink | 📝 |
| CH-2 | Channels MUST be user-definable: create, rename, reorder, delete, choose icon/colour. No fixed set, no fixed count. | owner | 📝 |
| CH-3 | Physical inputs (microphones, capture cards, line-in, Bluetooth) MUST be usable as channels alongside virtual ones. | wavelink | 📝 |
| CH-4 | An application MUST be assignable to a channel from within the app (drag-and-drop or picker); the assignment MUST persist across app restarts, PipeWire restarts and reboots. | owner, wavelink | 📝 |
| CH-5 | New, never-seen applications MUST land on a user-chosen default channel (default: *System*). | owner | 📝 |
| CH-6 | Assignment MUST survive PipeWire renaming or re-creating an app's node (see Sonusmix #38); matching MUST NOT rely on volatile node IDs alone. | platform | 📝 |
| CH-7 | Each channel MUST have: mute, gain trim, level meter (peak + RMS), clip indicator. | wavelink | 📝 |
| CH-8 | Channels MAY be grouped/linked so one fader moves several channels. | wavelink | 📝 |
| CH-9 | Deleting a channel or mix MUST be undoable (Wave Link 3.2 added undo after user complaints). | wavelink #12 | 📝 |
| CH-10 | The app picker MUST group applications by category and show which channel each running app is on; apps without a recognisable PipeWire node (Sonusmix #37: mpv) MUST still be listed via their client/application name. | wavelink #10, users | 📝 |
| CH-11 | Unused/hidden physical devices SHOULD be hideable from the channel list without deleting them. | wavelink #14 | 📝 |

## 2. Mixes (outputs)

| ID | Requirement | Source | Status |
|---|---|---|---|
| MX-1 | The app MUST support **any number of output mixes** (1…n), added with a `+` action and removed individually. There MUST NOT be a hard-coded maximum. | owner | 📝 (design decision; test lands with runtime add/remove of mixes, issue #2) |
| MX-2a | Mute MUST be per cell (channel × mix): muting *Game* in *Stream* MUST NOT affect *Game* in *Monitor* nor *System* in *Stream*. | owner; test `test_ch4_mute_is_per_cell` | ✅ |
| MX-2 | Every mix MUST have its own fader (and mute) **per channel**. Changing *Game* in mix *Monitor* MUST NOT change *Game* in mix *Stream*. | owner; test `test_mx2_per_mix_level_is_independent`, `test_mx2_other_direction` (−12.0 dB / +6.0 dB measured) | ✅ |
| MX-3 | A mix MUST be routable to (a) a physical output device (headphones, speakers), (b) a virtual capture device that other software (OBS, Discord, a recorder) can pick as its input, or (c) both. | owner, wavelink; (b) covered by `test_graph_comes_up_from_config_alone` (`kmixdeck.source.stream`), (a)/(c) open | 🔶 |
| MX-4 | Default setup on first run SHOULD create two mixes: *Monitor* → default output device, *Stream* → virtual capture device. | owner | 📝 |
| MX-5 | Mixes MUST be nameable, reorderable, colour-coded; the UI MUST scale to ≥ 8 mixes without hiding faders. | owner | 📝 |
| MX-6 | Each mix MUST have a master fader, mute and meter. | wavelink | 📝 |
| MX-7 | Per-channel fader per mix MUST include a *link to another mix* toggle (e.g. "Stream follows Monitor for this channel") that can be broken at any time. | wavelink | 📝 |
| MX-8 | A mix MAY be duplicated as a starting point for a new mix. | owner | 📝 |
| MX-9 | A mix MUST be sendable to several hardware outputs at once (e.g. headphones + speakers). | wavelink #5 | 📝 |
| MX-10 | Muted mixes MUST be unmistakable in the UI (Wave Link: header turns red). | wavelink #8 | 📝 |

## 3. Microphone & effects

| ID | Requirement | Source | Status |
|---|---|---|---|
| FX-1 | Any channel MUST accept an ordered effects chain (insert). Minimum built-in set: noise suppression, noise gate, compressor, EQ, high-pass, limiter. | wavelink | 📝 |
| FX-2 | Effects MUST be implemented with open plugin standards available on Linux (LV2/LADSPA) through PipeWire filter-chain; the user MAY add any installed LV2 plugin. | platform | 📝 |
| FX-3 | Effects on a channel apply before the mix faders, so every mix hears the processed signal. | wavelink | 📝 |
| FX-4 | The mic channel SHOULD offer one-click presets (e.g. "Voice — clean", "Voice — broadcast") that the user can edit and save. | users | 📝 |
| FX-5 | Effects MUST be bypassable per effect and per chain without audio dropouts. | wavelink | 📝 |
| FX-6 | Effects MUST be available on mixes (output) as well as on channels (input) — e.g. a limiter on the Stream mix. | wavelink #37 | 📝 |
| FX-7 | Effect chains MUST be copyable between channels, including from channels whose device is currently absent. | wavelink #35 | 📝 |

## 4. Control & integration

| ID | Requirement | Source | Status |
|---|---|---|---|
| CT-1 | Global shortcuts (mute mic, mute channel X in mix Y, volume up/down, switch monitoring mix) MUST work on Wayland, via KGlobalAccel. | owner, platform | 📝 |
| CT-2 | A documented local control API (D-Bus and/or a small IPC protocol) MUST expose every fader, mute and mix switch so external controllers can drive the app. | owner, wavelink | 📝 |
| CT-3 | Stream Deck support MUST be provided on top of CT-2 — via an existing Linux Stream Deck host (OpenDeck / streamdeck-ui plugin) rather than our own HID stack. | owner | 📝 |
| CT-4 | The app MUST integrate with the Plasma system tray (StatusNotifierItem): quick mute, mix switch, level indication. | owner | 📝 |
| CT-5 | Volumes shown in kmixdeck and in the Plasma volume applet MUST agree (no two truths). | platform | 📝 |
| CT-6 | OBS SHOULD see each mix as a cleanly named capture device, and MAY additionally see each channel separately (for multi-track recording). | owner | 📝 |
| CT-7 | Settings backup/restore and export MUST be available; a corrupt entry MUST NOT invalidate the whole config (Wave Link 3.3 fixed exactly this). | wavelink #24, L9 | 📝 |

## 5. Devices & audio behaviour

| ID | Requirement | Source | Status |
|---|---|---|---|
| DV-1 | The app MUST NOT sit in the audio path as a process. Audio MUST keep flowing when the UI is closed or crashes. | owner; test `test_graph_comes_up_from_config_alone` (graph from config, no app process) | ✅ |
| DV-2 | Added latency per hop (channel → mix → device) MUST be ≤ one PipeWire quantum at the session's rate; the design MUST avoid unnecessary resampling. | owner | 📝 |
| DV-3 | Hot-plug: when the monitor output device disappears (headset unplugged, Bluetooth drops), the mix MUST fall back to a user-defined device and return automatically when it reappears. | users | 📝 |
| DV-4 | Sample rate and quantum SHOULD follow the PipeWire session; the app MUST NOT force its own rate. | platform | 📝 |
| DV-5 | Configuration MUST be plain files under `$XDG_CONFIG_HOME`, human-readable, diff-able, and MUST restore the full graph on login without user action. | owner | 📝 |
| DV-6 | Sleep/wake and device re-enumeration MUST NOT lose routing or require a restart (Wave Link 3.x release notes list repeated fixes here; VoiceMeeter forum: crackling after updates). | wavelink #44, users | 📝 |
| DV-7 | Virtual device identity (node.name) MUST stay stable across app updates so OBS/Discord keep their device selection (Wave Link L7: driver update changed device IDs). | wavelink L7; tests `test_dv7_levels_and_mute_survive_daemon_restart`, `test_dv7_state_is_keyed_by_stable_name_not_display_name` | ✅ |

## 5a. Architecture: service, CLI, frontends

| ID | Requirement | Source | Status |
|---|---|---|---|
| AR-1 | All mixer logic (layout, graph management, app routing, persistence, hotkey actions) MUST live in a background service (`kmixdeckd`) that runs without any UI. | owner 2026-09-14 | 📝 |
| AR-2 | The service MUST expose its full functionality over a documented, versioned IPC API on the session bus, so that any desktop environment or third party can build a frontend without linking our code. The API spec (introspection XML) MUST ship in the repo and be the contract; the KDE UI MUST use only this API. | owner | 📝 |
| AR-3 | A CLI (`kmixdeck`) MUST cover 100 % of the API: everything the UI can do, the CLI can do, scriptable, with `--json` output and stable exit codes. | owner | 📝 |
| AR-4 | The service MUST be D-Bus-activatable and run as a `systemd --user` unit bound to `pipewire.service` (`BindsTo=` + `After=`, `Restart=on-failure`), following `pipewire-pulse.service`. It MUST re-discover the graph after a PipeWire restart instead of dying. | platform (pipewire-pulse.service on Ubuntu 26.04) | 📝 |
| AR-5 | The KDE UI MUST remain functional if started before the service (it activates it via D-Bus) and MUST show a clear state when the service is gone. | owner | 📝 |
| AR-6 | Frontends MUST NOT need PipeWire access themselves; the service is the only PipeWire client. (Level meters are the exception to evaluate: see open question Q-6.) | owner | 📝 |
| AR-7 | The repository MUST document how to write a frontend (docs/frontend-guide.md): bus name, object model, one worked example (the CLI). | owner | 📝 |

## 6. UX

| ID | Requirement | Source | Status |
|---|---|---|---|
| UX-1 | Main view: channels as rows/columns against mixes as the other axis — one fader per (channel, mix) cell, visible at once. | owner | 📝 |
| UX-2 | A "what am I hearing" indicator MUST show which mix is currently routed to the user's headphones; switching MUST be one click. | wavelink | 📝 |
| UX-3 | First-run wizard SHOULD create default channels and mixes, detect the microphone and the default output, and assign running apps. | wavelink | 📝 |
| UX-4 | Full keyboard operability and screen-reader labels per KDE HIG. | platform | 📝 |
| UX-5 | Languages: English first; German second; translatable via KDE's i18n. | owner | 📝 |
| UX-6 | Level meters (VU) on every channel and every mix (top Linux wish: Sonusmix #20; Pulsemeeter has them). | users | 📝 |
| UX-7 | Volume sliders MUST use a logarithmic curve and show dB and percent. | wavelink #19, #20 | 📝 |

## 7. Verification (binding)

| ID | Requirement | Source | Status |
|---|---|---|---|
| VF-1 | Every acoustic requirement (levels, mute, routing, persistence) MUST be asserted by an automated test that plays and records audio against a **private** PipeWire daemon — never the user's session. Method and rules: `docs/spec/testing.md`. | owner; pattern from PipeWire `pwtest` and plasma-pa | ✅ |
| VF-2 | A requirement is marked ✅ only when a test in this repository asserts it; the test names the requirement ID, the requirement names the test. | owner | ✅ |
| VF-3 | Measurements that justify an architecture decision MUST live in the ADR with a reproducible script/test, not in prose. | owner | ✅ (ADR 0002) |
| VF-4 | `ctest` (unit + integration) MUST pass before merge; no sound card may be required to run it. | owner | ✅ |
| VF-5 | Audio measurements MUST use explicit port linking (`pw-link` by port name). `--target` auto-connect is forbidden in tests — it attached to the wrong port once and hid a real result. | lesson 2026-09-14 | ✅ |

## 8. Non-goals (for now)

- Windows/macOS ports.
- Being a DAW: no recording, no timeline.
- Replacing OBS's own mixer.
- Proprietary plugin formats (VST3 on Linux is possible but not a target for v1).
- Hardware-tied features (Wave XLR Pro hardware mixer, firmware updates, console connectivity) — we have no hardware.
- A marketplace. Presets are files; share them however you like.

## Open questions

- **Q-6 (level meters):** VU meters (UX-3) need audio samples. Options: (a) service computes peak/RMS per node and publishes at ~20 Hz over IPC, (b) frontends read `pw-stream` monitors themselves (breaks AR-6), (c) shared-memory ring. Decide with a measurement of D-Bus overhead at 20 Hz × (channels+mixes) properties.

Tracked as issues with label `question`. Initial list:

1. Should each mix expose a *virtual capture device* by default, or only on demand? (device clutter vs. convenience)
2. Per-channel *pan/balance* — needed?
3. How to present the (channel × mix) matrix on small windows.
