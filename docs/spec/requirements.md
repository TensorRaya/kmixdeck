# Requirements — kmixdeck

Numbered, testable requirements. `MUST` / `SHOULD` / `MAY` as in RFC 2119.
Each requirement carries a source: `[owner]` = project owner's stated need,
`[wavelink]` = feature parity with Elgato Wave Link (see
`docs/research/wavelink-feature-inventory.md`), `[users]` = recurring user
feedback across comparable products (see `docs/research/user-feedback.md`),
`[platform]` = required by PipeWire/KDE integration.

Status legend: 📝 draft · ✅ agreed · 🔧 implemented · 🧪 verified

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

## 2. Mixes (outputs)

| ID | Requirement | Source | Status |
|---|---|---|---|
| MX-1 | The app MUST support **any number of output mixes** (1…n), added with a `+` action and removed individually. There MUST NOT be a hard-coded maximum. | owner | ✅ |
| MX-2 | Every mix MUST have its own fader (and mute) **per channel**. Changing *Game* in mix *Monitor* MUST NOT change *Game* in mix *Stream*. | owner | ✅ |
| MX-3 | A mix MUST be routable to (a) a physical output device (headphones, speakers), (b) a virtual capture device that other software (OBS, Discord, a recorder) can pick as its input, or (c) both. | owner, wavelink | 📝 |
| MX-4 | Default setup on first run SHOULD create two mixes: *Monitor* → default output device, *Stream* → virtual capture device. | owner | 📝 |
| MX-5 | Mixes MUST be nameable, reorderable, colour-coded; the UI MUST scale to ≥ 8 mixes without hiding faders. | owner | 📝 |
| MX-6 | Each mix MUST have a master fader, mute and meter. | wavelink | 📝 |
| MX-7 | Per-channel fader per mix MUST include a *link to another mix* toggle (e.g. "Stream follows Monitor for this channel") that can be broken at any time. | wavelink | 📝 |
| MX-8 | A mix MAY be duplicated as a starting point for a new mix. | owner | 📝 |

## 3. Microphone & effects

| ID | Requirement | Source | Status |
|---|---|---|---|
| FX-1 | Any channel MUST accept an ordered effects chain (insert). Minimum built-in set: noise suppression, noise gate, compressor, EQ, high-pass, limiter. | wavelink | 📝 |
| FX-2 | Effects MUST be implemented with open plugin standards available on Linux (LV2/LADSPA) through PipeWire filter-chain; the user MAY add any installed LV2 plugin. | platform | 📝 |
| FX-3 | Effects on a channel apply before the mix faders, so every mix hears the processed signal. | wavelink | 📝 |
| FX-4 | The mic channel SHOULD offer one-click presets (e.g. "Voice — clean", "Voice — broadcast") that the user can edit and save. | users | 📝 |
| FX-5 | Effects MUST be bypassable per effect and per chain without audio dropouts. | wavelink | 📝 |

## 4. Control & integration

| ID | Requirement | Source | Status |
|---|---|---|---|
| CT-1 | Global shortcuts (mute mic, mute channel X in mix Y, volume up/down, switch monitoring mix) MUST work on Wayland, via KGlobalAccel. | owner, platform | 📝 |
| CT-2 | A documented local control API (D-Bus and/or a small IPC protocol) MUST expose every fader, mute and mix switch so external controllers can drive the app. | owner, wavelink | 📝 |
| CT-3 | Stream Deck support MUST be provided on top of CT-2 — via an existing Linux Stream Deck host (OpenDeck / streamdeck-ui plugin) rather than our own HID stack. | owner | 📝 |
| CT-4 | The app MUST integrate with the Plasma system tray (StatusNotifierItem): quick mute, mix switch, level indication. | owner | 📝 |
| CT-5 | Volumes shown in kmixdeck and in the Plasma volume applet MUST agree (no two truths). | platform | 📝 |
| CT-6 | OBS SHOULD see each mix as a cleanly named capture device, and MAY additionally see each channel separately (for multi-track recording). | owner | 📝 |

## 5. Devices & audio behaviour

| ID | Requirement | Source | Status |
|---|---|---|---|
| DV-1 | The app MUST NOT sit in the audio path as a process. Audio MUST keep flowing when the UI is closed or crashes. | owner | ✅ |
| DV-2 | Added latency per hop (channel → mix → device) MUST be ≤ one PipeWire quantum at the session's rate; the design MUST avoid unnecessary resampling. | owner | 📝 |
| DV-3 | Hot-plug: when the monitor output device disappears (headset unplugged, Bluetooth drops), the mix MUST fall back to a user-defined device and return automatically when it reappears. | users | 📝 |
| DV-4 | Sample rate and quantum SHOULD follow the PipeWire session; the app MUST NOT force its own rate. | platform | 📝 |
| DV-5 | Configuration MUST be plain files under `$XDG_CONFIG_HOME`, human-readable, diff-able, and MUST restore the full graph on login without user action. | owner | 📝 |

## 6. UX

| ID | Requirement | Source | Status |
|---|---|---|---|
| UX-1 | Main view: channels as rows/columns against mixes as the other axis — one fader per (channel, mix) cell, visible at once. | owner | 📝 |
| UX-2 | A "what am I hearing" indicator MUST show which mix is currently routed to the user's headphones; switching MUST be one click. | wavelink | 📝 |
| UX-3 | First-run wizard SHOULD create default channels and mixes, detect the microphone and the default output, and assign running apps. | wavelink | 📝 |
| UX-4 | Full keyboard operability and screen-reader labels per KDE HIG. | platform | 📝 |
| UX-5 | Languages: English first; German second; translatable via KDE's i18n. | owner | 📝 |

## 7. Non-goals (for now)

- Windows/macOS ports.
- Being a DAW: no recording, no timeline.
- Replacing OBS's own mixer.
- Proprietary plugin formats (VST3 on Linux is possible but not a target for v1).

## Open questions

Tracked as issues with label `question`. Initial list:

1. Should each mix expose a *virtual capture device* by default, or only on demand? (device clutter vs. convenience)
2. Per-channel *pan/balance* — needed?
3. How to present the (channel × mix) matrix on small windows.
