// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "mixer.h"
#include <climits>
#include <algorithm>
#include <QSet>
#include <QTimer>
#include <QFile>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace kmixdeck {

QString Names::slugify(const QString &display) {
    QString s = display.toLower().normalized(QString::NormalizationForm_KD);
    s.remove(QRegularExpression(QStringLiteral("\\p{Mn}")));   // strip combining marks left by KD (Ü → U + ¨)
    // '_' not '-': a slug ends up in D-Bus object paths, which allow only [A-Za-z0-9_] (dbus-spec). Found the hard
    // way: "Übertragung" → "ubertragung-und-mehr" marshalled as an invalid path and the reply never left the daemon.
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    while (s.startsWith(QLatin1Char('_'))) s.remove(0, 1); while (s.endsWith(QLatin1Char('_'))) s.chop(1);
    return s;   // may be empty → callers reject (never invent a name)
}

Mixer::Mixer(QObject *parent) : QObject(parent), m_layout(Layout::starter()) {
    QObject::connect(&m_graph, &pw::Graph::nodeAdded,   this, &Mixer::onNode);
    QObject::connect(&m_graph, &pw::Graph::nodeChanged, this, &Mixer::onNode);
    QObject::connect(&m_graph, &pw::Graph::nodeRemoved, this, &Mixer::onNodeRemoved);
    QObject::connect(&m_graph, &pw::Graph::streamRouted, this, &Mixer::onStreamRouted);
    QObject::connect(&m_graph, &pw::Graph::connected, this, [this] {
        m_connected = true; m_reconnectMs = 500; Q_EMIT connectedChanged();
        QTimer::singleShot(400, this, [this] { reconcile(); });   // registry replay first, then fill the gaps
    });
    // AR-4: PipeWire restarted under us → drop everything (nodeRemoved for each → layout/app objects vanish on
    // the bus), then reconnect with backoff; the registry replays the graph and objects reappear.
    QObject::connect(&m_graph, &pw::Graph::disconnected, this, [this](const QString &why) {
        qWarning() << "PipeWire disconnected:" << why;
        m_graph.teardown();
        m_channels.clear(); m_mixes.clear(); m_cells.clear(); m_sinks.clear(); m_devices.clear(); m_edges.clear(); m_idToName.clear();
        Q_EMIT outputDevicesChanged(); Q_EMIT inputDevicesChanged();
        for (auto id : m_apps.keys()) Q_EMIT appRemoved(id); m_apps.clear();
        m_connected = false; Q_EMIT connectedChanged(); Q_EMIT layoutChanged();
        m_reconnect.start(m_reconnectMs);
    });
    m_reconnect.setSingleShot(true);
    QObject::connect(&m_reconnect, &QTimer::timeout, this, [this] {
        if (m_graph.connect()) { qInfo() << "PipeWire: reconnected"; return; }
        m_reconnectMs = std::min(m_reconnectMs * 2, 10000);
        qWarning() << "PipeWire: reconnect failed, retry in" << m_reconnectMs << "ms";
        m_reconnect.start(m_reconnectMs);
    });
    if (!m_graph.connect()) { qWarning() << "PipeWire: connect failed, retrying"; m_reconnect.start(m_reconnectMs); }
}

bool Mixer::loadLayout() {
    Layout l;
    if (!m_layoutPath.isEmpty() && l.load(m_layoutPath)) { m_layout = l; return true; }
    return false;
}
bool Mixer::saveLayout() const {
    bool ok = true;
    if (!m_layoutPath.isEmpty()) ok &= m_layout.save(m_layoutPath);
    if (!m_pwConfPath.isEmpty()) ok &= m_layout.writePipewireConf(m_pwConfPath);
    return ok;
}

// Layout → PipeWire. Mirrors Layout::toPipewireConf exactly (same loopbackArgs calls, so runtime and
// config fragment can never drift). Names carry the display strings.
void Mixer::reconcile() {
    if (!m_connected) return;
    if (!m_graph.node(QStringLiteral("kmixdeck.null"))) m_graph.createParkingSink();
    for (const auto &c : m_layout.channels) {
        requestNullNode(Names::channelNode(c.slug), [&] { m_graph.createNullSink(Names::channelNode(c.slug), c.name, true); });
        bool found = false; for (auto &ch : m_channels) if (ch.slug == c.slug) { found = true; if (ch.name != c.name || ch.icon != c.icon) { ch.name = c.name; ch.icon = c.icon; Q_EMIT channelChanged(c.slug); } }
        if (!found) m_channels.push_back({c.slug, c.name, c.icon, true});
    }
    for (const auto &m : m_layout.mixes) {
        requestNullNode(Names::mixNode(m.slug), [&] { m_graph.createNullSink(Names::mixNode(m.slug), QStringLiteral("Mix: ") + m.name, false); });
    for (const auto &v : m_layout.virtualDevices) {   // DV-23
        requestNullNode(v.outputNode(), [&] { m_graph.createNullNode(v.outputNode(), v.name + QStringLiteral(" (virtual) outputs"), QStringLiteral("Audio/Sink"), v.positions(v.outputs)); });
        requestNullNode(v.inputNode(),  [&] { m_graph.createNullNode(v.inputNode(),  v.name + QStringLiteral(" (virtual)"), QStringLiteral("Audio/Source/Virtual"), v.positions(v.inputs)); });
    }
        bool found = false; for (auto &mx : m_mixes) if (mx.slug == m.slug) { found = true; if (mx.name != m.name || mx.icon != m.icon) { mx.name = m.name; mx.icon = m.icon; Q_EMIT mixChanged(m.slug); } }
        if (!found) m_mixes.push_back({m.slug, m.name, m.icon, true});
    }
    // The registry replays nodes in id order; the layout owns the display order (UX-9). Reorder the runtime
    // lists to match — entries the layout does not know (adopted nodes) keep their relative order at the end.
    {
        QStringList want; for (const auto &c : m_layout.channels) want << c.slug;
        std::stable_sort(m_channels.begin(), m_channels.end(), [&](const Channel &a, const Channel &b) {
            const int ia = want.indexOf(a.slug), ib = want.indexOf(b.slug);
            return (ia < 0 ? INT_MAX : ia) < (ib < 0 ? INT_MAX : ib); });
        want.clear(); for (const auto &m : m_layout.mixes) want << m.slug;
        std::stable_sort(m_mixes.begin(), m_mixes.end(), [&](const Mix &a, const Mix &b) {
            const int ia = want.indexOf(a.slug), ib = want.indexOf(b.slug);
            return (ia < 0 ? INT_MAX : ia) < (ib < 0 ? INT_MAX : ib); });
    }
    // chains on top of the sinks (ADR 0008): only when the entry node is missing — applyFx() tears down and rebuilds,
    // which would drop every stream on the chain for a moment (and broke the "live control keeps the node" test
    // when the 400 ms start-up reconcile ran after a SetFx).
    for (const auto &c : m_layout.channels) if (c.fx.isActive() && !m_graph.node(QStringLiteral("kmixdeck.fx.%1").arg(c.slug))) applyFx(c.slug);
    for (const auto &m : m_layout.mixes)     if (m.fx.isActive() && !m_graph.node(QStringLiteral("kmixdeck.fx.mix.%1").arg(m.slug))) applyFx(m.slug);
    for (const auto &c : m_layout.channels)
        for (const auto &m : m_layout.mixes) {
            const QString cell = Names::cellNode(c.slug, m.slug);
            if (m_graph.node(cell)) continue;
            m_graph.loadLoopback(loopbackArgs(c.name + QStringLiteral(" → ") + m.name,
                                              cell + QStringLiteral(".in"), Names::channelNode(c.slug), true, {}, false,
                                              cell, m_layout.mixEntry(m.slug), {}, false, true));
        }
    ensureEdgeLoopbacks();
    for (const auto &a : m_layout.apps) ensureAppRelays(a);   // CH-12 relays are layout, so they come back like cells
    // Capture sides are plumbing, not faders. WirePlumber restores whatever volume it last saw on them (it did:
    // a test left kmixdeck.link.game.stream.in at 0.0156 → the stream mix was 36 dB down with the fader at 0 dB).
    for (const auto &n : m_graph.nodes())
        if (n.name.startsWith(QLatin1String("kmixdeck.")) && n.name.endsWith(QLatin1String(".in")) && (n.volume != 1.0f || n.mute)) {
            qInfo() << "resetting capture side" << n.name << "to 1.0/unmuted (was" << n.volume << n.mute << ")";
            m_graph.setVolume(n.id, 1.0f, false);
        }
    // Config fragment on disk must match what THIS binary renders — a stale one (older renderer, or written by
    // hand) would rebuild a different graph at next login. Compare content, write only on drift (idempotent).
    if (!m_pwConfPath.isEmpty()) {
        QFile f(m_pwConfPath);
        const bool same = f.open(QIODevice::ReadOnly) && f.readAll() == m_layout.toPipewireConf().toUtf8();
        if (!same) { qInfo() << "pipewire fragment differs from layout, rewriting" << m_pwConfPath; saveLayout(); }
    }
    applyFallbacks();
    m_reconciled = true;
    Q_EMIT layoutChanged();
}

void Mixer::requestNullNode(const QString &name, const std::function<void()> &create) {
    if (m_graph.node(name)) { m_nodeRequested.remove(name); return; }
    // a removed node may still be in the registry snapshot for a moment; the destroy path clears the set explicitly
    if (m_nodeRequested.contains(name)) return;   // asked already, registry has not caught up yet
    m_nodeRequested.insert(name);
    create();
}
// ---- effects (ADR 0008) ---------------------------------------------------------------------------------
namespace {
fx::Chain *chainOf(Layout &l, const QString &slug) {
    if (auto *c = l.channel(slug)) return &c->fx;
    if (auto *m = l.mix(slug))     return &m->fx;
    return nullptr;
}
} // namespace

QJsonObject Mixer::fxChain(const QString &slug) const {
    if (const auto *c = m_layout.channel(slug)) return c->fx.isActive() ? c->fx.toJson() : QJsonObject{};
    if (const auto *m = m_layout.mix(slug))     return m->fx.isActive() ? m->fx.toJson() : QJsonObject{};
    return {};
}

bool Mixer::setFxChain(const QString &slug, const QJsonObject &chainJson) {
    qInfo() << "fx: setFxChain" << slug;
    fx::Chain *chain = chainOf(m_layout, slug);
    if (!chain) return false;
    const fx::Chain next = fx::Chain::fromJson(chainJson);
    const QString why = fx::validate(next);
    if (!why.isEmpty()) { qWarning() << "kmixdeck: refusing fx chain:" << why; return false; }
    *chain = next;
    applyFx(slug);
    saveLayout();
    Q_EMIT layoutChanged();
    return true;
}

bool Mixer::setFxControl(const QString &slug, const QString &control, double value) {
    const fx::Chain *chain = nullptr; QString entry;
    if (const auto *c = m_layout.channel(slug)) { chain = &c->fx; entry = m_layout.channelEntry(slug); }
    else if (const auto *m = m_layout.mix(slug)) { chain = &m->fx; entry = m_layout.mixEntry(slug); }
    if (!chain || chain->effects.isEmpty()) return false;
    const auto info = m_graph.node(entry);
    if (!info) return false;

    // Keys on the node are "<graphNode>:<LadspaLabel>" (measured). Accept either the full key or the short
    // spec key: "threshold" matches the first control whose label starts with it ("Threshold (dB)" ✓).
    const auto pairs = fx::controlValues(*chain, slug);
    // On the node, keys read "<nodeName>:<LadspaLabel>" — nodeName = slug + effect prefix (measured: "gamegate:Threshold (dB)").
    // Accept the full key, or the label part: compare against the LAST ":" section of both sides.
    QString key;
    for (const auto &p : pairs) if (p.first == control) { key = p.first; break; }
    if (key.isEmpty()) {
        const QString want = control.section(QLatin1Char(':'), -1).toLower();
        for (const auto &p : pairs) {
            const QString ctl = p.first.section(QLatin1Char(':'), -1).toLower();
            if (ctl == want || ctl.startsWith(want + QLatin1Char(' ')) || ctl == want + QLatin1Char('s')) { key = p.first; break; }
        }
    }
    if (key.isEmpty()) return false;
    m_graph.setControl(info->id, key, value);
    return true;
}

QJsonObject Mixer::fxPresets() const { return fx::presetChains(); }

QJsonArray Mixer::fxTypes() const {
    QJsonArray out;
    for (const auto &t : fx::builtinTypes()) {
        QJsonArray params;
        for (const auto &p : t.params) params.append(QJsonObject{{QStringLiteral("key"), p.key}, {QStringLiteral("label"), p.label},
                                                                {QStringLiteral("unit"), p.unit}, {QStringLiteral("min"), p.min},
                                                                {QStringLiteral("max"), p.max}, {QStringLiteral("def"), p.def}});
        out.append(QJsonObject{{QStringLiteral("type"), t.type}, {QStringLiteral("label"), t.label},
                               {QStringLiteral("description"), t.description}, {QStringLiteral("params"), params}});
    }
    return out;
}

/// Rebuild one chain live: replace the filter-chain module, keep everything else. Streams that were
/// linked to the plain sink move onto the new entry so effects take effect without restarting PipeWire.
void Mixer::applyFx(const QString &slug) {
    qInfo() << "fx: applyFx" << slug << "connected" << m_connected;
    if (!m_connected) return;
    fx::Chain chain;
    QString desc, entry, exit, plainName;
    if (const auto *c = m_layout.channel(slug)) { chain = c->fx; desc = c->name; plainName = Names::channelNode(slug); entry = QStringLiteral("kmixdeck.fx.%1").arg(slug); exit = entry + QStringLiteral(".out"); }
    else if (const auto *m = m_layout.mix(slug)) { chain = m->fx; desc = QStringLiteral("Mix: ") + m->name; plainName = Names::mixNode(slug); entry = QStringLiteral("kmixdeck.fx.mix.%1").arg(slug); exit = entry + QStringLiteral(".out"); }
    else return;

    // Streams currently on the entry (fx node) or the plain sink: remembered BEFORE the old chain goes away — with
    // node.dont-fallback a stream whose sink vanishes ends up unlinked, and nothing would find it afterwards.
    // (Measured 2026-09-16: SetFx while playing → −inf on the mix until the app reconnected.) FX-5.
    QList<uint32_t> streams;
    {
        const auto e = m_graph.node(entry); const auto p = m_graph.node(plainName);
        for (const auto &n : m_graph.nodes()) {
            if (!n.mediaClass.contains(QLatin1String("Stream/Output")) || n.name.startsWith(QLatin1String("kmixdeck."))) continue;
            const uint32_t sink = m_graph.streamSink(n.id);
            if ((e && sink == e->id) || (p && sink == p->id)) streams << n.id;
        }
    }
    // drop the old chain: everything with this entry prefix (never the plain sink, cells or edges)
    destroyOurNodes([&](const QString &n) { return n == entry || n.startsWith(entry + QLatin1Char('.')) || n.startsWith(entry); });

    if (!chain.isActive()) {   // bypass / cleared → plain sink again; move streams back
        if (!m_graph.node(plainName)) m_graph.createNullSink(plainName, desc, !slug.isEmpty());
        for (uint32_t id : streams) m_graph.moveStream(id, plainName);
        return;
    }
    if (!m_graph.node(plainName)) m_graph.createNullSink(plainName, desc, true);   // tail for the chain
    const QString args = fx::renderFilterChainArgs(chain, desc, entry, exit, plainName, plainName, slug);
    qInfo() << "fx: rendered" << args.length() << "chars";
    if (args.isEmpty()) return;
    m_graph.loadLoopback(args, "libpipewire-module-filter-chain");
    // the module's node appears asynchronously — retarget the remembered streams once it is there
    retargetWhenPresent(entry, streams, 40);
}
// Poll (registry events are what fill m_graph) until `entry` exists, then move the streams onto it.
void Mixer::retargetWhenPresent(const QString &entry, const QList<uint32_t> &streams, int triesLeft) {
    if (streams.isEmpty() || triesLeft <= 0) return;
    if (!m_graph.node(entry)) { QTimer::singleShot(50, this, [=] { retargetWhenPresent(entry, streams, triesLeft - 1); }); return; }
    for (uint32_t id : streams) m_graph.moveStream(id, entry);
}


QStringList Mixer::channelSlugs() const { QStringList l; for (const auto &c : m_channels) l << c.slug; return l; }
QStringList Mixer::mixSlugs() const     { QStringList l; for (const auto &m : m_mixes) l << m.slug; return l; }
QString Mixer::channelName(const QString &slug) const { for (const auto &c : m_channels) if (c.slug == slug) return c.name; return slug; }
QString Mixer::mixName(const QString &slug) const     { for (const auto &m : m_mixes) if (m.slug == slug) return m.name; return slug; }

bool   Mixer::cellPresent(const QString &ch, const QString &mix) const { return m_cells.contains(Names::cellNode(ch, mix)); }
double Mixer::cellVolume(const QString &ch, const QString &mix) const {
    auto it = m_cells.constFind(Names::cellNode(ch, mix)); return it == m_cells.constEnd() ? 0.0 : linearToCubic(it->volume);
}
bool Mixer::cellMuted(const QString &ch, const QString &mix) const {
    auto it = m_cells.constFind(Names::cellNode(ch, mix)); return it == m_cells.constEnd() ? true : it->mute;
}
void Mixer::setCellVolume(const QString &ch, const QString &mix, double cubic) {
    auto it = m_cells.find(Names::cellNode(ch, mix)); if (it == m_cells.end()) return;
    breakLink(ch, mix);                       // MX-7: a direct write to a follower unlinks it
    const float lin = cubicToLinear(std::clamp(cubic, 0.0, 1.0));
    it->volume = lin;                         // optimistic; PipeWire echoes via nodeChanged
    m_graph.setVolume(it->id, lin, it->mute);
    Q_EMIT cellChanged(ch, mix);
    propagateLinks(ch, mix);
}
QString Mixer::cellFollows(const QString &ch, const QString &mix) const {
    for (const auto &l : m_layout.links) if (l.channel == ch && l.mix == mix) return l.follows;
    return {};
}
bool Mixer::setCellFollows(const QString &ch, const QString &mix, const QString &follows) {
    if (follows == mix) return false;
    if (!follows.isEmpty() && (!mixSlugs().contains(follows) || !channelSlugs().contains(ch) || !mixSlugs().contains(mix))) return false;
    if (!follows.isEmpty() && cellFollows(ch, follows) == mix) return false;   // no A↔B loops
    m_layout.links.removeIf([&](const LayoutLink &l) { return l.channel == ch && l.mix == mix; });
    if (!follows.isEmpty()) m_layout.links.push_back({ch, mix, follows});
    saveLayout();
    Q_EMIT cellChanged(ch, mix);
    if (!follows.isEmpty()) propagateLinks(ch, follows);   // snap to the source right away
    return true;
}
void Mixer::breakLink(const QString &ch, const QString &mix) {
    const int n = m_layout.links.removeIf([&](const LayoutLink &l) { return l.channel == ch && l.mix == mix; });
    if (n) { saveLayout(); qInfo() << "cell" << ch << mix << "unlinked (touched directly)"; }
}
void Mixer::propagateLinks(const QString &ch, const QString &sourceMix) {
    const auto src = m_cells.constFind(Names::cellNode(ch, sourceMix)); if (src == m_cells.constEnd()) return;
    for (const auto &l : m_layout.links) {
        if (l.channel != ch || l.follows != sourceMix) continue;
        auto it = m_cells.find(Names::cellNode(ch, l.mix)); if (it == m_cells.end()) continue;
        if (it->volume == src->volume && it->mute == src->mute) continue;
        it->volume = src->volume; it->mute = src->mute;
        m_graph.setVolume(it->id, it->volume, it->mute);
        Q_EMIT cellChanged(ch, l.mix);
    }
}
void Mixer::setCellMuted(const QString &ch, const QString &mix, bool muted) {
    auto it = m_cells.find(Names::cellNode(ch, mix)); if (it == m_cells.end()) return;
    breakLink(ch, mix);
    it->mute = muted;
    m_graph.setVolume(it->id, it->volume, muted);
    Q_EMIT cellChanged(ch, mix);
    propagateLinks(ch, mix);
}

double Mixer::channelTrim(const QString &slug) const { auto it = m_sinks.constFind(Names::channelNode(slug)); return it == m_sinks.constEnd() ? 1.0 : it->volume; }
bool   Mixer::channelMuted(const QString &slug) const { auto it = m_sinks.constFind(Names::channelNode(slug)); return it == m_sinks.constEnd() ? false : it->mute; }
void Mixer::setChannelTrim(const QString &slug, double linear) {
    auto it = m_sinks.find(Names::channelNode(slug)); if (it == m_sinks.end()) return;
    it->volume = static_cast<float>(std::clamp(linear, 0.0, 1.0)); m_graph.setVolume(it->id, it->volume, it->mute); Q_EMIT channelChanged(slug);
}
void Mixer::setChannelMuted(const QString &slug, bool muted) {
    auto it = m_sinks.find(Names::channelNode(slug)); if (it == m_sinks.end()) return;
    it->mute = muted; m_graph.setVolume(it->id, it->volume, muted); Q_EMIT channelChanged(slug);
}
double Mixer::mixVolume(const QString &slug) const { auto it = m_sinks.constFind(Names::mixNode(slug)); return it == m_sinks.constEnd() ? 1.0 : it->volume; }
bool   Mixer::mixMuted(const QString &slug) const  { auto it = m_sinks.constFind(Names::mixNode(slug)); return it == m_sinks.constEnd() ? false : it->mute; }
void Mixer::setMixVolume(const QString &slug, double linear) {
    auto it = m_sinks.find(Names::mixNode(slug)); if (it == m_sinks.end()) return;
    it->volume = static_cast<float>(std::clamp(linear, 0.0, 1.0)); m_graph.setVolume(it->id, it->volume, it->mute); Q_EMIT mixChanged(slug);
}
void Mixer::setMixMuted(const QString &slug, bool muted) {
    auto it = m_sinks.find(Names::mixNode(slug)); if (it == m_sinks.end()) return;
    it->mute = muted; m_graph.setVolume(it->id, it->volume, muted); Q_EMIT mixChanged(slug);
}
void Mixer::renameChannel(const QString &slug, const QString &name) { for (auto &c : m_channels) if (c.slug == slug) { c.name = name; Q_EMIT channelChanged(slug); } if (auto *l = m_layout.channel(slug)) { l->name = name; saveLayout(); } }
void Mixer::renameMix(const QString &slug, const QString &name)     { for (auto &m : m_mixes) if (m.slug == slug) { m.name = name; Q_EMIT mixChanged(slug); } if (auto *l = m_layout.mix(slug)) { l->name = name; saveLayout(); } }
void Mixer::setChannelIcon(const QString &slug, const QString &icon) { for (auto &c : m_channels) if (c.slug == slug) { c.icon = icon; Q_EMIT channelChanged(slug); } if (auto *l = m_layout.channel(slug)) { l->icon = icon; saveLayout(); } }
void Mixer::setMixIcon(const QString &slug, const QString &icon)     { for (auto &m : m_mixes) if (m.slug == slug) { m.icon = icon; Q_EMIT mixChanged(slug); } if (auto *l = m_layout.mix(slug)) { l->icon = icon; saveLayout(); } }
QString Mixer::channelIcon(const QString &slug) const { for (const auto &c : m_channels) if (c.slug == slug) return c.icon; return {}; }
QString Mixer::mixIcon(const QString &slug) const     { for (const auto &m : m_mixes) if (m.slug == slug) return m.icon; return {}; }

// UX-9: order lives in the layout (and in the runtime lists, which mirror it). Slugs, nodes and volumes are untouched —
// a move is a pure presentation change, so nothing in the graph has to be rebuilt.
template <class T> static bool moveBySlug(QVector<T> &v, const QString &slug, int index) {
    int from = -1; for (int i = 0; i < v.size(); ++i) if (v[i].slug == slug) { from = i; break; }
    if (from < 0) return false;
    index = qBound(0, index, v.size() - 1);
    if (index == from) return true;
    v.move(from, index); return true;
}
bool Mixer::moveChannel(const QString &slug, int index) {
    if (!moveBySlug(m_layout.channels, slug, index)) return false;
    moveBySlug(m_channels, slug, index); saveLayout(); Q_EMIT layoutChanged(); return true;
}
bool Mixer::moveMix(const QString &slug, int index) {
    if (!moveBySlug(m_layout.mixes, slug, index)) return false;
    moveBySlug(m_mixes, slug, index); saveLayout(); Q_EMIT layoutChanged(); return true;
}

// ---- device edges (ADR 0007) -----------------------------------------------------------------------
// Mix outputs are a list (MX-9); the bus-facing view stays a single node name for now: the first entry.
QVector<DeviceRef> Mixer::mixOutputs(const QString &slug) const {
    if (const auto *m = m_layout.mix(slug)) return m->outputs;
    return {};
}
QString Mixer::mixOutputDevice(const QString &slug) const {
    const auto outs = mixOutputs(slug);
    return outs.isEmpty() ? QString() : outs.first().ref();   // ADR 0009: "node" or "node:POS,POS"
}
DeviceRef Mixer::deviceRef(const QString &refStr) const {
    if (refStr.isEmpty()) return {};
    DeviceRef r = DeviceRef::fromRef(refStr);
    r.description = r.node;
    if (auto d = m_devices.value(r.node); !d.description.isEmpty()) r.description = d.description;
    if (!r.positions.isEmpty()) r.description += QStringLiteral(" [%1]").arg(r.positions.join(QLatin1Char('+')));
    return r;
}
QStringList Mixer::devicePorts(const QString &nodeName) const {
    QStringList out;
    const auto n = m_graph.node(nodeName); if (!n) return out;
    for (const auto &p : m_graph.ports(n->id)) out << p.position + QLatin1Char('|') + p.name + QLatin1Char('|') + p.alias;
    return out;
}
QString Mixer::validateDeviceRef(const DeviceRef &ref, bool wantSource) const {
    if (ref.node.isEmpty()) return {};
    if (!m_devices.contains(ref.node)) return QStringLiteral("unknown %1 device '%2'").arg(wantSource ? QStringLiteral("input") : QStringLiteral("output"), ref.node);
    if (ref.positions.isEmpty()) return {};
    if (ref.positions.size() > 2) return QStringLiteral("a virtual device is mono (1 port) or stereo (2 ports), got %1").arg(ref.positions.size());
    QSet<QString> have;
    if (const auto n = m_graph.node(ref.node)) for (const auto &p : m_graph.ports(n->id)) have.insert(p.position);
    for (const auto &pos : ref.positions) if (!have.contains(pos)) return QStringLiteral("device '%1' has no port '%2' (see `kmixdeck devices ports %1`)").arg(ref.node, pos);
    return {};
}
// Single-output view (Mix.OutputDevice): replaces the FIRST output, keeps the rest (MX-9 frontends and the
// pre-MX-9 UI must not clobber each other).
void Mixer::setMixOutputDevice(const QString &slug, const QString &nodeName) {
    QVector<DeviceRef> outs = mixOutputs(slug);
    if (nodeName.isEmpty()) { if (!outs.isEmpty()) outs.removeFirst(); }
    else if (outs.isEmpty()) outs.push_back(deviceRef(nodeName));
    else outs[0] = deviceRef(nodeName);
    setMixOutputs(slug, outs);
}
bool Mixer::setMixOutputs(const QString &slug, const QVector<DeviceRef> &outputs) {
    auto *m = m_layout.mix(slug);
    if (!m) return false;
    // ADR 0009 D2: the port subset lives in the edge's audio.position, which a retarget cannot change. An edge whose
    // positions differ from what is wanted now is destroyed here and rebuilt by ensureEdgeLoopbacks(). Same node,
    // same positions → keep it (retarget only), so plain device swaps stay glitch-free as before.
    for (int n = 0; n < std::max(m->outputs.size(), outputs.size()); ++n) {
        const QStringList before = n < m->outputs.size() ? m->outputs[n].positions : QStringList{};
        const QStringList after  = n < outputs.size()    ? outputs[n].positions    : QStringList{};
        if (before == after) continue;
        const QString out = EdgeNames::outputNode(slug, n);
        if (auto node = m_graph.node(out)) { m_graph.destroyObject(node->id); m_edges.remove(out); }
    }
    m->outputs = outputs;
    saveLayout();
    ensureEdgeLoopbacks();
    applyFallbacks();
    Q_EMIT mixChanged(slug);
    return true;
}
DeviceRef Mixer::mixFallbackOutput(const QString &slug) const {
    if (const auto *m = m_layout.mix(slug)) return m->fallbackOutput;
    return {};
}
void Mixer::setMixFallbackOutput(const QString &slug, const DeviceRef &dev) {
    if (auto *m = m_layout.mix(slug)) { m->fallbackOutput = dev; saveLayout(); applyFallbacks(); Q_EMIT mixChanged(slug); }
}
double Mixer::mixOutputVolume(const QString &slug, int index) const {
    auto it = m_edges.constFind(EdgeNames::outputNode(slug, index));
    return it == m_edges.constEnd() ? 1.0 : linearToCubic(it->volume);
}
bool Mixer::mixOutputMuted(const QString &slug, int index) const {
    auto it = m_edges.constFind(EdgeNames::outputNode(slug, index));
    return it == m_edges.constEnd() ? false : it->mute;
}
void Mixer::setMixOutputVolume(const QString &slug, int index, double cubic, bool muted) {
    auto it = m_edges.find(EdgeNames::outputNode(slug, index));
    if (it == m_edges.end()) return;
    it->volume = cubicToLinear(std::clamp(cubic, 0.0, 1.0));
    it->mute = muted;
    m_graph.setVolume(it->id, it->volume, muted);
    Q_EMIT mixChanged(slug);
}
QString Mixer::mixCaptureSource(const QString &slug) const {
    const QString src = EdgeNames::sourceNode(slug);
    return m_graph.node(src) ? src : QString();
}

QStringList Mixer::inputSlugs() const {
    QStringList l; for (const auto &i : m_layout.inputs) l << i.slug; return l;
}
QString Mixer::addInput(const QString &displayName, const DeviceRef &device, const QString &channel) {
    QString slug = Names::slugify(displayName);
    if (slug.isEmpty() || m_layout.input(slug)) return QString();
    LayoutInput in; in.slug = slug; in.name = displayName; in.device = device; in.channel = channel;
    m_layout.inputs.push_back(in);
    saveLayout(); ensureEdgeLoopbackForInput(slug);
    Q_EMIT inputsChanged(); Q_EMIT inputChanged(slug);
    return slug;
}
void Mixer::removeInput(const QString &slug) {
    m_layout.inputs.removeIf([&](const LayoutInput &i) { return i.slug == slug; });
    saveLayout();
    if (auto n = m_graph.node(EdgeNames::inputNode(slug))) m_graph.destroyObject(n->id);
    m_edges.remove(EdgeNames::inputNode(slug));
    Q_EMIT inputsChanged();
}
bool Mixer::setInputChannel(const QString &slug, const QString &channel) {
    auto *in = m_layout.input(slug);
    if (!in || in->channel == channel) return false;
    in->channel = channel;
    saveLayout(); ensureEdgeLoopbackForInput(slug);
    Q_EMIT inputChanged(slug);
    return true;
}
bool Mixer::setInputDevice(const QString &slug, const DeviceRef &device) {
    auto *in = m_layout.input(slug);
    if (!in) return false;
    in->device = device;
    saveLayout(); ensureEdgeLoopbackForInput(slug);
    Q_EMIT inputChanged(slug);
    return true;
}
double Mixer::inputVolume(const QString &slug) const {
    auto it = m_edges.constFind(EdgeNames::inputNode(slug));
    return it == m_edges.constEnd() ? 1.0 : linearToCubic(it->volume);
}
bool Mixer::inputMuted(const QString &slug) const {
    auto it = m_edges.constFind(EdgeNames::inputNode(slug));
    return it == m_edges.constEnd() ? false : it->mute;
}
void Mixer::setInputVolume(const QString &slug, double cubic, bool muted) {
    auto it = m_edges.find(EdgeNames::inputNode(slug));
    if (it == m_edges.end()) return;
    it->volume = cubicToLinear(std::clamp(cubic, 0.0, 1.0));
    it->mute = muted;
    m_graph.setVolume(it->id, it->volume, muted);
    Q_EMIT inputChanged(slug);
}
bool Mixer::inputPresent(const QString &slug) const {
    const auto *in = m_layout.input(slug);
    if (!in) return false;
    return in->device.node.isEmpty() || m_devices.contains(in->device.node);   // not bound yet → not greyed out
}

// ---- one-input-per-channel view (bus: Channel.InputDevice) ----------------------------------------
QString Mixer::channelInputDevice(const QString &channel) const {
    const auto *in = m_layout.input(channel);
    return in ? in->device.ref() : QString();    // ADR 0009: "node" or "node:POS,POS"
}
bool Mixer::channelInputPresent(const QString &channel) const {
    return m_layout.input(channel) ? inputPresent(channel) : true;
}
bool Mixer::setChannelInputDevice(const QString &channel, const QString &refStr) {
    if (!m_layout.channel(channel)) return false;
    const DeviceRef d = deviceRef(refStr);
    if (!refStr.isEmpty() && !validateDeviceRef(d, true).isEmpty()) return false;
    if (auto *in = m_layout.input(channel)) {
        if (in->device == d) return true;
        if (auto n = m_graph.node(EdgeNames::inputNode(channel))) m_graph.destroyObject(n->id);   // rebuild with the new target
        m_edges.remove(EdgeNames::inputNode(channel));
        if (refStr.isEmpty()) { removeInput(channel); Q_EMIT channelChanged(channel); return true; }
        in->device = d; saveLayout(); ensureEdgeLoopbackForInput(channel);
        Q_EMIT inputChanged(channel); Q_EMIT channelChanged(channel);
        return true;
    }
    if (refStr.isEmpty()) return true;
    LayoutInput in; in.slug = channel; in.name = channelName(channel); in.channel = channel;
    in.device = d;
    m_layout.inputs.push_back(in);
    saveLayout(); ensureEdgeLoopbackForInput(channel);
    Q_EMIT inputsChanged(); Q_EMIT inputChanged(channel); Q_EMIT channelChanged(channel);
    return true;
}
bool Mixer::mixOutputPresent(const QString &slug) const {
    const auto outs = mixOutputs(slug);
    if (outs.isEmpty()) return true;
    for (const auto &d : outs) if (m_devices.contains(d.node)) return true;
    return false;
}

// One loopback per edge, identical args to the config renderer (ADR 0007). Device-side capture streams
// carry node.linger: they wait for an absent device instead of dying, WirePlumber relinks on appearance.
void Mixer::ensureEdgeLoopbacks() {
    for (const auto &i : m_layout.inputs) ensureEdgeLoopbackForInput(i.slug);
    for (const auto &m : m_layout.mixes) {
        // Output edge, index 0, exists ALWAYS — parked on kmixdeck.null when nothing is configured. It carries
        // node.linger in both cases: it is retargeted onto real devices later and must outlive their absence (D3).
        // (Found by test DV-9/12: without linger the stream died on unplug and never came back.)
        const QString out0 = EdgeNames::outputNode(m.slug, 0);
        if (!m_graph.node(out0)) {
            const QString target = m.outputs.isEmpty() ? QStringLiteral("kmixdeck.null") : m.outputs.first().node;
            const QString what = m.outputs.isEmpty() ? QStringLiteral("output") : m.outputs.first().description;
            m_graph.loadLoopback(loopbackArgs(QStringLiteral("Mix: ") + m.name + QStringLiteral(" → ") + what,
                                              out0 + QStringLiteral(".in"), Names::mixNode(m.slug), true, {}, false,
                                              out0, target, m.outputs.isEmpty() ? QStringList{} : m.outputs.first().positions, true, false));
        }
        for (int n = 1; n < m.outputs.size(); ++n) {   // additional outputs (MX-9)
            const QString out = EdgeNames::outputNode(m.slug, n);
            if (m_graph.node(out)) continue;
            const DeviceRef &d = m.outputs[n];
            m_graph.loadLoopback(loopbackArgs(QStringLiteral("Mix: ") + m.name + QStringLiteral(" → ") + d.description,
                                              out + QStringLiteral(".in"), Names::mixNode(m.slug), true, {}, false,
                                              out, d.node, d.positions, true, false));
        }
        const QString src = EdgeNames::sourceNode(m.slug);   // virtual capture source for OBS/Discord (MX-3b)
        if (!m_graph.node(src))
            m_graph.loadLoopback(loopbackArgs(QStringLiteral("Mix: ") + m.name + QStringLiteral(" (capture)"),
                                              src + QStringLiteral(".in"), Names::mixNode(m.slug), true, {}, false,
                                              src, QString(), {}, false, false,
                                              QStringLiteral("node.description = %1 media.class = Audio/Source ")
                                                  .arg(QLatin1Char('"') + QStringLiteral("kmixdeck ") + m.name + QStringLiteral(" Mix\""))));
    }
}
void Mixer::ensureEdgeLoopbackForInput(const QString &slug) {
    const auto *in = m_layout.input(slug);
    if (!in || in->device.node.isEmpty() || in->channel.isEmpty()) return;
    const QString node = EdgeNames::inputNode(slug);
    if (m_graph.node(node)) return;
    m_graph.loadLoopback(loopbackArgs(QStringLiteral("Input: ") + in->name,
                                      node + QStringLiteral(".in"), in->device.node, false, in->device.positions, true,
                                      node, Names::channelNode(in->channel), {}, false, true));
}

// DV-11/DV-12/DV-15: an output whose device is unplugged parks on kmixdeck.null (or on the configured
// fallback when that one is present); when the device returns, WirePlumber's linger + link rules hand it
// back — and the next applyFallbacks() retargets once the primary is visible again. Calling this on every
// device-graph change keeps both directions automatic.
void Mixer::applyFallbacks() {
    if (!m_connected) return;
    for (const auto &m : m_layout.mixes) {
        const auto out = m_graph.node(EdgeNames::outputNode(m.slug, 0));
        if (!out) continue;
        QString target;                                   // first visible output, then fallback, then park
        for (const auto &d : m.outputs) if (m_devices.contains(d.node) || d.node == out->target) { target = d.node; break; }
        if (target.isEmpty() && !m.fallbackOutput.node.isEmpty() && m_devices.contains(m.fallbackOutput.node)) target = m.fallbackOutput.node;
        if (target.isEmpty()) target = QStringLiteral("kmixdeck.null");
        if (out->target != target) {
            if (m_graph.moveStream(out->id, target)) qInfo() << "mix output" << m.slug << "retargeted to" << target;
            else qWarning() << "output device not found:" << target;
        }
    }
}

QList<Device> Mixer::outputDevices() const { return devicesFor(QLatin1String("Audio/Sink")); }
QList<Device> Mixer::inputDevices() const  { return devicesFor(QLatin1String("Audio/Source")); }   // prefix match: includes Audio/Source/Virtual
QList<Device> Mixer::devicesFor(const QString &mediaClass) const {
    QList<Device> out;
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it) {
        if (!it->mediaClass.startsWith(mediaClass)) continue;   // "Audio/Source" also matches Audio/Source/Virtual
        out.append({it.key(), it->description.isEmpty() ? it.key() : it->description, it->positions, mediaClass.startsWith(QLatin1String("Audio/Source"))});
    }
    std::sort(out.begin(), out.end(), [](const Device &a, const Device &b) { return a.description.localeAwareCompare(b.description) < 0; });
    return out;
}

QList<uint32_t> Mixer::appIds() const { auto l = m_apps.keys(); std::sort(l.begin(), l.end()); return l; }
std::optional<App> Mixer::app(uint32_t id) const { auto it = m_apps.constFind(id); return it == m_apps.constEnd() ? std::nullopt : std::optional<App>(*it); }
bool Mixer::moveApp(uint32_t id, const QString &channelSlug) { return assignApp(id, {channelSlug}, false); }
// CH-12: channels[0] is the primary target (WirePlumber restore-target makes it survive restarts, CH-4); every
// further channel gets a relay loopback (capture ← primary sink, playback → its own channel). The assignment is
// keyed by appKey — application.name, else node.name — so it survives node re-creation (CH-6) and lives in the
// layout JSON, not in PipeWire state. Cumulative keeps the old channels and appends (UX-11: drop onto rows adds).
bool Mixer::assignApp(uint32_t id, const QStringList &wantedIn, bool cumulative) {
    auto it = m_apps.find(id);
    if (it == m_apps.end()) return false;
    QStringList want;
    for (const QString &s : wantedIn) if (channelSlugs().contains(s) && !want.contains(s)) want << s;
    const QString key = appKey(*it);
    if (key.isEmpty()) return false;
    LayoutApp *la = m_layout.app(key);
    if (cumulative && la) { QStringList merged = la->channels; for (const QString &s : want) if (!merged.contains(s)) merged << s; want = merged; }
    if (want.isEmpty()) {                                   // un-route: back to wherever WirePlumber puts it
        if (la) { removeAppRelays(*la); m_layout.apps.removeIf([&](const LayoutApp &a) { return a.key == key; }); }
        m_graph.clearStreamTarget(id);
        it->channels.clear(); saveLayout(); Q_EMIT appChanged(id); return true;
    }
    if (!m_graph.moveStream(id, m_layout.channelEntry(want.first()))) return false;
    m_pendingAutoRoute.remove(id);   // an explicit assignment wins over CH-5 auto-routing that may still be pending
    if (la) { removeAppRelays(*la); la->channels = want; la->nodeName = it->nodeName; }
    else { m_layout.apps.push_back({key, it->nodeName, want}); la = m_layout.app(key); }
    if (!m_layout.knownApps.contains(key)) m_layout.knownApps << key;   // CH-5: this app has an explicit home now
    saveLayout();
    ensureAppRelays(*la);
    it->channels = want;
    Q_EMIT appChanged(id);
    return true;
}
// Relay per extra channel, same loopbackArgs shape as the config renderer (ADR 0002) so runtime and fragment
// stay identical. Capture side sits on the app's OWN output node: capturing the primary channel's monitor would
// relay every other app on that channel as well (measured on boreas 2026-09-16: voice heard all of game).
// Trade-off: the app's node passes to the second channel unprocessed by the primary channel's fx (FX-3).
void Mixer::ensureAppRelays(const LayoutApp &a) {
    if (a.channels.size() < 2 || a.nodeName.isEmpty()) return;
    // Only while the app's node exists. A relay whose capture side lingers on an absent node is not "silent":
    // its playback half stays linked to the channel while the capture half is suspended, and that half-open
    // loopback stalled the whole channel→mix path (monitor recordings empty, 2026-09-16). The relay is layout,
    // so it comes back the moment the app node appears (onNode → ensureAppRelays).
    if (!m_graph.node(a.nodeName)) { removeAppRelays(a); return; }
    for (int n = 1; n < a.channels.size(); ++n) {
        const QString node = EdgeNames::relayNode(a.key, a.channels[n]);
        // Until 2026-09-16 `NodeInfo::target` only read target.object, never node.target → this test was always
        // false, the relay was destroyed and re-created on EVERY daemon start, and that churn left the channel's
        // monitor ports silent for downstream captures (test_ux12_* red after test_ch12_* in one session).
        if (const auto in = m_graph.node(node + QStringLiteral(".in")); in && (in->target == a.nodeName || in->configuredTarget == a.nodeName) && m_graph.node(node)) continue;
        // present but capturing the wrong node (older renderer captured the channel sink) → rebuild
        if (const auto in = m_graph.node(node + QStringLiteral(".in"))) m_graph.destroyObject(in->id);
        if (const auto out = m_graph.node(node)) m_graph.destroyObject(out->id);
        m_graph.loadLoopback(loopbackArgs(QStringLiteral("App: ") + a.key, node + QStringLiteral(".in"), a.nodeName, false, {}, true,
                                          node, m_layout.channelEntry(a.channels[n]), {}, false, true));
    }
}
void Mixer::removeAppRelays(const LayoutApp &a) {
    for (int n = 1; n < a.channels.size(); ++n) {
        const QString node = EdgeNames::relayNode(a.key, a.channels[n]);
        if (const auto info = m_graph.node(node)) m_graph.destroyObject(info->id);
    }
}
// CH-5. Only apps we have NEVER routed are touched: for everything else WirePlumber's restore-target has the
// user's last choice (CH-4) and must win — even if that choice was "system default, not kmixdeck at all".
void Mixer::autoRouteNewApp(const App &a) {
    if (m_layout.defaultChannel.isEmpty() || !channelSlugs().contains(m_layout.defaultChannel)) return;
    const QString key = appKey(a);
    if (key.isEmpty() || m_layout.knownApps.contains(key)) return;
    // Decide only once WirePlumber has linked the stream (its first onStreamRouted). Before that we cannot
    // tell a restored target from "nowhere yet", and moving a node WirePlumber has not registered is
    // linked but never remembered (state-stream.lua returns early) — broke CH-4 on 2026-09-15.
    m_pendingAutoRoute.insert(a.id);
}

void Mixer::finishAutoRoute(uint32_t id) {
    if (!m_pendingAutoRoute.remove(id)) return;
    auto it = m_apps.find(id); if (it == m_apps.end()) return;
    const QString key = appKey(*it);
    if (key.isEmpty() || m_layout.knownApps.contains(key)) return;
    if (!it->channels.isEmpty()) { m_layout.knownApps << key; saveLayout(); return; }   // already on one of ours (e.g. restored)
    if (m_layout.defaultChannel.isEmpty() || !channelSlugs().contains(m_layout.defaultChannel)) return;
    if (m_graph.moveStream(id, m_layout.channelEntry(m_layout.defaultChannel))) {
        m_layout.knownApps << key; saveLayout();
        qInfo() << "new application" << key << "→ default channel" << m_layout.defaultChannel;
    }
}
void Mixer::setListeningDevice(const QString &node) {
    if (m_layout.listeningDevice == node) return;
    m_layout.listeningDevice = node; saveLayout(); Q_EMIT listeningDeviceChanged();
}
bool Mixer::setDefaultChannel(const QString &slug) {
    if (!slug.isEmpty() && !channelSlugs().contains(slug)) return false;
    if (m_layout.defaultChannel == slug) return true;
    m_layout.defaultChannel = slug; saveLayout(); Q_EMIT defaultChannelChanged(); return true;
}
// UX-12 solo audition. Press-and-hold on any entity (channel or mix): snapshot every sink's level state, mute
// everything else, let exactly this one through at full level. Release restores the snapshot — the previous
// routing returns exactly as it was. Works identically on one built-in speaker and on phones: it is all state
// on the sinks, no extra streams, no dropped quants (DV-2).
void Mixer::startAudition(const QString &kind, const QString &slug) {
    if (!m_audition.slug.isEmpty() && m_audition.kind == kind && m_audition.slug == slug) return;   // already that one
    stopAudition();
    if (kind != QLatin1String("channel") && kind != QLatin1String("mix")) return;
    const bool isCh = kind == QLatin1String("channel");
    m_audition.kind = kind; m_audition.slug = slug;
    // Only the entity's OWN tier is soloed. Bug until 2026-09-16: both tiers were silenced against `slug`, so
    // auditioning a mix muted every channel (a mix of muted channels is silence — "ist alles stumm") and auditioning
    // a channel muted every mix including the one on the headphones.
    auto snapAndSilence = [&](const QStringList &slugs, QHash<QString, QPair<float, bool>> &saved,
                              QString (*nodeName)(const QString &)) {
        for (const QString &s : slugs) {
            const QString node = nodeName(s);
            auto it = m_sinks.find(node); if (it == m_sinks.end()) continue;
            saved.insert(s, {it->volume, it->mute});
            const bool mine = (s == slug);
            it->mute = !mine; if (mine) it->volume = 1.0f;
            m_graph.setVolume(it->id, it->volume, it->mute);
        }
    };
    if (isCh) snapAndSilence(channelSlugs(), m_audition.channels, &Names::channelNode);
    else      snapAndSilence(mixSlugs(),     m_audition.mixes,    &Names::mixNode);
    if (isCh) Q_EMIT channelChanged(slug); else Q_EMIT mixChanged(slug);
}
void Mixer::stopAudition() {
    if (m_audition.slug.isEmpty()) return;
    auto restore = [&](const QHash<QString, QPair<float, bool>> &saved, const QString &slugKind) {
        for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
            const QString node = slugKind == QLatin1String("channel") ? Names::channelNode(it.key()) : Names::mixNode(it.key());
            auto sink = m_sinks.find(node); if (sink == m_sinks.end()) continue;
            sink->volume = it->first; sink->mute = it->second;
            m_graph.setVolume(sink->id, sink->volume, sink->mute);
            if (slugKind == QLatin1String("channel")) Q_EMIT channelChanged(it.key()); else Q_EMIT mixChanged(it.key());
        }
    };
    restore(m_audition.channels, QStringLiteral("channel"));
    restore(m_audition.mixes, QStringLiteral("mix"));
    m_audition = {};
}
QString Mixer::auditionTarget() const { return m_audition.slug; }
QString Mixer::slugForSinkId(uint32_t sinkId) const {
    for (auto it = m_sinks.cbegin(); it != m_sinks.cend(); ++it)
        if (it->id == sinkId && it.key().startsWith(QLatin1String("kmixdeck.channel."))) return it.key().mid(17);
    // ADR 0008: a channel with effects is entered through kmixdeck.fx.<slug> — that IS the channel for routing
    // purposes. Without this, CH-5 saw such a stream as "nowhere" and dragged it onto the default channel
    // (measured 2026-09-16: fx test tone ended up on system instead of the voice chain).
    static const QString fxP = QStringLiteral("kmixdeck.fx.");
    for (const auto &n : m_graph.nodes())
        if (n.id == sinkId && n.name.startsWith(fxP) && !n.name.startsWith(fxP + QLatin1String("mix.")) && !n.name.endsWith(QLatin1String(".out")))
            return n.name.mid(fxP.size());
    return {};
}
void Mixer::onStreamRouted(uint32_t streamId, uint32_t sinkId) {
    auto it = m_apps.find(streamId); if (it == m_apps.end()) return;
    const QString slug = slugForSinkId(sinkId);
    // CH-6: a recreated node (same appKey) re-reads its assignment from the layout; until WirePlumber links it,
    // the layout list is what the UI shows. Only the PRIMARY follows live routing changes.
    if (const LayoutApp *la = m_layout.app(appKey(*it)); la && !la->channels.isEmpty()) it->channels = la->channels;
    else it->channels = slug.isEmpty() ? QStringList{} : QStringList{slug};
    Q_EMIT appChanged(streamId);
    finishAutoRoute(streamId);
}

QString Mixer::addChannel(const QString &displayName, QString *error) {
    const QString slug = Names::slugify(displayName);
    if (slug.isEmpty()) { if (error) *error = QStringLiteral("name has no usable characters"); return {}; }
    if (m_layout.channel(slug)) { if (error) *error = QStringLiteral("channel '%1' already exists").arg(slug); return {}; }
    m_layout.channels.push_back({slug, displayName.trimmed(), {}});
    if (!m_undo.isEmpty()) { m_undo = {}; Q_EMIT undoChanged(); }
    saveLayout(); reconcile();
    return slug;
}
QString Mixer::addVirtualDevice(const QString &displayName, int inputs, int outputs, QString *error) {   // DV-23
    const QString slug = Names::slugify(displayName);
    if (slug.isEmpty()) { if (error) *error = QStringLiteral("name has no usable characters"); return {}; }
    if (m_layout.virtualDevice(slug)) { if (error) *error = QStringLiteral("virtual device '%1' already exists").arg(slug); return {}; }
    if (inputs < 1 || inputs > 64 || outputs < 0 || outputs > 64) { if (error) *error = QStringLiteral("inputs 1..64, outputs 0..64"); return {}; }
    LayoutVirtualDevice v; v.slug = slug; v.name = displayName.trimmed(); v.inputs = inputs; v.outputs = outputs;
    m_layout.virtualDevices.push_back(v);
    saveLayout(); reconcile();
    return slug;
}
bool Mixer::removeVirtualDevice(const QString &slug) {
    auto *v = m_layout.virtualDevice(slug); if (!v) return false;
    const QStringList nodes{v->inputNode(), v->outputNode()};
    m_layout.virtualDevices.removeIf([&](const LayoutVirtualDevice &d) { return d.slug == slug; });
    saveLayout();   // layout first: a reconcile triggered by the removal below must not re-create them
    for (const QString &n : nodes) { m_nodeRequested.remove(n); if (auto node = m_graph.node(n)) m_graph.destroyObject(node->id); }
    Q_EMIT layoutChanged();
    return true;
}
QStringList Mixer::virtualDeviceSlugs() const { QStringList l; for (const auto &v : m_layout.virtualDevices) l << v.slug; return l; }
QString Mixer::addMix(const QString &displayName, QString *error) {
    const QString slug = Names::slugify(displayName);
    if (slug.isEmpty()) { if (error) *error = QStringLiteral("name has no usable characters"); return {}; }
    if (m_layout.mix(slug)) { if (error) *error = QStringLiteral("mix '%1' already exists").arg(slug); return {}; }
    m_layout.mixes.push_back({slug, displayName.trimmed(), {}, {}, {}});
    if (!m_undo.isEmpty()) { m_undo = {}; Q_EMIT undoChanged(); }
    saveLayout(); reconcile();
    return slug;
}
// CH-9 -----------------------------------------------------------------------------------------------------
void Mixer::snapshotForUndo(const QString &kind, const QString &slug) {
    QJsonObject u{{QStringLiteral("kind"), kind}};
    QJsonArray cells, links, inputs;
    const bool isCh = kind == QLatin1String("channel");
    QString name = isCh ? channelName(slug) : mixName(slug);
    u.insert(QStringLiteral("what"), (isCh ? QStringLiteral("channel “%1”") : QStringLiteral("mix “%1”")).arg(name));
    for (auto it = m_cells.cbegin(); it != m_cells.cend(); ++it) {
        const QStringList parts = it.key().mid(14).split(QLatin1Char('.'));   // "kmixdeck.link." is 14 chars
        if (parts.size() != 2 || parts[isCh ? 0 : 1] != slug) continue;
        cells.append(QJsonObject{{QStringLiteral("channel"), parts[0]}, {QStringLiteral("mix"), parts[1]}, {QStringLiteral("volume"), it->volume}, {QStringLiteral("mute"), it->mute}});
    }
    for (const auto &l : m_layout.links)
        if ((isCh && l.channel == slug) || (!isCh && (l.mix == slug || l.follows == slug)))
            links.append(QJsonObject{{QStringLiteral("channel"), l.channel}, {QStringLiteral("mix"), l.mix}, {QStringLiteral("follows"), l.follows}});
    if (isCh) {
        for (const auto &i : m_layout.inputs) if (i.channel == slug || i.slug == slug)
            inputs.append(QJsonObject{{QStringLiteral("slug"), i.slug}, {QStringLiteral("name"), i.name}, {QStringLiteral("device"), i.device.toJson()}, {QStringLiteral("channel"), i.channel}});
        if (auto *c = m_layout.channel(slug)) u.insert(QStringLiteral("layout"), QJsonObject{{QStringLiteral("slug"), c->slug}, {QStringLiteral("name"), c->name}, {QStringLiteral("icon"), c->icon}});
        u.insert(QStringLiteral("wasDefault"), m_layout.defaultChannel == slug);
        // channel trim/mute live on the channel sink
        if (auto s = m_sinks.constFind(Names::channelNode(slug)); s != m_sinks.constEnd()) { u.insert(QStringLiteral("trim"), s->volume); u.insert(QStringLiteral("muted"), s->mute); }
    } else if (auto *m = m_layout.mix(slug)) {
        QJsonArray outs; for (const auto &d : m->outputs) outs.append(d.toJson());
        u.insert(QStringLiteral("layout"), QJsonObject{{QStringLiteral("slug"), m->slug}, {QStringLiteral("name"), m->name}, {QStringLiteral("icon"), m->icon}, {QStringLiteral("outputs"), outs}, {QStringLiteral("fallbackOutput"), m->fallbackOutput.toJson()}});
        if (auto s = m_sinks.constFind(Names::mixNode(slug)); s != m_sinks.constEnd()) { u.insert(QStringLiteral("trim"), s->volume); u.insert(QStringLiteral("muted"), s->mute); }
    }
    u.insert(QStringLiteral("cells"), cells); u.insert(QStringLiteral("links"), links); u.insert(QStringLiteral("inputs"), inputs);
    m_undo = u; Q_EMIT undoChanged();
}
bool Mixer::undo() {
    if (m_undo.isEmpty()) return false;
    const QJsonObject u = m_undo; m_undo = {};
    const QJsonObject lay = u.value(QStringLiteral("layout")).toObject();
    const QString slug = lay.value(QStringLiteral("slug")).toString();
    const bool isCh = u.value(QStringLiteral("kind")).toString() == QLatin1String("channel");
    if (slug.isEmpty() || (isCh ? m_layout.channel(slug) != nullptr : m_layout.mix(slug) != nullptr)) { Q_EMIT undoChanged(); return false; }
    if (isCh) m_layout.channels.push_back({slug, lay.value(QStringLiteral("name")).toString(), lay.value(QStringLiteral("icon")).toString()});
    else {
        LayoutMix m; m.slug = slug; m.name = lay.value(QStringLiteral("name")).toString(); m.icon = lay.value(QStringLiteral("icon")).toString();
        for (const auto &d : lay.value(QStringLiteral("outputs")).toArray()) m.outputs.push_back(DeviceRef::fromJson(d.toObject()));
        m.fallbackOutput = DeviceRef::fromJson(lay.value(QStringLiteral("fallbackOutput")).toObject());
        m_layout.mixes.push_back(m);
    }
    for (const auto &v : u.value(QStringLiteral("links")).toArray()) { const auto j = v.toObject(); m_layout.links.push_back({j.value(QStringLiteral("channel")).toString(), j.value(QStringLiteral("mix")).toString(), j.value(QStringLiteral("follows")).toString()}); }
    for (const auto &v : u.value(QStringLiteral("inputs")).toArray()) { const auto j = v.toObject(); m_layout.inputs.push_back({j.value(QStringLiteral("slug")).toString(), j.value(QStringLiteral("name")).toString(), DeviceRef::fromJson(j.value(QStringLiteral("device")).toObject()), j.value(QStringLiteral("channel")).toString()}); }
    if (isCh && u.value(QStringLiteral("wasDefault")).toBool()) m_layout.defaultChannel = slug;
    // fader/mute per cell: the loopbacks do not exist yet — apply as soon as each node shows up (onNode)
    for (const auto &v : u.value(QStringLiteral("cells")).toArray()) {
        const auto j = v.toObject();
        m_pendingCellState.insert(Names::cellNode(j.value(QStringLiteral("channel")).toString(), j.value(QStringLiteral("mix")).toString()),
                                  {static_cast<float>(j.value(QStringLiteral("volume")).toDouble(1.0)), j.value(QStringLiteral("mute")).toBool()});
    }
    if (u.contains(QStringLiteral("trim")))
        m_pendingCellState.insert(isCh ? Names::channelNode(slug) : Names::mixNode(slug), {static_cast<float>(u.value(QStringLiteral("trim")).toDouble(1.0)), u.value(QStringLiteral("muted")).toBool()});
    saveLayout(); reconcile();
    restorePendingCellStates();
    qInfo() << "undo:" << u.value(QStringLiteral("what")).toString() << "restored";
    Q_EMIT undoChanged(); if (isCh) Q_EMIT defaultChannelChanged();
    return true;
}
void Mixer::restorePendingCellStates() {
    for (auto it = m_pendingCellState.begin(); it != m_pendingCellState.end();) {
        const pw::NodeInfo *n = nullptr;
        if (auto c = m_cells.constFind(it.key()); c != m_cells.constEnd()) n = &*c;
        else if (auto s = m_sinks.constFind(it.key()); s != m_sinks.constEnd()) n = &*s;
        if (!n) { ++it; continue; }
        m_graph.setVolume(n->id, it->first, it->second);
        it = m_pendingCellState.erase(it);
    }
}
void Mixer::removeChannel(const QString &slug) {
    if (!m_layout.channel(slug)) return;
    snapshotForUndo(QStringLiteral("channel"), slug);
    m_layout.channels.removeIf([&](const LayoutChannel &c) { return c.slug == slug; });
    m_layout.inputs.removeIf([&](const LayoutInput &i) { return i.channel == slug || i.slug == slug; });   // no orphan inputs
    if (m_layout.defaultChannel == slug) { m_layout.defaultChannel.clear(); Q_EMIT defaultChannelChanged(); }
    m_layout.links.removeIf([&](const LayoutLink &l) { return l.channel == slug; });
    saveLayout();
    destroyOurNodes([&](const QString &n) {
        return n.startsWith(Names::cellNode(slug, QString())) || n == Names::channelNode(slug)
            || n == EdgeNames::inputNode(slug) || n == EdgeNames::inputNode(slug) + QStringLiteral(".in");
    });
    m_edges.remove(EdgeNames::inputNode(slug));
    m_channels.removeIf([&](const Channel &c) { return c.slug == slug; }); Q_EMIT layoutChanged(); Q_EMIT inputsChanged();
}
void Mixer::removeMix(const QString &slug) {
    if (!m_layout.mix(slug)) return;
    snapshotForUndo(QStringLiteral("mix"), slug);
    m_layout.mixes.removeIf([&](const LayoutMix &m) { return m.slug == slug; });
    m_layout.links.removeIf([&](const LayoutLink &l) { return l.mix == slug || l.follows == slug; });
    saveLayout();
    const QString out = QStringLiteral("kmixdeck.out.") + slug, src = EdgeNames::sourceNode(slug);
    destroyOurNodes([&](const QString &n) {
        if (n.startsWith(QLatin1String("kmixdeck.link.")) && n.section(QLatin1Char('.'), 3, 3) == slug) return true;   // cells incl. .in
        return n == Names::mixNode(slug) || n == out || n.startsWith(out + QLatin1Char('.')) || n == src || n.startsWith(src + QLatin1Char('.'));
    });
    for (auto it = m_edges.begin(); it != m_edges.end();) (it.key() == out || it.key().startsWith(out + QLatin1Char('.')) || it.key() == src) ? it = m_edges.erase(it) : ++it;
    m_mixes.removeIf([&](const Mix &m) { return m.slug == slug; }); Q_EMIT layoutChanged();
}
// Destroying a loopback's playback node tears the whole module down (both streams); capture nodes are
// listed too so nothing is missed when the graph is in a half state. Snapshot first: destroy mutates m_graph.
void Mixer::destroyOurNodes(const std::function<bool(const QString &)> &match) {
    QList<uint32_t> ids;
    for (const auto &n : m_graph.nodes()) if (n.name.startsWith(QLatin1String("kmixdeck.")) && match(n.name)) { ids << n.id; m_nodeRequested.remove(n.name); }
    for (uint32_t id : ids) m_graph.destroyObject(id);
}

// Discover our objects from the live graph — the graph is the source of truth (DV-1).
void Mixer::onNode(const pw::NodeInfo &n) {
    m_idToName[n.id] = n.name;
    static const QString chP = QStringLiteral("kmixdeck.channel."), mxP = QStringLiteral("kmixdeck.mix."), lkP = QStringLiteral("kmixdeck.link.");
    bool layout = false;
    if (n.name.startsWith(chP)) {
        const QString slug = n.name.mid(chP.size());
        const bool isNew = !m_sinks.contains(n.name);
        m_sinks[n.name] = n;
        if (isNew && !m_pendingCellState.isEmpty()) restorePendingCellStates();   // undo of a channel: trim/mute
        bool found = false; for (auto &c : m_channels) if (c.slug == slug) { found = true; if (c.name.isEmpty()) c.name = n.description; }
        if (!found) { m_channels.push_back({slug, n.description, {}, true}); layout = true; }
        Q_EMIT channelChanged(slug);
    } else if (n.name.startsWith(mxP)) {
        const QString slug = n.name.mid(mxP.size());
        const bool isNew = !m_sinks.contains(n.name);
        m_sinks[n.name] = n;
        if (isNew && !m_pendingCellState.isEmpty()) restorePendingCellStates();   // undo of a mix: master volume/mute (nodes arrive in any order — CI showed the mix sink after its cells)
        QString disp = n.description; if (disp.startsWith(QLatin1String("Mix: "))) disp.remove(0, 5);
        bool found = false; for (auto &m : m_mixes) if (m.slug == slug) { found = true; if (m.name.isEmpty()) m.name = disp; }
        if (!found) { m_mixes.push_back({slug, disp, {}, true}); layout = true; }
        Q_EMIT mixChanged(slug);
    } else if (n.name.startsWith(lkP) && !n.name.endsWith(QLatin1String(".in")) && n.mediaClass.startsWith(QLatin1String("Stream/Output"))) {
        const bool isNew = !m_cells.contains(n.name);
        m_cells[n.name] = n;
        const QStringList parts = n.name.mid(lkP.size()).split(QLatin1Char('.'));
        if (parts.size() == 2) { Q_EMIT cellChanged(parts[0], parts[1]); if (!isNew) propagateLinks(parts[0], parts[1]); }
        if (isNew) { layout = true; if (!m_pendingCellState.isEmpty()) restorePendingCellStates(); }
    }
    else if (n.name.startsWith(QLatin1String("kmixdeck.in.")) || n.name.startsWith(QLatin1String("kmixdeck.out.")) || n.name.startsWith(QLatin1String("kmixdeck.source."))) {
        m_edges[n.name] = n;                                    // device-edge playback side (DV-14 volume lives here)
        if (n.name.startsWith(QLatin1String("kmixdeck.in."))) Q_EMIT inputChanged(n.name.mid(12));
        else for (const auto &m : m_mixes) {
            const QString base = QStringLiteral("kmixdeck.out.") + m.slug;
            if (n.name == base || n.name.startsWith(base + QLatin1Char('.')) || n.name == EdgeNames::sourceNode(m.slug)) { Q_EMIT mixChanged(m.slug); break; }
        }
    }
    else if ((n.mediaClass == QLatin1String("Audio/Sink") || n.mediaClass.startsWith(QLatin1String("Audio/Source")))
             && (!n.name.startsWith(QLatin1String("kmixdeck.")) || n.name.startsWith(QLatin1String("kmixdeck.virt.")))) {   // our own plumbing is not a device — except DV-23 virtual devices, which are meant to be picked
        const bool wasNew = !m_devices.contains(n.name);
        m_devices[n.name] = n;
        // Remember the human name in the layout, so an unplugged device can still be named (DV-9). Layouts migrated
        // from v1 carry the node.name as description — this heals them the first time the device is seen.
        if (!n.description.isEmpty()) {
            bool touched = false;
            for (auto &m : m_layout.mixes) {
                for (auto &d : m.outputs) if (d.node == n.name && d.description != n.description) { d.description = n.description; touched = true; }
                if (m.fallbackOutput.node == n.name && m.fallbackOutput.description != n.description) { m.fallbackOutput.description = n.description; touched = true; }
            }
            for (auto &i : m_layout.inputs) if (i.device.node == n.name && i.device.description != n.description) { i.device.description = n.description; touched = true; }
            if (touched) { saveLayout(); for (const auto &m : m_layout.mixes) Q_EMIT mixChanged(m.slug); }
        }
        if (wasNew) {                                            // DV-12: appearance → re-pick outputs and inputs
            applyFallbacks();
            for (const auto &i : m_layout.inputs) ensureEdgeLoopbackForInput(i.slug);
            notifyPresence();
        }
        if (n.mediaClass == QLatin1String("Audio/Sink")) Q_EMIT outputDevicesChanged(); else Q_EMIT inputDevicesChanged();
    }
    else if (n.mediaClass == QLatin1String("Stream/Output/Audio") && !n.name.startsWith(QLatin1String("kmixdeck."))) {
        const bool isNew = !m_apps.contains(n.id);
        App &a = m_apps[n.id];
        a.id = n.id; a.name = n.appName.isEmpty() ? n.name : n.appName; a.binary = n.appBinary; a.mediaName = n.mediaName; a.mediaRole = n.mediaRole; a.nodeName = n.name;
        a.iconName = n.iconName; a.running = (n.state == QLatin1String("running"));   // UX-10: is it making sound right now
        // Layout is the authority once it has an entry (CH-4/CH-12): the live link may still point at the old sink
        // while WirePlumber moves the stream — a nodeChanged in that window must not overwrite the assignment.
        // No layout entry → follow whatever the graph says (unassigned apps, WirePlumber restore).
        if (const LayoutApp *la = m_layout.app(appKey(a)); la && !la->channels.isEmpty()) a.channels = la->channels;
        else { const QString live = slugForSinkId(m_graph.streamSink(n.id)); a.channels = live.isEmpty() ? QStringList{} : QStringList{live}; }
        if (isNew) { Q_EMIT appAdded(n.id); autoRouteNewApp(a); } else Q_EMIT appChanged(n.id);
        // CH-12: the app node is here → its relays may exist now (they are torn down while the node is absent)
        if (isNew) if (const LayoutApp *la = m_layout.app(appKey(a)); la && la->channels.size() > 1) ensureAppRelays(*la);
    }
    if (layout) Q_EMIT layoutChanged();
}

void Mixer::onNodeRemoved(uint32_t id) {
    const QString name = m_idToName.take(id);
    m_pendingAutoRoute.remove(id);
    m_nodeRequested.remove(name);   // gone for real → may be requested again (CH-9 undo re-creates a removed mix)
    if (m_apps.remove(id)) Q_EMIT appRemoved(id);
    if (name.isEmpty()) return;
    // CH-12 relays follow the app node: gone → tear the relay down (see ensureAppRelays for why a lingering one hurts)
    for (const auto &la : m_layout.apps) if (la.nodeName == name && la.channels.size() > 1) removeAppRelays(la);
    if (m_edges.remove(name)) for (const auto &m : m_mixes) if (name == EdgeNames::outputNode(m.slug, 0) || name == EdgeNames::sourceNode(m.slug)) { Q_EMIT mixChanged(m.slug); break; }
    // An edge we destroyed on purpose (port subset changed, ADR 0009) is rebuilt as soon as PipeWire confirms it
    // is gone — destroyObject() is asynchronous, so ensureEdgeLoopbacks() right after it would still see the old node.
    if (m_connected && name.startsWith(QLatin1String("kmixdeck.out.")) && !name.endsWith(QLatin1String(".in")))
        QTimer::singleShot(0, this, [this] { ensureEdgeLoopbacks(); applyFallbacks(); });
    if (m_devices.remove(name)) { Q_EMIT outputDevicesChanged(); Q_EMIT inputDevicesChanged(); applyFallbacks(); notifyPresence(); }   // DV-9: absence → grey out + park outputs
    bool layout = false;
    if (m_cells.remove(name)) layout = true;
    m_sinks.remove(name);
    for (int i = 0; i < m_channels.size(); ++i) if (Names::channelNode(m_channels[i].slug) == name) { m_channels.remove(i); layout = true; break; }
    for (int i = 0; i < m_mixes.size(); ++i) if (Names::mixNode(m_mixes[i].slug) == name) { m_mixes.remove(i); layout = true; break; }
    if (layout) Q_EMIT layoutChanged();
}

void Mixer::rebuildLayoutFromGraph() { for (const auto &n : m_graph.nodes()) onNode(n); }

// Present-flags live on Channel/Mix objects; a device coming or going changes them without any layout edit.
void Mixer::notifyPresence() {
    for (const auto &i : m_layout.inputs) Q_EMIT channelChanged(i.channel);
    for (const auto &m : m_layout.mixes) if (!m.outputs.isEmpty()) Q_EMIT mixChanged(m.slug);
}

} // namespace kmixdeck
