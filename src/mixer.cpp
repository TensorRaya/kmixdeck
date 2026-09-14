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
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    s = s.trimmed(); while (s.startsWith(QLatin1Char('-'))) s.remove(0, 1); while (s.endsWith(QLatin1Char('-'))) s.chop(1);
    return s.isEmpty() ? QStringLiteral("x") : s;
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
        m_channels.clear(); m_mixes.clear(); m_cells.clear(); m_sinks.clear(); m_devices.clear(); m_idToName.clear(); Q_EMIT outputDevicesChanged();
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
void Mixer::reconcile() {
    if (!m_connected) return;
    // Layout → PipeWire: create what is declared but missing. Names carry the display strings.
    if (!m_graph.node(QStringLiteral("kmixdeck.null"))) m_graph.createParkingSink();
    for (const auto &c : m_layout.channels) {
        if (!m_graph.node(Names::channelNode(c.slug))) m_graph.createNullSink(Names::channelNode(c.slug), c.name, true);
        bool found = false; for (auto &ch : m_channels) if (ch.slug == c.slug) { found = true; ch.name = c.name; }
        if (!found) m_channels.push_back({c.slug, c.name, c.icon, true});
    }
    for (const auto &m : m_layout.mixes) {
        if (!m_graph.node(Names::mixNode(m.slug))) m_graph.createNullSink(Names::mixNode(m.slug), QStringLiteral("Mix: ") + m.name, false);
        bool found = false; for (auto &mx : m_mixes) if (mx.slug == m.slug) { found = true; mx.name = m.name; mx.outputDevice = m.outputDevice; }
        if (!found) m_mixes.push_back({m.slug, m.name, m.icon, true, m.outputDevice});
    }
    for (const auto &c : m_layout.channels)
        for (const auto &m : m_layout.mixes)
            if (!m_graph.node(Names::cellNode(c.slug, m.slug)))
                m_graph.createLoopback(Names::cellNode(c.slug, m.slug), c.name + QStringLiteral(" → ") + m.name, Names::channelNode(c.slug), Names::mixNode(m.slug));
    for (const auto &m : m_layout.mixes) {
        if (!m_graph.node(QStringLiteral("kmixdeck.out.") + m.slug)) m_graph.createMixOutput(m.slug, QStringLiteral("Mix: ") + m.name + QStringLiteral(" → output"), m.outputDevice);
        if (!m_graph.node(QStringLiteral("kmixdeck.source.") + m.slug)) m_graph.createMixSource(m.slug, QStringLiteral("kmixdeck ") + m.name + QStringLiteral(" Mix"));
    }
    // First start without a config fragment on disk: write it now so the graph exists at next login without us.
    if (!m_pwConfPath.isEmpty() && !QFile::exists(m_pwConfPath)) saveLayout();
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
QString Mixer::mixOutputDevice(const QString &slug) const { for (const auto &m : m_mixes) if (m.slug == slug) return m.outputDevice; return {}; }
void Mixer::setMixOutputDevice(const QString &slug, const QString &nodeName) {
    for (auto &m : m_mixes) if (m.slug == slug) { m.outputDevice = nodeName; Q_EMIT mixChanged(slug); }
    if (auto *l = m_layout.mix(slug)) { l->outputDevice = nodeName; saveLayout(); }
    // live retarget of kmixdeck.out.<slug>: metadata target.object on its playback stream (the loopback has
    // dont-fallback but NOT dont-reconnect, so WirePlumber follows; clearing the key unlinks it).
    if (auto out = m_graph.node(QStringLiteral("kmixdeck.out.") + slug)) {
        if (!m_graph.moveStream(out->id, nodeName.isEmpty() ? QStringLiteral("kmixdeck.null") : nodeName)) qWarning() << "output device not found:" << nodeName;
    }
}
QString Mixer::mixCaptureSource(const QString &slug) const {
    return m_graph.node(QStringLiteral("kmixdeck.source.") + slug) ? QStringLiteral("kmixdeck.source.") + slug : QString();
}

QList<QPair<QString, QString>> Mixer::outputDevices() const {
    QList<QPair<QString, QString>> out;
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it) out.append(qMakePair(it.key(), it->description.isEmpty() ? it.key() : it->description));
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.second.localeAwareCompare(b.second) < 0; });
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

void Mixer::addChannel(const QString &displayName) {
    const QString slug = Names::slugify(displayName);
    if (slug.isEmpty() || m_layout.channel(slug)) return;
    m_layout.channels.push_back({slug, displayName, {}});
    saveLayout(); reconcile();
}
void Mixer::addMix(const QString &displayName) {
    const QString slug = Names::slugify(displayName);
    if (slug.isEmpty() || m_layout.mix(slug)) return;
    m_layout.mixes.push_back({slug, displayName, {}, {}});
    saveLayout(); reconcile();
}
void Mixer::removeChannel(const QString &slug) {
    m_layout.channels.removeIf([&](const LayoutChannel &c) { return c.slug == slug; });
    saveLayout();
    for (auto it = m_cells.begin(); it != m_cells.end(); ++it) if (it.key().startsWith(Names::cellNode(slug, QString()))) m_graph.destroyObject(it->id);
    if (auto n = m_graph.node(Names::channelNode(slug))) m_graph.destroyObject(n->id);
    m_channels.removeIf([&](const Channel &c) { return c.slug == slug; }); Q_EMIT layoutChanged();
}
void Mixer::removeMix(const QString &slug) {
    m_layout.mixes.removeIf([&](const LayoutMix &m) { return m.slug == slug; });
    saveLayout();
    for (auto it = m_cells.begin(); it != m_cells.end(); ++it) if (it.key().endsWith(QLatin1Char('.') + slug)) m_graph.destroyObject(it->id);
    if (auto n = m_graph.node(Names::mixNode(slug))) m_graph.destroyObject(n->id);
    m_mixes.removeIf([&](const Mix &m) { return m.slug == slug; }); Q_EMIT layoutChanged();
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
        if (!found) { m_mixes.push_back({slug, disp, {}, true, {}}); layout = true; }
        Q_EMIT mixChanged(slug);
    } else if (n.name.startsWith(lkP) && !n.name.endsWith(QLatin1String(".in")) && n.mediaClass.startsWith(QLatin1String("Stream/Output"))) {
        const bool isNew = !m_cells.contains(n.name);
        m_cells[n.name] = n;
        const QStringList parts = n.name.mid(lkP.size()).split(QLatin1Char('.'));
        if (parts.size() == 2) Q_EMIT cellChanged(parts[0], parts[1]);
        if (isNew) layout = true;
    }
    else if (n.mediaClass == QLatin1String("Audio/Sink") && !n.name.startsWith(QLatin1String("kmixdeck."))) {
        m_devices[n.name] = n; Q_EMIT outputDevicesChanged();
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
    if (m_devices.remove(name)) Q_EMIT outputDevicesChanged();
    bool layout = false;
    if (m_cells.remove(name)) layout = true;
    m_sinks.remove(name);
    for (int i = 0; i < m_channels.size(); ++i) if (Names::channelNode(m_channels[i].slug) == name) { m_channels.remove(i); layout = true; break; }
    for (int i = 0; i < m_mixes.size(); ++i) if (Names::mixNode(m_mixes[i].slug) == name) { m_mixes.remove(i); layout = true; break; }
    if (layout) Q_EMIT layoutChanged();
}

void Mixer::rebuildLayoutFromGraph() { for (const auto &n : m_graph.nodes()) onNode(n); }

} // namespace kmixdeck
