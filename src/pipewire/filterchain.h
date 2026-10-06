// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// ADR 0015: which filter-chain module the cell and effects chains use. PipeWire 1.6.0-1.6.2 ship one that reads freed
// memory (#5202) and crashes kmixdeckd and the PipeWire server; on those versions kmixdeck installs upstream's fixed
// 1.6.3 module under its own name. The daemon's runtime loads and the login fragment take the SAME name from here, so
// the server and the daemon never run different chain code. No PipeWire header here: Layout includes this.
#include <QString>
#include <QStringList>

namespace kmixdeck::pw {

inline constexpr char kStockFilterChain[] = "libpipewire-module-filter-chain";
inline constexpr char kFixedFilterChain[] = "libpipewire-module-kmixdeck-filter-chain";

/// True for the libpipewire versions whose filter-chain has #5202 (1.6.0, 1.6.1, 1.6.2 — checked against all tags).
bool hasFilterChainBug5202(const QString &libraryVersion);

/// Where PipeWire looks for modules: $PIPEWIRE_MODULE_DIR (colon-separated) or its compiled-in module directory.
/// The same rule as pw_context_load_module(); an absolute module path is not an option (refused since 6bc07dfe0e).
QStringList pipewireModuleDirs();

/// The pure decision, unit-tested: the fixed module when the version is affected AND the file is in one of `moduleDirs`,
/// otherwise PipeWire's own. Returns one of the two constants above.
const char *chooseFilterChainModule(const QString &libraryVersion, const QStringList &moduleDirs);

/// The decision for this process, made once from the linked libpipewire and the module path. Logs a warning when the
/// version is affected and the fixed module is not installed.
const char *filterChainModule();

} // namespace kmixdeck::pw
