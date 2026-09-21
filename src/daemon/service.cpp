// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "service.h"
#include "../logging.h"
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
QString ChannelObject::duckingJson() const { return QJsonDocument(m_mixer->ducking(m_slug)).toJson(QJsonDocument::Compact); }
void ChannelObject::setDuckingJson(const QString &json) {
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) { rejectProperty(QStringLiteral("Ducking"), QStringLiteral("expected a JSON object {duckedBy, depth, attack, release, threshold}")); return; }
    QString warum;
    // FX-9: pass the reason on, like SetFx does. "rejected" without a reason makes a client guess which of
    // five values was out of range.
    if (!m_mixer->setDucking(m_slug, doc.object(), &warum)) rejectProperty(QStringLiteral("Ducking"), warum);
}
bool ChannelObject::SetDucking(const QString &json) {
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("expected a JSON object {duckedBy, depth, attack, release, threshold}")); return false; }
    QString warum;
    if (!m_mixer->setDucking(m_slug, doc.object(), &warum)) { sendErrorReply(QDBusError::InvalidArgs, warum); return false; }
    return true;
}
double ChannelObject::duckReduction() const { return m_mixer->duckReduction(m_slug); }
QString ChannelObject::fxChainJson() const { return QJsonDocument(m_mixer->fxChain(m_slug)).toJson(QJsonDocument::Compact); }
bool ChannelObject::SetFx(const QString &chainJson) {
    const QJsonDocument doc = QJsonDocument::fromJson(chainJson.toUtf8());
    if (!doc.isObject()) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("expected a JSON object")); return false; }
    QString warum;
    if (!m_mixer->setFxChain(m_slug, doc.object(), &warum)) {
        // FX-8: den Grund weitergeben, nicht auf den Daemon-Log verweisen. Wer die Meldung
        // liest, ist genau der, der das Paket installieren kann — er muss dessen Namen sehen.
        sendErrorReply(QDBusError::InvalidArgs, warum.isEmpty() ? QStringLiteral("refused") : warum);
        return false;
    }
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
            {QStringLiteral("Inputs"), inputs()}, {QStringLiteral("FxChain"), fxChainJson()},
            // FX-9: this map is what GetManagedObjects and InterfacesAdded carry, i.e. everything a client sees
            // without asking property by property. Leaving Ducking out here made the web UI's state show
            // `undefined` while the bus had the value all along (measured 2026-09-21).
            {QStringLiteral("Ducking"), duckingJson()}, {QStringLiteral("DuckReduction"), duckReduction()}};
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
    QString warum;
    if (!m_mixer->setFxChain(m_slug, doc.object(), &warum)) {
        // FX-8: den Grund weitergeben, nicht auf den Daemon-Log verweisen. Wer die Meldung
        // liest, ist genau der, der das Paket installieren kann — er muss dessen Namen sehen.
        sendErrorReply(QDBusError::InvalidArgs, warum.isEmpty() ? QStringLiteral("refused") : warum);
        return false;
    }
    emitPropertiesChanged(m_path, interfaceName(), {{QStringLiteral("FxChain"), fxChainJson()}});
    return true;
}
bool MixObject::SetFxControl(const QString &control, double value) {
    if (!m_mixer->setFxControl(m_slug, control, value)) { sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("no node for control '%1'").arg(control)); return false; }
    return true;
}
void MixObject::setFallbackOutput(const QString &n) { m_mixer->setMixFallbackOutput(m_slug, m_mixer->deviceRef(n)); }
bool MixObject::loudness() const { return m_mixer->mixLoudness(m_slug); }
void MixObject::setLoudness(bool on) { m_mixer->setMixLoudness(m_slug, on); }
double MixObject::loudnessTarget() const { return m_mixer->mixLoudnessTarget(m_slug); }
void MixObject::setLoudnessTarget(double lufs) { m_mixer->setMixLoudnessTarget(m_slug, lufs); }
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
            {QStringLiteral("OutputDevice"), outputDevice()}, {QStringLiteral("Loudness"), loudness()}, {QStringLiteral("LoudnessTarget"), loudnessTarget()}, {QStringLiteral("Outputs"), outputs()}, {QStringLiteral("OutputDescriptions"), outputDescriptions()}, {QStringLiteral("FallbackOutput"), fallbackOutput()},
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
    connect(m_mixer->meters(), &pw::Meters::loudness, this, [this](const QHash<QString, QVector<float>> &lu) {
        // UX-18: node name → mix slug, so the signal speaks the same language as every other Levels key.
        // The signal is a{sad}: a QVariantList inside a QVariantMap marshals as a{sv} with a nested variant
        // (the reader then sees null), so the value type has to be a real QList<double>.
        QMap<QString, QList<double>> out;
        for (auto it = lu.cbegin(); it != lu.cend(); ++it) {
            QString slug = it.key();
            if (slug.startsWith(QLatin1String("kmixdeck.mix."))) slug = slug.mid(13);
            QList<double> vals;
            for (float v : it.value()) vals << double(v);
            out.insert(slug, vals);
        }
        if (out.isEmpty()) return;
        QDBusMessage sig = QDBusMessage::createSignal(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Levels"), QStringLiteral("Loudness"));
        sig << QVariant::fromValue(out);
        QDBusConnection::sessionBus().send(sig);
    });
    connect(m_mixer->meters(), &pw::Meters::peaks, this, [this](const QHash<QString, float> &raw) {
        QHash<QString, float> p = raw;
        // FX-10: gain reduction of a mix's brick-wall limiter. swh's limiter has an "Attenuation (dB)" OUTPUT
        // control port, but PipeWire's filter-chain only exposes INPUT controls through Props (measured
        // 2026-09-19), so the reduction is derived from the two meter points that already exist: the summing
        // bus in front of the chain and the output edge behind it. Published as "gr/<mix>" in dB (>= 0), and
        // only while a chain is actually active — otherwise the difference is meaningless noise.
        for (const auto &slug : m_mixer->mixSlugs()) {
            if (!m_mixer->mixChainActive(slug)) continue;
            const auto bus = raw.constFind(Names::mixNode(slug));
            const auto edge = raw.constFind(EdgeNames::outputNode(slug, 0));
            if (bus == raw.constEnd() || edge == raw.constEnd()) continue;
            p.insert(QStringLiteral("gr-src/") + slug, qMax(0.0f, *bus - *edge));
        }

        if (m_subscribers.isEmpty()) return;
        QVariantMap out;
        for (auto it = p.cbegin(); it != p.cend(); ++it) out.insert(meterKey(it.key()), static_cast<double>(it.value()));
        Q_EMIT Peaks(out);
    });
    // layout / app set changes while subscribed → meter the new set (UX-13: apps come and go)
    // UX-18: the loudness flag must take effect even with nobody subscribed, so this one is unconditional.
    connect(m_mixer, &Mixer::layoutChanged, this, [this] {
        if (!m_subscribers.isEmpty()) syncTargets();
        // UX-18: the loudness flag must take effect without a subscriber, but NOT synchronously from here.
        // layoutChanged() also fires while the graph is reconnecting after a PipeWire restart (CH-4), and
        // creating capture streams in that window took the daemon's bus name down with it — A/B verified
        // 2026-09-19: with the call inline test_ch4_routing_survives_pipewire_restart fails, queued it passes.
        else QTimer::singleShot(0, this, [this] {
            if (!m_subscribers.isEmpty()) return;
            m_mixer->meters()->setLoudnessTargets(m_mixer->loudnessMixNodes());
            // FX-9: ducking needs its trigger metered with nobody subscribed too. Queued for the same reason
            // as the loudness call above (CH-4: creating streams inline during a reconnect killed the bus name).
            syncTargets();
        });
    });
    // FX-9: a changed ducking assignment changes which channels must be metered, subscribers or not.
    connect(m_mixer, &Mixer::channelChanged, this, [this](const QString &) {
        QTimer::singleShot(0, this, [this] { syncTargets(); });
    });
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
    // FX-10: already a public key, computed above rather than read from a node
    if (n.startsWith(QLatin1String("gr-src/"))) return QStringLiteral("gr/") + n.mid(7);
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
    // FX-9: a trigger channel is metered whether or not a UI is watching. Ducking is steered off these peaks
    // in the daemon (Mixer::tickDucking), so tying them to Levels subscribers would mean ducking only works
    // while some meter happens to be open — measured that way round first (2026-09-21).
    for (const auto &c : m_mixer->channelSlugs()) {
        const QString trigger = m_mixer->ducking(c).value(QStringLiteral("duckedBy")).toString();
        if (!trigger.isEmpty()) t << Names::channelNode(trigger);
    }
    if (!m_subscribers.isEmpty()) {
        for (const auto &c : m_mixer->channelSlugs()) t << Names::channelNode(c);
        // FX-9: a ducked channel gets its ducker's tail metered too. The gain reduction cannot be read from
        // the plugin — measured 2026-09-21: filter-chain publishes only INPUT controls through Props, so SC3's
        // "Gain reduction (dB)" output port is invisible there. Comparing the two peaks IS the reduction, and
        // it is what the user hears rather than what the plugin claims.
        for (const auto &c : m_mixer->channelSlugs())
            if (!m_mixer->ducking(c).value(QStringLiteral("duckedBy")).toString().isEmpty())
                t << fx::duckerNode(c) + QStringLiteral(".out");
        for (const auto &m : m_mixer->mixSlugs()) { t << Names::mixNode(m); t << EdgeNames::outputNode(m, 0); }
        for (const auto &c : m_mixer->channelSlugs()) for (const auto &m : m_mixer->mixSlugs()) t << Names::cellNode(c, m);
        for (const auto &i : m_mixer->inputSlugs()) t << EdgeNames::inputNode(i);
        for (uint32_t id : m_mixer->appIds()) if (const auto a = m_mixer->app(id); a && !a->nodeName.isEmpty()) { t << a->nodeName; m_appNodes.insert(a->nodeName, id); }
    }
    t.removeDuplicates();
    m_mixer->meters()->setTargets(t);
    // UX-18: the R128 analysers follow the per-mix flag ALONE, not the subscriber count. A 3 s short-term window
    // and a gated integration need seconds of continuous audio, so an analyser that is torn down between readings
    // can never produce a valid number — measured 2026-09-19: a verified -20 LUFS tone read -32.7 / -41.5 / -70
    // when the reader's own Subscribe() created the analyser, and -20.0 / -20.1 / -20.1 once it had been running.
    // That is the trade the opt-in buys: the user enables the meter per mix and pays for it continuously.
    m_mixer->meters()->setLoudnessTargets(m_mixer->loudnessMixNodes());
}

namespace {
// UX-18: a{sad} for the Loudness signal. Qt marshals QMap<QString, QList<double>> only after the type is
// registered — without this the send fails with "type is not registered with D-Bus" on every tick.
struct LoudnessTypeRegistration {
    LoudnessTypeRegistration() { qDBusRegisterMetaType<QMap<QString, QList<double>>>(); }
};
const LoudnessTypeRegistration s_loudnessTypeRegistration;
} // namespace

// ---- Mixer root
MixerAdaptor::MixerAdaptor(Mixer *mixer, QObject *parent) : QDBusAbstractAdaptor(parent), m_mixer(mixer) {}
QString MixerAdaptor::version() const { return QStringLiteral(KMIXDECK_VERSION_STRING); }
bool MixerAdaptor::connected() const { return m_mixer->connected(); }
QString MixerAdaptor::lastError() const { return m_mixer->lastError(); }
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
QStringList MixerAdaptor::scenes() const { return m_mixer->scenes(); }
// CT-9: these three return void on the bus (see interfaces/org.kmixdeck1.Mixer.xml) — so a failure has to
// travel through the D-Bus ERROR channel, not a return value. Dropping the bool on the floor made every
// call look like a success: `kmixdeck scene recall does-not-exist` exited 0 and printed nothing, and a
// frontend had no way to tell a recalled scene from a typo. Found 2026-09-19 by the first CT-9 test.
// replyError (NOT sendErrorReply — MixerAdaptor is no QDBusContext, that is the CellObject/ChannelObject
// pattern) keeps the signature and the shipped XML exactly as they are: this is not a contract change.
void MixerAdaptor::SaveScene(const QString &name) {
    if (!m_mixer->saveScene(name))
        static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.Failed"), QStringLiteral("cannot save scene '%1': see the daemon log").arg(name));
}
void MixerAdaptor::RecallScene(const QString &name, bool exclusive) {
    if (!m_mixer->recallScene(name, exclusive))
        static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("no such scene '%1'").arg(name));
}
void MixerAdaptor::DeleteScene(const QString &name) {
    if (!m_mixer->deleteScene(name))
        static_cast<RootObject *>(parent())->replyError(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"), QStringLiteral("no such scene '%1'").arg(name));
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
void MixerAdaptor::Save() { if (!m_mixer->saveLayout()) qCWarning(lcDbus) << "Save(): could not write layout"; }
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
        if (!QFile::exists(Layout::defaultPath())) { qCInfo(lcDbus) << "no layout yet — writing starter layout to" << Layout::defaultPath(); }
        else qCWarning(lcDbus) << "layout.json unreadable; running with the starter layout, NOT overwriting the file";
    }
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ManagedObjects>();
    qDBusRegisterMetaType<StringMap>();
    qDBusRegisterMetaType<PortMap>();
    connect(&m_mixer, &Mixer::layoutChanged, this, &Service::syncObjects);
    connect(&m_mixer, &Mixer::connectedChanged, this, [this] {
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("Connected"), m_mixer.connected()}});
    });
    connect(&m_mixer, &Mixer::lastErrorChanged, this, [this] {
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("LastError"), m_mixer.lastError()}});
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
    // CT-9: the scene list is a property, a recall is a signal — frontends refresh their picker on the first
    // and move their faders on the second (the fader values arrive through the usual per-cell notifications).
    connect(&m_mixer, &Mixer::scenesChanged, this, [this] {
        emitPropertiesChanged(QLatin1String(kRootPath), QStringLiteral("org.kmixdeck1.Mixer"), {{QStringLiteral("Scenes"), m_mixer.scenes()}});
    });
    connect(&m_mixer, &Mixer::sceneRecalled, this, [this](const QString &name) {
        if (m_mixerAdaptor) Q_EMIT m_mixerAdaptor->SceneRecalled(name);
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
    if (!bus.isConnected()) { qCCritical(lcDbus) << "no session bus"; return false; }
    m_mixerAdaptor = new MixerAdaptor(&m_mixer, &m_root);
    m_levelsAdaptor = new LevelsAdaptor(&m_mixer, &m_root);
    m_om = new ObjectManagerAdaptor(&m_root, [this] { return managedObjects(); });
    if (!bus.registerObject(QLatin1String(kRootPath), &m_root, QDBusConnection::ExportAdaptors)) { qCCritical(lcDbus) << "registerObject failed" << bus.lastError().message(); return false; }
    // Export every channel/mix/cell/app object BEFORE claiming the bus name: the name is the "I am ready" signal
    // clients wait for. With the old order a client could see org.kmixdeck1 up and GetManagedObjects() still
    // empty — under full-suite load three tests hit exactly that window after a daemon restart (ctest15).
    syncObjects();
    if (!bus.registerService(QLatin1String(kBusName))) { qCCritical(lcDbus) << "bus name taken:" << kBusName; return false; }
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
        {{QStringLiteral("Version"), m_mixerAdaptor->version()}, {QStringLiteral("Connected"), m_mixerAdaptor->connected()}, {QStringLiteral("LastError"), m_mixerAdaptor->lastError()},
         {QStringLiteral("OutputDevices"), QVariant::fromValue(m_mixerAdaptor->outputDevices())}, {QStringLiteral("InputDevices"), QVariant::fromValue(m_mixerAdaptor->inputDevices())},
         {QStringLiteral("DevicePorts"), QVariant::fromValue(m_mixerAdaptor->devicePorts())}, {QStringLiteral("VirtualDevices"), m_mixerAdaptor->virtualDevices()}, {QStringLiteral("HiddenDevices"), m_mixerAdaptor->hiddenDevices()}, {QStringLiteral("FirstRun"), m_mixerAdaptor->firstRun()}, {QStringLiteral("DefaultSink"), m_mixerAdaptor->defaultSink()}, {QStringLiteral("DefaultSource"), m_mixerAdaptor->defaultSource()},
         {QStringLiteral("DefaultChannel"), QVariant::fromValue(m_mixerAdaptor->defaultChannel())}, {QStringLiteral("ListeningDevice"), m_mixer.listeningDevice()}, {QStringLiteral("UndoDescription"), m_mixerAdaptor->undoDescription()}, {QStringLiteral("Scenes"), m_mixerAdaptor->scenes()}, {QStringLiteral("ChannelOrder"), m_mixer.channelSlugs()}, {QStringLiteral("MixOrder"), m_mixer.mixSlugs()},
         {QStringLiteral("FxTypes"), m_mixerAdaptor->fxTypes()}, {QStringLiteral("FxPresets"), m_mixerAdaptor->fxPresets()}}}});
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it)
        out.insert(QDBusObjectPath(it.key()), InterfaceMap{{it.value()->interfaceName(), it.value()->properties()}});
    return out;
}

} // namespace kmixdeck::daemon
