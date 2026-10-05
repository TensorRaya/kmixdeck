# ADR 0014 — Hyprland (and any non-KDE Wayland session) as a first-class desktop

**Status:** proposed 2026-09-28, phase 0 measured 2026-10-05 · **Owner:** project owner · **Drives:** CT-1, UX-17, new HY-1..HY-8 (draft below)

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
   Sway…) via LayerShellQt, anchored to the corner the bar sits on. KWin keeps the current path. Fallback when
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

The laptop has `layer-shell-qt` 6.7.5 but neither `wtype` nor `hyprqt6engine` installed; the synthetic-key question
for HY-8 is still open.

## Open points

- **Version skew.** The laptop runs Hyprland 0.56.2 (Lua config). This build host offers 0.53.3 (Ubuntu package,
  hyprlang). Tests must run against the Lua API we ship, so either the test host gets ≥ 0.55 or Phase 3 runs on the
  laptop.
- Synthetic key input for the test: Hyprland is expected to offer `zwp_virtual_keyboard_v1` (`wtype`); unverified.
- Whether XDPH remembers triggers when a GlobalShortcuts session is re-created with more shortcuts.
