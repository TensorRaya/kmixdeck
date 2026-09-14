// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once

#include <QObject>
#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <optional>
#include "pipewire/graph.h"

namespace kmixdeck {

/// Naming convention from ADR 0002. A slug is [a-z0-9-]+ derived once from the display name
/// and never changed afterwards (DV-7), so renames never move volumes.
struct Names {
    static QString channelNode(const QString &slug) { return QStringLiteral("kmixdeck.channel.") + slug; }
    static QString mixNode(const QString &slug)     { return QStringLiteral("kmixdeck.mix.") + slug; }
    static QString cellNode(const QString &ch, const QString &mix) { return QStringLiteral("kmixdeck.link.%1.%2").arg(ch, mix); }
    static QString slugify(const QString &display);
};

struct Channel { QString slug; QString name; QString icon; bool virt = true; };
struct Mix     { QString slug; QString name; QString icon; bool capture = true; QString outputDevice; };

/// The (channel × mix) matrix. One fader per cell = channelVolumes on the cell's loopback playback
/// stream (validated in ADR 0002). Cubic UI curve ↔ linear PipeWire volume conversion lives here.
class Mixer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QStringList channelSlugs READ channelSlugs NOTIFY layoutChanged)
    Q_PROPERTY(QStringList mixSlugs READ mixSlugs NOTIFY layoutChanged)
public:
    explicit Mixer(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    QStringList channelSlugs() const;
    QStringList mixSlugs() const;

    Q_INVOKABLE QString channelName(const QString &slug) const;
    Q_INVOKABLE QString mixName(const QString &slug) const;

    /// Cell fader, cubic 0..1 as shown in the UI (0.5 ≈ −18 dB, like Plasma).
    Q_INVOKABLE double cellVolume(const QString &ch, const QString &mix) const;
    Q_INVOKABLE bool   cellMuted (const QString &ch, const QString &mix) const;
    Q_INVOKABLE bool   cellPresent(const QString &ch, const QString &mix) const;
    Q_INVOKABLE void   setCellVolume(const QString &ch, const QString &mix, double cubic);
    Q_INVOKABLE void   setCellMuted (const QString &ch, const QString &mix, bool muted);

    /// Channel-wide trim (CH-7) = channelVolumes on the channel null sink → affects every mix.
    Q_INVOKABLE double channelTrim(const QString &slug) const;
    Q_INVOKABLE bool   channelMuted(const QString &slug) const;
    Q_INVOKABLE void   setChannelTrim(const QString &slug, double linear);
    Q_INVOKABLE void   setChannelMuted(const QString &slug, bool muted);
    Q_INVOKABLE void   renameChannel(const QString &slug, const QString &name);
    Q_INVOKABLE void   renameMix(const QString &slug, const QString &name);
    Q_INVOKABLE QString mixOutputDevice(const QString &slug) const;
    Q_INVOKABLE void    setMixOutputDevice(const QString &slug, const QString &nodeName);
    Q_INVOKABLE QString mixCaptureSource(const QString &slug) const;

    /// Layout edits (MX-1: any number of mixes; CH-2: any number of channels).
    Q_INVOKABLE void addChannel(const QString &displayName);
    Q_INVOKABLE void addMix(const QString &displayName);
    Q_INVOKABLE void removeChannel(const QString &slug);
    Q_INVOKABLE void removeMix(const QString &slug);

    static double linearToCubic(float lin) { return std::cbrt(static_cast<double>(lin)); }
    static float  cubicToLinear(double cub) { return static_cast<float>(cub * cub * cub); }

Q_SIGNALS:
    void connectedChanged();
    void layoutChanged();
    void cellChanged(const QString &ch, const QString &mix);
    void channelChanged(const QString &slug);
    void mixChanged(const QString &slug);

private:
    void onNode(const pw::NodeInfo &n);
    void onNodeRemoved(uint32_t id);
    void rebuildLayoutFromGraph();

    pw::Graph m_graph;
    bool m_connected = false;
    QVector<Channel> m_channels;
    QVector<Mix> m_mixes;
    QHash<QString, pw::NodeInfo> m_cells;      // key: cell node name
    QHash<QString, pw::NodeInfo> m_sinks;      // channel + mix null sinks, key: node name
    QHash<uint32_t, QString> m_idToName;
};

} // namespace kmixdeck
