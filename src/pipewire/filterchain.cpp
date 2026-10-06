// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "filterchain.h"
#include "../logging.h"

#include <pipewire/pipewire.h>
#include <QFileInfo>
#include <QVersionNumber>

namespace kmixdeck::pw {

bool hasFilterChainBug5202(const QString &libraryVersion) {
    const QVersionNumber v = QVersionNumber::fromString(libraryVersion);
    return v >= QVersionNumber(1, 6, 0) && v < QVersionNumber(1, 6, 3);
}

QStringList pipewireModuleDirs() {
    const QByteArray env = qgetenv("PIPEWIRE_MODULE_DIR");
    const QString dirs = env.isEmpty() ? QStringLiteral(KMIXDECK_PW_MODULEDIR) : QString::fromLocal8Bit(env);
    return dirs.split(QLatin1Char(':'), Qt::SkipEmptyParts);
}

const char *chooseFilterChainModule(const QString &libraryVersion, const QStringList &moduleDirs) {
    if (!hasFilterChainBug5202(libraryVersion)) return kStockFilterChain;
    const QString file = QLatin1String(kFixedFilterChain) + QLatin1String(".so");
    for (const QString &dir : moduleDirs)
        if (QFileInfo(dir + QLatin1Char('/') + file).isFile()) return kFixedFilterChain;
    return kStockFilterChain;
}

const char *filterChainModule() {
    static const char *const chosen = [] {
        const QString version = QString::fromLatin1(pw_get_library_version());
        const QStringList dirs = pipewireModuleDirs();
        const char *m = chooseFilterChainModule(version, dirs);
        if (m == kFixedFilterChain)
            qCInfo(lcPipewire).noquote() << "PipeWire" << version << "has the filter-chain bug #5202; cell and effects chains use"
                                         << kFixedFilterChain << "(ADR 0015)";
        else if (hasFilterChainBug5202(version))
            qCWarning(lcPipewire).noquote()
                << "PipeWire" << version << "has the filter-chain bug #5202, and" << QLatin1String(kFixedFilterChain) + QLatin1String(".so")
                << "is not in" << dirs.join(QLatin1Char(':')) << "- cell and effects chains can crash kmixdeckd and the PipeWire"
                << "server. Install kmixdeck's module (cmake --install <build> --component pipewire-module) or PipeWire >= 1.6.3.";
        return m;
    }();
    return chosen;
}

} // namespace kmixdeck::pw
