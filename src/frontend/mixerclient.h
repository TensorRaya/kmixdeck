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
    Q_PROPERTY(QVariantList hiddenDevices READ hiddenDevices NOTIFY hiddenDevicesChanged)  // CH-11: [{nodeName, description, present}]
    Q_PROPERTY(bool firstRun READ firstRun NOTIFY firstRunChanged)                          // UX-3: daemon started without a layout
    Q_PROPERTY(QString defaultSink READ defaultSink NOTIFY defaultDevicesChanged)           // UX-3
    Q_PROPERTY(QString defaultSource READ defaultSource NOTIFY defaultDevicesChanged)
    Q_PROPERTY(int devicePortsVersion READ devicePortsVersion NOTIFY devicePortsChanged)   // bump → QML re-asks devicePorts()
    Q_PROPERTY(bool metersEnabled READ metersEnabled WRITE setMetersEnabled NOTIFY metersEnabledChanged)   // Levels.Subscribe while true
    Q_PROPERTY(QString defaultChannel READ defaultChannel WRITE setDefaultChannel NOTIFY defaultChannelChanged)   // CH-5, slug or ""
    Q_PROPERTY(QString listeningDevice READ listeningDevice WRITE setListeningDevice NOTIFY listeningDeviceChanged)   // UX-2, node.name or ""
    Q_PROPERTY(QString undoDescription READ undoDescription NOTIFY undoChanged)   // CH-9
    Q_PROPERTY(QStringList scenes READ scenes NOTIFY scenesChanged)   // CT-9: named snapshots, empty until the user saves one
    // CT-8: the tray binds to this, so it has to be a NOTIFYing property — samples() as a plain function call is
    // evaluated once and never again (measured 2026-09-22: the tray button kept its first label while a sample
    // started and stopped). Same shape as `scenes` right above.
    Q_PROPERTY(QVariantList allSamples READ samples NOTIFY samplesChanged)
    // CT-8: the window's "Soundboard…" action binds to this. A Q_INVOKABLE would be evaluated once and then never
    // react to a board being added or removed (same trap as allSamples above).
    Q_PROPERTY(QStringList soundboardSlugs READ soundboardSlugs NOTIFY layoutChanged)
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
    QStringList scenes() const { return m_scenes; }
    Q_INVOKABLE void undo();
    // CT-9: the window and the tray drive scenes through these — no frontend talks to the daemon directly (AR-13).
    Q_INVOKABLE void saveScene(const QString &name);
    Q_INVOKABLE void recallScene(const QString &name, bool exclusive = true);
    Q_INVOKABLE void deleteScene(const QString &name);
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
    QVariantList hiddenDevices() const;
    bool firstRun() const { return m_firstRun; }
    QString defaultSink() const { return m_defaultSink; }
    QString defaultSource() const { return m_defaultSource; }
    Q_INVOKABLE QVariantMap firstRunPlan() const;     // UX-3: the daemon's plan, for the wizard to show
    Q_INVOKABLE QVariantMap firstRunApply();          // UX-3: do it; {} + lastError on failure
    Q_INVOKABLE void dismissFirstRun() { if (m_firstRun) { m_firstRun = false; Q_EMIT firstRunChanged(); } }   // "I'll set it up myself" — window-local, the daemon keeps FirstRun until something is saved
    Q_INVOKABLE bool deviceInUse(const QString &node) const;
    Q_INVOKABLE void setDeviceHidden(const QString &node, bool hidden);   // CH-11
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
    // UX-18: EBU R128 for a mix. The daemon sends [M, S, I, true-peak] per mix on Levels.Loudness,
    // LUFS resp. dBTP, with -70 standing in for "nothing yet" (silence, or the gate never opened).
    // `which`: 0=M 1=S 2=I 3=TP. Anything unknown reads -70 so the UI has one rule for "no value".
    Q_INVOKABLE double loudness(const QString &slug, int which) const {
        const QList<double> v = m_loudness.value(slug);
        return which >= 0 && which < v.size() ? v.at(which) : -70.0;
    }
    /// True while the daemon is actually delivering R128 numbers for this mix (analyser on AND a reading in).
    Q_INVOKABLE bool loudnessLive(const QString &slug) const { return m_loudness.contains(slug); }
    Q_INVOKABLE QString mixOutputDevice(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("OutputDevice")).toString(); }
    Q_INVOKABLE QString mixCaptureSource(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("CaptureSource")).toString(); }
    Q_INVOKABLE void    setMixOutputDevice(const QString &slug, const QString &nodeName);
    Q_INVOKABLE void    setMixLoudness(const QString &slug, bool on);              // UX-18
    Q_INVOKABLE void    setMixLoudnessTarget(const QString &slug, double lufs);    // UX-18
    Q_INVOKABLE void    renameChannel(const QString &slug, const QString &name);
    Q_INVOKABLE void    renameMix(const QString &slug, const QString &name);
    Q_INVOKABLE void    duplicateMix(const QString &slug, const QString &name);   // MX-8
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
    // UX-18: analyser state and target of a mix, straight from the cached Mix properties
    Q_INVOKABLE bool    mixLoudness(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("Loudness"), false).toBool(); }
    Q_INVOKABLE double  mixLoudnessTarget(const QString &slug) const { return m_mixes.value(slug).value(QStringLiteral("LoudnessTarget"), -14.0).toDouble(); }
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
    Q_INVOKABLE QString channelGroup(const QString &slug) const { return m_channels.value(slug).value(QStringLiteral("Group")).toString(); }   // CH-8
    Q_INVOKABLE void setChannelGroup(const QString &slug, const QString &group);
    Q_INVOKABLE QStringList channelGroups() const;   // every distinct group name, sorted
    Q_INVOKABLE void setMixColor(const QString &slug, const QString &color);
    // the palette every colour picker offers — same eight everywhere
    Q_INVOKABLE QStringList colorPalette() const { return {QStringLiteral("#e93d58"), QStringLiteral("#ef973c"), QStringLiteral("#e8cb2d"), QStringLiteral("#3dd425"), QStringLiteral("#00d3b8"), QStringLiteral("#3daee9"), QStringLiteral("#b875dc"), QStringLiteral("#926ee4")}; }
    Q_INVOKABLE QString mixIcon(const QString &slug) const { const auto i = m_mixes.value(slug).value(QStringLiteral("Icon")).toString(); return i.isEmpty() ? QStringLiteral("audio-headphones") : i; }
    /// True when a non-empty, enabled effect chain sits on that channel/mix (ADR 0008) — for the highlighted FX button.
    Q_INVOKABLE bool    fxEnabled(const QString &kind, const QString &slug) const;
    Q_INVOKABLE QString fxChain(const QString &kind, const QString &slug) const;
    // FX-9 ducking (channels only — a mix has no trigger). ducking() reads the cached property, so it needs
    // Ducking to be in ChannelObject::properties(); setDucking() calls the METHOD and waits, because the reason
    // for a refusal has to reach the user (a property write cannot answer an error — see rejectProperty).
    Q_INVOKABLE QVariantMap ducking(const QString &slug) const;
    Q_INVOKABLE bool        setDucking(const QString &slug, const QVariantMap &cfg);
    Q_INVOKABLE double      duckReduction(const QString &slug) const;

    // CT-8 soundboard. samples()/isSoundboard() read cached properties (Mixer.Samples, Channel.Kind), the rest
    // call methods and block — same reason as setDucking above: a refusal has to reach the user, and a
    // fire-and-forget call would leave a broken file looking registered.
    Q_INVOKABLE QVariantList samples(const QString &channel = QString()) const;
    Q_INVOKABLE bool         isSoundboard(const QString &slug) const;
    QStringList              soundboardSlugs() const;
    Q_INVOKABLE QString      addSoundboard(const QString &name);
    Q_INVOKABLE QString      addSample(const QString &channel, const QString &path, const QString &name = QString());
    Q_INVOKABLE bool         removeSample(const QString &channel, const QString &name);
    Q_INVOKABLE bool         playSample(const QString &channel, const QString &name);
    Q_INVOKABLE bool         stopSample(const QString &name = QString());
    Q_INVOKABLE bool         setSampleGain(const QString &channel, const QString &name, double gain);
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
    void scenesChanged();   // CT-9
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
    void loudnessChanged();                                     // UX-18, same tick rate as peaksChanged
    void lastErrorChanged();
    void samplesChanged();   // CT-8
    void hiddenDevicesChanged();
    void firstRunChanged();
    void defaultDevicesChanged();
    void mixChanged(const QString &slug);

private Q_SLOTS:
    void onPeaks(const QVariantMap &peaks);
    void onLoudness(const QDBusMessage &msg);   // UX-18: a{sad}, needs the raw message to demarshal
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
    QStringList m_scenes;   // CT-9
    bool m_metersEnabled = false;
    QHash<QString, double> m_peaks;
    QHash<QString, QList<double>> m_loudness;   // UX-18: slug -> [M, S, I, TP]
    QStringList m_channelOrder, m_mixOrder;
    QString m_lastError;
    QVariantList m_samples;   // CT-8: Mixer.Samples cache, rows of {channel,name,path,length,gain,sounding}
    QStringList m_hiddenDevices;
    bool m_firstRun = false; QString m_defaultSink, m_defaultSource;
};

} // namespace kmixdeck::frontend
