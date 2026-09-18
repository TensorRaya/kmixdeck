// SPDX-FileCopyrightText: 2026 Raya Elena Solano
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QLoggingCategory>

// Logging categories (review 2026-09-18, BP-3). Filter at runtime with the standard Qt switch, e.g.
//   QT_LOGGING_RULES="kmixdeck.pipewire.debug=true"     everything the PipeWire layer sees
//   QT_LOGGING_RULES="kmixdeck.*.warning=false"         silence
// or in ~/.config/QtProject/qtlogging.ini. Category names are part of the operator-facing surface: keep them stable.
Q_DECLARE_LOGGING_CATEGORY(lcMixer)      // kmixdeck.mixer     — layout changes, routing decisions, persistence
Q_DECLARE_LOGGING_CATEGORY(lcPipewire)   // kmixdeck.pipewire  — connection, node/link/param traffic
Q_DECLARE_LOGGING_CATEGORY(lcDbus)       // kmixdeck.dbus      — service export, refused calls
Q_DECLARE_LOGGING_CATEGORY(lcFrontend)   // kmixdeck.frontend  — MixerClient, window, tray
