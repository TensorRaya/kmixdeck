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
    /// DV-14: the wire's own trim (cubic 0..1, 1 = unity) and mute, applied to kmixdeck's edge loopback node — never to the
    /// hardware device (Plasma owns that, CT-5). Persisted with the wire; re-applied every time the edge node appears.
    double trim = 1.0;
    bool   muted = false;
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
        if (c < 0) { r.node = ref.trimmed(); return r; }
        r.node = ref.left(c).trimmed();   // positions and side are trimmed too — a pasted "dev : AUX3" must not yield node "dev "
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

/// Format version written by this build (`"version"` in layout.json). Older files are read with defaults for what they
/// lack; a NEWER file is moved aside and the daemon starts fresh (Layout::load) — never read-as-old and saved back.
constexpr int kLayoutVersion = 2;

/// CT-8: one registered soundboard sample. `name` is the slug the bus and the CLI address it by, `path` the
/// file on disk, `length` its duration in seconds as the decoder reported it (0 = unknown/unreadable at
/// registration time), `gain` a linear per-sample trim so a quiet jingle can be brought up without touching
/// the channel fader.
struct LayoutSample {
    QString name;
    QString path;
    double length = 0.0;
    double gain = 1.0;
    QJsonObject toJson() const;
    static LayoutSample fromJson(const QJsonObject &o);
};
struct LayoutChannel {
    QString slug, name, icon;
    fx::Chain fx;
    double pan = 0.0;             // DV-22: −1 = left … 0 = centre … +1 = right
    QString color;                // MX-5: "#rrggbb" or empty (theme)
    QString group;                // CH-8: free-text group name, "" = none
    // FX-9: side-chain ducking. `duckedBy` is the slug of the trigger channel (the mic), "" = off,
    // which is the default — nothing ducks until the user says so. depth/attack/release are the
    // user's settings; the reduction the compressor actually applies is read back live from the
    // graph, not stored here.
    QString duckedBy;
    double duckDepth = -12.0;     // dB the channel drops while the trigger carries signal
    double duckAttack = 10.0;     // ms
    double duckRelease = 300.0;   // ms
    double duckThreshold = -40.0; // dBFS the trigger channel must exceed before ducking starts
    // CT-8: a channel of kind "soundboard" plays registered samples into the graph on demand. Empty (= "input",
    // the normal channel) by default, because the requirement makes the kind opt-in: no soundboard exists until
    // the user adds one. 🔴 New fields belong at the END of this struct — Layout::fromJson builds channels with
    // a POSITIONAL braced initialiser, so inserting one in the middle silently shifts every value after it.
    QString kind;                 // "" / "input" = normal channel, "soundboard" = CT-8
    QVector<LayoutSample> samples;// CT-8: registered samples, only ever non-empty on a soundboard channel
    // CT-6: an extra capture source for this channel alone (one stem per channel for multi-track recording). Off by
    // default (opt-in rule): a layout without the key renders the same graph as before. Last field, see above.
    bool capture = false;
    // CT-10: this channel accepts audio streams from clients (Channel.Play, `kmixdeck channel play`, the web bridge's
    // play endpoint). Off by default (opt-in rule): Play is refused until the user turns it on. Last field, see above.
    bool playback = false;
    bool isSoundboard() const { return kind == QLatin1String("soundboard"); }
    static LayoutChannel make(const QString &slug, const QString &name, const QString &icon = {}) { LayoutChannel c; c.slug = slug; c.name = name; c.icon = icon; return c; }
};
/// A physical input feeding a channel (mic, capture card, BT headset mic) — ADR 0007 D2.
struct LayoutInput   { QString slug, name; DeviceRef device; QString channel; };
struct LayoutMix     {
    QString slug, name, icon;
    QString color;                // MX-5: "#rrggbb" or empty = theme default
    QVector<DeviceRef> outputs;   // MX-9: several hardware outputs at once
    DeviceRef fallbackOutput;     // DV-15: used while outputs[0] is absent; empty node = none
    fx::Chain fx;                 // FX-6: same chain model on the output side
    bool loudness = false;        // UX-18: EBU R128 analyser on this mix — off by default (CPU + screen space)
    double loudnessTarget = -14.0;// UX-18: the target line, -14 LUFS for Twitch/YouTube, user-editable
    static LayoutMix make(const QString &slug, const QString &name, const QString &icon = {}) { LayoutMix m; m.slug = slug; m.name = name; m.icon = icon; return m; }
};

/// MX-7: cell (channel, mix) mirrors volume+mute of cell (channel, follows). Broken by touching the follower.
struct LayoutLink    { QString channel, mix, follows; };

/// ADR 0013: the fader state of one (channel × mix) cell. A cell is NO LONGER a PipeWire node — it is the
/// "Gain 1" of the channel's per-mix mixer nodes inside one filter-chain that feeds the shared bus. WirePlumber
/// does not persist filter controls, so the cell's volume (linear 0..1) and mute are stored HERE (layout.json),
/// and rendered as the chain's initial control values in the conf fragment and re-applied live by the daemon.
/// Mute keeps the stored volume; the live gain is 0 while muted.
struct LayoutCell    { QString channel, mix; double volume = 1.0; bool mute = false; };

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
    QVector<LayoutCell> cells;                     // ADR 0013: per-cell fader state (volume linear 0..1 + mute)
    LayoutVirtualDevice *virtualDevice(const QString &slug) { for (auto &v : virtualDevices) if (v.slug == slug) return &v; return nullptr; }
    /// CH-5: where a never-seen application lands. Empty = leave it on the system default (no auto-routing).
    QString defaultChannel = QStringLiteral("system");
    /// UX-2: the device the user listens on (node.name). The mix routed to it is "what I hear". Empty = not chosen.
    QString listeningDevice;
    /// Apps kmixdeck has routed at least once — keyed by application.name (falls back to node.name).
    /// WirePlumber restores THEIR target itself; this set only exists to tell "new" from "known".
    QStringList knownApps;
    QStringList hiddenDevices;    // CH-11: node.names the pickers do not offer (device is untouched, still routable)

    static QString defaultPath();                   // $XDG_CONFIG_HOME/kmixdeck/layout.json
    static QString defaultPipewireConfPath();       // $XDG_CONFIG_HOME/pipewire/pipewire.conf.d/90-kmixdeck.conf
    static Layout starter();                        // Game/System/Voice × Monitor/Stream

    bool load(const QString &path);
    bool loadFromJson(const QByteArray &roh);   // B2: judge a captured snapshot, not the live file
    bool save(const QString &path) const;
    QJsonObject toJson() const;
    static Layout fromJson(const QJsonObject &o);

    /// Render the whole graph as a PipeWire config fragment (context.objects + context.modules).
    /// `filterChain`: the module every cell and effects chain is loaded with — the daemon passes pw::filterChainModule()
    /// (ADR 0015), so the server at login runs the same chain code as the daemon. The default keeps the golden stable.
    QString toPipewireConf(const char *filterChain = "libpipewire-module-filter-chain") const;
    bool writePipewireConf(const QString &path, const char *filterChain = "libpipewire-module-filter-chain") const;

    LayoutChannel *channel(const QString &slug);
    LayoutMix *mix(const QString &slug);
    LayoutInput *input(const QString &slug);
    const LayoutChannel *channel(const QString &slug) const;
    const LayoutMix *mix(const QString &slug) const;
    const LayoutInput *input(const QString &slug) const;
    /// ADR 0013: cell state. Null when the cell is at the default (volume 1.0, unmuted) — layout.json stores
    /// only non-default cells, like every other piece of state here.
    LayoutCell *cell(const QString &ch, const QString &mix);
    const LayoutCell *cell(const QString &ch, const QString &mix) const;
    void setCell(const QString &ch, const QString &mix, double volume, bool mute);
    /// ADR 0013: the linear gain the cell runs at right now — 0.0 when muted, else the stored volume.
    double cellGain(const QString &ch, const QString &mix) const;
    /// CH-12 lookup by appKey (application.name / node.name), not by node id (CH-6).
    LayoutApp *app(const QString &key);
    const LayoutApp *app(const QString &key) const;

    /// Where streams should aim: the fx entry when a chain is active, the plain node otherwise (ADR 0008).
    QString channelEntry(const QString &slug) const;
    QString mixEntry(const QString &slug) const;
    /// FX-6: what a mix OUTPUT captures from. A mix sums into its sink; an active mix chain sits BEHIND that
    /// sink (limiter on the sum, FX-10), so every output edge and the capture source must read the chain's
    /// tail instead of the raw sink. Cells keep targeting the sink itself (mixEntry) — they are the summing bus.
    QString mixExit(const QString &slug) const;
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
/// CT-6: the per-channel capture source. Its own prefix, so no prefix match on kmixdeck.source.<mix> or
/// kmixdeck.channel.<ch> ever catches it. Taps the channel sink monitor: post FX, trim, pan, mute and ducking,
/// before every cell and mix fader — a direct out.
inline QString channelSourceNode(const QString &chSlug) { return QStringLiteral("kmixdeck.chsource.") + chSlug; }
/// DV-29: the pass-through of a virtual device — what apps play into `.out` port N appears on the source's port N,
/// so a channel wired to the device hears the app (Loopback's "virtual device" semantics). One loopback per device.
inline QString virtualPassNode(const QString &virtSlug) { return QStringLiteral("kmixdeck.virt.") + virtSlug + QStringLiteral(".pass"); }   // only "<pass>.in" exists as a node; the playback half is inputNode()
/// CH-12: one relay loopback per extra channel of a multi-assigned app — captures from the primary channel
/// sink and plays into the next one, so every assigned channel hears the app. Key: <appKey>.<channelSlug>.
inline QString relayNode(const QString &appKey, const QString &channelSlug) {
    return QStringLiteral("kmixdeck.relay.%1.%2").arg(appKey, channelSlug);
}
}

/// ADR 0013: cells are no longer per-cell module-loopbacks. One libpipewire-module-filter-chain per channel
/// ("kmixdeck.cells.<ch>") captures the channel sink's monitor and fans it out to a builtin "mixer" node per
/// (mix, side) — its "Gain 1" IS the cell fader — whose outputs land on a shared multichannel bus
/// ("kmixdeck.bus[,k]"); PipeWire sums the channels there. One loopback per mix then reads the bus'
/// AUX2m/AUX2m+1 monitor into the (unchanged) mix sink. This collapses 4×N×M loopback clients into
/// N chains + M loopbacks. The bus is a null sink (summing bus, not a device — ADR 0007 keeps kmixdeck sinks
/// out of the pickers); MX-1 forbids a hard mix cap, so for >32 mixes the mixes spill onto additional buses
/// (SPA_AUDIO_MAX_CHANNELS = 64 = 2×32). Names and args live HERE — shared by the config renderer and the
/// runtime path — so the two can never drift (the ADR 0002 invariant, carried over to the new shape).
namespace ADR13 {
constexpr int kMixesPerBus = 32;                        // SPA_AUDIO_MAX_CHANNELS / 2
inline int busCount(const int mixes) { return mixes <= 0 ? 1 : (mixes + kMixesPerBus - 1) / kMixesPerBus; }
inline int busIndex(const int mixIdx) { return mixIdx / kMixesPerBus; }                        // 0-based
inline int channelMixIndex(const int bus, const int mixIdx) { return mixIdx - bus * kMixesPerBus; }   // 0-based in the bus
inline QString busNode(const int bus) { return bus <= 0 ? QStringLiteral("kmixdeck.bus") : QStringLiteral("kmixdeck.bus.%1").arg(bus); }
/// ADR 0013: the bus carries 2 channels per mix (left+right), so a bus with `mixes` mixes has 2*mixes
/// channels AUX0..AUX(2*mixes-1); mix i's pair is AUX2i / AUX2i+1. This is the position list of a chain's
/// playback side (one AUX per (mix,side), left before right — the prototype's exact ordering).
inline QStringList busPositions(const int mixes) { QStringList p; for (int i = 0; i < mixes; ++i) { p << QStringLiteral("AUX") + QString::number(2 * i) << QStringLiteral("AUX") + QString::number(2 * i + 1); } return p; }
/// Chain per (channel, bus group). Group 0 keeps the short name. The CAPTURE node carries the filter controls
/// ("L<mix>:Gain 1"); the playback node is <chain>.out and lands on the bus.
inline QString chainNode(const QString &ch, const int bus = 0) { return QStringLiteral("kmixdeck.cells.") + ch + (bus > 0 ? QStringLiteral("@%1").arg(bus) : QString()); }
inline QString chainPlaybackNode(const QString &ch, const int bus = 0) { return chainNode(ch, bus) + QStringLiteral(".out"); }
/// Per-mix bus tap, a loopback: capture half <tap>.in reads the bus monitor, playback half <tap> plays into the mix.
/// NOT under kmixdeck.mix.* — that prefix is how the daemon recognises mix sinks.
inline QString tapNode(const QString &mix) { return QStringLiteral("kmixdeck.tap.") + mix; }
/// What a chain was built for: its node.description carries the mix slugs of its group, so the daemon can tell a
/// chain rendered for an older mix set (conf.d at login, or before an AddMix) and rebuild it.
QString chainSignature(const Layout &l, const int bus);
/// ADR 0013: the per-channel cell filter-chain. `mixes` = how many (mix,side) pairs this channel feeds
/// (usually M; the bus-grouping split is applied by the caller, one chain per (channel, bus group)).
/// Cell gain comes from the layout (mute → 0, else the stored volume). Returns the module args string.
QString cellChainArgs(const Layout &l, const QString &ch, const int bus, const int nMixesOnBus);
/// ADR 0013: the per-mix bus tap. `mixIdx` is the 0-based mix index in the layout; captures the bus'
/// AUX2i/AUX2i+1 monitor (stream.dont-remix: the pair, not the whole bus) and plays into the mix entry.
QString mixTapArgs(const QString &mix, const int mixIdx, const QString &mixEntry);
}

/// One loopback = one module-loopback args string. Shared by the config renderer and the runtime path so the
/// two can never drift (ADR 0002/0007). Device-side streams get node.linger + dont-fallback: they wait for an
/// absent device instead of dying, and WirePlumber links them by itself when the device appears (D3).
QString virtualPassArgs(const LayoutVirtualDevice &v);   // DV-29
QString loopbackArgs(const QString &description,
                     const QString &captureName, const QString &captureTarget, bool captureIsSink, const QStringList &capturePositions, bool captureLinger,
                     const QString &playbackName, const QString &playbackTarget, const QStringList &playbackPositions, bool playbackLinger, bool playbackDontReconnect,
                     const QString &playbackExtra = {});

} // namespace kmixdeck
