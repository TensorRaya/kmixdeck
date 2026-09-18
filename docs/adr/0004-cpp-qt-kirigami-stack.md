# ADR 0004 — Implementation language and UI stack: C++20 / Qt 6 / Kirigami / KDE Frameworks 6

- Status: accepted
- Date: 2026-09-14
- Closes: issue #8

## Context

The owner's brief for the UI: *"build it modern — everything KDE offers."* Candidates
evaluated in `docs/research/pipewire-kde-technical-notes.md` §6: C++/QML and Rust + cxx-qt
(with pipewire-rs).

What "everything KDE offers" concretely means for this app:

| Need | KDE Framework | Rust reachability |
|---|---|---|
| Global shortcuts on Wayland (CT-1) | KGlobalAccel | no binding |
| Tray with menu, live icon (CT-4) | KStatusNotifierItem | no binding |
| Config files, KConfigXT-generated settings | KConfig | no binding |
| Notifications ("Mic muted") | KNotifications | no binding |
| Translations (UX-5) | KI18n | partial via cxx |
| Crash handler → DrKonqi | KCrash | no binding |
| Single-instance, D-Bus activation (CT-2) | KDBusAddons | no binding |
| UI components, convergent layouts | Kirigami, Kirigami Addons (FormCard, StatefulApp) | QML: language-neutral |
| Consistency with the Plasma volume applet (CT-5) | PulseAudioQt (what plasma-pa uses) — optional | no binding |

## Decision

**C++20 + Qt 6 + QML/Kirigami + KDE Frameworks 6**, PipeWire through `libpipewire-0.3`
directly. Build with CMake + Extra CMake Modules (ECM), the KDE standard.

## Reasons

1. Every KDE Framework in the table is C++/Qt with no maintained Rust binding. Through
   cxx-qt each one would be a hand-written bridge to maintain — *our* code, not the
   framework's — and "everything KDE offers" would in practice become "what we had time to
   bridge".
2. EasyEffects proves the exact stack (C++/QML/Kirigami on PipeWire) works for a
   PipeWire-native audio app with a Plasma-native feel.
3. KDE's own tooling assumes it: `kde-builder`, Craft, Flatpak KDE runtime, KDE HIG QML
   components, Breeze QQC2 style, i18n extraction (`Messages.sh`), KDE CI templates.
4. `libpipewire` is a C API; wrapping it in a thin C++ RAII layer is a day's work and gives
   us full control (pw-cli's exact parameters, see ADR 0002).
5. What we give up: Rust's memory safety. Mitigation: the PipeWire layer is small and
   isolated in one module (`src/pipewire/`); UI logic lives in QML; we compile with
   `-Wall -Wextra -Werror`, sanitizers in CI.

## Consequences

- Repo layout: `src/` (C++ backend + QML), `src/qml/`, `src/pipewire/`, `po/`, `data/`
  (desktop file, appdata, icons), `tests/`.
- Reverse-DNS app id: `org.kmixdeck.kmixdeck` (no KDE org membership yet; can move to
  `org.kde.kmixdeck` if the project is ever incubated).
- Minimum versions: Qt 6.6, KF 6.0, PipeWire 1.0 — what Ubuntu 26.04 / CachyOS ship or
  exceed.
- License GPL-3.0-or-later fits KDE Frameworks (LGPL) and PipeWire (MIT).
