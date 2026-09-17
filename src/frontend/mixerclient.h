// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// The KDE frontend's view of the service: a mirror of org.kmixdeck1 kept live via ObjectManager +
// PropertiesChanged. Same QML-facing API the old in-process Mixer had, so the QML did not change.
#include <QObject>
#include <QUrl>
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
using PortMap = QMap<QString, QStringList>;
Q_DECLARE_METATYPE(StringMap)
Q_DECLARE_METATYPE(PortMap)
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjects)

namespace kmixdeck::frontend {

class MixerClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)          // PipeWire state as reported by the service
    Q_PROPERTY(bool serviceAvailable READ serviceAvailable NOTIFY serviceAvailableChanged)
    Q_PROPERTY(bool hideToTray READ hideToTray WRITE setHideToTray NOTIFY hideToTrayChanged)   // UX-17: window close → hide, tray stays; false in headless/test modes
    Q_PROPERTY(QStringList channelSlugs READ channelSlugs NOTIFY layoutChanged)
    Q_PROPERTY(QStringList mixSlugs READ mixSlugs NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)   // [{path,name,binary,mediaName,channel}] for QML
    Q_PROPERTY(QVariantList outputDevices READ outputDevices NOTIFY outputDevicesChanged)   // [{nodeName, description}]
    Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY inputDevicesChanged)   // hardware sources a channel can be fed by
    Q_PROPERTY(int devicePortsVersion READ devicePortsVersion NOTIFY devicePortsChanged)   // bump → QML re-asks devicePorts()
    Q_PROPERTY(bool metersEnabled READ metersEnabled WRITE setMetersEnabled NOTIFY metersEnabledChanged)   // Levels.Subscribe while true
    Q_PROPERTY(QString defaultChannel READ defaultChannel WRITE setDefaultChannel NOTIFY defaultChannelChanged)   // CH-5, slug or ""
    Q_PROPERTY(QString listeningDevice READ listeningDevice WRITE setListeningDevice NOTIFY listeningDeviceChanged)   // UX-2, node.name or ""
    Q_PROPERTY(QString undoDescription READ undoDescription NOTIFY undoChanged)   // CH-9
public:
    explicit MixerClient(QObject *parent = nullptr);

    bool connected() const { return m_pwConnected; }
    bool serviceAvailable() const { return m_available; }
    QStringList channelSlugs() const { return m_channelOrder; }
    QStringList mixSlugs() const { return m_mixOrder; }

    Q_INVOKABLE QString channelName(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("Name")).toString(); }
    QString defaultChannel() const { return m_defaultChannel; }
    QString listeningDevice() const { return m_listeningDevice; }
    bool hideToTray() const { return m_hideToTray; }
    void setHideToTray(bool v) { if (m_hideToTray == v) return; m_hideToTray = v; Q_EMIT hideToTrayChanged(); }
    void setListeningDevice(const QString &node);
    QString undoDescription() const { return m_undoDescription; }
    Q_INVOKABLE void undo();
    // CT-7: synchronous on purpose — the user waits for the file dialog's result anyway, and the notification needs
    // the outcome. URLs from FileDialog, plain paths from the CLI-style probes both work.
    Q_INVOKABLE bool exportToFile(const QUrl &url);
    Q_INVOKABLE bool importFromFile(const QUrl &url);
    Q_INVOKABLE QString displayPath(const QUrl &url) const { return url.isLocalFile() ? url.toLocalFile() : url.toString(); }
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    QString lastError() const { return m_lastError; }
    void setDefaultChannel(const QString &slug);
    Q_INVOKABLE QString mixName(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("Name")).toString(); }
    Q_INVOKABLE bool   cellPresent(const QString &ch, const QString &mix) const { return m_cells.contains(cellKey(ch, mix)); }
    /// cubic 0..1 for the UI; the bus speaks linear
    Q_INVOKABLE double cellVolume(const QString &ch, const QString &mix) const { return std::cbrt(m_cells.value(cellKey(ch, mix)).value(QStringLiteral("Volume"), 0.0).toDouble()); }
    Q_INVOKABLE bool   cellMuted(const QString &ch, const QString &mix) const { return m_cells.value(cellKey(ch, mix)).value(QStringLiteral("Muted"), true).toBool(); }
    Q_INVOKABLE void   setCellVolume(const QString &ch, const QString &mix, double cubic);
    Q_INVOKABLE void   setCellMuted(const QString &ch, const QString &mix, bool muted);
    Q_INVOKABLE void   toggleCellMute(const QString &ch, const QString &mix);
    /// MX-7: slug of the mix this cell follows, "" when independent
    Q_INVOKABLE QString cellFollows(const QString &ch, const QString &mix) const {
        const QString p = m_cells.value(cellKey(ch, mix)).value(QStringLiteral("Follows")).toString();
        return p == QLatin1String("/") || p.isEmpty() ? QString() : p.section(QLatin1Char('/'), -1);
    }
    Q_INVOKABLE void   setCellFollows(const QString &ch, const QString &mix, const QString &followsSlug);
    Q_INVOKABLE void   toggleChannelMute(const QString &slug);
    Q_INVOKABLE bool   channelMuted(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("Muted")).toBool(); }
    Q_INVOKABLE double channelPan(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("Pan")).toDouble(); }   // DV-22
    Q_INVOKABLE void   setChannelPan(const QString &slug, double pan);
    Q_INVOKABLE double channelTrim(const QString &slug) const { return std::cbrt(m_channels.value(slug).value(QStringLiteral("Trim"), 1.0).toDouble()); }   // CH-7, cubic like every fader
    Q_INVOKABLE void   setChannelTrim(const QString &slug, double cubic);
    Q_INVOKABLE void   addChannel(const QString &name);
    /// CH-13: one picker for every source. kind = "app" (ref = app object path), "device" (ref = input node.name)
    /// or "" (apps only). Creates the channel, then assigns the app / sets the input on the new channel.
    Q_INVOKABLE void   addChannelWithSource(const QString &name, const QString &kind, const QString &ref);
    Q_INVOKABLE void   addMix(const QString &name);
    Q_INVOKABLE void   removeChannel(const QString &slug);
    Q_INVOKABLE void   removeMix(const QString &slug);
    QVariantList apps() const;
    QVariantList outputDevices() const;
    QVariantList inputDevices() const;
    /// ADR 0009 / DV-20: [{position, port, label, usedBy:[names]}] for one device; empty until the daemon told us.
    Q_INVOKABLE QVariantList devicePorts(const QString &nodeName) const;
    int devicePortsVersion() const { return m_devicePortsVersion; }
    /// Compose/decompose the one reference syntax "node[:POS,POS]" so QML never string-fiddles.
    Q_INVOKABLE QString makeDeviceRef(const QString &node, const QStringList &positions, const QString &side = QString()) const {
        QString r = positions.isEmpty() ? node : node + QLatin1Char(':') + positions.join(QLatin1Char(','));
        return side.isEmpty() ? r : r + QLatin1Char('>') + side;
    }
    Q_INVOKABLE QString refNode(const QString &ref) const { return ref.section(QLatin1Char(':'), 0, 0); }
    Q_INVOKABLE QStringList refPositions(const QString &ref) const { return ref.section(QLatin1Char(':'), 1).section(QLatin1Char('>'), 0, 0).split(QLatin1Char(','), Qt::SkipEmptyParts); }
    /// "L", "R" or "" — ADR 0009 A2 side selector
    Q_INVOKABLE QString refSide(const QString &ref) const { return ref.contains(QLatin1Char('>')) ? ref.section(QLatin1Char('>'), -1) : QString(); }
    /// Human label for a ref: "RØDECaster Pro II · Mic 2" / "Ui24R · AUX18+AUX19"
    Q_INVOKABLE QString deviceRefLabel(const QString &ref) const;
    /// Compact form for narrow boxes (routing view): "AUX7 · Soundcraft Ui24R" — port first so it survives eliding
    Q_INVOKABLE QString deviceRefShort(const QString &ref) const;
    // ---- ADR 0009 B2 / AR-9: the patchbay view model. One JSON-shaped structure for every frontend:
    //   { cards: [{id, kind: app|device|channel|mix|output, title, icon, on, present,
    //              rows: [{pos, label, meterKey, jackIn, jackOut, usedBy}]}],
    //     wires: [{from: {card, pos}, to: {card, pos}, ref, muted, meterKey, kind: input|cell|output}] }
    // Card ids: "app/<path>", "dev/<node>", "ch/<slug>", "mix/<slug>", "out/<mix>/<ref>". Row pos for stereo cards is "L"/"R".
    Q_INVOKABLE QVariantMap patchbay() const;
    /// ADR 0010 D4 — the ONE definition of "the essentials": what the tray popover shows and what the window's
    /// header summarises. Anything the tray needs goes in here, never into the tray directly.
    Q_INVOKABLE QVariantMap overview() const;
    Q_INVOKABLE QStringList channelInputs(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("Inputs")).toStringList(); }
    // Patchbay gestures. connectJacks: drag from one jack to another → the right daemon call, or "" on success /
    // a human reason when the pair makes no sense (same column, device→device …). removeWire: click on a wire.
    Q_INVOKABLE QString connectJacks(const QString &fromCard, const QString &fromPos, const QString &toCard, const QString &toPos);
    Q_INVOKABLE void removeWire(const QVariantMap &wire);
    /// DV-14: the wire's own trim (cubic 0..1) and mute; only for kind "input" / "output" (device wires).
    Q_INVOKABLE double wireTrim(const QVariantMap &wire) const;
    Q_INVOKABLE bool   wireMuted(const QVariantMap &wire) const;
    Q_INVOKABLE void   setWireTrim(const QVariantMap &wire, double trim, bool muted);
    Q_INVOKABLE void addChannelInput(const QString &slug, const QString &ref);
    Q_INVOKABLE void removeChannelInput(const QString &slug, const QString &ref);
    bool metersEnabled() const { return m_metersEnabled; }
    void setMetersEnabled(bool on);
    /// Last peak (linear 0..1) for "channel/<slug>" or "mix/<slug>"; 0 when unknown.
    Q_INVOKABLE double peak(const QString &key) const { return m_peaks.value(key, 0.0); }
    Q_INVOKABLE QString mixOutputDevice(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("OutputDevice")).toString(); }
    Q_INVOKABLE QString mixCaptureSource(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("CaptureSource")).toString(); }
    Q_INVOKABLE void    setMixOutputDevice(const QString &slug, const QString &nodeName);
    Q_INVOKABLE void    renameChannel(const QString &slug, const QString &name);
    Q_INVOKABLE void    renameMix(const QString &slug, const QString &name);
    Q_INVOKABLE void    setChannelIcon(const QString &slug, const QString &icon);   // UX-8
    Q_INVOKABLE void    setMixIcon(const QString &slug, const QString &icon);
    Q_INVOKABLE void    moveChannel(const QString &slug, int index);              // UX-9
    Q_INVOKABLE void    moveMix(const QString &slug, int index);
    Q_INVOKABLE void   moveApp(const QString &appPath, const QString &channelSlug);
    Q_INVOKABLE void   assignApp(const QString &appPath, const QStringList &channelSlugs, bool addOn);   // CH-12/UX-11
    Q_INVOKABLE void   audition(const QString &kind, const QString &slug);   // UX-12 press
    Q_INVOKABLE void   stopAudition();                                        // UX-12 release
    Q_INVOKABLE void   setChannelDevice(const QString &slug, const QString &deviceNode);
    Q_INVOKABLE QString channelDevice(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("InputDevice")).toString(); }
    Q_INVOKABLE bool    channelInputPresent(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("InputPresent"), true).toBool(); }
    Q_INVOKABLE QStringList mixOutputs(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("Outputs")).toStringList(); }
    Q_INVOKABLE QString mixFallbackOutput(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("FallbackOutput")).toString(); }
    Q_INVOKABLE void    setMixFallbackOutput(const QString &slug, const QString &node);
    Q_INVOKABLE void    toggleMixOutput(const QString &slug, const QString &node);   // MX-9: add if absent, remove if present
    Q_INVOKABLE QString deviceDescription(const QString &ref) const {
        if (ref.contains(QLatin1Char(':'))) return deviceRefLabel(ref);   // ADR 0009 port ref
        const QString &node = ref;
        if (m_outputDevices.contains(node)) return m_outputDevices.value(node);
        if (m_inputDevices.contains(node)) return m_inputDevices.value(node);
        for (auto it = m_mixes.constBegin(); it != m_mixes.constEnd(); ++it) {   // unplugged: the daemon remembers the name (DV-9)
            const auto outs = it->value(QStringLiteral("Outputs")).toStringList(), descs = it->value(QStringLiteral("OutputDescriptions")).toStringList();
            const int i = outs.indexOf(node); if (i >= 0 && i < descs.size()) return descs.at(i);
        }
        return node;
    }
    Q_INVOKABLE double  mixVolume(const QString &slug) const { return std::cbrt(m_mixes.value(slug).value(QStringLiteral("Volume"), 1.0).toDouble()); }
    Q_INVOKABLE bool    mixMuted(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("Muted"), false).toBool(); }
    Q_INVOKABLE void    setMixVolume(const QString &slug, double cubic);
    Q_INVOKABLE void    toggleMixMute(const QString &slug);
    Q_INVOKABLE bool    mixOutputPresent(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("OutputPresent"), true).toBool(); }

    // ---- effects (ADR 0008): kind is "channel" or "mix"
    Q_INVOKABLE QString channelIcon(const QString &slug) const { const auto i = m_channels.value(slug).value(QStringLiteral("Icon")).toString(); return i.isEmpty() ? QStringLiteral("audio-card") : i; }
    Q_INVOKABLE QString channelIconRaw(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("Icon")).toString(); }   // "" = default
    Q_INVOKABLE QString mixIconRaw(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("Icon")).toString(); }
    // MX-5 colour code, "" = theme. One accessor for every frontend (window header, tray row, patchbay card).
    Q_INVOKABLE QString channelColor(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("Color")).toString(); }
    Q_INVOKABLE QString mixColor(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("Color")).toString(); }
    Q_INVOKABLE void setChannelColor(const QString &slug, const QString &color);
    Q_INVOKABLE void setMixColor(const QString &slug, const QString &color);
    // the palette every colour picker offers — same eight everywhere
    Q_INVOKABLE QStringList colorPalette() const { return {QStringLiteral("#e93d58"), QStringLiteral("#ef973c"), QStringLiteral("#e8cb2d"), QStringLiteral("#3dd425"), QStringLiteral("#00d3b8"), QStringLiteral("#3daee9"), QStringLiteral("#b875dc"), QStringLiteral("#926ee4")}; }
    Q_INVOKABLE QString mixIcon(const QString &slug) const { const auto i = m_mixes.value(slug).value(QStringLiteral("Icon")).toString(); return i.isEmpty() ? QStringLiteral("audio-headphones") : i; }
    /// True when a non-empty, enabled effect chain sits on that channel/mix (ADR 0008) — for the highlighted FX button.
    Q_INVOKABLE bool    fxEnabled(const QString &kind, const QString &slug) const;
    Q_INVOKABLE QString fxChain(const QString &kind, const QString &slug) const;
    Q_INVOKABLE void    setFxChain(const QString &kind, const QString &slug, const QString &chainJson);
    Q_INVOKABLE void    setFxControl(const QString &kind, const QString &slug, const QString &control, double value);
    Q_INVOKABLE QVariantList fxTypes() const { return m_fxTypes; }
    Q_INVOKABLE QVariantMap fxPresets() const { return m_fxPresets; }

Q_SIGNALS:
    void errorOccurred(const QString &message);
    void defaultChannelChanged();
    void listeningDeviceChanged();
    void hideToTrayChanged();
    void undoChanged();   // a refused request (duplicate name, unknown device, …)
    void connectedChanged();
    void serviceAvailableChanged();
    void layoutChanged();
    void cellChanged(const QString &ch, const QString &mix);
    void channelChanged(const QString &slug);
    void appsChanged();
    void outputDevicesChanged();
    void inputDevicesChanged();
    void devicePortsChanged();
    void fxTypesReady();
    void metersEnabledChanged();
    void peaksChanged();                                        // once per tick
    void lastErrorChanged();
    void mixChanged(const QString &slug);

private Q_SLOTS:
    void onPeaks(const QVariantMap &peaks);
    void onPropertiesChanged(const QDBusMessage &msg);
    void onInterfacesAdded(const QDBusObjectPath &path, const InterfaceMap &ifaces);
    void onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &ifaces);
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    void callReportingErrors(const QString &method, const QVariant &arg, const QVariant &arg2 = QVariant());
    static QString cellKey(const QString &ch, const QString &mix) { return ch + QLatin1Char('/') + mix; }
    void refresh();                    // GetManagedObjects → rebuild mirror
    void absorb(const QString &path, const QString &iface, const QVariantMap &props, bool *layout);
    void setProperty(const QString &path, const QString &iface, const QString &name, const QVariant &v);

    bool m_available = false, m_pwConnected = false;
    QMap<QString, QVariantMap> m_channels, m_mixes, m_cells;   // keyed by slug / slug / "ch/mix"
    QVariantList m_fxTypes; QVariantMap m_fxPresets;           // FX catalog + presets, read once
    bool m_hideToTray = true;
    QMap<QString, QVariantMap> m_apps;                          // keyed by object path
    QMap<QString, QString> m_outputDevices, m_inputDevices;    // node.name → description (both directions)
    PortMap m_devicePorts;                                     // node.name → ["POS|port.name|alias", …] (ADR 0009)
    int m_devicePortsVersion = 0;
    QString m_defaultChannel, m_undoDescription, m_listeningDevice;
    bool m_metersEnabled = false;
    QHash<QString, double> m_peaks;
    QStringList m_channelOrder, m_mixOrder;
    QString m_lastError;
};

} // namespace kmixdeck::frontend
