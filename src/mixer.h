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
    // CH-8 channel groups: channels sharing a group name move together — trim changes are applied as the same dB
    // delta to every member (so the balance you set between them survives), mute is mirrored. No master, no owner:
    // touch any member, all follow. A group of one is just a label.
    QString channelGroup(const QString &slug) const { const auto *c = m_layout.channel(slug); return c ? c->group : QString(); }
    bool setChannelGroup(const QString &slug, const QString &group);
    QStringList groupMembers(const QString &group) const;

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
    // MX-5 colour code: "#rrggbb" or "" (theme). Validated here so every frontend sees the same value.
    Q_INVOKABLE bool   setChannelColor(const QString &slug, const QString &color);
    Q_INVOKABLE bool   setMixColor(const QString &slug, const QString &color);
    QString channelColor(const QString &slug) const { const auto *c = m_layout.channel(slug); return c ? c->color : QString(); }
    QString mixColor(const QString &slug) const     { const auto *m = m_layout.mix(slug);     return m ? m->color : QString(); }
    QString mixIcon(const QString &slug) const;
    Q_INVOKABLE QString mixCaptureSource(const QString &slug) const;
    // ---- effects per channel/mix (ADR 0008) --------------------------------------------------------
    /// The chain of a channel or mix as JSON ({enabled, chain:[{type,params,…}]}). {} when none.
    Q_INVOKABLE QJsonObject fxChain(const QString &slug) const;
    /// Validate + store + apply the chain (live, no PipeWire restart). False keeps the old chain; the
    /// refusal reason goes to the log. An empty chain clears the effects.
    /// FX-8: why_out (optional) carries the refusal reason out — "effect 'noise' needs
    /// librnnoise_ladspa — noise-suppression-for-voice (Arch/AUR) or …". Logging it and
    /// returning a bare false leaves the D-Bus caller with nothing to show the user.
    Q_INVOKABLE bool setFxChain(const QString &slug, const QJsonObject &chainJson, QString *why_out = nullptr);
    // FX-9: side-chain ducking for one channel. `json` is {duckedBy, depth, attack, release, threshold};
    // an empty duckedBy switches ducking off. Returns false with the reason in why_out.
    Q_INVOKABLE bool setDucking(const QString &slug, const QJsonObject &json, QString *why_out = nullptr);
    Q_INVOKABLE QJsonObject ducking(const QString &slug) const;
    /// The gain reduction the ducker applies right now, in dB (0 = not ducking, negative = ducking).
    Q_INVOKABLE double duckReduction(const QString &slug) const;
    void applyDucking(const QString &slug);
    /// FX-9: wire the ducker's AUX0 input to the trigger channel once both nodes exist. The module's node
    /// appears asynchronously, same as an FX chain's — hence the retry, modelled on retargetWhenPresent.
    void linkDuckTriggerWhenPresent(const QString &slug, const QString &triggerNode, int triesLeft);
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
    bool mixChainActive(const QString &slug) const;   // FX-10: is a mix chain live (gain-reduction meter)?
    // CT-9: named snapshots of the MIXABLE state — cell faders/mutes, mix masters/mutes, listening device, FX
    // bypass. Not the channel/mix set and not the wiring: a scene must stay applicable after a rename or a
    // device swap, so it stores slugs and values, never node ids or link topology.
    Q_INVOKABLE QStringList scenes() const;
    Q_INVOKABLE bool saveScene(const QString &name);
    Q_INVOKABLE bool recallScene(const QString &name, bool exclusive = true);
    bool applyScene(const QJsonObject &sc, bool exclusive);   // CT-9: shared by recallScene() and undo()
    Q_INVOKABLE bool deleteScene(const QString &name);
    bool mixLoudness(const QString &slug) const; void setMixLoudness(const QString &slug, bool on);            // UX-18
    double mixLoudnessTarget(const QString &slug) const; void setMixLoudnessTarget(const QString &slug, double lufs);
    QStringList loudnessMixNodes() const;   // UX-18: mix sinks that want an R128 analyser
    double inputTrimLayout(const QString &slug) const;                    // DV-14: the wire's persisted value (also while unplugged)
    bool   inputMutedLayout(const QString &slug) const;
    double mixOutputTrimLayout(const QString &slug, int index) const;
    bool   mixOutputMutedLayout(const QString &slug, int index) const;
    void   applyEdgeState(const pw::NodeInfo &n);
    /// DV-14 by wire ref: input wires of a channel (ref → input slug) and output wires of a mix (ref → index).
    QString inputSlugForWire(const QString &channel, const QString &ref) const;
    bool   setChannelWireTrim(const QString &channel, const QString &ref, double trim, bool muted);
    bool   channelWireTrim(const QString &channel, const QString &ref, double *trim, bool *muted) const;
    bool   setMixWireTrim(const QString &mix, const QString &ref, double trim, bool muted);
    bool   mixWireTrim(const QString &mix, const QString &ref, double *trim, bool *muted) const;
    bool   inputMuted(const QString &slug) const;
    void   setInputVolume(const QString &slug, double cubic, bool muted);
    bool   inputPresent(const QString &slug) const;
    // One-input-per-channel view for the bus (Channel.InputDevice, CH-3): the input slug IS the channel slug.
    QString channelInputDevice(const QString &channel) const;
    bool    channelInputPresent(const QString &channel) const;
    bool    setChannelInputDevice(const QString &channel, const QString &ref);   // ref = node[:POS,POS] (ADR 0009)
    // ADR 0009 B1 — wires: a channel may take several inputs (one loopback edge each). Refs in wire order.
    // DV-22: pan of a channel, −1..+1, constant-power law on the channel sink's L/R volumes; persisted in the layout
    double channelPan(const QString &slug) const;
    void   setChannelPan(const QString &slug, double pan);
    void   applyChannelGain(const QString &slug);   // trim × pan → L/R volumes on the sink
    QStringList channelInputs(const QString &channel) const;
    QString     addChannelInput(const QString &channel, const QString &ref);      // returns the wire's input slug, "" on refusal
    bool        removeChannelInput(const QString &channel, const QString &ref);
    bool        channelInputPresentRef(const QString &channel, const QString &ref) const;
    bool        hasAnyInputFor(const QString &channel) const;
    // DV-23 virtual multichannel devices (Ui24R stand-in for testing)
    QString addVirtualDevice(const QString &displayName, int inputs, int outputs, QString *error);
    bool    removeVirtualDevice(const QString &slug);
    QStringList virtualDeviceSlugs() const;
private:
    // Nodes we asked PipeWire to create that have not shown up in the registry yet. reconcile() runs again on
    // every graph event; without this, two reconciles a few ms apart created the same node twice (DV-23 test).
    QSet<QString> m_nodeRequested;
    /// DV-30: loopbacks we asked for whose playback node has not shown up yet, with the moment we asked. A node still
    /// missing after ~3 s is an edge that failed asynchronously (EMFILE shows as "Protocol error" on the module's own
    /// core, pw_context_load_module returns success) → LastError. Key: playback node name; value: description + ms.
    QHash<QString, QPair<QString, qint64>> m_edgePending;
    QTimer m_edgeWatch;
    void expectEdge(const QString &playbackNode, const QString &description);
    void checkPendingEdges();
    void requestNullNode(const QString &name, const std::function<void()> &create);
public:
    /// ADR 0009 D4: non-monitor ports of a device node, as "POSITION|port.name|port.alias" — for pickers.
    QStringList devicePorts(const QString &nodeName) const;
    /// Validate a reference against the live device: node known and every position an actual port. Empty = ok.
    QString validateDeviceRef(const DeviceRef &ref, bool wantSource) const;
    /// false only when an output is configured and its device is currently not in the graph (DV-9).
    bool    mixOutputPresent(const QString &slug) const;

    // FX-9: the most recent peak per node, kept so duckReduction() can compare the ducker's two sides.
    // Filled from Meters::peaks, which ticks 25x/s — one hash assignment per tick, no extra streams.
    QHash<QString, float> m_lastPeaks;

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
    // CT-7 backup/restore: the whole layout PLUS every fader, trim and mute as one JSON document. import() replaces the
    // layout, rebuilds the graph and applies the levels as each node comes up (same pending mechanism as undo()).
    // UX-3 first run: what the wizard would do, as one daemon call. `plan` only describes, `apply` does it.
    // {defaultSink, defaultSource, sinkKnown, sourceKnown, runningApps:[{id,name,channel}], firstRun}
    QJsonObject firstRunPlan() const;
    // Applies the plan: Monitor mix → default sink, listening device = default sink, Voice channel ← default source
    // (mono), every running app without a channel → Game (media.role music → System, communication → Voice).
    // Returns what was actually done, error on a missing default sink (nothing sensible to route to).
    QJsonObject firstRunApply(QString *error = nullptr);
    bool firstRun() const { return m_firstRun; }
    // MX-8: new mix from an existing one — name, icon, colour, FX chain, master level/mute and every cell fader/mute
    // are copied; outputs are NOT (two mixes on one device = the same audio twice) and MX-7 links are not either.
    QString duplicateMix(const QString &from, const QString &displayName, QString *error = nullptr);
    // CH-11: hide a physical device from every picker without touching it (still listed in InputDevices/OutputDevices,
    // still routable by name — hidden is a presentation flag the daemon owns so every frontend agrees)
    QStringList hiddenDevices() const { return m_layout.hiddenDevices; }
    bool setDeviceHidden(const QString &node, bool hidden);
    QJsonObject exportSettings() const;
    bool importSettings(const QJsonObject &doc, QString *error = nullptr);

    static double linearToCubic(float lin) { return std::cbrt(static_cast<double>(lin)); }
    static float  cubicToLinear(double cub) { return static_cast<float>(cub * cub * cub); }

    /// DV-31: human label for a device port. Hardware counts 0-based in PipeWire (Ui24R: AUX0..AUX31) while the desk's
    /// surface counts 1-based ("Aux 1/2 = Main"); our virtual devices already count 1-based. Label = 1-based number
    /// ("USB 1" for AUX0 on a device whose positions start at 0), identity for everything else.
    QString portLabel(const QString &node, const QString &position) const;
    /// Accept a label ("USB 1") or a PipeWire position ("AUX0") in a ref and return the position; "" if neither exists.
    QString resolvePortName(const QString &node, const QString &nameOrLabel) const;
    QString lastError() const { return m_lastError; }   // last graph-level failure (EMFILE, bad module args); "" = none

Q_SIGNALS:
    void lastErrorChanged();
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
    void scenesChanged();                 // CT-9
    void sceneRecalled(const QString &name);
    void defaultDevicesChanged();   // UX-3
    void undoChanged();
    void hiddenDevicesChanged();

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
    QString m_lastError;
    void ensureEdgeLoopbackForInput(const QString &slug);
    void ensureAppRelays(const LayoutApp &a);            // CH-12: relay loopbacks for extra channels
    void rebuildMixOutputEdges(const QString &slug, int triesLeft = 40);   // FX-6: output edges follow mixExit()
    void retargetWhenPresent(const QString &entry, const QList<uint32_t> &streams, int triesLeft);   // FX-5: after a chain swap
    void removeAppRelays(const LayoutApp &a);
    void notifyPresence();
    void snapshotForUndo(const QString &kind, const QString &slug);
    void restorePendingCellStates();
    QJsonObject m_undo;                                  // {"what","kind","layout":{…},"cells":[{ch,mix,volume,mute}], "links":[…], "inputs":[…]}
    QHash<QString, QPair<float, bool>> m_pendingCellState;   // cell node → (volume, mute) to apply once the node exists
    QString sceneDir() const;                                // CT-9: <layout dir>/scenes
    QString scenePath(const QString &name) const;
    QJsonObject captureScene() const;                        // CT-9: the snapshot, without layout structure
    void applyFx(const QString &slug);                                   // rebuild one chain live (ADR 0008)
    void propagateLinks(const QString &ch, const QString &sourceMix);   // source cell changed → push to followers
    void propagateGroupTrim(const QString &slug, float oldLinear, float newLinear);   // CH-8
    void propagateGroupMute(const QString &slug, bool muted);                          // CH-8
    bool m_inGroupPropagation = false;
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
    bool m_firstRun = false;              // no layout.json existed when we started (UX-3)
    // What the user wants a cell/mix node to be, by node name. WirePlumber's restore-stream writes ITS value (a stored
    // one or the 1.0 default) to a node whenever it (re)appears, at a moment we cannot predict — 400 ms after creation
    // in a quiet session, seconds later under load (ctest35: CT-7 import lost a mute, DV-6 lost a cell volume, both
    // AFTER the one-shot retry had fired). So instead of racing it with timers, onNode() compares every echo from
    // PipeWire against the intent and writes the intent back when the echo disagrees and did not come from us.
    struct Intent { float volume; bool mute; int rewrites = 0; qint64 lastRewriteMs = 0; };
    QHash<QString, Intent> m_intent;
    void intend(const QString &node, float volume, bool mute) { m_intent[node] = {volume, mute}; }
    /// THE way to write a cell/mix node: records the intent, then writes PipeWire. Every path that changes a cell's
    /// volume/mute goes through here — a write that skips the intent is fought back by onNode() (MX-7 under ctest37:
    /// propagateLinks() wrote the follower to 0.1 with a stale 1.0 intent, and the guard restored 1.0).
    void writeCell(const QString &node, uint32_t id, float volume, bool mute) { intend(node, volume, mute); m_graph.setVolume(id, volume, mute); }
    /// onNode(): echo vs intent for one node; returns true if a rewrite was issued (caller then trusts the intent, not the echo)
    bool enforceIntent(const pw::NodeInfo &n);
    QString m_defaultSink, m_defaultSource;
};

} // namespace kmixdeck
