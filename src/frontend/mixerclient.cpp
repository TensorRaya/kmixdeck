// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "mixerclient.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDBusConnectionInterface>
#include <QDebug>

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
    qDBusRegisterMetaType<InterfaceMap>(); qDBusRegisterMetaType<ManagedObjects>();
    auto bus = QDBusConnection::sessionBus();
    bus.connect(BUS, QString(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this, SLOT(onPropertiesChanged(QDBusMessage)));
    bus.connect(BUS, ROOT, QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("InterfacesAdded"), this, SLOT(onInterfacesAdded(QDBusObjectPath,InterfaceMap)));
    bus.connect(BUS, ROOT, QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("InterfacesRemoved"), this, SLOT(onInterfacesRemoved(QDBusObjectPath,QStringList)));
    connect(bus.interface(), &QDBusConnectionInterface::serviceOwnerChanged, this, &MixerClient::onNameOwnerChanged);
    refresh();   // triggers D-Bus activation of kmixdeckd if it is not running (AR-5)
}

void MixerClient::refresh() {
    QDBusInterface om(BUS, ROOT, QStringLiteral("org.freedesktop.DBus.ObjectManager"), QDBusConnection::sessionBus());
    QDBusReply<ManagedObjects> r = om.call(QStringLiteral("GetManagedObjects"));
    const bool was = m_available;
    m_channels.clear(); m_mixes.clear(); m_cells.clear(); m_channelOrder.clear(); m_mixOrder.clear();
    if (!r.isValid()) {
        m_available = false; qWarning() << "kmixdeckd not reachable:" << r.error().message();
    } else {
        m_available = true; bool layout = false;
        for (auto it = r.value().cbegin(); it != r.value().cend(); ++it)
            for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt) absorb(it.key().path(), jt.key(), jt.value(), &layout);
    }
    if (was != m_available) Q_EMIT serviceAvailableChanged();
    Q_EMIT connectedChanged(); Q_EMIT layoutChanged();
}

void MixerClient::absorb(const QString &path, const QString &iface, const QVariantMap &rawProps, bool *layout) {
    const QVariantMap props = plain(rawProps);
    const QString rel = path.mid(QString(ROOT).size() + 1);   // "channel/game", "cell/game/stream"
    const QStringList parts = rel.split(QLatin1Char('/'));
    if (iface == QLatin1String("org.kmixdeck1.Mixer")) {
        if (props.contains(QStringLiteral("Connected"))) { m_pwConnected = props.value(QStringLiteral("Connected")).toBool(); Q_EMIT connectedChanged(); }
    } else if (iface == QLatin1String("org.kmixdeck1.Channel") && parts.size() == 2) {
        auto &m = m_channels[parts[1]]; for (auto it = props.cbegin(); it != props.cend(); ++it) m[it.key()] = it.value();
        if (!m_channelOrder.contains(parts[1])) { m_channelOrder << parts[1]; *layout = true; } else Q_EMIT layoutChanged();  // name change
    } else if (iface == QLatin1String("org.kmixdeck1.Mix") && parts.size() == 2) {
        auto &m = m_mixes[parts[1]]; for (auto it = props.cbegin(); it != props.cend(); ++it) m[it.key()] = it.value();
        if (!m_mixOrder.contains(parts[1])) { m_mixOrder << parts[1]; *layout = true; } else Q_EMIT layoutChanged();
    } else if (iface == QLatin1String("org.kmixdeck1.Cell") && parts.size() == 3) {
        const bool isNew = !m_cells.contains(cellKey(parts[1], parts[2]));
        auto &m = m_cells[cellKey(parts[1], parts[2])]; for (auto it = props.cbegin(); it != props.cend(); ++it) m[it.key()] = it.value();
        if (isNew) *layout = true; Q_EMIT cellChanged(parts[1], parts[2]);
    }
}

void MixerClient::onPropertiesChanged(const QDBusMessage &msg) {
    const auto args = msg.arguments(); bool layout = false;
    absorb(msg.path(), args.value(0).toString(), qdbus_cast<QVariantMap>(args.value(1)), &layout);
    if (layout) Q_EMIT layoutChanged();
}
void MixerClient::onInterfacesAdded(const QDBusObjectPath &path, const InterfaceMap &ifaces) {
    bool layout = false; for (auto it = ifaces.cbegin(); it != ifaces.cend(); ++it) absorb(path.path(), it.key(), it.value(), &layout);
    if (layout) Q_EMIT layoutChanged();
}
void MixerClient::onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &) {
    const QStringList parts = path.path().mid(QString(ROOT).size() + 1).split(QLatin1Char('/'));
    if (parts.size() == 2 && parts[0] == QLatin1String("channel")) { m_channels.remove(parts[1]); m_channelOrder.removeAll(parts[1]); }
    else if (parts.size() == 2 && parts[0] == QLatin1String("mix")) { m_mixes.remove(parts[1]); m_mixOrder.removeAll(parts[1]); }
    else if (parts.size() == 3) m_cells.remove(cellKey(parts[1], parts[2]));
    Q_EMIT layoutChanged();
}
void MixerClient::onNameOwnerChanged(const QString &name, const QString &, const QString &newOwner) {
    if (name != QLatin1String(BUS)) return;
    if (newOwner.isEmpty()) { m_available = false; Q_EMIT serviceAvailableChanged(); } else refresh();
}

void MixerClient::setProperty(const QString &path, const QString &iface, const QString &name, const QVariant &v) {
    QDBusInterface props(BUS, path, QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::sessionBus());
    props.asyncCall(QStringLiteral("Set"), iface, name, QVariant::fromValue(QDBusVariant(v)));
}
void MixerClient::setCellVolume(const QString &ch, const QString &mix, double cubic) {
    const double lin = std::clamp(cubic, 0.0, 1.0); m_cells[cellKey(ch, mix)][QStringLiteral("Volume")] = lin * lin * lin;  // optimistic
    setProperty(QStringLiteral("%1/cell/%2/%3").arg(ROOT, ch, mix), QStringLiteral("org.kmixdeck1.Cell"), QStringLiteral("Volume"), lin * lin * lin);
}
void MixerClient::setCellMuted(const QString &ch, const QString &mix, bool muted) {
    m_cells[cellKey(ch, mix)][QStringLiteral("Muted")] = muted;
    setProperty(QStringLiteral("%1/cell/%2/%3").arg(ROOT, ch, mix), QStringLiteral("org.kmixdeck1.Cell"), QStringLiteral("Muted"), muted);
}
void MixerClient::addChannel(const QString &name) { QDBusInterface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("AddChannel"), name); }
void MixerClient::addMix(const QString &name)     { QDBusInterface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("AddMix"), name); }
void MixerClient::removeChannel(const QString &slug) { QDBusInterface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("RemoveChannel"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("%1/channel/%2").arg(ROOT, slug)))); }
void MixerClient::removeMix(const QString &slug)     { QDBusInterface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("RemoveMix"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("%1/mix/%2").arg(ROOT, slug)))); }

} // namespace kmixdeck::frontend
