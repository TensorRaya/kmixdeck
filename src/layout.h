// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// The declared layout: what the user configured, independent of what PipeWire currently shows.
// Persisted as JSON; also rendered to a pipewire.conf.d fragment so the graph exists before the
// service runs (and keeps running if the service dies — ADR 0002/0005).
#include <QString>
#include <QVector>
#include <QJsonObject>

namespace kmixdeck {

struct LayoutChannel { QString slug, name, icon; };
struct LayoutMix     { QString slug, name, icon, outputDevice; };

struct Layout {
    QVector<LayoutChannel> channels;
    QVector<LayoutMix> mixes;

    static QString defaultPath();                   // $XDG_CONFIG_HOME/kmixdeck/layout.json
    static QString defaultPipewireConfPath();       // $XDG_CONFIG_HOME/pipewire/pipewire.conf.d/90-kmixdeck.conf
    static Layout starter();                        // Game/System/Voice × Monitor/Stream

    bool load(const QString &path);
    bool save(const QString &path) const;
    QJsonObject toJson() const;
    static Layout fromJson(const QJsonObject &o);

    /// Render the whole graph as a PipeWire config fragment (context.objects + context.modules).
    QString toPipewireConf() const;
    bool writePipewireConf(const QString &path) const;

    LayoutChannel *channel(const QString &slug);
    LayoutMix *mix(const QString &slug);
    const LayoutChannel *channel(const QString &slug) const;
    const LayoutMix *mix(const QString &slug) const;
};

} // namespace kmixdeck
