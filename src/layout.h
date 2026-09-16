// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// The declared layout: what the user configured, independent of what PipeWire currently shows.
// Persisted as JSON; also rendered to a pipewire.conf.d fragment so the graph exists before the
// service runs (and keeps running if the service dies — ADR 0002/0005). Devices: ADR 0007.
#include "fx.h"

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
    /// ADR 0009 A2: which side of the CHANNEL/MIX this ref is bound to. Empty = both (mono→centre / stereo pair).
    /// "L"/"R" on an input: the port(s) feed only that side. On an output: only that side of the mix goes to the port(s).
    QString side;
    bool operator==(const DeviceRef &o) const { return node == o.node && positions == o.positions && side == o.side; }
    QJsonObject toJson() const;
    static DeviceRef fromJson(const QJsonObject &o);
    /// ADR 0009 D1/A2: "node", "node:POS[,POS]", "node:POS[,POS]>L|R" (input side) or "…<L|R" (output side) —
    /// the one reference syntax on the bus, in the CLI and in the UI. Both arrows parse; ref() emits '>' .
    QString ref() const {
        QString r = positions.isEmpty() ? node : node + QLatin1Char(':') + positions.join(QLatin1Char(','));
        if (!side.isEmpty()) r += QLatin1Char('>') + side;
        return r;
    }
    static DeviceRef fromRef(QString ref) {
        DeviceRef r;
        int arrow = ref.lastIndexOf(QLatin1Char('>')); if (arrow < 0) arrow = ref.lastIndexOf(QLatin1Char('<'));
        if (arrow > 0) { r.side = ref.mid(arrow + 1).trimmed().toUpper(); ref = ref.left(arrow); }
        const int c = ref.indexOf(QLatin1Char(':'));
        if (c < 0) { r.node = ref; return r; }
        r.node = ref.left(c);
        for (const auto &p : ref.mid(c + 1).split(QLatin1Char(','), Qt::SkipEmptyParts)) r.positions << p.trimmed();
        return r;
    }
    bool sideValid() const { return side.isEmpty() || side == QLatin1String("L") || side == QLatin1String("R"); }
    /// ADR 0009 A2: the positions this ref occupies on the CHANNEL/MIX side of its edge. Empty = default
    /// (stereo pair, or [MONO] for a one-port ref — see loopbackArgs). "L" → [FL], "R" → [FR].
    QStringList channelSidePositions() const {
        if (side == QLatin1String("L")) return {QStringLiteral("FL")};
        if (side == QLatin1String("R")) return {QStringLiteral("FR")};
        return {};
    }
};

struct LayoutChannel { QString slug, name, icon; fx::Chain fx; };
/// A physical input feeding a channel (mic, capture card, BT headset mic) — ADR 0007 D2.
struct LayoutInput   { QString slug, name; DeviceRef device; QString channel; };
struct LayoutMix     {
    QString slug, name, icon;
    QVector<DeviceRef> outputs;   // MX-9: several hardware outputs at once
    DeviceRef fallbackOutput;     // DV-15: used while outputs[0] is absent; empty node = none
    fx::Chain fx;                 // FX-6: same chain model on the output side
};

/// MX-7: cell (channel, mix) mirrors volume+mute of cell (channel, follows). Broken by touching the follower.
struct LayoutLink    { QString channel, mix, follows; };

/// CH-4/CH-12: what we remembered about one application stream. Key = appKey (application.name, else node.name).
/// channels[0] is the primary target (WirePlumber restore-target); the rest are carried by relay loopbacks so
/// every assigned channel hears the app. nodeName = last seen node.name of the stream (relay capture side).
struct LayoutApp     { QString key, nodeName; QStringList channels; };

/// DV-23: a persistent virtual multichannel device (stands in for a Ui24R / RØDECaster when testing port routing).
/// Rendered as TWO null-sink adapters: "<node>.out" (Audio/Sink, `outputs` ports — apps and mixes play into it) and
/// "<node>" (Audio/Source/Virtual, `inputs` ports — channels capture from it; feeding its input_* ports emulates a mic).
struct LayoutVirtualDevice {
    QString slug, name;           // slug → node.name "kmixdeck.virt.<slug>"
    int inputs = 8, outputs = 8;  // port counts, positions AUX1..AUXn
    QString portPrefix = QStringLiteral("AUX");
    QString inputNode() const { return QStringLiteral("kmixdeck.virt.") + slug; }
    QString outputNode() const { return QStringLiteral("kmixdeck.virt.") + slug + QStringLiteral(".out"); }
    QStringList positions(int n) const { QStringList p; for (int i = 1; i <= n; ++i) p << portPrefix + QString::number(i); return p; }
};

struct Layout {
    QVector<LayoutLink> links;
    QVector<LayoutChannel> channels;
    QVector<LayoutMix> mixes;
    QVector<LayoutInput> inputs;
    QVector<LayoutApp> apps;                       // CH-12: remembered per-app channel assignments
    QVector<LayoutVirtualDevice> virtualDevices;   // DV-23
    LayoutVirtualDevice *virtualDevice(const QString &slug) { for (auto &v : virtualDevices) if (v.slug == slug) return &v; return nullptr; }
    /// CH-5: where a never-seen application lands. Empty = leave it on the system default (no auto-routing).
    QString defaultChannel = QStringLiteral("system");
    /// UX-2: the device the user listens on (node.name). The mix routed to it is "what I hear". Empty = not chosen.
    QString listeningDevice;
    /// Apps kmixdeck has routed at least once — keyed by application.name (falls back to node.name).
    /// WirePlumber restores THEIR target itself; this set only exists to tell "new" from "known".
    QStringList knownApps;

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
    /// CH-12 lookup by appKey (application.name / node.name), not by node id (CH-6).
    LayoutApp *app(const QString &key);
    const LayoutApp *app(const QString &key) const;

    /// Where streams should aim: the fx entry when a chain is active, the plain node otherwise (ADR 0008).
    QString channelEntry(const QString &slug) const;
    QString mixEntry(const QString &slug) const;
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
/// CH-12: one relay loopback per extra channel of a multi-assigned app — captures from the primary channel
/// sink and plays into the next one, so every assigned channel hears the app. Key: <appKey>.<channelSlug>.
inline QString relayNode(const QString &appKey, const QString &channelSlug) {
    return QStringLiteral("kmixdeck.relay.%1.%2").arg(appKey, channelSlug);
}
}

/// One loopback = one module-loopback args string. Shared by the config renderer and the runtime path so the
/// two can never drift (ADR 0002/0007). Device-side streams get node.linger + dont-fallback: they wait for an
/// absent device instead of dying, and WirePlumber links them by itself when the device appears (D3).
QString loopbackArgs(const QString &description,
                     const QString &captureName, const QString &captureTarget, bool captureIsSink, const QStringList &capturePositions, bool captureLinger,
                     const QString &playbackName, const QString &playbackTarget, const QStringList &playbackPositions, bool playbackLinger, bool playbackDontReconnect,
                     const QString &playbackExtra = {});

} // namespace kmixdeck
