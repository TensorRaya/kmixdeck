// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once

#include <QObject>
#include <QTimer>
#include <QPair>
#include <QAbstractListModel>
#include <QVector>
#include <QSet>
#include <QString>
#include <optional>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include "pipewire/graph.h"
#include "pipewire/meters.h"
#include "layout.h"

namespace kmixdeck {

/// Naming convention from ADR 0002. A slug is [a-z0-9_]+ (D-Bus path safe) derived once, empty = invalid, from the display name
/// and never changed afterwards (DV-7), so renames never move volumes.
struct Names {
    static QString channelNode(const QString &slug) { return QStringLiteral("kmixdeck.channel.") + slug; }
    static QString mixNode(const QString &slug)     { return QStringLiteral("kmixdeck.mix.") + slug; }
    static QString cellNode(const QString &ch, const QString &mix) { return QStringLiteral("kmixdeck.link.%1.%2").arg(ch, mix); }
    static QString slugify(const QString &display);
};

struct Channel { QString slug; QString name; QString icon; bool virt = true; };
/// A running application audio stream (Stream/Output/Audio that is not one of ours). CH-12: channels holds every
/// assigned channel; the first is the primary target (what WirePlumber restores), the rest are relayed to.
struct App { uint32_t id = 0; QString name, binary, mediaName, mediaRole, nodeName, iconName; QStringList channels; bool running = false; };
struct Mix     { QString slug; QString name; QString icon; bool capture = true; };
/// A device the daemon can see right now (ADR 0007): node.name → face + channel layout.
struct Device  { QString node, description; QStringList positions; bool isSource = false; };

/// The (channel × mix) matrix. One fader per cell = channelVolumes on the cell's loopback playback
/// stream (validated in ADR 0002). Cubic UI curve ↔ linear PipeWire volume conversion lives here.
class Mixer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QStringList channelSlugs READ channelSlugs NOTIFY layoutChanged)
    Q_PROPERTY(QStringList mixSlugs READ mixSlugs NOTIFY layoutChanged)
public:
    explicit Mixer(QObject *parent = nullptr);

    /// The declared layout (source of truth). Loaded from layoutPath at start; saved on every edit.
    const Layout &layout() const { return m_layout; }
    void setLayoutPaths(const QString &jsonPath, const QString &pwConfPath) { m_layoutPath = jsonPath; m_pwConfPath = pwConfPath; }
    bool loadLayout();                 // returns false if missing/corrupt (then keeps current)
    bool saveLayout() const;           // JSON + pipewire.conf.d fragment (DV-1, DV-5)
    void reconcile();                  // make PipeWire match the layout (create missing nodes)

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
    /// MX-7: (ch, mix) follows (ch, follows): volume+mute mirrored; "" = unlinked. Any direct write to the
    /// follower breaks the link (Wave Link semantics: "can be broken at any time").
    Q_INVOKABLE QString cellFollows(const QString &ch, const QString &mix) const;
    bool setCellFollows(const QString &ch, const QString &mix, const QString &follows);

    /// Channel-wide trim (CH-7) = channelVolumes on the channel null sink → affects every mix.
    Q_INVOKABLE double channelTrim(const QString &slug) const;
    Q_INVOKABLE bool   channelMuted(const QString &slug) const;
    Q_INVOKABLE void   setChannelTrim(const QString &slug, double linear);
    Q_INVOKABLE void   setChannelMuted(const QString &slug, bool muted);
    /// Mix master (MX-6): volume/mute on the mix sink itself → hits every output AND the capture source at once.
    Q_INVOKABLE double mixVolume(const QString &slug) const;
    Q_INVOKABLE bool   mixMuted(const QString &slug) const;
    Q_INVOKABLE void   setMixVolume(const QString &slug, double linear);
    Q_INVOKABLE void   setMixMuted(const QString &slug, bool muted);
    Q_INVOKABLE void   renameChannel(const QString &slug, const QString &name);
    Q_INVOKABLE void   renameMix(const QString &slug, const QString &name);
    Q_INVOKABLE void   setChannelIcon(const QString &slug, const QString &icon);   // UX-8: persisted in the layout
    Q_INVOKABLE void   setMixIcon(const QString &slug, const QString &icon);
    Q_INVOKABLE bool   moveChannel(const QString &slug, int index);               // UX-9: reorder, index clamped
    Q_INVOKABLE bool   moveMix(const QString &slug, int index);
    QString channelIcon(const QString &slug) const;
    QString mixIcon(const QString &slug) const;
    Q_INVOKABLE QString mixCaptureSource(const QString &slug) const;
    // ---- effects per channel/mix (ADR 0008) --------------------------------------------------------
    /// The chain of a channel or mix as JSON ({enabled, chain:[{type,params,…}]}). {} when none.
    Q_INVOKABLE QJsonObject fxChain(const QString &slug) const;
    /// Validate + store + apply the chain (live, no PipeWire restart). False keeps the old chain; the
    /// refusal reason goes to the log. An empty chain clears the effects.
    Q_INVOKABLE bool setFxChain(const QString &slug, const QJsonObject &chainJson);
    /// Live control update for one object's chain. `control` is the short key ("threshold") or the full
    /// Props key ("gate:Threshold (dB)"); resolved against the object's own chain, first match wins.
    Q_INVOKABLE bool setFxControl(const QString &slug, const QString &control, double value);
    /// The built-in catalog for UIs (FX-4): [{type,label,description,params:[{key,label,unit,min,max,def}]}].
    Q_INVOKABLE QJsonArray fxTypes() const;
    /// One-click chains, name → chain JSON (FX-4). Editable afterwards: a preset is just a starting chain.
    Q_INVOKABLE QJsonObject fxPresets() const;

    /// Bus-facing single-device view of the output list (first entry; empty when none).
    Q_INVOKABLE QString mixOutputDevice(const QString &slug) const;
    Q_INVOKABLE void    setMixOutputDevice(const QString &slug, const QString &nodeName);

    // ---- device edges (ADR 0007) -----------------------------------------------------------------------
    /// Outputs of a mix as configured (present or not). Empty = plays nowhere.
    QVector<DeviceRef> mixOutputs(const QString &slug) const;
    DeviceRef deviceRef(const QString &nodeName) const;    // description filled from the live graph when known
    /// Replace the whole output list (MX-9). Unknown node names are accepted: the device may be unplugged
    /// right now (DV-11) — the loopback waits for it (DV-12).
    bool setMixOutputs(const QString &slug, const QVector<DeviceRef> &outputs);
    DeviceRef mixFallbackOutput(const QString &slug) const;
    void setMixFallbackOutput(const QString &slug, const DeviceRef &dev);
    /// Is this device node in the graph right now?
    bool devicePresent(const QString &nodeName) const { return m_devices.contains(nodeName); }
    /// Level/mute of one mix output (DV-14): channelVolumes on kmixdeck.out.<mix>.<n>.
    double mixOutputVolume(const QString &slug, int index) const;
    bool   mixOutputMuted(const QString &slug, int index) const;
    void   setMixOutputVolume(const QString &slug, int index, double cubic, bool muted);

    QStringList inputSlugs() const;
    const LayoutInput *input(const QString &slug) const { return m_layout.input(slug); }
    /// Add a physical input feeding `channel` (CH-3). Returns the slug ("" on failure).
    QString addInput(const QString &displayName, const DeviceRef &device, const QString &channel);
    void removeInput(const QString &slug);
    bool setInputChannel(const QString &slug, const QString &channel);
    bool setInputDevice(const QString &slug, const DeviceRef &device);
    /// Input trim/mute (DV-14) = channelVolumes on kmixdeck.in.<slug> (playback side).
    double inputVolume(const QString &slug) const;
    bool   inputMuted(const QString &slug) const;
    void   setInputVolume(const QString &slug, double cubic, bool muted);
    bool   inputPresent(const QString &slug) const;
    // One-input-per-channel view for the bus (Channel.InputDevice, CH-3): the input slug IS the channel slug.
    QString channelInputDevice(const QString &channel) const;
    bool    channelInputPresent(const QString &channel) const;
    bool    setChannelInputDevice(const QString &channel, const QString &ref);   // ref = node[:POS,POS] (ADR 0009)
    /// ADR 0009 D4: non-monitor ports of a device node, as "POSITION|port.name|port.alias" — for pickers.
    QStringList devicePorts(const QString &nodeName) const;
    /// Validate a reference against the live device: node known and every position an actual port. Empty = ok.
    QString validateDeviceRef(const DeviceRef &ref, bool wantSource) const;
    /// false only when an output is configured and its device is currently not in the graph (DV-9).
    bool    mixOutputPresent(const QString &slug) const;

    /// Peak meters (ADR 0006); owned here so they share the graph's loop.
    pw::Meters *meters() { return &m_meters; }

    /// Hardware (non-kmixdeck) sinks a mix can play to (DV-8) / sources that can feed a channel (DV-10).
    QList<Device> outputDevices() const;
    QList<Device> inputDevices() const;

    /// Running application streams (CH-4/CH-10).
    QList<uint32_t> appIds() const;
    std::optional<App> app(uint32_t id) const;
    /// Route an app stream to a channel. WirePlumber remembers it (restore-target) keyed by the stream's
    /// media.role → application.id → application.name → media.name → node.name (state-stream.lua formKey).
    Q_INVOKABLE bool moveApp(uint32_t id, const QString &channelSlug);
    /// CH-12: assign an app to several channels at once — first = primary target, the rest get relay loopbacks.
    /// Empty list = un-route everything. Cumulative=true adds the channel keeping the rest (UX-11 drop on a row).
    Q_INVOKABLE bool assignApp(uint32_t id, const QStringList &channelSlugs, bool cumulative = false);
    /// CH-5: default channel for applications kmixdeck has never seen. Empty string = off.
    QString defaultChannel() const { return m_layout.defaultChannel; }
    /// UX-2: listening device (node.name), persisted in the layout. Any string accepted — the device may be unplugged.
    QString listeningDevice() const { return m_layout.listeningDevice; }
    void setListeningDevice(const QString &node);
    bool    setDefaultChannel(const QString &slug);

    /// UX-12 solo audition: while a channel or mix is auditioned, exactly that entity plays to the main output;
    /// everything else returns to its previous routing on stopAudition(). Only one entity at a time.
    Q_INVOKABLE void startAudition(const QString &kind, const QString &slug);   // kind: "channel" | "mix"
    Q_INVOKABLE void stopAudition();
    Q_INVOKABLE QString auditionTarget() const;   // "" when nothing is auditioned

    /// Layout edits (MX-1: any number of mixes; CH-2: any number of channels).
    /// Returns the new slug, or empty with *error set (empty slug, duplicate). Never invents a name.
    QString addChannel(const QString &displayName, QString *error = nullptr);
    QString addMix(const QString &displayName, QString *error = nullptr);
    Q_INVOKABLE void removeChannel(const QString &slug);
    Q_INVOKABLE void removeMix(const QString &slug);
    /// CH-9: the last removal (channel or mix) can be undone — layout entry, links, inputs/outputs AND every
    /// cell's fader/mute come back. One level; anything that mutates the layout afterwards clears it.
    Q_INVOKABLE QString undoDescription() const { return m_undo.isEmpty() ? QString() : m_undo.value(QStringLiteral("what")).toString(); }
    Q_INVOKABLE bool   undo();

    static double linearToCubic(float lin) { return std::cbrt(static_cast<double>(lin)); }
    static float  cubicToLinear(double cub) { return static_cast<float>(cub * cub * cub); }

Q_SIGNALS:
    void connectedChanged();
    void layoutChanged();
    void cellChanged(const QString &ch, const QString &mix);
    void channelChanged(const QString &slug);
    void mixChanged(const QString &slug);
    void appAdded(uint32_t id);
    void appChanged(uint32_t id);
    void appRemoved(uint32_t id);
    void outputDevicesChanged();
    void inputDevicesChanged();
    void inputChanged(const QString &slug);
    void inputsChanged();                       // list of inputs changed
    void defaultChannelChanged();
    void listeningDeviceChanged();
    void undoChanged();

private:
    void onNode(const pw::NodeInfo &n);
    void onNodeRemoved(uint32_t id);
    void onStreamRouted(uint32_t streamId, uint32_t sinkId);
    void finishAutoRoute(uint32_t id);
    QString slugForSinkId(uint32_t sinkId) const;
    void rebuildLayoutFromGraph();

    pw::Graph m_graph;
    pw::Meters m_meters{&m_graph};
    bool m_connected = false;
    QVector<Channel> m_channels;
    QVector<Mix> m_mixes;
    QHash<QString, pw::NodeInfo> m_cells;      // key: cell node name
    QHash<QString, pw::NodeInfo> m_sinks;
    QHash<QString, pw::NodeInfo> m_devices;    // foreign Audio/Sink + Audio/Source nodes, key: node name
    QHash<QString, pw::NodeInfo> m_edges;      // kmixdeck.in.* / kmixdeck.out.* playback streams, key: node name
    void applyFallbacks();
    void ensureEdgeLoopbacks();
    void ensureEdgeLoopbackForInput(const QString &slug);
    void ensureAppRelays(const LayoutApp &a);            // CH-12: relay loopbacks for extra channels
    void retargetWhenPresent(const QString &entry, const QList<uint32_t> &streams, int triesLeft);   // FX-5: after a chain swap
    void removeAppRelays(const LayoutApp &a);
    void notifyPresence();
    void snapshotForUndo(const QString &kind, const QString &slug);
    void restorePendingCellStates();
    QJsonObject m_undo;                                  // {"what","kind","layout":{…},"cells":[{ch,mix,volume,mute}], "links":[…], "inputs":[…]}
    QHash<QString, QPair<float, bool>> m_pendingCellState;   // cell node → (volume, mute) to apply once the node exists
    void applyFx(const QString &slug);                                   // rebuild one chain live (ADR 0008)
    void propagateLinks(const QString &ch, const QString &sourceMix);   // source cell changed → push to followers
    void breakLink(const QString &ch, const QString &mix);
    void autoRouteNewApp(const App &a);
    static QString appKey(const App &a) { return a.name.isEmpty() ? a.nodeName : a.name; }
    void destroyOurNodes(const std::function<bool(const QString &)> &match);
    QList<Device> devicesFor(const QString &mediaClass) const;
    QHash<uint32_t, App> m_apps;
    QSet<uint32_t> m_pendingAutoRoute;   // CH-5: apps waiting for WirePlumber's first link before we decide
    struct Audition {                                            // UX-12 snapshot: slug → (volume/trim, mute)
        QString kind, slug;
        QHash<QString, QPair<float, bool>> channels, mixes;
    };
    Audition m_audition;
    Layout m_layout;
    QString m_layoutPath, m_pwConfPath;
    bool m_reconciled = false;
    QTimer m_reconnect;
    int m_reconnectMs = 500;      // channel + mix null sinks, key: node name
    QHash<uint32_t, QString> m_idToName;
};

} // namespace kmixdeck
