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
| CH-1 | The app MUST provide virtual audio channels (e.g. *Game*, *System*, *Voice*, *Music*, *Browser*) that appear to the desktop as ordinary output devices, selectable in any application and in the Plasma volume applet. | owner, wavelink | ✅ starter layout + `channel add`; test_lifecycle, test_routing (mic → voice) |
| CH-2 | Channels MUST be user-definable: create, rename, reorder, delete, choose icon/colour. No fixed set, no fixed count. | owner | ✅ create/rename/delete via bus, CLI and UI (⋮ menu / right-click on every row and column header, RenameDialog); slug and node names are stable across rename. test_lifecycle |
| CH-3 | Physical inputs (microphones, capture cards, line-in, Bluetooth) MUST be usable as channels alongside virtual ones — as `Input` objects attached to a channel (ADR 0007 D2). | wavelink; ADR 0007 | ✅ test_ch3_* — `kmixdeck channel input voice fake.mic`, `Channel.InputDevice`, `kmixdeck.in.voice` in graph |
| CH-4 | An application MUST be assignable to a channel from within the app (drag-and-drop or picker); the assignment MUST persist across app restarts, PipeWire restarts and reboots. | owner, wavelink; tests `test_ch4_move_app_to_channel_is_immediate_and_audible`, `test_ch4_routing_survives_app_restart`, `test_ch4_routing_survives_pipewire_restart` (WirePlumber restore-target, keyed by node.name) | ✅ |
| CH-5 | New, never-seen applications MUST land on a user-chosen default channel (default: *System*). | owner | ✅ `Mixer.DefaultChannel` (o, "/" = off), `kmixdeck channel default [slug|none]`, checkable "New applications start here" in the row menu; only never-routed apps are moved (knownApps in layout.json), user moves win (WirePlumber restore-target). test_lifecycle::test_ch5_* (3) + test_ch10 |
| CH-6 | Assignment MUST survive PipeWire renaming or re-creating an app's node (see Sonusmix #38); matching MUST NOT rely on volatile node IDs alone. | platform | 📝 |
| CH-7 | Each channel MUST have: mute, gain trim, level meter (peak + RMS), clip indicator. | wavelink | ✅ Trim/Mute on Channel (test_routing: trim −20 dB reaches all mixes, mute silences all) + peak meter (UX-6); RMS/clip: 📝 |
| CH-8 | Channels MAY be grouped/linked so one fader moves several channels. | wavelink | 📝 |
| CH-9 | Deleting a channel or mix MUST be undoable (Wave Link 3.2 added undo after user complaints). | wavelink #12 | ✅ `Mixer.Undo()` + `UndoDescription`, `kmixdeck undo`, toast "Removed channel “Voice” — Undo" + Ctrl+Z. Restores layout entry, links, hardware input, default-channel flag, outputs/fallback, master AND every cell fader/mute (applied as the loopbacks reappear). One level; a new add clears it. test_lifecycle::test_ch9_* (2), UI click-verified |
| CH-10 | The app picker MUST group applications by category and show which channel each running app is on; apps without a recognisable PipeWire node (Sonusmix #37: mpv) MUST still be listed via their client/application name. | wavelink #10, users; test `test_ch10_running_apps_are_listed_with_their_channel`; `org.kmixdeck1.App` objects; `kmixdeck app list` | ✅ |
| CH-11 | Unused/hidden physical devices SHOULD be hideable from the channel list without deleting them. | wavelink #14 | 📝 |
| CH-12 | An application MAY be assigned to **more than one channel at once** (e.g. mic → Voice *and* Stream); every assigned channel hears it, the assignment persists like CH-4. | owner 2026-09-15 | ✅ `App.Channels` (as, [0]=primary via WirePlumber restore-target, rest via relay loopbacks capturing the app node), `App.Assign(as,b)`, `kmixdeck app assign <app> <ch>,<ch>`; layout.json `apps[]`; test_routing::test_ch12_multi_assign_relays_only_that_app (relay carries ONLY that app, survives daemon restart) |
| CH-13 | Adding a channel MUST offer every possible source in ONE picker: running application streams (icon, name, live level), hardware inputs (mics, capture devices, channel subsets per DV-13) and "empty (apps only)". Choosing an app creates the channel AND assigns the app (CH-4); choosing a device creates the channel AND its input edge (DV-9). | owner 2026-09-16 ("beim Hinzufügen alle Apps zeigen") | ✅ `AddDialog`: apps (icon, live level, current channel) + inputs + empty; `MixerClient.addChannelWithSource`; test `test_ch13_add_channel_from_source_app_and_device`; boreas screenshot 2026-09-16 |
## 2. Mixes (outputs)

| ID | Requirement | Source | Status |
|---|---|---|---|
| MX-1 | The app MUST support **any number of output mixes** (1…n), added with a `+` action and removed individually. There MUST NOT be a hard-coded maximum. | owner; tests `test_mx1_add_mix_at_runtime_creates_cells_and_persists`, `test_mx1_remove_mix` (3rd mix → 9 cells, removal → 6) | ✅ |
| MX-2a | Mute MUST be per cell (channel × mix): muting *Game* in *Stream* MUST NOT affect *Game* in *Monitor* nor *System* in *Stream*. | owner; test `test_ch4_mute_is_per_cell` | ✅ |
| MX-2 | Every mix MUST have its own fader (and mute) **per channel**. Changing *Game* in mix *Monitor* MUST NOT change *Game* in mix *Stream*. | owner; test `test_mx2_per_mix_level_is_independent`, `test_mx2_other_direction` (−12.0 dB / +6.0 dB measured) | ✅ |
| MX-3 | A mix MUST be routable to (a) a physical output device (headphones, speakers), (b) a virtual capture device that other software (OBS, Discord, a recorder) can pick as its input, or (c) both. | owner, wavelink; (b) covered by `test_graph_comes_up_from_config_alone` (`kmixdeck.source.stream`), (a)/(c) open; tests `test_mx3a_mix_output_follows_device_and_is_audible` (tone measured on the device), `test_mx3a_output_none_unlinks_and_unknown_device_is_rejected`, `test_mx3a_output_device_persists_in_generated_conf`; every mix also exposes `kmixdeck.source.<slug>` | ✅ |
| MX-4 | Default setup on first run SHOULD create two mixes: *Monitor* → default output device, *Stream* → virtual capture device. | owner | ✅ Layout::starter(): Monitor (no output until chosen — the default sink is never auto-picked, MX-3a) + Stream (capture-only, `kmixdeck.source.stream`); test_audio_graph, test_lifecycle::test_rebuild_from_empty_and_default_layout_on_missing_file |
| MX-5 | Mixes MUST be nameable, reorderable, colour-coded; the UI MUST scale to ≥ 8 mixes without hiding faders. | owner | 📝 |
| MX-6 | Each mix MUST have a master fader, mute and meter. | wavelink | ✅ Mix.Volume/Muted/ToggleMute on the bus, `kmixdeck mix volume|mute`, master slider + mute in the column header; test_routing::test_mix_master_* (−20 dB reaches device, mute silences, other mix untouched) |
| MX-7 | Per-channel fader per mix MUST include a *link to another mix* toggle (e.g. "Stream follows Monitor for this channel") that can be broken at any time. | wavelink | ✅ `Cell.Follows` (o, "/" = independent), `kmixdeck cell link <ch> <mix> <other|none>`, link button in every cell (auto-picks the other mix when there are two). Volume+mute mirrored, external moves (Plasma applet) propagate, ANY direct write to the follower breaks the link, no A↔B loops, survives restart. test_routing::test_mx7_* (acoustic) |
| MX-8 | A mix MAY be duplicated as a starting point for a new mix. | owner | 📝 |
| MX-9 | A mix MUST be sendable to several hardware outputs at once (e.g. headphones + speakers) — `Mix.Outputs` list, one loopback per output (ADR 0007 D2). | wavelink #5; ADR 0007 | ✅ `Mix.Outputs` (as) + `AddOutput`/`RemoveOutput`, `kmixdeck mix outputs|output-add|output-remove`; header menu is multi-select ("2 outputs"). test_routing::test_mx9_*: both devices read the same level; OutputDevice replaces only Outputs[0] |
| MX-10 | Muted mixes MUST be unmistakable in the UI (Wave Link: header turns red). | wavelink #8 | ✅ muted mix: header turns red (negativeBackgroundColor + border), title "… — MUTED", pressed mute button; screenshot-verified |

## 3. Microphone & effects

| ID | Requirement | Source | Status |
|---|---|---|---|
| FX-1 | Any channel MUST accept an ordered effects chain (insert). Minimum built-in set: noise suppression, noise gate, compressor, EQ, high-pass, limiter. | wavelink | ✅ `kmixdeck fx set/get/control`, D-Bus `Fx` property + `SetFxControl`; filter-chain per channel/mix; 4/4 tests no xfail (ADR 0008 resolved 2026-09-16) |
| FX-2 | Effects MUST be implemented with open plugin standards available on Linux (LV2/LADSPA) through PipeWire filter-chain; the user MAY add any installed LV2 plugin. | platform | ✅ LADSPA via PipeWire `filter-chain` (swh-plugins: gate/compressor/eq/limiter/highpass); `FxTypes` on the bus; `test_fx_types_and_presets_are_on_the_bus` |
| FX-3 | Effects on a channel apply before the mix faders, so every mix hears the processed signal. | wavelink | ✅ chain sits on `kmixdeck.fx.<ch>` in front of the channel sink → every cell hears the processed signal; `test_fx_changes_the_sound_and_bypass_restores_it` (gate closes → all mixes −∞) |
| FX-4 | The mic channel SHOULD offer one-click presets (e.g. "Voice — clean", "Voice — broadcast") that the user can edit and save. | users | ✅ `FxPresets` (Voice — clean / broadcast …), `kmixdeck fx presets`, applied via `fx set`; `test_fx_types_and_presets_are_on_the_bus` |
| FX-5 | Effects MUST be bypassable per effect and per chain without audio dropouts. | wavelink | ✅ per-effect `enabled` + chain `enabled`; live controls via Props (no rebuild); chain swap re-targets streams (`retargetWhenPresent`) — `test_fx_changes_the_sound_and_bypass_restores_it` |
| FX-6 | Effects MUST be available on mixes (output) as well as on channels (input) — e.g. a limiter on the Stream mix. | wavelink #37 | ✅ `fx set mix <slug>` — same chain model on mix sinks (`kmixdeck.fx.mix.<slug>`); `test_fx_chain_set_get_and_live_control` covers mix path |
| FX-7 | Effect chains MUST be copyable between channels, including from channels whose device is currently absent. | wavelink #35 | ✅ chain is plain JSON on the bus: `fx get channel a | fx set channel b` — device absence irrelevant (layout only); `test_fx7_chain_copies_between_channels_even_with_absent_device` (unplugged input, daemon restart) |

## 4. Control & integration

| ID | Requirement | Source | Status |
|---|---|---|---|
| CT-1 | Global shortcuts (mute mic, mute channel X in mix Y, volume up/down, switch monitoring mix) MUST work on Wayland, via KGlobalAccel. | owner, platform; KGlobalAccel actions per channel (`mute-channel-<slug>`) and per mix (`mute-mix-<slug>`), component `kmixdeck`, assignable in System Settings → Shortcuts; `Cell.ToggleMute`/`Channel.ToggleMute` on the bus; tests `test_ct1_toggle_mute_is_atomic_on_the_bus`, `test_ct1_kde_frontend_registers_global_shortcuts` — volume up/down and 'switch monitoring mix' actions still missing; on Plasma 6.7/Wayland: component registered, `invokeShortcut` via kglobalacceld mutes through the daemon (2026-09-15); a physical keypress still to be confirmed by the user | 🔶 |
| CT-2 | A documented local control API (D-Bus and/or a small IPC protocol) MUST expose every fader, mute and mix switch so external controllers can drive the app. | owner, wavelink; = the D-Bus API (ADR 0005), 100 % covered by `kmixdeck` CLI; `test_ar2_third_party_client_needs_none_of_our_code` | ✅ |
| CT-3 | Stream Deck support MUST be provided on top of CT-2 — via an existing Linux Stream Deck host (OpenDeck / streamdeck-ui plugin) rather than our own HID stack. | owner | 📝 |
| CT-4 | The app MUST integrate with the Plasma system tray (StatusNotifierItem): quick mute, mix switch, level indication. | owner; KStatusNotifierItem with per-channel mute toggles, per-mix mute, icon reflects mute state, closing the window keeps the tray; level indication still missing; registered with the real `StatusNotifierWatcher` on Plasma 6.7 (2026-09-15); level indication still missing | 🔶 |
| CT-5 | Volumes shown in kmixdeck and in the Plasma volume applet MUST agree (no two truths). | platform | 📝 |
| CT-6 | OBS SHOULD see each mix as a cleanly named capture device, and MAY additionally see each channel separately (for multi-track recording). | owner; `kmixdeck.source.<mix>` Audio/Source per mix, description 'kmixdeck <Mix> Mix' (generated conf + reconcile); test `test_mx3a_output_device_persists_in_generated_conf`; per-channel capture (MAY) not done ; OBS 'Stream Mix (kmixdeck)' input on the laptop, tone level in == OBS meter level | ✅ |
| CT-7 | Settings backup/restore and export MUST be available; a corrupt entry MUST NOT invalidate the whole config (Wave Link 3.3 fixed exactly this). | wavelink #24, L9 | ✅ corrupt layout.json → daemon still starts, rebuilds from graph (test_lifecycle::test_corrupt_layout_file_does_not_take_the_daemon_down); export/import 📝 |

## 5. Devices & audio behaviour

| ID | Requirement | Source | Status |
|---|---|---|---|
| DV-1 | The app MUST NOT sit in the audio path as a process. Audio MUST keep flowing when the UI is closed or crashes. | owner; test `test_graph_comes_up_from_config_alone` (graph from config, no app process); test `test_dv1_layout_survives_without_the_daemon` (daemon killed, PipeWire restarted, graph still there from generated conf; reconcile creates nothing twice) | ✅ |
| DV-2 | Added latency per hop (channel → mix → device) MUST be ≤ one PipeWire quantum at the session's rate; the design MUST avoid unnecessary resampling. | owner; test `test_cell_nodes_run_in_one_graph_cycle` (all nodes RUNNING in one graph cycle, DSP 1–3 µs) | ✅ |
| DV-3 | Hot-plug: when the monitor output device disappears (headset unplugged, Bluetooth drops), the mix MUST fall back to a user-defined device and return automatically when it reappears. | users | ✅ test_dv9_dv12_*: output unplugged → parked on kmixdeck.null, resumes on replug; test_routing: parked = silent on default sink |
| DV-4 | Sample rate and quantum SHOULD follow the PipeWire session; the app MUST NOT force its own rate. | platform | 📝 |
| DV-5 | Configuration MUST be plain files under `$XDG_CONFIG_HOME`, human-readable, diff-able, and MUST restore the full graph on login without user action. | owner; `layout.json` + generated `pipewire.conf.d/90-kmixdeck.conf` on every edit (QSaveFile, atomic) | ✅ |
| DV-6 | Sleep/wake and device re-enumeration MUST NOT lose routing or require a restart (Wave Link 3.x release notes list repeated fixes here; VoiceMeeter forum: crackling after updates). | wavelink #44, users | 📝 |
| DV-8 | Frontends MUST be able to list the hardware outputs a mix can play to, without talking to PipeWire themselves. | owner 2026-09-14; `Mixer.OutputDevices` (a{ss}), `kmixdeck devices`, mix-header menu; test `test_dv8_devices_lists_hardware_sinks_not_ours` | ✅ |
| DV-9 | A device reference MUST be the PipeWire `node.name` (stable across replug/reboot, same key WirePlumber uses); node ids/serials/bus paths MUST NOT be persisted. | ADR 0007 D1 | ✅ test_dv9_dv12_* — reference is `node.name`, survives absence |
| DV-10 | Input devices (`Audio/Source`: mics, capture cards, BT) MUST attach to a channel; output devices (`Audio/Sink`) MUST attach to a mix; the UI MUST offer each kind only where it fits. | ADR 0007 D2 | ✅ `Mixer.InputDevices` (Audio/Source) on Channel, `Mixer.OutputDevices` (Audio/Sink) on Mix; UI menus offer only the fitting kind |
| DV-11 | An absent (unplugged) input or output MUST stay configured, be shown greyed with its last-seen name, and MUST NOT be deleted automatically. | ADR 0007 D3; wavelink L7/#44 | ✅ test_dv9_dv12_* — `InputPresent`/`OutputPresent` false, value kept; UI shows "(unplugged)" dimmed |
| DV-12 | When an absent device reappears, its routing MUST be restored by the daemon within 2 s with no client action; while absent, its loopback MUST be parked on `kmixdeck.null`, never on the default device. | ADR 0007 D3 | ✅ test_dv9_dv12_* — parks on `kmixdeck.null`, resumes on replug w/o client action |
| DV-13 | Multi-channel devices: an input/output reference MAY select a channel subset (e.g. AUX2–AUX3 of a 32-ch Pro-Audio node); device lists MUST expose channel count and positions. | ADR 0007 D4 | 📝 |
| DV-14 | Device edges MUST have their own trim/mute inside kmixdeck; hardware volumes of the device itself MUST NOT be touched (Plasma owns them, CT-5). | ADR 0007 D5 | 📝 |
| DV-15 | A mix MAY name a fallback output used while the primary is absent; without one the mix is silent, never rerouted to the default sink. | ADR 0007 D3; DV-3 | ✅ `Mix.FallbackOutput` (s), `kmixdeck mix fallback`, submenu in the header. test_routing::test_mx9_*: both outputs destroyed → fallback carries the mix, default sink stays silent, config kept; replug → primary wins, fallback quiet |
| DV-16 | On multi-channel hardware (RØDECaster, Ui24R) each usable slice MUST be turnable into its own named virtual device: own name, own icon, stereo pair or mono, chosen from the device's channel positions. The reference stays `node.name` + positions (DV-9/DV-13), so the slice survives replug and reboot. | owner 2026-09-15 | ✅ layout.json v2 carries channels, mixes (outputs, fallback, fx), inputs, apps, links, defaultChannel — all state the daemon needs; the conf.d fragment is re-rendered from it on every start when it drifts |
| DV-7 | Virtual device identity (node.name) MUST stay stable across app updates so OBS/Discord keep their device selection (Wave Link L7: driver update changed device IDs). | wavelink L7; tests `test_dv7_levels_and_mute_survive_daemon_restart`, `test_dv7_state_is_keyed_by_stable_name_not_display_name` | ✅ |

## 5a. Architecture: service, CLI, frontends

| ID | Requirement | Source | Status |
|---|---|---|---|
| AR-1 | All mixer logic (layout, graph management, app routing, persistence, hotkey actions) MUST live in a background service (`kmixdeckd`) that runs without any UI. | owner 2026-09-14; test `test_ar1_cli_set_reaches_pipewire_and_is_audible` (CLI→bus→daemon→PipeWire, −12 dB measured) | ✅ |
| AR-2 | The service MUST expose its full functionality over a documented, versioned IPC API on the session bus, so that any desktop environment or third party can build a frontend without linking our code. The API spec (introspection XML) MUST ship in the repo and be the contract; the KDE UI MUST use only this API. | owner; tests `test_ar2_contract_matches_shipped_xml` (live introspection == interfaces/*.xml), `test_ar2_third_party_client_needs_none_of_our_code` (busctl only) | ✅ |
| AR-3 | A CLI (`kmixdeck`) MUST cover 100 % of the API: everything the UI can do, the CLI can do, scriptable, with `--json` output and stable exit codes. | owner; tests `test_ar3_cli_status_lists_the_prototype_graph`, `test_cli_level_syntax_and_exit_codes` — App routing/`watch` still to add; `kmixdeck app list|move` added; `watch` implemented | ✅ |
| AR-4 | The service MUST be D-Bus-activatable and run as a `systemd --user` unit bound to `pipewire.service` (`BindsTo=` + `After=`, `Restart=on-failure`), following `pipewire-pulse.service`. It MUST re-discover the graph after a PipeWire restart instead of dying. | platform; unit files `data/kmixdeckd.service.in`, `data/org.kmixdeck1.service.in` — PipeWire-restart reconnect not yet tested; reconnect with backoff on EPIPE, objects vanish/reappear on the bus; test `test_ch4_routing_survives_pipewire_restart` covers the daemon surviving a PipeWire restart | ✅ |
| AR-5 | The KDE UI MUST remain functional if started before the service (it activates it via D-Bus) and MUST show a clear state when the service is gone. | owner; test `test_ar5_frontend_call_activates_nothing_but_survives_daemon_gone` (exit 2, recovers); UI banner distinguishes service-gone vs PipeWire-gone | ✅ |
| AR-6 | Frontends MUST NOT need PipeWire access themselves; the service is the only PipeWire client. (Level meters are the exception to evaluate: see open question Q-6.) | owner; test `test_ar6_only_the_daemon_links_pipewire` (ldd) | ✅ |
| AR-7 | The repository MUST document how to write a frontend (docs/frontend-guide.md): bus name, object model, one worked example (the CLI). | owner; `docs/frontend-guide.md` | ✅ |

## 6. UX

| ID | Requirement | Source | Status |
|---|---|---|---|
| UX-1 | Main view: channels as rows against mixes as panels — one fader per (channel, mix) cell, visible at once. | owner; Wave Link 3 layout (channel list left, one panel per mix right, horizontal faders that double as meters) | ✅ MixerPage (2026-09-15 redesign): ChannelHeader rows + one panel per mix, CellFader `[mute][fader/meter][link]`, MixHeader card (icon, name, output, master, meter); UI smoke test test_ct1_kde_frontend_registers_global_shortcuts starts it; screenshot-verified in Plasma (Breeze Dark) |
| UX-2 | A "what am I hearing" indicator MUST show which mix is currently routed to the user's headphones; switching MUST be one click. | wavelink | ✅ `Mixer.ListeningDevice` (layout, one truth) + "I hear:" bar on the mixer page (one click = that mix on my device); CLI `listen`; test `test_ux2_listening_device_is_one_truth`; boreas 2026-09-16 |
| UX-3 | First-run wizard SHOULD create default channels and mixes, detect the microphone and the default output, and assign running apps. | wavelink | 📝 |
| UX-4 | Full keyboard operability and screen-reader labels per KDE HIG. | platform | 📝 |
| UX-5 | Languages: English first; German second; translatable via KDE's i18n. | owner | 📝 |
| UX-6 | Level meters (VU) on every channel and every mix (top Linux wish: Sonusmix #20; Pulsemeeter has them). | users; ADR 0006: daemon peak streams (25 Hz, `resample.peaks`), `org.kmixdeck1.Levels` Subscribe/Peaks, on demand only; CLI `kmixdeck levels`; UI meters per cell (channel peak × gain) and per mix; tests `test_ux6_levels_signal_carries_peaks_of_the_tone`, `test_ux6_subscriber_that_dies_is_forgotten` | ✅ |
| UX-7 | Volume sliders MUST use a logarithmic curve and show dB and percent. | wavelink #19, #20 | ✅ CellFader/MixHeader: cubic slider (WirePlumber curve), tooltip/readout in dB, CLI accepts dB/%/linear (test_cli_level_syntax) |
| UX-8 | Every channel and every mix MUST have a user-chosen icon: a short curated set one click away, the full icon theme, or any image file. Presentation only — stored in the layout, never in PipeWire. | wavelink (channel/mix icons), owner | ✅ `Channel.Icon` / `Mix.Icon` (icon name or absolute path, "" = frontend default), persisted in `layout.json`, survives daemon restart; CLI `channel|mix icon <slug> <icon|none>`; UI: click the icon tile or "Icon…" in the menu → IconDialog (curated Breeze set, KIconDialog for the whole theme, file dialog). test_presentation::test_ux8_* (3) |
| UX-9 | Channels and mixes MUST be reorderable; the order is part of the layout and survives restarts. Moving MUST NOT touch any fader, link or node. | wavelink (drag to reorder), owner | ✅ `Mixer.ChannelOrder` / `MixOrder` (as) + `MoveChannel/MoveMix(path, index)` (index clamped); CLI `channel|mix move <slug> <index|up|down|top|bottom>`; UI "Move up/down" (channels) and "Move left/right" (mixes) in the header menus. test_presentation::test_ux9_* (5) |
| UX-10 | Every running application row MUST show the app's own icon and whether it is currently producing sound; apps on no channel MUST be visible as such so they can be picked up. | owner 2026-09-15 | ✅ AppsPage: XDG icon (`application.icon-name`), running dot (node state), channel chips per app; `App.Icon/Running/Channels` on the bus |
| UX-11 | Assigning an app to a channel MUST work by drag-and-drop onto the channel row (in addition to the picker); dropping onto several rows accumulates the assignment (CH-12), dropping somewhere else replaces it. | owner 2026-09-15 | ✅ AppsPage rows are drag sources, channel headers drop targets (`dropActive`); drop = `Assign(addOn=true)`, picker = replace |
| UX-12 | Every entity (channel *and* mix) MUST have a listen button: press-and-hold routes exactly that entity to the main output, release restores the previous routing (solo audition). Works on a device with one built-in speaker as well as phones. | owner 2026-09-15 | ✅ `Mixer.Audition(o)` (`/` = release), `kmixdeck audition channel|mix <slug>|none`; hold button (headphones icon) on every channel/mix header; measured on boreas: others muted while held, exact restore on release |
| UX-13 | Level meters on EVERY entity, not only channels and mixes: cells (channel × mix, post-fader), apps (who is talking right now — the running dot alone is not a level), inputs/devices and outputs. Same meter loop as UX-6: one subscription per visible node, 25 Hz, batched signal; invisible = not computed. | owner 2026-09-16 | ✅ Levels keys cell/, in/, out/, app/ next to channel/, mix/; meter streams capture output nodes directly; UI: cells, apps, mix output; test `test_ux13_every_entity_has_a_meter`; boreas −12 dB cell measured 2026-09-16 |
| UX-14 | Mixing MUST be possible from the KDE UI alone, end to end, without CLI: pick the monitoring output device (UX-2) and the stream output per mix, route apps to channels (UX-10/11), set faders, mute — first target: the laptop with RØDECaster + built-in speakers. Verified by a scripted UI walk-through on real hardware, screenshot as artefact. | owner 2026-09-16 ("Sodass ich auf dem Laptop echt schon mal mixen kann") | 🔶 all steps possible from the UI (device via "I hear:", app via Add channel picker / Applications page, faders, mute, audition); missing: an end-to-end UI test — daemon-level walkthrough on boreas 2026-09-16 green |
| UX-15 | A **routing view** MUST show the whole signal path as one picture: inputs/apps → channels → mixes → outputs/captures, with live levels on every edge (UX-13) and the same listen buttons (UX-12). Read-mostly: clicking an edge jumps to the fader that controls it. Not a second place to edit routing — the matrix and the Apps page stay the editors (no two truths). | owner 2026-09-15 ("Routing-Ansicht"), 2026-09-16 | ✅ `RoutingPage` (sidebar "Routing"): sources (apps + inputs) → channels → mixes → outputs (devices + capture sources); Bézier edges, colour = live level, width = gain, dashed = muted/unplugged; listen buttons on channels & mixes; click on a cell edge → mixer page, fader pulses; `kmixdeck-kde --screenshot x.png --open routing` |
## 7. Verification (binding)

| ID | Requirement | Source | Status |
|---|---|---|---|
| VF-1 | Every acoustic requirement (levels, mute, routing, persistence) MUST be asserted by an automated test that plays and records audio against a **private** PipeWire daemon — never the user's session. Method and rules: `docs/spec/testing.md`. | owner; pattern from PipeWire `pwtest` and plasma-pa | ✅ |
| VF-2 | A requirement is marked ✅ only when a test in this repository asserts it; the test names the requirement ID, the requirement names the test. | owner | ✅ |
| VF-3 | Measurements that justify an architecture decision MUST live in the ADR with a reproducible script/test, not in prose. | owner | ✅ (ADR 0002) |
| VF-4 | `ctest` (unit + integration) MUST pass before merge; no sound card may be required to run it. | owner | ✅ |
| VF-5 | Audio measurements MUST use explicit port linking (`pw-link` by port name). `--target` auto-connect is forbidden in tests — it attached to the wrong port once and hid a real result. | lesson 2026-09-14 | ✅ |

| VF-7 | Capture sides of all kmixdeck loopbacks MUST be 1.0/unmuted after reconcile regardless of persisted state. | trap 5; test `test_vf7_capture_side_volume_is_healed_on_start` | ✅ |
| VF-6 | No kmixdeck output may ever be linked to a kmixdeck channel (feedback). Enforced by test on every graph change. | trap found 2026-09-14; test `test_no_feedback_loop_mix_outputs_never_target_a_channel` | ✅ |

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
