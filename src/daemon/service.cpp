// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "service.h"
#include <cstring>
#include "kmixdeck_version.h"
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusError>
#include <QJsonDocument>
#include <QDebug>
#include <QFile>
#include <cmath>

namespace kmixdeck::daemon {

void emitPropertiesChanged(const QString &path, const QString &iface, const QVariantMap &changed) {
    QDBusMessage m = QDBusMessage::createSignal(path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
    m << iface << changed << QStringList{};
    QDBusConnection::sessionBus().send(m);
}

// ---- ObjectManager
ObjectManagerAdaptor::ObjectManagerAdaptor(QObject *parent, Provider provider) : QDBusAbstractAdaptor(parent), m_provider(std::move(provider)) {}
ManagedObjects ObjectManagerAdaptor::GetManagedObjects() { return m_provider(); }

// ---- Cell
CellObject::CellObject(Mixer *mixer, const QString &ch, const QString &mix, QObject *parent)
    : ExportedObject(Service::cellPath(ch, mix), parent), m_mixer(mixer), m_ch(ch), m_mix(mix) {}
QDBusObjectPath CellObject::channel() const { return QDBusObjectPath(Service::channelPath(m_ch)); }
QDBusObjectPath CellObject::mix() const { return QDBusObjectPath(Service::mixPath(m_mix)); }
QDBusObjectPath CellObject::follows() const { const QString f = m_mixer->cellFollows(m_ch, m_mix); return QDBusObjectPath(f.isEmpty() ? QStringLiteral("/") : Service::mixPath(f)); }
void CellObject::setFollows(const QDBusObjectPath &p) {
    const QString prefix = Service::mixPath(QString());
    QString slug;
    if (p.path() == QLatin1String("/")) slug.clear();
    else if (p.path().startsWith(prefix)) slug = p.path().mid(prefix.size());
    else { rejectProperty(QStringLiteral("Follows"), QStringLiteral("not a mix path")); return; }
    if (!m_mixer->setCellFollows(m_ch, m_mix, slug)) rejectProperty(QStringLiteral("Follows"), QStringLiteral("unknown mix, self, or would form a loop"));
}
double CellObject::volume() const { return Mixer::cubicToLinear(m_mixer->cellVolume(m_ch, m_mix)); }
bool CellObject::muted() const { return m_mixer->cellMuted(m_ch, m_mix); }
void CellObject::setVolume(double linear) {
    if (!(linear >= 0.0 && linear <= 1.0)) { rejectProperty(QStringLiteral("Volume"), QStringLiteral("must be linear 0..1")); return; }
    m_mixer->setCellVolume(m_ch, m_mix, Mixer::linearToCubic(static_cast<float>(linear)));
}
void CellObject::setMuted(bool m) { m_mixer->setCellMuted(m_ch, m_mix, m); }
void CellObject::ToggleMute() { m_mixer->setCellMuted(m_ch, m_mix, !m_mixer->cellMuted(m_ch, m_mix)); }
void CellObject::SetVolumeDb(double db) {
    if (db > 0.0) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("dB must be <= 0")); return; }
    setVolume(db < -200.0 ? 0.0 : std::pow(10.0, db / 20.0));
}
QVariantMap CellObject::properties() const {
    return {{QStringLiteral("Channel"), QVariant::fromValue(channel())}, {QStringLiteral("Mix"), QVariant::fromValue(mix())},
            {QStringLiteral("Volume"), volume()}, {QStringLiteral("Muted"), muted()}, {QStringLiteral("Follows"), QVariant::fromValue(follows())}};
}
void CellObject::notifyChanged() { emitPropertiesChanged(m_path, interfaceName(), {{QStringLiteral("Volume"), volume()}, {QStringLiteral("Muted"), muted()}, {QStringLiteral("Follows"), QVariant::fromValue(follows())}}); }

// ---- Channel
ChannelObject::ChannelObject(Mixer *mixer, const QString &slug, QObject *parent) : ExportedObject(Service::channelPath(slug), parent), m_mixer(mixer), m_slug(slug) {}
QString ChannelObject::name() const { return m_mixer->channelName(m_slug); }
void ChannelObject::setName(const QString &n) { m_mixer->renameChannel(m_slug, n); }
QString ChannelObject::icon() const { return m_mixer->channelIcon(m_slug); }
void ChannelObject::setIcon(const QString &i) { m_mixer->setChannelIcon(m_slug, i); }
QString ChannelObject::group() const { return m_mixer->channelGroup(m_slug); }
void ChannelObject::setGroup(const QString &g) { if (!m_mixer->setChannelGroup(m_slug, g)) rejectProperty(QStringLiteral("Group"), QStringLiteral("group: up to 40 characters, no '/'")); }
QString ChannelObject::color() const { return m_mixer->channelColor(m_slug); }
void ChannelObject::setColor(const QString &c) { if (!m_mixer->setChannelColor(m_slug, c)) rejectProperty(QStringLiteral("Color"), QStringLiteral("colour must be #rrggbb or empty")); }
double ChannelObject::trim() const { return m_mixer->channelTrim(m_slug); }
double ChannelObject::pan() const { return m_mixer->channelPan(m_slug); }
void ChannelObject::setPan(double v) { if (v < -1 || v > 1) { rejectProperty(QStringLiteral("Pan"), QStringLiteral("must be -1..1")); return; } m_mixer->setChannelPan(m_slug, v); }
void ChannelObject::setTrim(double v) { if (v < 0 || v > 1) { rejectProperty(QStringLiteral("Trim"), QStringLiteral("must be 0..1")); return; } m_mixer->setChannelTrim(m_slug, v); }
bool ChannelObject::muted() const { return m_mixer->channelMuted(m_slug); }
void ChannelObject::setMuted(bool m) { m_mixer->setChannelMuted(m_slug, m); }
void ChannelObject::ToggleMute() { m_mixer->setChannelMuted(m_slug, !m_mixer->channelMuted(m_slug)); }
QString ChannelObject::inputDevice() const { return m_mixer->channelInputDevice(m_slug); }
QStringList ChannelObject::inputs() const { return m_mixer->channelInputs(m_slug); }
bool ChannelObject::SetWireTrim(const QString &ref, double trim, bool muted) { return m_mixer->setChannelWireTrim(m_slug, ref, trim, muted); }
double ChannelObject::WireTrim(const QString &ref) { double t; bool m; return m_mixer->channelWireTrim(m_slug, ref, &t, &m) ? t : -1.0; }
bool ChannelObject::WireMuted(const QString &ref) { double t; bool m; return m_mixer->channelWireTrim(m_slug, ref, &t, &m) && m; }
bool ChannelObject::AddInput(const QString &ref) { return !m_mixer->addChannelInput(m_slug, ref).isEmpty(); }
bool ChannelObject::RemoveInput(const QString &ref) { return m_mixer->removeChannelInput(m_slug, ref); }
void ChannelObject::setInputDevice(const QString &d) {
    if (!m_mixer->setChannelInputDevice(m_slug, d)) rejectProperty(QStringLiteral("InputDevice"), d.isEmpty() ? QStringLiteral("no such channel") : m_mixer->validateDeviceRef(m_mixer->deviceRef(d), true));
}
bool ChannelObject::inputPresent() const { return m_mixer->channelInputPresent(m_slug); }
QString ChannelObject::fxChainJson() const { return QJsonDocument(m_mixer->fxChain(m_slug)).toJson(QJsonDocument::Compact); }
bool ChannelObject::SetFx(const QString &chainJson) {
    const QJsonDocument doc = QJsonDocument::fromJson(chainJson.toUtf8());
    if (!doc.isObject()) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("expected a JSON object")); return false; }
    if (!m_mixer->setFxChain(m_slug, doc.object())) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("refused: see daemon log")); return false; }
    emitPropertiesChanged(m_path, interfaceName(), {{QStringLiteral("FxChain"), fxChainJson()}});
    return true;
}
bool ChannelObject::SetFxControl(const QString &control, double value) {
    if (!m_mixer->setFxControl(m_slug, control, value)) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("no node for control '%1'").arg(control)); return false; }
    return true;
}
QVariantMap ChannelObject::properties() const {
    return {{QStringLiteral("Slug"), m_slug}, {QStringLiteral("Name"), name()}, {QStringLiteral("Icon"), icon()}, {QStringLiteral("Color"), color()}, {QStringLiteral("Group"), group()},
            {QStringLiteral("Trim"), trim()}, {QStringLiteral("Pan"), pan()}, {QStringLiteral("Muted"), muted()}, {QStringLiteral("NodeName"), nodeName()},
            {QStringLiteral("InputDevice"), inputDevice()}, {QStringLiteral("InputPresent"), inputPresent()},
            {QStringLiteral("Inputs"), inputs()}, {QStringLiteral("FxChain"), fxChainJson()}};
}

// ---- Mix
MixObject::MixObject(Mixer *mixer, const QString &slug, QObject *parent) : ExportedObject(Service::mixPath(slug), parent), m_mixer(mixer), m_slug(slug) {}
QString MixObject::name() const { return m_mixer->mixName(m_slug); }
void MixObject::setName(const QString &n) { m_mixer->renameMix(m_slug, n); }
QString MixObject::icon() const { return m_mixer->mixIcon(m_slug); }
void MixObject::setIcon(const QString &i) { m_mixer->setMixIcon(m_slug, i); }
QString MixObject::color() const { return m_mixer->mixColor(m_slug); }
void MixObject::setColor(const QString &c) { if (!m_mixer->setMixColor(m_slug, c)) rejectProperty(QStringLiteral("Color"), QStringLiteral("colour must be #rrggbb or empty")); }
QString MixObject::outputDevice() const { return m_mixer->mixOutputDevice(m_slug); }
void MixObject::setOutputDevice(const QString &d) {
    if (!d.isEmpty()) if (const QString err = m_mixer->validateDeviceRef(m_mixer->deviceRef(d), false); !err.isEmpty()) { rejectProperty(QStringLiteral("OutputDevice"), err); return; }
    m_mixer->setMixOutputDevice(m_slug, d);
}
QString MixObject::captureSource() const { return m_mixer->mixCaptureSource(m_slug); }
bool MixObject::outputPresent() const { return m_mixer->mixOutputPresent(m_slug); }
double MixObject::volume() const { return m_mixer->mixVolume(m_slug); }
void MixObject::setVolume(double v) { if (!(v >= 0.0 && v <= 1.0)) { rejectProperty(QStringLiteral("Volume"), QStringLiteral("must be linear 0..1")); return; } m_mixer->setMixVolume(m_slug, v); }
bool MixObject::muted() const { return m_mixer->mixMuted(m_slug); }
void MixObject::setMuted(bool m) { m_mixer->setMixMuted(m_slug, m); }
QStringList MixObject::outputs() const { QStringList l; for (const auto &d : m_mixer->mixOutputs(m_slug)) l << d.ref(); return l; }   // ADR 0009 refs
QStringList MixObject::outputDescriptions() const { QStringList l; for (const auto &d : m_mixer->mixOutputs(m_slug)) l << (d.description.isEmpty() ? d.node : d.description); return l; }
QString MixObject::fallbackOutput() const { return m_mixer->mixFallbackOutput(m_slug).node; }
QString MixObject::fxChainJson() const { return QJsonDocument(m_mixer->fxChain(m_slug)).toJson(QJsonDocument::Compact); }
bool MixObject::SetFx(const QString &chainJson) {
    const QJsonDocument doc = QJsonDocument::fromJson(chainJson.toUtf8());
    if (!doc.isObject()) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("expected a JSON object")); return false; }
    if (!m_mixer->setFxChain(m_slug, doc.object())) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("refused: see daemon log")); return false; }
    emitPropertiesChanged(m_path, interfaceName(), {{QStringLiteral("FxChain"), fxChainJson()}});
    return true;
}
bool MixObject::SetFxControl(const QString &control, double value) {
    if (!m_mixer->setFxControl(m_slug, control, value)) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("no node for control '%1'").arg(control)); return false; }
    return true;
}
void MixObject::setFallbackOutput(const QString &n) { m_mixer->setMixFallbackOutput(m_slug, m_mixer->deviceRef(n)); }
void MixObject::AddOutput(const QString &n) {
    if (n.isEmpty()) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("empty node name")); return; }
    const DeviceRef ref = m_mixer->deviceRef(n);
    if (const QString err = m_mixer->validateDeviceRef(ref, false); !err.isEmpty()) { sendErrorReply(QDBusError::InvalidArgs, err); return; }
    auto outs = m_mixer->mixOutputs(m_slug);
    for (const auto &d : outs) if (d == ref) return;    // idempotent
    // same node + same ports, different side (ADR 0009 A2) → this REPLACES that entry; two edges on one port would fight
    for (auto &d : outs) if (d.node == ref.node && d.positions == ref.positions) { d.side = ref.side; m_mixer->setMixOutputs(m_slug, outs); return; }
    outs.push_back(ref);
    m_mixer->setMixOutputs(m_slug, outs);
}
bool MixObject::SetWireTrim(const QString &ref, double trim, bool muted) { return m_mixer->setMixWireTrim(m_slug, ref, trim, muted); }
double MixObject::WireTrim(const QString &ref) { double t; bool m; return m_mixer->mixWireTrim(m_slug, ref, &t, &m) ? t : -1.0; }
bool MixObject::WireMuted(const QString &ref) { double t; bool m; return m_mixer->mixWireTrim(m_slug, ref, &t, &m) && m; }
void MixObject::RemoveOutput(const QString &n) {
    auto outs = m_mixer->mixOutputs(m_slug);
    const int before = outs.size();
    const DeviceRef ref = DeviceRef::fromRef(n);
    outs.removeIf([&](const DeviceRef &d) { return d == ref || (ref.positions.isEmpty() && d.node == ref.node); });
    if (outs.size() == before) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("not an output of this mix")); return; }
    m_mixer->setMixOutputs(m_slug, outs);
}
void MixObject::ToggleMute() { m_mixer->setMixMuted(m_slug, !m_mixer->mixMuted(m_slug)); }
QVariantMap MixObject::properties() const {
    return {{QStringLiteral("Slug"), m_slug}, {QStringLiteral("Name"), name()}, {QStringLiteral("Icon"), icon()}, {QStringLiteral("Color"), color()},
            {QStringLiteral("OutputDevice"), outputDevice()}, {QStringLiteral("Outputs"), outputs()}, {QStringLiteral("OutputDescriptions"), outputDescriptions()}, {QStringLiteral("FallbackOutput"), fallbackOutput()},
            {QStringLiteral("CaptureSource"), captureSource()}, {QStringLiteral("NodeName"), nodeName()},
            {QStringLiteral("OutputPresent"), outputPresent()}, {QStringLiteral("Volume"), volume()}, {QStringLiteral("Muted"), muted()},
            {QStringLiteral("FxChain"), fxChainJson()}};
}

// ---- App
AppObject::AppObject(Mixer *mixer, uint32_t id, QObject *parent) : ExportedObject(Service::appPath(id), parent), m_mixer(mixer), m_id(id) {}
QString AppObject::name() const { auto a = m_mixer->app(m_id); return a ? a->name : QString(); }
QString AppObject::binary() const { auto a = m_mixer->app(m_id); return a ? a->binary : QString(); }
QString AppObject::mediaName() const { auto a = m_mixer->app(m_id); return a ? a->mediaName : QString(); }
QString AppObject::mediaRole() const { auto a = m_mixer->app(m_id); return a ? a->mediaRole : QString(); }
QDBusObjectPath AppObject::channel() const { auto a = m_mixer->app(m_id); return QDBusObjectPath(a && !a->channels.isEmpty() ? Service::channelPath(a->channels.first()) : QStringLiteral("/")); }
QString AppObject::icon() const { auto a = m_mixer->app(m_id); return a ? a->iconName : QString(); }
bool AppObject::running() const { auto a = m_mixer->app(m_id); return a ? a->running : false; }
QStringList AppObject::channels() const { auto a = m_mixer->app(m_id); return a ? a->channels : QStringList(); }
void AppObject::MoveTo(const QDBusObjectPath &channel) {
    const QString prefix = Service::channelPath(QString());
    if (!channel.path().startsWith(prefix)) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("not a channel path")); return; }
    if (!m_mixer->moveApp(m_id, channel.path().mid(prefix.size()))) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("unknown app or channel"));
}
// CH-12: several channels at once. Paths (not slugs) so third-party clients stay on object-path semantics.
void AppObject::Assign(const QStringList &paths, bool addOn) {
    QStringList slugs;
    const QString prefix = Service::channelPath(QString());
    for (const QString &p : paths) {
        if (!p.startsWith(prefix)) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("not a channel path")); return; }
        const QString s = p.mid(prefix.size());
        if (!s.isEmpty() && !slugs.contains(s)) slugs << s;
    }
    if (!m_mixer->assignApp(m_id, slugs, addOn)) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("unknown app or channel"));
}
QVariantMap AppObject::properties() const {
    return {{QStringLiteral("Name"), name()}, {QStringLiteral("Binary"), binary()}, {QStringLiteral("MediaName"), mediaName()}, {QStringLiteral("MediaRole"), mediaRole()},
            {QStringLiteral("NodeId"), nodeId()}, {QStringLiteral("Icon"), icon()}, {QStringLiteral("Running"), running()},
            {QStringLiteral("Channels"), channels()}, {QStringLiteral("Channel"), QVariant::fromValue(channel())}};
}
void AppObject::notifyChanged() {
    emitPropertiesChanged(m_path, interfaceName(), {{QStringLiteral("Channel"), QVariant::fromValue(channel())},
                                                    {QStringLiteral("Channels"), channels()},
                                                    {QStringLiteral("Running"), running()},
                                                    {QStringLiteral("Icon"), icon()}});
}

// ---- Levels (ADR 0006)
LevelsAdaptor::LevelsAdaptor(Mixer *mixer, QObject *parent) : QDBusAbstractAdaptor(parent), m_mixer(mixer) {
    m_teardown.setSingleShot(true); m_teardown.setInterval(3000);
    connect(&m_teardown, &QTimer::timeout, this, &LevelsAdaptor::syncTargets);
    connect(m_mixer->meters(), &pw::Meters::peaks, this, [this](const QHash<QString, float> &p) {
        if (m_subscribers.isEmpty()) return;
        QVariantMap out;
        for (auto it = p.cbegin(); it != p.cend(); ++it) out.insert(meterKey(it.key()), static_cast<double>(it.value()));
        Q_EMIT Peaks(out);
    });
    // layout / app set changes while subscribed → meter the new set (UX-13: apps come and go)
    connect(m_mixer, &Mixer::layoutChanged, this, [this] { if (!m_subscribers.isEmpty()) syncTargets(); });
    connect(m_mixer, &Mixer::appAdded,   this, [this](uint32_t) { if (!m_subscribers.isEmpty()) syncTargets(); });
    connect(m_mixer, &Mixer::appRemoved, this, [this](uint32_t) { if (!m_subscribers.isEmpty()) syncTargets(); });
    // subscribers that leave the bus without Unsubscribe()
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"), this, SLOT(onNameOwnerChangedSlot(QString,QString,QString)));
}
uint LevelsAdaptor::rate() const { return pw::Meters::kRateHz; }
void LevelsAdaptor::Subscribe(const QDBusMessage &msg) {
    m_teardown.stop();
    if (m_subscribers.contains(msg.service())) return;
    m_subscribers.insert(msg.service());
    syncTargets();
    emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Levels"), {{QStringLiteral("Subscribers"), subscribers()}});
}
void LevelsAdaptor::Unsubscribe(const QDBusMessage &msg) {
    if (!m_subscribers.remove(msg.service())) return;
    if (m_subscribers.isEmpty()) m_teardown.start(); else syncTargets();
    emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Levels"), {{QStringLiteral("Subscribers"), subscribers()}});
}
void LevelsAdaptor::onNameOwnerChanged(const QString &name, const QString &, const QString &newOwner) {
    if (!newOwner.isEmpty() || !m_subscribers.remove(name)) return;
    if (m_subscribers.isEmpty()) m_teardown.start();
    emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Levels"), {{QStringLiteral("Subscribers"), subscribers()}});
}
// UX-13: one meter per visible entity. Node name → bus key:
//   kmixdeck.channel.<s>      → channel/<s>          kmixdeck.mix.<s>          → mix/<s>
//   kmixdeck.link.<c>.<m>     → cell/<c>/<m>  (post-fader: the cell loopback's playback side)
//   kmixdeck.in.<s>           → in/<s>        (what the hardware input delivers)
//   kmixdeck.out.<m>[.n]      → out/<m>       (what leaves towards the device, post master)
//   <app node, by id>         → app/<id>      (the application's own output — "who is talking")
QString LevelsAdaptor::meterKey(const QString &n) const {
    // CH-7 companions arrive as "rms/<node>" / "clip/<node>" and keep their prefix in front of the public key
    for (const char *pre : {"rms/", "clip/"}) if (n.startsWith(QLatin1String(pre))) return QLatin1String(pre) + meterKey(n.mid(int(strlen(pre))));
    if (n.startsWith(QLatin1String("kmixdeck.channel."))) return QStringLiteral("channel/") + n.mid(17);
    if (n.startsWith(QLatin1String("kmixdeck.mix.")))     return QStringLiteral("mix/") + n.mid(13);
    if (n.startsWith(QLatin1String("kmixdeck.link.")))    { const auto p = n.mid(14).split(QLatin1Char('.')); if (p.size() == 2) return QStringLiteral("cell/%1/%2").arg(p[0], p[1]); }
    if (n.startsWith(QLatin1String("kmixdeck.in.")))      return QStringLiteral("in/") + n.mid(12);
    if (n.startsWith(QLatin1String("kmixdeck.out.")))     return QStringLiteral("out/") + n.mid(13).section(QLatin1Char('.'), 0, 0);
    if (const uint32_t id = m_appNodes.value(n, 0)) return QStringLiteral("app/%1").arg(id);
    return n;
}
void LevelsAdaptor::syncTargets() {
    QStringList t; m_appNodes.clear();
    if (!m_subscribers.isEmpty()) {
        for (const auto &c : m_mixer->channelSlugs()) t << Names::channelNode(c);
        for (const auto &m : m_mixer->mixSlugs()) { t << Names::mixNode(m); t << EdgeNames::outputNode(m, 0); }
        for (const auto &c : m_mixer->channelSlugs()) for (const auto &m : m_mixer->mixSlugs()) t << Names::cellNode(c, m);
        for (const auto &i : m_mixer->inputSlugs()) t << EdgeNames::inputNode(i);
        for (uint32_t id : m_mixer->appIds()) if (const auto a = m_mixer->app(id); a && !a->nodeName.isEmpty()) { t << a->nodeName; m_appNodes.insert(a->nodeName, id); }
    }
    m_mixer->meters()->setTargets(t);
}

// ---- Mixer root
MixerAdaptor::MixerAdaptor(Mixer *mixer, QObject *parent) : QDBusAbstractAdaptor(parent), m_mixer(mixer) {}
QString MixerAdaptor::version() const { return QStringLiteral(KMIXDECK_VERSION_STRING); }
bool MixerAdaptor::connected() const { return m_mixer->connected(); }
QString MixerAdaptor::fxTypes() const { return QJsonDocument(QJsonArray(m_mixer->fxTypes())).toJson(QJsonDocument::Compact); }
QString MixerAdaptor::fxPresets() const { return QJsonDocument(m_mixer->fxPresets()).toJson(QJsonDocument::Compact); }
StringMap MixerAdaptor::outputDevices() const {
    StringMap m;
    for (const auto &d : m_mixer->outputDevices()) {
        m.insert(d.node, d.description);
    }
    return m;
}
QStringList MixerAdaptor::channelOrder() const { return m_mixer->channelSlugs(); }
QStringList MixerAdaptor::mixOrder() const { return m_mixer->mixSlugs(); }
void MixerAdaptor::MoveChannel(const QDBusObjectPath &p, int index) {
    const QString slug = p.path().section(QLatin1Char('/'), -1);
    if (!p.path().startsWith(Service::channelPath(QString())) || !m_mixer->moveChannel(slug, index))
        static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("no such channel"));
}
void MixerAdaptor::MoveMix(const QDBusObjectPath &p, int index) {
    const QString slug = p.path().section(QLatin1Char('/'), -1);
    if (!p.path().startsWith(Service::mixPath(QString())) || !m_mixer->moveMix(slug, index))
        static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("no such mix"));
}
// UX-12: press-and-hold audition. An empty path stops it; any channel/mix path solos that entity.
void MixerAdaptor::Audition(const QDBusObjectPath &p) {
    const QString path = p.path();
    if (path == QLatin1String("/") || path.isEmpty()) { m_mixer->stopAudition(); return; }
    const bool isCh = path.startsWith(Service::channelPath(QString()));
    const bool isMix = !isCh && path.startsWith(Service::mixPath(QString()));
    const QString slug = path.section(QLatin1Char('/'), -1);
    if ((!isCh && !isMix) || slug.isEmpty()) {
        static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("expected a channel or mix path"));
        return;
    }
    m_mixer->startAudition(isCh ? QStringLiteral("channel") : QStringLiteral("mix"), slug);
}
void MixerAdaptor::Undo() {
    if (!m_mixer->undo()) static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.Failed"), QStringLiteral("nothing to undo"));
}
QString MixerAdaptor::FirstRunPlan() { return QString::fromUtf8(QJsonDocument(m_mixer->firstRunPlan()).toJson(QJsonDocument::Compact)); }
QString MixerAdaptor::FirstRunApply() {
    QString why; const QJsonObject done = m_mixer->firstRunApply(&why);
    if (!why.isEmpty()) { static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.Failed"), why); return {}; }
    return QString::fromUtf8(QJsonDocument(done).toJson(QJsonDocument::Compact));
}
QString MixerAdaptor::Export() {
    return QString::fromUtf8(QJsonDocument(m_mixer->exportSettings()).toJson(QJsonDocument::Indented));
}
void MixerAdaptor::Import(const QString &json) {
    QJsonParseError err; const auto doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    auto *root = static_cast<RootObject *>(parent());
    if (err.error != QJsonParseError::NoError || !doc.isObject()) { root->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("not JSON: ") + err.errorString()); return; }
    QString why;
    if (!m_mixer->importSettings(doc.object(), &why)) root->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), why);
}
QDBusObjectPath MixerAdaptor::defaultChannel() const {
    const QString s = m_mixer->defaultChannel();
    return QDBusObjectPath(s.isEmpty() ? QStringLiteral("/") : Service::channelPath(s));
}
void MixerAdaptor::setDefaultChannel(const QDBusObjectPath &p) {
    const QString prefix = Service::channelPath(QString());
    QString slug;
    if (p.path() == QLatin1String("/")) slug.clear();
    else if (p.path().startsWith(prefix)) slug = p.path().mid(prefix.size());
    else slug = QStringLiteral("?");   // not a channel path → setDefaultChannel() rejects it
    if (!m_mixer->setDefaultChannel(slug)) static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("no such channel"));
}
PortMap MixerAdaptor::devicePorts() const {   // ADR 0009 D4
    PortMap m;
    for (const auto &d : m_mixer->outputDevices()) m.insert(d.node, m_mixer->devicePorts(d.node));
    for (const auto &d : m_mixer->inputDevices())  m.insert(d.node, m_mixer->devicePorts(d.node));
    return m;
}
StringMap MixerAdaptor::inputDevices() const {
    StringMap m;
    for (const auto &d : m_mixer->inputDevices()) {
        m.insert(d.node, d.description);
    }
    return m;
}
QDBusObjectPath MixerAdaptor::AddChannel(const QString &name) {
    QString err; const QString slug = m_mixer->addChannel(name, &err);
    if (slug.isEmpty()) { static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), err); return QDBusObjectPath(QStringLiteral("/")); }
    return QDBusObjectPath(Service::channelPath(slug));
}
QDBusObjectPath MixerAdaptor::AddMix(const QString &name) {
    QString err; const QString slug = m_mixer->addMix(name, &err);
    if (slug.isEmpty()) { static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), err); return QDBusObjectPath(QStringLiteral("/")); }
    return QDBusObjectPath(Service::mixPath(slug));
}
QDBusObjectPath MixerAdaptor::DuplicateMix(const QDBusObjectPath &source, const QString &name) {
    const QString from = source.path().section(QLatin1Char('/'), -1);
    if (!source.path().startsWith(Service::mixPath(QString())) || !m_mixer->mixSlugs().contains(from)) { static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("no such mix")); return QDBusObjectPath(QStringLiteral("/")); }
    QString err; const QString slug = m_mixer->duplicateMix(from, name, &err);
    if (slug.isEmpty()) { static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), err); return QDBusObjectPath(QStringLiteral("/")); }
    return QDBusObjectPath(Service::mixPath(slug));
}
void MixerAdaptor::SetDeviceHidden(const QString &node, bool hidden) {
    if (!m_mixer->setDeviceHidden(node, hidden)) static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("not a device node"));
}
void MixerAdaptor::RemoveChannel(const QDBusObjectPath &p) {
    const QString slug = p.path().section(QLatin1Char('/'), -1);
    if (!p.path().startsWith(Service::channelPath(QString())) || !m_mixer->channelSlugs().contains(slug)) { static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("no such channel")); return; }
    m_mixer->removeChannel(slug);
}
void MixerAdaptor::RemoveMix(const QDBusObjectPath &p) {
    const QString slug = p.path().section(QLatin1Char('/'), -1);
    if (!p.path().startsWith(Service::mixPath(QString())) || !m_mixer->mixSlugs().contains(slug)) { static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("no such mix")); return; }
    m_mixer->removeMix(slug);
}
void MixerAdaptor::Save() { if (!m_mixer->saveLayout()) qWarning() << "Save(): could not write layout"; }
// DV-23
QString MixerAdaptor::AddVirtualDevice(const QString &name, int inputs, int outputs) {
    QString err; const QString slug = m_mixer->addVirtualDevice(name, inputs, outputs, &err);
    if (slug.isEmpty()) { static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), err); return {}; }
    return QStringLiteral("kmixdeck.virt.") + slug;
}
void MixerAdaptor::RemoveVirtualDevice(const QString &slug) {
    if (!m_mixer->removeVirtualDevice(slug.startsWith(QLatin1String("kmixdeck.virt.")) ? slug.mid(14) : slug))
        static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("no such virtual device"));
}
QStringList MixerAdaptor::virtualDevices() const { QStringList l; for (const auto &s : m_mixer->virtualDeviceSlugs()) l << QStringLiteral("kmixdeck.virt.") + s; return l; }

// ---- Service
Service::Service(QObject *parent) : QObject(parent) {
    m_mixer.setLayoutPaths(Layout::defaultPath(), Layout::defaultPipewireConfPath());
    if (!m_mixer.loadLayout()) {
        // First run (or unreadable file → DV-6: never overwrite a corrupt file with defaults silently)
        if (!QFile::exists(Layout::defaultPath())) { qInfo() << "no layout yet — writing starter layout to" << Layout::defaultPath(); }
        else qWarning() << "layout.json unreadable; running with the starter layout, NOT overwriting the file";
    }
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ManagedObjects>();
    qDBusRegisterMetaType<StringMap>();
    qDBusRegisterMetaType<PortMap>();
    connect(&m_mixer, &Mixer::layoutChanged, this, &Service::syncObjects);
    connect(&m_mixer, &Mixer::connectedChanged, this, [this] {
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("Connected"), m_mixer.connected()}});
    });
    connect(&m_mixer, &Mixer::cellChanged, this, [this](const QString &ch, const QString &mix) {
        if (auto *o = qobject_cast<CellObject *>(m_objects.value(cellPath(ch, mix)))) o->notifyChanged();
    });
    connect(&m_mixer, &Mixer::channelChanged, this, [this](const QString &slug) {
        if (auto *o = m_objects.value(channelPath(slug))) emitPropertiesChanged(o->path(), o->interfaceName(), o->properties());
    });
    connect(&m_mixer, &Mixer::outputDevicesChanged, this, [this] {
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("OutputDevices"), QVariant::fromValue(m_mixerAdaptor ? m_mixerAdaptor->outputDevices() : StringMap{})},
                                                                                                   {QStringLiteral("DevicePorts"), QVariant::fromValue(m_mixerAdaptor ? m_mixerAdaptor->devicePorts() : PortMap{})}});
    });
    connect(&m_mixer, &Mixer::layoutChanged, this, [this] {   // UX-9: order is part of the layout
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("ChannelOrder"), m_mixer.channelSlugs()}, {QStringLiteral("MixOrder"), m_mixer.mixSlugs()}});
    });
    connect(&m_mixer, &Mixer::undoChanged, this, [this] {
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("UndoDescription"), m_mixer.undoDescription()}});
    });
    connect(&m_mixer, &Mixer::defaultDevicesChanged, this, [this] {   // UX-3
        const QJsonObject p = m_mixer.firstRunPlan();
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("DefaultSink"), p.value(QStringLiteral("defaultSink")).toString()}, {QStringLiteral("DefaultSource"), p.value(QStringLiteral("defaultSource")).toString()}});
    });
    connect(&m_mixer, &Mixer::hiddenDevicesChanged, this, [this] {   // CH-11
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("HiddenDevices"), m_mixer.hiddenDevices()}});
    });
    connect(&m_mixer, &Mixer::listeningDeviceChanged, this, [this] {
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("ListeningDevice"), m_mixer.listeningDevice()}});
    });
    connect(&m_mixer, &Mixer::defaultChannelChanged, this, [this] {
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("DefaultChannel"), QVariant::fromValue(m_mixerAdaptor ? m_mixerAdaptor->defaultChannel() : QDBusObjectPath(QStringLiteral("/")))}});
    });
    connect(&m_mixer, &Mixer::inputDevicesChanged, this, [this] {
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("InputDevices"), QVariant::fromValue(m_mixerAdaptor ? m_mixerAdaptor->inputDevices() : StringMap{})}});
    });
    connect(&m_mixer, &Mixer::appAdded, this, [this](uint32_t id) { if (!m_objects.contains(appPath(id))) exportObject(new AppObject(&m_mixer, id, this)); });
    connect(&m_mixer, &Mixer::appRemoved, this, [this](uint32_t id) { unexportObject(appPath(id)); });
    connect(&m_mixer, &Mixer::appChanged, this, [this](uint32_t id) { if (auto *o = qobject_cast<AppObject *>(m_objects.value(appPath(id)))) o->notifyChanged(); });
    connect(&m_mixer, &Mixer::mixChanged, this, [this](const QString &slug) {
        if (auto *o = m_objects.value(mixPath(slug))) emitPropertiesChanged(o->path(), o->interfaceName(), o->properties());
    });
}

bool Service::start() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { qCritical() << "no session bus"; return false; }
    m_mixerAdaptor = new MixerAdaptor(&m_mixer, &m_root);
    m_levelsAdaptor = new LevelsAdaptor(&m_mixer, &m_root);
    m_om = new ObjectManagerAdaptor(&m_root, [this] { return managedObjects(); });
    if (!bus.registerObject(QLatin1String(kRootPath), &m_root, QDBusConnection::ExportAdaptors)) { qCritical() << "registerObject failed" << bus.lastError().message(); return false; }
    // Export every channel/mix/cell/app object BEFORE claiming the bus name: the name is the "I am ready" signal
    // clients wait for. With the old order a client could see org.kmixdeck1 up and GetManagedObjects() still
    // empty — under full-suite load three tests hit exactly that window after a daemon restart (ctest15, 2026-09-16).
    syncObjects();
    if (!bus.registerService(QLatin1String(kBusName))) { qCritical() << "bus name taken:" << kBusName; return false; }
    return true;
}

void Service::exportObject(ExportedObject *o) {
    QDBusConnection::sessionBus().registerObject(o->path(), o, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
    m_objects.insert(o->path(), o);
    Q_EMIT m_om->InterfacesAdded(QDBusObjectPath(o->path()), InterfaceMap{{o->interfaceName(), o->properties()}});
}
void Service::unexportObject(const QString &path) {
    auto *o = m_objects.take(path); if (!o) return;
    QDBusConnection::sessionBus().unregisterObject(path);
    Q_EMIT m_om->InterfacesRemoved(QDBusObjectPath(path), QStringList{o->interfaceName()});
    o->deleteLater();
}

void Service::syncObjects() {
    QSet<QString> want;
    for (const auto &ch : m_mixer.channelSlugs()) want.insert(channelPath(ch));
    for (const auto &mx : m_mixer.mixSlugs()) want.insert(mixPath(mx));
    for (const auto &ch : m_mixer.channelSlugs()) for (const auto &mx : m_mixer.mixSlugs()) if (m_mixer.cellPresent(ch, mx)) want.insert(cellPath(ch, mx));
    for (const auto &id : m_mixer.appIds()) want.insert(appPath(id));
    for (const auto &p : m_objects.keys()) if (!want.contains(p)) unexportObject(p);
    for (const auto &id : m_mixer.appIds()) if (!m_objects.contains(appPath(id))) exportObject(new AppObject(&m_mixer, id, this));
    for (const auto &ch : m_mixer.channelSlugs()) if (!m_objects.contains(channelPath(ch))) exportObject(new ChannelObject(&m_mixer, ch, this));
    for (const auto &mx : m_mixer.mixSlugs()) if (!m_objects.contains(mixPath(mx))) exportObject(new MixObject(&m_mixer, mx, this));
    for (const auto &ch : m_mixer.channelSlugs()) for (const auto &mx : m_mixer.mixSlugs())
        if (m_mixer.cellPresent(ch, mx) && !m_objects.contains(cellPath(ch, mx))) exportObject(new CellObject(&m_mixer, ch, mx, this));
}

ManagedObjects Service::managedObjects() const {
    ManagedObjects out;
    out.insert(QDBusObjectPath(QLatin1String(kRootPath)), InterfaceMap{{QStringLiteral("org.kmixdeck1.Mixer"),
        {{QStringLiteral("Version"), m_mixerAdaptor->version()}, {QStringLiteral("Connected"), m_mixerAdaptor->connected()},
         {QStringLiteral("OutputDevices"), QVariant::fromValue(m_mixerAdaptor->outputDevices())}, {QStringLiteral("InputDevices"), QVariant::fromValue(m_mixerAdaptor->inputDevices())},
         {QStringLiteral("DevicePorts"), QVariant::fromValue(m_mixerAdaptor->devicePorts())}, {QStringLiteral("VirtualDevices"), m_mixerAdaptor->virtualDevices()}, {QStringLiteral("HiddenDevices"), m_mixerAdaptor->hiddenDevices()}, {QStringLiteral("FirstRun"), m_mixerAdaptor->firstRun()}, {QStringLiteral("DefaultSink"), m_mixerAdaptor->defaultSink()}, {QStringLiteral("DefaultSource"), m_mixerAdaptor->defaultSource()},
         {QStringLiteral("DefaultChannel"), QVariant::fromValue(m_mixerAdaptor->defaultChannel())}, {QStringLiteral("ListeningDevice"), m_mixer.listeningDevice()}, {QStringLiteral("UndoDescription"), m_mixerAdaptor->undoDescription()}, {QStringLiteral("ChannelOrder"), m_mixer.channelSlugs()}, {QStringLiteral("MixOrder"), m_mixer.mixSlugs()},
         {QStringLiteral("FxTypes"), m_mixerAdaptor->fxTypes()}, {QStringLiteral("FxPresets"), m_mixerAdaptor->fxPresets()}}}});
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it)
        out.insert(QDBusObjectPath(it.key()), InterfaceMap{{it.value()->interfaceName(), it.value()->properties()}});
    return out;
}

} // namespace kmixdeck::daemon
