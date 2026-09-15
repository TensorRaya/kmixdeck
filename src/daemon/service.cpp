// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "service.h"
#include "kmixdeck_version.h"
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusError>
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
double CellObject::volume() const { return Mixer::cubicToLinear(m_mixer->cellVolume(m_ch, m_mix)); }
bool CellObject::muted() const { return m_mixer->cellMuted(m_ch, m_mix); }
void CellObject::setVolume(double linear) {
    if (!(linear >= 0.0 && linear <= 1.0)) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Volume must be linear 0..1")); return; }
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
            {QStringLiteral("Volume"), volume()}, {QStringLiteral("Muted"), muted()}};
}
void CellObject::notifyChanged() { emitPropertiesChanged(m_path, interfaceName(), {{QStringLiteral("Volume"), volume()}, {QStringLiteral("Muted"), muted()}}); }

// ---- Channel
ChannelObject::ChannelObject(Mixer *mixer, const QString &slug, QObject *parent) : ExportedObject(Service::channelPath(slug), parent), m_mixer(mixer), m_slug(slug) {}
QString ChannelObject::name() const { return m_mixer->channelName(m_slug); }
void ChannelObject::setName(const QString &n) { m_mixer->renameChannel(m_slug, n); }
void ChannelObject::setIcon(const QString &i) { m_icon = i; emitPropertiesChanged(m_path, interfaceName(), {{QStringLiteral("Icon"), i}}); }
double ChannelObject::trim() const { return m_mixer->channelTrim(m_slug); }
void ChannelObject::setTrim(double v) { if (v < 0 || v > 1) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Trim must be 0..1")); return; } m_mixer->setChannelTrim(m_slug, v); }
bool ChannelObject::muted() const { return m_mixer->channelMuted(m_slug); }
void ChannelObject::setMuted(bool m) { m_mixer->setChannelMuted(m_slug, m); }
void ChannelObject::ToggleMute() { m_mixer->setChannelMuted(m_slug, !m_mixer->channelMuted(m_slug)); }
QString ChannelObject::inputDevice() const { return m_mixer->channelInputDevice(m_slug); }
void ChannelObject::setInputDevice(const QString &d) {
    if (!m_mixer->setChannelInputDevice(m_slug, d)) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("unknown input device (see Mixer.InputDevices)"));
}
bool ChannelObject::inputPresent() const { return m_mixer->channelInputPresent(m_slug); }
QVariantMap ChannelObject::properties() const {
    return {{QStringLiteral("Slug"), m_slug}, {QStringLiteral("Name"), name()}, {QStringLiteral("Icon"), m_icon},
            {QStringLiteral("Trim"), trim()}, {QStringLiteral("Muted"), muted()}, {QStringLiteral("NodeName"), nodeName()},
            {QStringLiteral("InputDevice"), inputDevice()}, {QStringLiteral("InputPresent"), inputPresent()}};
}

// ---- Mix
MixObject::MixObject(Mixer *mixer, const QString &slug, QObject *parent) : ExportedObject(Service::mixPath(slug), parent), m_mixer(mixer), m_slug(slug) {}
QString MixObject::name() const { return m_mixer->mixName(m_slug); }
void MixObject::setName(const QString &n) { m_mixer->renameMix(m_slug, n); }
void MixObject::setIcon(const QString &i) { m_icon = i; emitPropertiesChanged(m_path, interfaceName(), {{QStringLiteral("Icon"), i}}); }
QString MixObject::outputDevice() const { return m_mixer->mixOutputDevice(m_slug); }
void MixObject::setOutputDevice(const QString &d) { m_mixer->setMixOutputDevice(m_slug, d); }
QString MixObject::captureSource() const { return m_mixer->mixCaptureSource(m_slug); }
bool MixObject::outputPresent() const { return m_mixer->mixOutputPresent(m_slug); }
QVariantMap MixObject::properties() const {
    return {{QStringLiteral("Slug"), m_slug}, {QStringLiteral("Name"), name()}, {QStringLiteral("Icon"), m_icon},
            {QStringLiteral("OutputDevice"), outputDevice()}, {QStringLiteral("CaptureSource"), captureSource()}, {QStringLiteral("NodeName"), nodeName()},
            {QStringLiteral("OutputPresent"), outputPresent()}};
}

// ---- App
AppObject::AppObject(Mixer *mixer, uint32_t id, QObject *parent) : ExportedObject(Service::appPath(id), parent), m_mixer(mixer), m_id(id) {}
QString AppObject::name() const { auto a = m_mixer->app(m_id); return a ? a->name : QString(); }
QString AppObject::binary() const { auto a = m_mixer->app(m_id); return a ? a->binary : QString(); }
QString AppObject::mediaName() const { auto a = m_mixer->app(m_id); return a ? a->mediaName : QString(); }
QString AppObject::mediaRole() const { auto a = m_mixer->app(m_id); return a ? a->mediaRole : QString(); }
QDBusObjectPath AppObject::channel() const { auto a = m_mixer->app(m_id); return QDBusObjectPath(a && !a->channelSlug.isEmpty() ? Service::channelPath(a->channelSlug) : QStringLiteral("/")); }
void AppObject::MoveTo(const QDBusObjectPath &channel) {
    const QString prefix = Service::channelPath(QString());
    if (!channel.path().startsWith(prefix)) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("not a channel path")); return; }
    if (!m_mixer->moveApp(m_id, channel.path().mid(prefix.size()))) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("unknown app or channel"));
}
QVariantMap AppObject::properties() const {
    return {{QStringLiteral("Name"), name()}, {QStringLiteral("Binary"), binary()}, {QStringLiteral("MediaName"), mediaName()}, {QStringLiteral("MediaRole"), mediaRole()},
            {QStringLiteral("NodeId"), nodeId()}, {QStringLiteral("Channel"), QVariant::fromValue(channel())}};
}
void AppObject::notifyChanged() { emitPropertiesChanged(m_path, interfaceName(), {{QStringLiteral("Channel"), QVariant::fromValue(channel())}}); }

// ---- Levels (ADR 0006)
LevelsAdaptor::LevelsAdaptor(Mixer *mixer, QObject *parent) : QDBusAbstractAdaptor(parent), m_mixer(mixer) {
    m_teardown.setSingleShot(true); m_teardown.setInterval(3000);
    connect(&m_teardown, &QTimer::timeout, this, &LevelsAdaptor::syncTargets);
    connect(m_mixer->meters(), &pw::Meters::peaks, this, [this](const QHash<QString, float> &p) {
        if (m_subscribers.isEmpty()) return;
        QVariantMap out;
        for (auto it = p.cbegin(); it != p.cend(); ++it) {
            const QString n = it.key();   // kmixdeck.channel.<slug> / kmixdeck.mix.<slug> → channel/<slug> / mix/<slug>
            QString key = n.startsWith(QLatin1String("kmixdeck.channel.")) ? QStringLiteral("channel/") + n.mid(17)
                        : n.startsWith(QLatin1String("kmixdeck.mix."))     ? QStringLiteral("mix/") + n.mid(13) : n;
            out.insert(key, static_cast<double>(it.value()));
        }
        Q_EMIT Peaks(out);
    });
    // layout changes while subscribed → meter the new set
    connect(m_mixer, &Mixer::layoutChanged, this, [this] { if (!m_subscribers.isEmpty()) syncTargets(); });
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
void LevelsAdaptor::syncTargets() {
    QStringList t;
    if (!m_subscribers.isEmpty()) {
        for (const auto &c : m_mixer->channelSlugs()) t << Names::channelNode(c);
        for (const auto &m : m_mixer->mixSlugs()) t << Names::mixNode(m);
    }
    m_mixer->meters()->setTargets(t);
}

// ---- Mixer root
MixerAdaptor::MixerAdaptor(Mixer *mixer, QObject *parent) : QDBusAbstractAdaptor(parent), m_mixer(mixer) {}
QString MixerAdaptor::version() const { return QStringLiteral(KMIXDECK_VERSION_STRING); }
bool MixerAdaptor::connected() const { return m_mixer->connected(); }
StringMap MixerAdaptor::outputDevices() const {
    StringMap m;
    for (const auto &d : m_mixer->outputDevices()) {
        m.insert(d.node, d.description);
    }
    return m;
}
StringMap MixerAdaptor::inputDevices() const {
    StringMap m;
    for (const auto &d : m_mixer->inputDevices()) {
        m.insert(d.node, d.description);
    }
    return m;
}
QDBusObjectPath MixerAdaptor::AddChannel(const QString &name) { m_mixer->addChannel(name); return QDBusObjectPath(Service::channelPath(Names::slugify(name))); }
QDBusObjectPath MixerAdaptor::AddMix(const QString &name) { m_mixer->addMix(name); return QDBusObjectPath(Service::mixPath(Names::slugify(name))); }
void MixerAdaptor::RemoveChannel(const QDBusObjectPath &p) { m_mixer->removeChannel(p.path().section(QLatin1Char('/'), -1)); }
void MixerAdaptor::RemoveMix(const QDBusObjectPath &p) { m_mixer->removeMix(p.path().section(QLatin1Char('/'), -1)); }
void MixerAdaptor::Save() { if (!m_mixer->saveLayout()) qWarning() << "Save(): could not write layout"; }

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
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("OutputDevices"), QVariant::fromValue(m_mixerAdaptor ? m_mixerAdaptor->outputDevices() : StringMap{})}});
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
    if (!bus.registerService(QLatin1String(kBusName))) { qCritical() << "bus name taken:" << kBusName; return false; }
    syncObjects();
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
         {QStringLiteral("OutputDevices"), QVariant::fromValue(m_mixerAdaptor->outputDevices())}, {QStringLiteral("InputDevices"), QVariant::fromValue(m_mixerAdaptor->inputDevices())}}}});
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it)
        out.insert(QDBusObjectPath(it.key()), InterfaceMap{{it.value()->interfaceName(), it.value()->properties()}});
    return out;
}

} // namespace kmixdeck::daemon
