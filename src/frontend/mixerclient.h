// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// The KDE frontend's view of the service: a mirror of org.kmixdeck1 kept live via ObjectManager +
// PropertiesChanged. Same QML-facing API the old in-process Mixer had, so the QML did not change.
#include <QObject>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusMessage>
#include <QVariantMap>
#include <QMap>
#include <QStringList>
#include <cmath>

using InterfaceMap = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, InterfaceMap>;
using StringMap = QMap<QString, QString>;
Q_DECLARE_METATYPE(StringMap)
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjects)

namespace kmixdeck::frontend {

class MixerClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)          // PipeWire state as reported by the service
    Q_PROPERTY(bool serviceAvailable READ serviceAvailable NOTIFY serviceAvailableChanged)
    Q_PROPERTY(QStringList channelSlugs READ channelSlugs NOTIFY layoutChanged)
    Q_PROPERTY(QStringList mixSlugs READ mixSlugs NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)   // [{path,name,binary,mediaName,channel}] for QML
    Q_PROPERTY(QVariantList outputDevices READ outputDevices NOTIFY outputDevicesChanged)   // [{nodeName, description}]
    Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY inputDevicesChanged)   // hardware sources a channel can be fed by
    Q_PROPERTY(bool metersEnabled READ metersEnabled WRITE setMetersEnabled NOTIFY metersEnabledChanged)   // Levels.Subscribe while true
public:
    explicit MixerClient(QObject *parent = nullptr);

    bool connected() const { return m_pwConnected; }
    bool serviceAvailable() const { return m_available; }
    QStringList channelSlugs() const { return m_channelOrder; }
    QStringList mixSlugs() const { return m_mixOrder; }

    Q_INVOKABLE QString channelName(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("Name")).toString(); }
    Q_INVOKABLE QString mixName(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("Name")).toString(); }
    Q_INVOKABLE bool   cellPresent(const QString &ch, const QString &mix) const { return m_cells.contains(cellKey(ch, mix)); }
    /// cubic 0..1 for the UI; the bus speaks linear
    Q_INVOKABLE double cellVolume(const QString &ch, const QString &mix) const { return std::cbrt(m_cells.value(cellKey(ch, mix)).value(QStringLiteral("Volume"), 0.0).toDouble()); }
    Q_INVOKABLE bool   cellMuted(const QString &ch, const QString &mix) const { return m_cells.value(cellKey(ch, mix)).value(QStringLiteral("Muted"), true).toBool(); }
    Q_INVOKABLE void   setCellVolume(const QString &ch, const QString &mix, double cubic);
    Q_INVOKABLE void   setCellMuted(const QString &ch, const QString &mix, bool muted);
    Q_INVOKABLE void   toggleCellMute(const QString &ch, const QString &mix);
    Q_INVOKABLE void   toggleChannelMute(const QString &slug);
    Q_INVOKABLE bool   channelMuted(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("Muted")).toBool(); }
    Q_INVOKABLE void   addChannel(const QString &name);
    Q_INVOKABLE void   addMix(const QString &name);
    Q_INVOKABLE void   removeChannel(const QString &slug);
    Q_INVOKABLE void   removeMix(const QString &slug);
    QVariantList apps() const;
    QVariantList outputDevices() const;
    QVariantList inputDevices() const;
    bool metersEnabled() const { return m_metersEnabled; }
    void setMetersEnabled(bool on);
    /// Last peak (linear 0..1) for "channel/<slug>" or "mix/<slug>"; 0 when unknown.
    Q_INVOKABLE double peak(const QString &key) const { return m_peaks.value(key, 0.0); }
    Q_INVOKABLE QString mixOutputDevice(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("OutputDevice")).toString(); }
    Q_INVOKABLE QString mixCaptureSource(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("CaptureSource")).toString(); }
    Q_INVOKABLE void    setMixOutputDevice(const QString &slug, const QString &nodeName);
    Q_INVOKABLE void    renameChannel(const QString &slug, const QString &name);
    Q_INVOKABLE void    renameMix(const QString &slug, const QString &name);
    Q_INVOKABLE void   moveApp(const QString &appPath, const QString &channelSlug);
    Q_INVOKABLE void   setChannelDevice(const QString &slug, const QString &deviceNode);
    Q_INVOKABLE QString channelDevice(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("InputDevice")).toString(); }
    Q_INVOKABLE bool    channelInputPresent(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("InputPresent"), true).toBool(); }
    Q_INVOKABLE bool    mixOutputPresent(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("OutputPresent"), true).toBool(); }

Q_SIGNALS:
    void errorOccurred(const QString &message);   // a refused request (duplicate name, unknown device, …)
    void connectedChanged();
    void serviceAvailableChanged();
    void layoutChanged();
    void cellChanged(const QString &ch, const QString &mix);
    void channelChanged(const QString &slug);
    void appsChanged();
    void outputDevicesChanged();
    void inputDevicesChanged();
    void metersEnabledChanged();
    void peaksChanged();                                        // once per tick
    void mixChanged(const QString &slug);

private Q_SLOTS:
    void onPeaks(const QVariantMap &peaks);
    void onPropertiesChanged(const QDBusMessage &msg);
    void onInterfacesAdded(const QDBusObjectPath &path, const InterfaceMap &ifaces);
    void onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &ifaces);
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    void callReportingErrors(const QString &method, const QVariant &arg);
    static QString cellKey(const QString &ch, const QString &mix) { return ch + QLatin1Char('/') + mix; }
    void refresh();                    // GetManagedObjects → rebuild mirror
    void absorb(const QString &path, const QString &iface, const QVariantMap &props, bool *layout);
    void setProperty(const QString &path, const QString &iface, const QString &name, const QVariant &v);

    bool m_available = false, m_pwConnected = false;
    QMap<QString, QVariantMap> m_channels, m_mixes, m_cells;   // keyed by slug / slug / "ch/mix"
    QMap<QString, QVariantMap> m_apps;                          // keyed by object path
    QMap<QString, QString> m_outputDevices, m_inputDevices;    // node.name → description (both directions)
    bool m_metersEnabled = false;
    QHash<QString, double> m_peaks;
    QStringList m_channelOrder, m_mixOrder;
};

} // namespace kmixdeck::frontend
