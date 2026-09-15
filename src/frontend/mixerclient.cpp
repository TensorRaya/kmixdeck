// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "mixerclient.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDBusServiceWatcher>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <algorithm>

namespace kmixdeck::frontend {

static constexpr const char *BUS = "org.kmixdeck1";
static constexpr const char *ROOT = "/org/kmixdeck1";

static QVariant unwrap(const QVariant &v) {
    if (v.userType() == qMetaTypeId<QDBusVariant>()) return unwrap(v.value<QDBusVariant>().variant());
    if (v.userType() == qMetaTypeId<QDBusObjectPath>()) return v.value<QDBusObjectPath>().path();
    return v;
}
static QVariantMap plain(const QVariantMap &m) { QVariantMap r; for (auto it = m.cbegin(); it != m.cend(); ++it) r[it.key()] = unwrap(it.value()); return r; }

MixerClient::MixerClient(QObject *parent) : QObject(parent) {
    qDBusRegisterMetaType<StringMap>(); qDBusRegisterMetaType<InterfaceMap>(); qDBusRegisterMetaType<ManagedObjects>();
    auto bus = QDBusConnection::sessionBus();
    bus.connect(BUS, QString(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this, SLOT(onPropertiesChanged(QDBusMessage)));
    bus.connect(BUS, ROOT, QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("InterfacesAdded"), this, SLOT(onInterfacesAdded(QDBusObjectPath,InterfaceMap)));
    bus.connect(BUS, ROOT, QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("InterfacesRemoved"), this, SLOT(onInterfacesRemoved(QDBusObjectPath,QStringList)));
    auto *watcher = new QDBusServiceWatcher(QLatin1String(BUS), bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &MixerClient::onNameOwnerChanged);
    refresh();   // triggers D-Bus activation of kmixdeckd if it is not running (AR-5)
}

void MixerClient::refresh() {
    QDBusInterface om(BUS, ROOT, QStringLiteral("org.freedesktop.DBus.ObjectManager"), QDBusConnection::sessionBus());
    QDBusReply<ManagedObjects> r = om.call(QStringLiteral("GetManagedObjects"));
    const bool was = m_available;
    m_channels.clear(); m_mixes.clear(); m_cells.clear(); m_apps.clear(); m_channelOrder.clear(); m_mixOrder.clear();
    m_outputDevices.clear(); m_inputDevices.clear();
    if (!r.isValid()) {
        m_available = false; qWarning() << "kmixdeckd not reachable:" << r.error().message();
    } else {
        m_available = true; bool layout = false;
        for (auto it = r.value().cbegin(); it != r.value().cend(); ++it)
            for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt) absorb(it.key().path(), jt.key(), jt.value(), &layout);
        Q_EMIT layoutChanged();
    }
    if (was != m_available) Q_EMIT serviceAvailableChanged();
    Q_EMIT connectedChanged();
    Q_EMIT appsChanged();
}

void MixerClient::absorb(const QString &path, const QString &iface, const QVariantMap &rawProps, bool *layout) {
    const QVariantMap props = plain(rawProps);
    const QString rel = path.mid(QString(ROOT).size() + 1);   // "channel/game", "cell/game/stream"
    const QStringList parts = rel.split(QLatin1Char('/'));
    if (iface == QLatin1String("org.kmixdeck1.Mixer")) {
        if (props.contains(QStringLiteral("Connected"))) { m_pwConnected = props.value(QStringLiteral("Connected")).toBool(); Q_EMIT connectedChanged(); }
        if (rawProps.contains(QStringLiteral("OutputDevices"))) {
            QVariant v = rawProps.value(QStringLiteral("OutputDevices"));
            if (v.userType() == qMetaTypeId<QDBusVariant>()) v = v.value<QDBusVariant>().variant();
            const StringMap d = qdbus_cast<StringMap>(v);
            for (auto it = d.cbegin(); it != d.cend(); ++it) m_outputDevices.insert(it.key(), it.value());
            Q_EMIT outputDevicesChanged();
        }
        if (rawProps.contains(QStringLiteral("InputDevices"))) {
            QVariant v = rawProps.value(QStringLiteral("InputDevices"));
            if (v.userType() == qMetaTypeId<QDBusVariant>()) v = v.value<QDBusVariant>().variant();
            const StringMap d = qdbus_cast<StringMap>(v);
            for (auto it = d.cbegin(); it != d.cend(); ++it) m_inputDevices.insert(it.key(), it.value());
            Q_EMIT inputDevicesChanged();
        }
    } else if (iface == QLatin1String("org.kmixdeck1.Channel") && parts.size() == 2) {
        auto &m = m_channels[parts[1]]; for (auto it = props.cbegin(); it != props.cend(); ++it) m[it.key()] = it.value();
        if (!m_channelOrder.contains(parts[1])) { m_channelOrder << parts[1]; *layout = true; }
        Q_EMIT channelChanged(parts[1]);
    } else if (iface == QLatin1String("org.kmixdeck1.Mix") && parts.size() == 2) {
        auto &m = m_mixes[parts[1]]; for (auto it = props.cbegin(); it != props.cend(); ++it) m[it.key()] = it.value();
        if (!m_mixOrder.contains(parts[1])) { m_mixOrder << parts[1]; *layout = true; }
        Q_EMIT mixChanged(parts[1]);
    } else if (iface == QLatin1String("org.kmixdeck1.App") && parts.size() == 2) {
        auto &m = m_apps[path]; for (auto it = props.cbegin(); it != props.cend(); ++it) m[it.key()] = it.value();
        Q_EMIT appsChanged();
    } else if (iface == QLatin1String("org.kmixdeck1.Cell") && parts.size() == 3) {
        const bool isNew = !m_cells.contains(cellKey(parts[1], parts[2]));
        auto &m = m_cells[cellKey(parts[1], parts[2])]; for (auto it = props.cbegin(); it != props.cend(); ++it) m[it.key()] = it.value();
        if (isNew) *layout = true;
        Q_EMIT cellChanged(parts[1], parts[2]);
    }
}

void MixerClient::onPropertiesChanged(const QDBusMessage &msg) {
    const auto args = msg.arguments(); bool layout = false;
    absorb(msg.path(), args.value(0).toString(), qdbus_cast<QVariantMap>(args.value(1)), &layout);
    if (layout) Q_EMIT layoutChanged();
}
void MixerClient::onInterfacesAdded(const QDBusObjectPath &path, const InterfaceMap &ifaces) {
    bool layout = false;
    for (auto it = ifaces.cbegin(); it != ifaces.cend(); ++it) absorb(path.path(), it.key(), it.value(), &layout);
    if (layout) Q_EMIT layoutChanged();
}
void MixerClient::onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &) {
    const QStringList parts = path.path().mid(QString(ROOT).size() + 1).split(QLatin1Char('/'));
    if (parts.size() == 2 && parts[0] == QLatin1String("channel")) { m_channels.remove(parts[1]); m_channelOrder.removeAll(parts[1]); }
    else if (parts.size() == 2 && parts[0] == QLatin1String("mix")) { m_mixes.remove(parts[1]); m_mixOrder.removeAll(parts[1]); }
    else if (parts.size() == 2 && parts[0] == QLatin1String("app")) { m_apps.remove(path.path()); Q_EMIT appsChanged(); return; }
    else if (parts.size() == 3) m_cells.remove(cellKey(parts[1], parts[2]));
    Q_EMIT layoutChanged();
}
void MixerClient::onNameOwnerChanged(const QString &name, const QString &, const QString &newOwner) {
    if (name != QLatin1String(BUS)) return;
    if (newOwner.isEmpty()) { m_available = false; Q_EMIT serviceAvailableChanged(); }
    else {
        refresh();
        if (m_metersEnabled) QDBusInterface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Levels"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("Subscribe"));
    }
}

void MixerClient::setProperty(const QString &path, const QString &iface, const QString &name, const QVariant &v) {
    QDBusInterface props(BUS, path, QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::sessionBus());
    props.asyncCall(QStringLiteral("Set"), iface, name, QVariant::fromValue(QDBusVariant(v)));
}
void MixerClient::setCellVolume(const QString &ch, const QString &mix, double cubic) {
    const double lin = std::clamp(cubic, 0.0, 1.0);
    m_cells[cellKey(ch, mix)][QStringLiteral("Volume")] = lin * lin * lin;
    setProperty(QStringLiteral("%1/cell/%2/%3").arg(ROOT, ch, mix), QStringLiteral("org.kmixdeck1.Cell"), QStringLiteral("Volume"), lin * lin * lin);
}
void MixerClient::setCellMuted(const QString &ch, const QString &mix, bool muted) {
    m_cells[cellKey(ch, mix)][QStringLiteral("Muted")] = muted;
    setProperty(QStringLiteral("%1/cell/%2/%3").arg(ROOT, ch, mix), QStringLiteral("org.kmixdeck1.Cell"), QStringLiteral("Muted"), muted);
}
void MixerClient::toggleCellMute(const QString &ch, const QString &mix) {
    QDBusInterface(BUS, QStringLiteral("%1/cell/%2/%3").arg(ROOT, ch, mix), QStringLiteral("org.kmixdeck1.Cell"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("ToggleMute"));
}
void MixerClient::toggleChannelMute(const QString &slug) {
    QDBusInterface(BUS, QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("ToggleMute"));
}
// Creation can be refused (duplicate, unusable name). The daemon answers with a D-Bus error; surface it
// instead of leaving the user staring at a dialog that closed and a matrix that did not change.
void MixerClient::callReportingErrors(const QString &method, const QVariant &arg) {
    auto *w = new QDBusPendingCallWatcher(QDBusInterface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus()).asyncCall(method, arg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<> r = *w;
        if (r.isError()) { qWarning() << "kmixdeck: request refused:" << r.error().message(); Q_EMIT errorOccurred(r.error().message()); }
        w->deleteLater();
    });
}
void MixerClient::addChannel(const QString &name) { callReportingErrors(QStringLiteral("AddChannel"), name); }
void MixerClient::addMix(const QString &name)     { callReportingErrors(QStringLiteral("AddMix"), name); }
void MixerClient::removeChannel(const QString &slug) { QDBusInterface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("RemoveChannel"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("%1/channel/%2").arg(ROOT, slug)))); }
void MixerClient::removeMix(const QString &slug)     { QDBusInterface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("RemoveMix"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("%1/mix/%2").arg(ROOT, slug)))); }

QVariantList MixerClient::apps() const {
    QVariantList out;
    for (auto it = m_apps.cbegin(); it != m_apps.cend(); ++it) {
        const auto &a = it.value();
        const QString chPath = a.value(QStringLiteral("Channel")).toString();
        out.push_back(QVariantMap{{QStringLiteral("path"), it.key()}, {QStringLiteral("name"), a.value(QStringLiteral("Name"))},
                                  {QStringLiteral("binary"), a.value(QStringLiteral("Binary"))},
                                  {QStringLiteral("mediaName"), a.value(QStringLiteral("MediaName"))},
                                  {QStringLiteral("channel"), chPath.startsWith(QStringLiteral("%1/channel/").arg(ROOT)) ? chPath.section(QLatin1Char('/'), -1) : QString()}});
    }
    return out;
}
QVariantList MixerClient::outputDevices() const {
    QVariantList out;
    for (auto it = m_outputDevices.cbegin(); it != m_outputDevices.cend(); ++it)
        out.push_back(QVariantMap{{QStringLiteral("nodeName"), it.key()}, {QStringLiteral("description"), it.value()}});
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) { return a.toMap().value(QStringLiteral("description")).toString().localeAwareCompare(b.toMap().value(QStringLiteral("description")).toString()) < 0; });
    return out;
}
QVariantList MixerClient::inputDevices() const {
    QVariantList out;
    for (auto it = m_inputDevices.cbegin(); it != m_inputDevices.cend(); ++it)
        out.push_back(QVariantMap{{QStringLiteral("nodeName"), it.key()}, {QStringLiteral("description"), it.value()}});
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) { return a.toMap().value(QStringLiteral("description")).toString().localeAwareCompare(b.toMap().value(QStringLiteral("description")).toString()) < 0; });
    return out;
}
void MixerClient::setMetersEnabled(bool on) {
    if (on == m_metersEnabled) return;
    m_metersEnabled = on; Q_EMIT metersEnabledChanged();
    auto bus = QDBusConnection::sessionBus();
    QDBusInterface lv(BUS, ROOT, QStringLiteral("org.kmixdeck1.Levels"), bus);
    if (on) {
        bus.connect(BUS, ROOT, QStringLiteral("org.kmixdeck1.Levels"), QStringLiteral("Peaks"), this, SLOT(onPeaks(QVariantMap)));
        lv.asyncCall(QStringLiteral("Subscribe"));
    } else {
        bus.disconnect(BUS, ROOT, QStringLiteral("org.kmixdeck1.Levels"), QStringLiteral("Peaks"), this, SLOT(onPeaks(QVariantMap)));
        lv.asyncCall(QStringLiteral("Unsubscribe"));
        m_peaks.clear(); Q_EMIT peaksChanged();
    }
}
void MixerClient::onPeaks(const QVariantMap &peaks) {
    for (auto it = peaks.cbegin(); it != peaks.cend(); ++it) m_peaks[it.key()] = it.value().toDouble();
    Q_EMIT peaksChanged();
}
void MixerClient::setMixOutputDevice(const QString &slug, const QString &nodeName) {
    m_mixes[slug][QStringLiteral("OutputDevice")] = nodeName;
    setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("OutputDevice"), nodeName);
}
void MixerClient::setMixVolume(const QString &slug, double cubic) {
    const double lin = std::pow(std::clamp(cubic, 0.0, 1.0), 3.0);
    m_mixes[slug][QStringLiteral("Volume")] = lin;
    setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("Volume"), lin);
}
void MixerClient::toggleMixMute(const QString &slug) {
    QDBusInterface(BUS, QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("ToggleMute"));
}
void MixerClient::renameChannel(const QString &slug, const QString &name) { setProperty(QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("Name"), name); }
void MixerClient::renameMix(const QString &slug, const QString &name) { setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("Name"), name); }
void MixerClient::moveApp(const QString &appPath, const QString &channelSlug) {
    QDBusInterface(BUS, appPath, QStringLiteral("org.kmixdeck1.App"), QDBusConnection::sessionBus())
        .asyncCall(QStringLiteral("MoveTo"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("%1/channel/%2").arg(ROOT, channelSlug))));
}
void MixerClient::setChannelDevice(const QString &slug, const QString &deviceNode) {
    m_channels[slug][QStringLiteral("InputDevice")] = deviceNode;
    setProperty(QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("InputDevice"), deviceNode);
}

} // namespace kmixdeck::frontend
