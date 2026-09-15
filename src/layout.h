// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// The declared layout: what the user configured, independent of what PipeWire currently shows.
// Persisted as JSON; also rendered to a pipewire.conf.d fragment so the graph exists before the
// service runs (and keeps running if the service dies — ADR 0002/0005). Devices: ADR 0007.
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>

namespace kmixdeck {

/// A reference to a hardware device node (ADR 0007 D1): keyed by node.name, with a cached face so the UI
/// can show the device while it is unplugged. `positions` = channel subset of the device to use (D4), empty =
/// the device's natural stereo pair.
struct DeviceRef {
    QString node;                 // PipeWire node.name — the only key
    QString description;          // last seen node.description
    QStringList positions;        // e.g. {"AUX2","AUX3"}; empty = default
    bool operator==(const DeviceRef &o) const { return node == o.node && positions == o.positions; }
    QJsonObject toJson() const;
    static DeviceRef fromJson(const QJsonObject &o);
};

struct LayoutChannel { QString slug, name, icon; };
/// A physical input feeding a channel (mic, capture card, BT headset mic) — ADR 0007 D2.
struct LayoutInput   { QString slug, name; DeviceRef device; QString channel; };
struct LayoutMix     {
    QString slug, name, icon;
    QVector<DeviceRef> outputs;   // MX-9: several hardware outputs at once
    DeviceRef fallbackOutput;     // DV-15: used while outputs[0] is absent; empty node = none
};

struct Layout {
    QVector<LayoutChannel> channels;
    QVector<LayoutMix> mixes;
    QVector<LayoutInput> inputs;

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
    LayoutInput *input(const QString &slug);
    const LayoutChannel *channel(const QString &slug) const;
    const LayoutMix *mix(const QString &slug) const;
    const LayoutInput *input(const QString &slug) const;
};

/// Node names of the device-edge loopbacks (ADR 0007).
namespace EdgeNames {
inline QString inputNode(const QString &inputSlug) { return QStringLiteral("kmixdeck.in.") + inputSlug; }
/// Index 0 keeps the plain name (that is what a single-output mix — the common case — is called on the bus
/// and in pipewire pactl listings); additional outputs get .1, .2 … (MX-9).
inline QString outputNode(const QString &mixSlug, int index) {
    return index <= 0 ? QStringLiteral("kmixdeck.out.") + mixSlug
                      : QStringLiteral("kmixdeck.out.%1.%2").arg(mixSlug).arg(index);
}
inline QString sourceNode(const QString &mixSlug) { return QStringLiteral("kmixdeck.source.") + mixSlug; }
}

/// One loopback = one module-loopback args string. Shared by the config renderer and the runtime path so the
/// two can never drift (ADR 0002/0007). Device-side streams get node.linger + dont-fallback: they wait for an
/// absent device instead of dying, and WirePlumber links them by itself when the device appears (D3).
QString loopbackArgs(const QString &description,
                     const QString &captureName, const QString &captureTarget, bool captureIsSink, const QStringList &capturePositions, bool captureLinger,
                     const QString &playbackName, const QString &playbackTarget, const QStringList &playbackPositions, bool playbackLinger, bool playbackDontReconnect,
                     const QString &playbackExtra = {});

} // namespace kmixdeck
