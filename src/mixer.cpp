// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "mixer.h"
#include <QTimer>
#include <QFile>
#include <QRegularExpression>
#include <QDebug>
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
        if (!m_graph.node(Names::channelNode(c.slug))) m_graph.createNullSink(Names::channelNode(c.slug), c.name, true);
        bool found = false; for (auto &ch : m_channels) if (ch.slug == c.slug) { found = true; ch.name = c.name; }
        if (!found) m_channels.push_back({c.slug, c.name, c.icon, true});
    }
    for (const auto &m : m_layout.mixes) {
        if (!m_graph.node(Names::mixNode(m.slug))) m_graph.createNullSink(Names::mixNode(m.slug), QStringLiteral("Mix: ") + m.name, false);
        bool found = false; for (auto &mx : m_mixes) if (mx.slug == m.slug) { found = true; mx.name = m.name; }
        if (!found) m_mixes.push_back({m.slug, m.name, m.icon, true});
    }
    for (const auto &c : m_layout.channels)
        for (const auto &m : m_layout.mixes) {
            const QString cell = Names::cellNode(c.slug, m.slug);
            if (m_graph.node(cell)) continue;
            m_graph.loadLoopback(loopbackArgs(c.name + QStringLiteral(" → ") + m.name,
                                              cell + QStringLiteral(".in"), Names::channelNode(c.slug), true, {}, false,
                                              cell, Names::mixNode(m.slug), {}, false, true));
        }
    ensureEdgeLoopbacks();
    // Capture sides are plumbing, not faders. WirePlumber restores whatever volume it last saw on them (it did:
    // a test left kmixdeck.link.game.stream.in at 0.0156 → the stream mix was 36 dB down with the fader at 0 dB).
    for (const auto &n : m_graph.nodes())
        if (n.name.startsWith(QLatin1String("kmixdeck.")) && n.name.endsWith(QLatin1String(".in")) && (n.volume != 1.0f || n.mute)) {
            qInfo() << "resetting capture side" << n.name << "to 1.0/unmuted (was" << n.volume << n.mute << ")";
            m_graph.setVolume(n.id, 1.0f, false);
        }
    // First start without a config fragment on disk: write it now so the graph exists at next login without us.
    if (!m_pwConfPath.isEmpty() && !QFile::exists(m_pwConfPath)) saveLayout();
    applyFallbacks();
    m_reconciled = true;
    Q_EMIT layoutChanged();
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
    const float lin = cubicToLinear(std::clamp(cubic, 0.0, 1.0));
    it->volume = lin;                         // optimistic; PipeWire echoes via nodeChanged
    m_graph.setVolume(it->id, lin, it->mute);
    Q_EMIT cellChanged(ch, mix);
}
void Mixer::setCellMuted(const QString &ch, const QString &mix, bool muted) {
    auto it = m_cells.find(Names::cellNode(ch, mix)); if (it == m_cells.end()) return;
    it->mute = muted;
    m_graph.setVolume(it->id, it->volume, muted);
    Q_EMIT cellChanged(ch, mix);
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
void Mixer::renameChannel(const QString &slug, const QString &name) { for (auto &c : m_channels) if (c.slug == slug) { c.name = name; Q_EMIT channelChanged(slug); } if (auto *l = m_layout.channel(slug)) { l->name = name; saveLayout(); } }
void Mixer::renameMix(const QString &slug, const QString &name)     { for (auto &m : m_mixes) if (m.slug == slug) { m.name = name; Q_EMIT mixChanged(slug); } if (auto *l = m_layout.mix(slug)) { l->name = name; saveLayout(); } }

// ---- device edges (ADR 0007) -----------------------------------------------------------------------
// Mix outputs are a list (MX-9); the bus-facing view stays a single node name for now: the first entry.
QVector<DeviceRef> Mixer::mixOutputs(const QString &slug) const {
    if (const auto *m = m_layout.mix(slug)) return m->outputs;
    return {};
}
QString Mixer::mixOutputDevice(const QString &slug) const {
    const auto outs = mixOutputs(slug);
    return outs.isEmpty() ? QString() : outs.first().node;
}
void Mixer::setMixOutputDevice(const QString &slug, const QString &nodeName) {
    QVector<DeviceRef> outs;
    if (!nodeName.isEmpty()) {
        QString desc = nodeName;
        if (auto d = m_devices.value(nodeName); !d.description.isEmpty()) desc = d.description;
        outs.push_back({nodeName, desc, {}});
    }
    setMixOutputs(slug, outs);
}
bool Mixer::setMixOutputs(const QString &slug, const QVector<DeviceRef> &outputs) {
    auto *m = m_layout.mix(slug);
    if (!m) return false;
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
    return in ? in->device.node : QString();
}
bool Mixer::channelInputPresent(const QString &channel) const {
    return m_layout.input(channel) ? inputPresent(channel) : true;
}
bool Mixer::setChannelInputDevice(const QString &channel, const QString &nodeName) {
    if (!m_layout.channel(channel)) return false;
    if (!nodeName.isEmpty() && !m_devices.contains(nodeName)) return false;
    if (auto *in = m_layout.input(channel)) {
        if (in->device.node == nodeName) return true;
        if (auto n = m_graph.node(EdgeNames::inputNode(channel))) m_graph.destroyObject(n->id);   // rebuild with the new target
        m_edges.remove(EdgeNames::inputNode(channel));
        if (nodeName.isEmpty()) { removeInput(channel); Q_EMIT channelChanged(channel); return true; }
        DeviceRef d{nodeName, m_devices.value(nodeName).description, {}};
        in->device = d; saveLayout(); ensureEdgeLoopbackForInput(channel);
        Q_EMIT inputChanged(channel); Q_EMIT channelChanged(channel);
        return true;
    }
    if (nodeName.isEmpty()) return true;
    LayoutInput in; in.slug = channel; in.name = channelName(channel); in.channel = channel;
    in.device = {nodeName, m_devices.value(nodeName).description, {}};
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
QList<Device> Mixer::inputDevices() const  { return devicesFor(QLatin1String("Audio/Source")); }
QList<Device> Mixer::devicesFor(const QString &mediaClass) const {
    QList<Device> out;
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it) {
        if (it->mediaClass != mediaClass) continue;
        out.append({it.key(), it->description.isEmpty() ? it.key() : it->description, it->positions, mediaClass == QLatin1String("Audio/Source")});
    }
    std::sort(out.begin(), out.end(), [](const Device &a, const Device &b) { return a.description.localeAwareCompare(b.description) < 0; });
    return out;
}

QList<uint32_t> Mixer::appIds() const { auto l = m_apps.keys(); std::sort(l.begin(), l.end()); return l; }
std::optional<App> Mixer::app(uint32_t id) const { auto it = m_apps.constFind(id); return it == m_apps.constEnd() ? std::nullopt : std::optional<App>(*it); }
bool Mixer::moveApp(uint32_t id, const QString &channelSlug) {
    if (!m_apps.contains(id) || !channelSlugs().contains(channelSlug)) return false;
    return m_graph.moveStream(id, Names::channelNode(channelSlug));
}
QString Mixer::slugForSinkId(uint32_t sinkId) const {
    for (auto it = m_sinks.cbegin(); it != m_sinks.cend(); ++it)
        if (it->id == sinkId && it.key().startsWith(QLatin1String("kmixdeck.channel."))) return it.key().mid(17);
    return {};
}
void Mixer::onStreamRouted(uint32_t streamId, uint32_t sinkId) {
    auto it = m_apps.find(streamId); if (it == m_apps.end()) return;
    const QString slug = slugForSinkId(sinkId);
    if (it->channelSlug != slug) { it->channelSlug = slug; Q_EMIT appChanged(streamId); }
}

QString Mixer::addChannel(const QString &displayName, QString *error) {
    const QString slug = Names::slugify(displayName);
    if (slug.isEmpty()) { if (error) *error = QStringLiteral("name has no usable characters"); return {}; }
    if (m_layout.channel(slug)) { if (error) *error = QStringLiteral("channel '%1' already exists").arg(slug); return {}; }
    m_layout.channels.push_back({slug, displayName.trimmed(), {}});
    saveLayout(); reconcile();
    return slug;
}
QString Mixer::addMix(const QString &displayName, QString *error) {
    const QString slug = Names::slugify(displayName);
    if (slug.isEmpty()) { if (error) *error = QStringLiteral("name has no usable characters"); return {}; }
    if (m_layout.mix(slug)) { if (error) *error = QStringLiteral("mix '%1' already exists").arg(slug); return {}; }
    m_layout.mixes.push_back({slug, displayName.trimmed(), {}, {}, {}});
    saveLayout(); reconcile();
    return slug;
}
void Mixer::removeChannel(const QString &slug) {
    m_layout.channels.removeIf([&](const LayoutChannel &c) { return c.slug == slug; });
    m_layout.inputs.removeIf([&](const LayoutInput &i) { return i.channel == slug || i.slug == slug; });   // no orphan inputs
    saveLayout();
    destroyOurNodes([&](const QString &n) {
        return n.startsWith(Names::cellNode(slug, QString())) || n == Names::channelNode(slug)
            || n == EdgeNames::inputNode(slug) || n == EdgeNames::inputNode(slug) + QStringLiteral(".in");
    });
    m_edges.remove(EdgeNames::inputNode(slug));
    m_channels.removeIf([&](const Channel &c) { return c.slug == slug; }); Q_EMIT layoutChanged(); Q_EMIT inputsChanged();
}
void Mixer::removeMix(const QString &slug) {
    m_layout.mixes.removeIf([&](const LayoutMix &m) { return m.slug == slug; });
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
    for (const auto &n : m_graph.nodes()) if (n.name.startsWith(QLatin1String("kmixdeck.")) && match(n.name)) ids << n.id;
    for (uint32_t id : ids) m_graph.destroyObject(id);
}

// Discover our objects from the live graph — the graph is the source of truth (DV-1).
void Mixer::onNode(const pw::NodeInfo &n) {
    m_idToName[n.id] = n.name;
    static const QString chP = QStringLiteral("kmixdeck.channel."), mxP = QStringLiteral("kmixdeck.mix."), lkP = QStringLiteral("kmixdeck.link.");
    bool layout = false;
    if (n.name.startsWith(chP)) {
        const QString slug = n.name.mid(chP.size());
        m_sinks[n.name] = n;
        bool found = false; for (auto &c : m_channels) if (c.slug == slug) { found = true; if (c.name.isEmpty()) c.name = n.description; }
        if (!found) { m_channels.push_back({slug, n.description, {}, true}); layout = true; }
        Q_EMIT channelChanged(slug);
    } else if (n.name.startsWith(mxP)) {
        const QString slug = n.name.mid(mxP.size());
        m_sinks[n.name] = n;
        QString disp = n.description; if (disp.startsWith(QLatin1String("Mix: "))) disp.remove(0, 5);
        bool found = false; for (auto &m : m_mixes) if (m.slug == slug) { found = true; if (m.name.isEmpty()) m.name = disp; }
        if (!found) { m_mixes.push_back({slug, disp, {}, true}); layout = true; }
        Q_EMIT mixChanged(slug);
    } else if (n.name.startsWith(lkP) && !n.name.endsWith(QLatin1String(".in")) && n.mediaClass.startsWith(QLatin1String("Stream/Output"))) {
        const bool isNew = !m_cells.contains(n.name);
        m_cells[n.name] = n;
        const QStringList parts = n.name.mid(lkP.size()).split(QLatin1Char('.'));
        if (parts.size() == 2) Q_EMIT cellChanged(parts[0], parts[1]);
        if (isNew) layout = true;
    }
    else if (n.name.startsWith(QLatin1String("kmixdeck.in.")) || n.name.startsWith(QLatin1String("kmixdeck.out.")) || n.name.startsWith(QLatin1String("kmixdeck.source."))) {
        m_edges[n.name] = n;                                    // device-edge playback side (DV-14 volume lives here)
        if (n.name.startsWith(QLatin1String("kmixdeck.in."))) Q_EMIT inputChanged(n.name.mid(12));
        else for (const auto &m : m_mixes) {
            const QString base = QStringLiteral("kmixdeck.out.") + m.slug;
            if (n.name == base || n.name.startsWith(base + QLatin1Char('.')) || n.name == EdgeNames::sourceNode(m.slug)) { Q_EMIT mixChanged(m.slug); break; }
        }
    }
    else if ((n.mediaClass == QLatin1String("Audio/Sink") || n.mediaClass == QLatin1String("Audio/Source")) && !n.name.startsWith(QLatin1String("kmixdeck."))) {
        const bool wasNew = !m_devices.contains(n.name);
        m_devices[n.name] = n;
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
        a.channelSlug = slugForSinkId(m_graph.streamSink(n.id));
        if (isNew) Q_EMIT appAdded(n.id); else Q_EMIT appChanged(n.id);
    }
    if (layout) Q_EMIT layoutChanged();
}

void Mixer::onNodeRemoved(uint32_t id) {
    const QString name = m_idToName.take(id);
    if (m_apps.remove(id)) Q_EMIT appRemoved(id);
    if (name.isEmpty()) return;
    if (m_edges.remove(name)) for (const auto &m : m_mixes) if (name == EdgeNames::outputNode(m.slug, 0) || name == EdgeNames::sourceNode(m.slug)) { Q_EMIT mixChanged(m.slug); break; }
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
