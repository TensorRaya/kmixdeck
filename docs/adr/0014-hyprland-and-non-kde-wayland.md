# ADR 0014 — Hyprland (and any non-KDE Wayland session) as a first-class desktop

**Status:** proposed 2026-09-28, phase 0 measured 2026-10-05, HY-2 and HY-1 done 2026-10-05 · **Owner:** project owner · **Drives:** CT-1, UX-17, new HY-1..HY-8 (draft below)

## Context

kmixdeck is tested on KWin only. The service, the CLI and the web UI never touch the desktop, but the KDE frontend
(`kmixdeck-kde`) does, in five places — and two of them assume Plasma:

| Piece | Today | On Hyprland (read from code + upstream docs, NOT yet measured) |
|---|---|---|
| Global shortcuts (CT-1) | `KGlobalAccel` → `kglobalacceld`, which on Wayland is KWin itself (`test_shortcuts_wayland.py`) | **Do not fire.** No KWin, no key grab. Hyprland's way is the XDG GlobalShortcuts portal (XDPH) or its own binds |
| Tray (UX-17) | `KStatusNotifierItem` | Waybar's `tray` module speaks SNI → the icon should appear |
| Tray popover | `TrayOverview.qml`: a `Qt.Popup` window placed at the click position | Suspect. A parentless popup on Wayland has no global position; Hyprland may tile it like a normal window |
| Notifications | `KNotification` → `org.freedesktop.Notifications` | Any notification daemon (mako) |
| Look | `org.kde.desktop` QQC2 style + KIconTheme | Needs a Qt platform theme outside Plasma (`QT_QPA_PLATFORMTHEME=kde` or `hyprqt6engine`) |

Upstream guidance this ADR follows (read 2026-09-28):

- Hyprland wiki, *Binds → DBus Global Shortcuts*: apps that implement the GlobalShortcuts portal are listed by
  `hyprctl globalshortcuts` and bound with `hl.bind("…", hl.dsp.global("appid:id"))`. Recommended over `pass`. Only
  works with xdg-desktop-portal-hyprland (XDPH).
- XDG portal spec, *GlobalShortcuts v2*: `CreateSession` → `BindShortcuts` (**once per session**) → `Activated` /
  `Deactivated` signals; `ListShortcuts`, `ConfigureShortcuts`. Host (non-Flatpak) apps register their app id first
  via `org.freedesktop.host.portal.Registry.Register`; the id must match the `.desktop` basename
  (`org.kmixdeck.kmixdeck`).
- Hyprland wiki, *Must have*: notification daemon, PipeWire + WirePlumber, XDPH, polkit agent, `qt6-wayland`.
- Hyprland wiki, *hyprqt6engine*: Qt 6 theme provider compatible with KColorScheme, set via `QT_QPA_PLATFORMTHEME`.
- Hyprland wiki, *Window Rules*: `hl.window_rule({ match = { class = … }, float = true, size = … })`; static rules
  match the initial class/title.
- Hyprland ≥ 0.55 configures in **Lua** (`hyprland.lua`); hyprlang is deprecated. Anything we ship as config is Lua.
- uwsm: apps belong in their own user units (`uwsm app --`), XDG autostart runs via `xdg-desktop-autostart.target`.
- waybar-custom(5): `return-type: json`, a script without `interval` that loops itself is the event-driven form.

## Decision

1. **Nothing in the service changes.** Hyprland is a platform for the KDE frontend, not a new frontend. The
   four-frontend parity rule (AR-12) stays as it is.
2. **Shortcuts get two backends behind one interface.** `KGlobalAccel` when KWin owns `org.kde.kglobalaccel`; the
   XDG GlobalShortcuts portal otherwise. Same action ids for both (`mute-channel-<slug>`, …), so a binding written
   once keeps working. Because `BindShortcuts` works once per session, adding a channel re-creates the session
   (measure first whether XDPH keeps the user's triggers across that).
3. **The tray popover becomes a layer-shell surface** on compositors that offer `wlr-layer-shell` (Hyprland,
   Sway…) via LayerShellQt, anchored to the corner the bar sits on. ~~KWin keeps the current path.~~ Amended
   2026-10-05: KWin too — the current path fails there in exactly the same way (see HY-2 below). Fallback when
   neither works: double-click behaviour only (open the window) — never a tiled half-screen popover.
4. **We ship config, not instructions.** `data/hyprland/kmixdeck.lua` (window rules + example binds via
   `hl.dsp.global`) to be `require()`d from the user's `hyprland.lua`, and a Waybar snippet. Installed under
   `share/kmixdeck/`, never written into the user's config by us.
5. **Waybar module = a CLI output mode, not new logic.** `kmixdeck waybar` prints one JSON line per change
   (listening mix, its level, mute, loudness if on) by reusing `watch`. Clicks call existing CLI commands.
6. **Every claim in the table above gets measured** in a nested Hyprland before code is written for it.

## Plan

**Phase 0 — measure (no product code).** Nested Hyprland with a private D-Bus, private `XDG_RUNTIME_DIR` and its
own portal instances (the laptop's Hyprland session already showed that a nested test can take down the live
document portal when the runtime is shared). Record for each row of the table: works / broken / how.
Prediction before running: shortcuts dead, tray icon present, popover tiled, notifications fine, style OK only with
a platform theme set.

**Phase 1 — must have to use it daily.**
- HY-1 shortcuts via portal (decision 2), proven by a real key event in nested Hyprland.
- HY-2 tray popover as layer-shell or clean fallback (decision 3).
- HY-3 window rules + `require()`-able Lua file (decision 4).
- HY-4 platform theme: the window looks right with `QT_QPA_PLATFORMTHEME=kde` and with `hyprqt6engine`; no
  hard dependency on a running Plasma.

**Phase 2 — integration.**
- HY-5 `kmixdeck waybar` JSON stream + documented Waybar module (left click mute, right click open window,
  scroll = volume of the listening mix).
- HY-6 autostart under uwsm: the tray starts via XDG autostart; the daemon stays D-Bus/systemd activated.
- HY-7 docs: `docs/hyprland.md` — what to install (XDPH, notification daemon, polkit agent, qt6-wayland), the
  `require()` line, the Waybar block, troubleshooting (`hyprctl globalshortcuts`).

**Phase 3 — gate.**
- HY-8 `test_hyprland.py` in the integration suites: shortcut key press → channel mute on the PipeWire node,
  tray SNI registered, popover is a layer surface (`hyprctl layers`) and not a client (`hyprctl clients`),
  window rules applied. Runs only where Hyprland is installed (skip otherwise, like the Chrome tests).

## Phase 0 result (measured 2026-10-05, Hyprland 0.56.2, laptop, nested)

Setup: a private `kwin_wayland --virtual` (own session bus, so its `org.kde.kglobalaccel` cannot leak into the
measurement) serves only as the monitor; Hyprland runs as its Wayland client with a private `XDG_RUNTIME_DIR`, session
bus, PipeWire, XDPH, Waybar (`tray` module) and mako. The harness is `tools/hyprland-sandbox.py`. Hyprland's own
headless backend does not work for this: without a seat aquamarine has no allocator (`CBackend::create() failed`),
and weston's headless backend offers `wl_compositor` v5 where Hyprland binds v6.

| Piece | Predicted | Measured |
|---|---|---|
| App id | (not in the table) | **Wrong: `org.kde.kmixdeck`**, not the `.desktop` basename. `KAboutData::setApplicationData()` overwrote `QGuiApplication::desktopFileName`. No window rule, `.desktop` match or portal id could have worked. Fixed, guarded by `test_wayland_app_id_is_the_desktop_file_name` (red without the fix). |
| Global shortcuts | dead | **Dead, as predicted.** `org.kde.kglobalaccel` is not on the bus; `kf.globalaccel: Failed to get dbus path for component "kmixdeck"` once per action (10×). `hyprctl globalshortcuts`: none. |
| Tray | icon present | **Present.** Waybar owns `org.kde.StatusNotifierWatcher`, one item registered (`Id kmixdeck_kmixdeck`, `Status Active`), icon visible in the bar. |
| Tray popover | tiled like a window | **Worse: never appears.** After `Activate` no new client and no new layer; Qt logs `Failed to create grabbing popup. Ensure popup TrayOverview has a transientParent set and that parent window has received input.` An `xdg_popup` needs an input serial of the app's own surface; a click on Waybar belongs to Waybar. Layer shell (decision 3) is therefore required, not optional. |
| Notifications | fine | **Not measured.** They are only sent from shortcut actions, which are dead here; re-measure with HY-1. mako answers `GetServerInformation`. |
| Look | right only with a platform theme | **Wrong prediction: right without one.** `QT_QPA_PLATFORMTHEME` unset, Breeze controls and icons render, no missing glyphs. HY-4 shrinks to "also check with `kde`/`hyprqt6engine`". |
| Main window | — | Tiled by Hyprland (`floating false`, 1878×1008), as expected for a normal toplevel; HY-3 window rules decide. |

The laptop has `layer-shell-qt` 6.7.5 but neither `wtype` nor `hyprqt6engine` installed. (HY-1 builds `wtype` 0.4
from source for the test; Arch/CachyOS package it as `wtype`.)

## HY-2 result (2026-10-05)

The control group decided it: the same stack with KWin 6.7.5 as THE compositor (Waybar standing in for plasmashell)
gives the same `Failed to create grabbing popup` and no window. The Qt.Popup path never worked under any Wayland
compositor whose tray host is a separate process — the earlier tests ran it offscreen or under X11, where a popup needs
no serial. So the layer surface is used under every Wayland compositor, KWin included.

- `KdeIntegration::showPopover()`: X11 → the old popup; Wayland + `zwlr_layer_shell_v1` in the registry → layer surface,
  namespace `kmixdeck-tray`, layer `top`, exclusive zone 0, keyboard on demand, anchored to the screen edge nearest the
  click (top or bottom) with an 8 px gap, centred on the click and clamped on screen; a click with no usable position
  falls back to the top right. Second click closes it (a layer surface has no grab, so "click elsewhere" cannot close
  it). Wayland without layer shell → the window opens.
- LayerShellQt cannot report a missing protocol (it warns and leaves a plain toplevel, which Hyprland would tile), so
  the frontend asks the registry once, on a private event queue.
- Measured in nested Hyprland 0.56.2: `hyprctl layers` shows `kmixdeck-tray` 396×343 at (1502, 38) under a 30 px
  Waybar, `hyprctl clients` unchanged; second Activate removes the layer. Under KWin 6.7.5: layer 3 (above normal), at
  (1524, 38).
- Guarded by `test_tray_wayland.py` (KWin, both bar edges): red without the change (`a popover after the tray click did
  not happen within 5s`, 2/2), green with it.
- LayerShellQt is a RECOMMENDED build dependency, not a required one: without it the build still works and the Wayland
  click opens the window.

## HY-1 result (2026-10-05)

- `PortalShortcuts` (frontend): when nobody owns or can activate `org.kde.kglobalaccel` and the portal is reachable,
  the frontend binds the SAME QActions with the SAME ids through `org.freedesktop.portal.GlobalShortcuts`. Order:
  `Registry.Register(desktopFileName)` (xdg-desktop-portal ≥ 1.19 refuses a host app without an app id),
  `CreateSession`, `BindShortcuts`; `Activated(id)` triggers the action. All calls asynchronous. When the set of
  actions changes (a channel added), the session is closed and a new one bound, debounced by 200 ms.
- Under Plasma nothing changes: KWin owns `org.kde.kglobalaccel`, so no portal session is created.
- Measured in nested Hyprland 0.56.2, XDPH 1.4.1, xdg-desktop-portal 1.22.1: `hyprctl globalshortcuts` lists all
  ids as `org.kmixdeck.kmixdeck:<id>`; frontend log `global shortcuts bound through the portal: 10`.
- The user binds a key in the Lua config: `hl.bind("CTRL + SHIFT + ALT + F9",
  hl.dsp.global("org.kmixdeck.kmixdeck:mute-channel-game"))`. The trigger lives in Hyprland's config, not in XDPH, so
  re-creating the portal session does not lose it: after `channel add Chat` the old F9 bind still toggled the game
  channel, and the new `mute-channel-chat` worked with its own bind.
- Synthetic key input: `wtype` works (Hyprland offers `zwp_virtual_keyboard_v1`), with one catch: wtype uploads its
  own keymap with ad-hoc keycodes, and Hyprland resolves a bind's key by keycode through ITS layout. With the default
  a bind on F8 stayed silent for `wtype -k F8`; with `input.resolve_binds_by_sym = true` it fired (and silent again
  after switching back). A real keyboard needs neither; the test sets the option.
- Guarded by `test_shortcuts_hyprland.py` (bound through the portal, real key mutes the PipeWire node, unbound key
  does nothing, channel added later). Without `PortalShortcuts` 3 of 4 red (2/2 runs; the unbound-key probe stays
  green by design), with it 4/4 green (4 runs on the laptop). On hosts without Hyprland/XDPH/Waybar/mako/wtype the
  file skips itself.
- Notifications are still not measured: the action fires, mako answers, but no assertion on the notification yet.

## HY-3 result (2026-10-06, Blade, nested Hyprland 0.56.2)

- `data/hyprland/kmixdeck.lua`, installed to `share/kmixdeck/hyprland/`. The user adds two lines to `hyprland.lua`
  (`package.path = "<prefix>/share/kmixdeck/hyprland/?.lua;" .. package.path`, `local kmixdeck = require("kmixdeck")`).
  It adds one named window rule and a helper `kmixdeck.bind(keys, id)` (`hl.bind` + `hl.dsp.global` with the app id
  filled in). It binds no key and writes nothing into the user's config.
- Measured without any rule: main window tiled (as in phase 0). Dialogs (`--open duck/voice`, `--open soundboard`) are
  separate toplevels with the SAME class and the SAME initial title `kmixdeck`, so neither prop tells them apart from
  the main window; the rule covers both.
- The rule is `float = true` on the class alone. Measured per variant, one at a time:
  - `float` → the main window opens centred at the size it asks for (1152×648 = gridUnit 18 × 64×36);
  - `float` + `center` → the same, so `center` is dropped;
  - `float` + `center` + `initial_title = "^kmixdeck$"` → the SAME window floats at 1920×1080, the whole monitor. Not
    understood why (the plain `title` match gives 1152×648); it is the mutant the test keeps red.
- **Two things around it needed fixing, the rule itself did not**:
  - The sandbox's IPC socket: Hyprland puts `.socket2.sock` 82 characters below `$XDG_RUNTIME_DIR`, and a unix socket
    path has at most 107. With `TMPDIR=/var/tmp` the runtime dir was 4 characters too long, Hyprland logged `Socket2
    path is too long. IPC will not work.` and every Hyprland suite timed out ("timeout waiting for Hyprland IPC
    socket"). `tools/hyprland-sandbox.py` now makes its runtime dir under `/tmp` whatever `TMPDIR` says and checks
    the budget before starting.
  - **A headless `kmixdeck-kde --probe` stole the user's shortcuts.** It runs next to the user's instance
    (`KDBusService::Multiple`), and it bound the same ids through the portal. After it exited, the user's instance
    never saw a key again (F12: toggled before the run, nothing after, while the portal still listed all 10 ids).
    The headless modes (`--probe`, `--screenshot`, `--gesture`, `--self-test`) now register no global shortcut,
    neither KGlobalAccel nor portal. Guarded by `test_hy1_a_headless_run_leaves_the_shortcuts_alone`: red on the
    build without the change, green with it.
- Guarded by `test_window_rules_hyprland.py` (4 tests): main window floats inside the work area and centred; a
  dialog floats inside the work area; `kmixdeck.rules.window:set_enabled(false)` tiles the next window;
  `kmixdeck.bind()` reaches the action through the portal. Mutants of the shipped file: `initial_title` in the match
  → 1 red; `float = false` → 2 red (each once, Blade). Green: 4 passed in 24 of 27 runs on the Blade. The 3 red
  runs were Hyprland starts without a monitor (open points): two confirmed in the logs (no clients, configure 0×0,
  `monitors all` empty), the third had the same "3 failed in 73.5 s". The sandbox now reports that at the start.

## Open points

- **Version skew.** The laptop runs Hyprland 0.56.2 (Lua config). This build host offers 0.53.3 (Ubuntu package,
  hyprlang). Tests must run against the Lua API we ship, so either the test host gets ≥ 0.55 or Phase 3 runs on the
  laptop. As of HY-3 both Hyprland suites run on the Blade only; CI and this build host skip them.
- **Nested Hyprland sometimes starts without a monitor** (10 of 109 sandbox starts on the Blade, aquamarine 0.15.1):
  IPC up, `hyprctl monitors all` empty, every window gets configure 0×0 and is never mapped, Hyprland's main thread
  idle in `epoll_wait`. A 2 s pause between the host KWin and Hyprland did not change it (3 of 40). Likely cause:
  aquamarine's Wayland backend, fixed upstream after 0.15.1 by `7bb8bdf` (PR #415, 2026-09-22, "fixes hyprland
  wayland monitors sometimes not poppin up": the output's first requests were not flushed and nothing woke the loop).
  Not proven here yet: the A/B with aquamarine at `7bb8bdf` in a private prefix is still to run (prediction 0 bad
  starts). This only hits the NESTED backend, never a real session. The sandbox now waits for a monitor and fails in
  ≤ 20 s and keeps Hyprland's log (path in its last line), instead of tests failing later on windows
  that never map.
- ~~Synthetic key input for the test~~ — `wtype` works, see HY-1 result.
- ~~Whether XDPH remembers triggers when a GlobalShortcuts session is re-created~~ — the trigger is in Hyprland's
  config, not in XDPH; measured to survive the re-create (HY-1 result).
