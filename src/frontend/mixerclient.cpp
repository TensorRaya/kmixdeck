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
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
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
    qDBusRegisterMetaType<StringMap>(); qDBusRegisterMetaType<PortMap>(); qDBusRegisterMetaType<InterfaceMap>(); qDBusRegisterMetaType<ManagedObjects>();
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
        if (props.contains(QStringLiteral("ListeningDevice"))) {
            const QString d = props.value(QStringLiteral("ListeningDevice")).toString();
            if (d != m_listeningDevice) { m_listeningDevice = d; Q_EMIT listeningDeviceChanged(); }
        }
        if (props.contains(QStringLiteral("DefaultChannel"))) {
            const QString p = props.value(QStringLiteral("DefaultChannel")).toString();
            const QString slug = p == QLatin1String("/") ? QString() : p.section(QLatin1Char('/'), -1);
            if (slug != m_defaultChannel) { m_defaultChannel = slug; Q_EMIT defaultChannelChanged(); }
        }
        if (rawProps.contains(QStringLiteral("FxTypes")) || rawProps.contains(QStringLiteral("FxPresets"))) {
            const QString t = props.value(QStringLiteral("FxTypes")).toString();
            const QString pr = props.value(QStringLiteral("FxPresets")).toString();
            if (!t.isEmpty()) m_fxTypes = QJsonDocument::fromJson(t.toUtf8()).array().toVariantList();
            if (!pr.isEmpty()) m_fxPresets = QJsonDocument::fromJson(pr.toUtf8()).object().toVariantMap();
            Q_EMIT fxTypesReady();
        }
        if (props.contains(QStringLiteral("ChannelOrder")) || props.contains(QStringLiteral("MixOrder"))) {   // UX-9
            const QStringList co = props.value(QStringLiteral("ChannelOrder")).toStringList(), mo = props.value(QStringLiteral("MixOrder")).toStringList();
            bool changed = false;
            if (!co.isEmpty() && co != m_channelOrder) { m_channelOrder = co; changed = true; }
            if (!mo.isEmpty() && mo != m_mixOrder) { m_mixOrder = mo; changed = true; }
            if (changed) { if (layout) *layout = true; else Q_EMIT layoutChanged(); }
        }
        if (props.contains(QStringLiteral("UndoDescription"))) {
            const QString u = props.value(QStringLiteral("UndoDescription")).toString();
            if (u != m_undoDescription) { m_undoDescription = u; Q_EMIT undoChanged(); }
        }
        if (rawProps.contains(QStringLiteral("InputDevices"))) {
            QVariant v = rawProps.value(QStringLiteral("InputDevices"));
            if (v.userType() == qMetaTypeId<QDBusVariant>()) v = v.value<QDBusVariant>().variant();
            const StringMap d = qdbus_cast<StringMap>(v);
            for (auto it = d.cbegin(); it != d.cend(); ++it) m_inputDevices.insert(it.key(), it.value());
            Q_EMIT inputDevicesChanged();
        }
        if (rawProps.contains(QStringLiteral("DevicePorts"))) {   // ADR 0009 D4
            QVariant v = rawProps.value(QStringLiteral("DevicePorts"));
            if (v.userType() == qMetaTypeId<QDBusVariant>()) v = v.value<QDBusVariant>().variant();
            m_devicePorts = qdbus_cast<PortMap>(v); ++m_devicePortsVersion;
            Q_EMIT devicePortsChanged();
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
void MixerClient::callReportingErrors(const QString &method, const QVariant &arg, const QVariant &arg2) {
    QDBusInterface iface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
    auto *w = new QDBusPendingCallWatcher(arg2.isValid() ? iface.asyncCall(method, arg, arg2) : arg.isValid() ? iface.asyncCall(method, arg) : iface.asyncCall(method), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<> r = *w;
        if (r.isError()) { qWarning() << "kmixdeck: request refused:" << r.error().message(); Q_EMIT errorOccurred(r.error().message()); }
        w->deleteLater();
    });
}
void MixerClient::addChannel(const QString &name) { callReportingErrors(QStringLiteral("AddChannel"), name); }
void MixerClient::addMix(const QString &name)     { callReportingErrors(QStringLiteral("AddMix"), name); }
void MixerClient::addChannelWithSource(const QString &name, const QString &kind, const QString &ref) {
    QDBusInterface iface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
    auto *w = new QDBusPendingCallWatcher(iface.asyncCall(QStringLiteral("AddChannel"), name), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, kind, ref](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusObjectPath> r = *w; w->deleteLater();
        if (r.isError()) { qWarning() << "kmixdeck: request refused:" << r.error().message(); Q_EMIT errorOccurred(r.error().message()); return; }
        const QString path = r.value().path(), slug = path.section(QLatin1Char('/'), -1);
        if (kind == QLatin1String("app") && !ref.isEmpty()) assignApp(ref, {slug}, false);          // CH-4
        else if (kind == QLatin1String("device") && !ref.isEmpty()) setChannelDevice(slug, ref);   // DV-9
    });
}
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
                                  {QStringLiteral("icon"), a.value(QStringLiteral("Icon"))},                       // UX-10
                                  {QStringLiteral("running"), a.value(QStringLiteral("Running"))},                // UX-10
                                  {QStringLiteral("nodeId"), a.value(QStringLiteral("NodeId"))},                  // UX-13: meter key app/<id>
                                  {QStringLiteral("allChannels"), a.value(QStringLiteral("Channels"))},           // CH-12
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
// ADR 0009 D4 / DV-20: ports of one device, each with who uses it (scanned from the layout the client already has).
QVariantList MixerClient::devicePorts(const QString &nodeName) const {
    QVariantList out;
    QHash<QString, QStringList> users;   // position ("*" = whole device) → "Voice" / "Stream"
    auto note = [&](const QString &ref, const QString &who) {
        if (ref.section(QLatin1Char(':'), 0, 0) != nodeName) return;
        const QString posList = ref.section(QLatin1Char(':'), 1);
        if (posList.isEmpty()) { users[QStringLiteral("*")] << who; return; }
        for (const auto &p : posList.split(QLatin1Char(','), Qt::SkipEmptyParts)) users[p] << who;
    };
    for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it) note(it->value(QStringLiteral("InputDevice")).toString(), it->value(QStringLiteral("Name")).toString());
    for (auto it = m_mixes.cbegin(); it != m_mixes.cend(); ++it) for (const auto &r : it->value(QStringLiteral("Outputs")).toStringList()) note(r, it->value(QStringLiteral("Name")).toString());
    for (const auto &t : m_devicePorts.value(nodeName)) {
        const QStringList f = t.split(QLatin1Char('|'));
        const QString pos = f.value(0);
        // "Fake Ui24R:capture_AUX2" → keep the part after the colon when the alias only repeats the device name
        QString label = f.value(2).section(QLatin1Char(':'), -1); if (label.isEmpty() || label == f.value(1)) label = pos;
        out.push_back(QVariantMap{{QStringLiteral("position"), pos}, {QStringLiteral("port"), f.value(1)}, {QStringLiteral("label"), label},
                                  {QStringLiteral("usedBy"), users.value(pos) + users.value(QStringLiteral("*"))}});
    }
    return out;
}
QString MixerClient::deviceRefLabel(const QString &ref) const {
    if (ref.isEmpty()) return {};
    const QString node = refNode(ref);
    QString dev = m_outputDevices.value(node, m_inputDevices.value(node, node));
    const QStringList pos = refPositions(ref);
    if (pos.isEmpty()) return dev;
    QStringList labels;
    for (const auto &p : pos) { QString l = p; for (const auto &pt : devicePorts(node)) if (pt.toMap().value(QStringLiteral("position")) == p) { l = pt.toMap().value(QStringLiteral("label")).toString(); break; } labels << l; }
    return dev + QStringLiteral(" · ") + labels.join(QStringLiteral("+"));
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
void MixerClient::undo() { callReportingErrors(QStringLiteral("Undo"), QVariant()); }
void MixerClient::setListeningDevice(const QString &node) {
    setProperty(ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QStringLiteral("ListeningDevice"), node);
}
void MixerClient::setDefaultChannel(const QString &slug) {
    const QString path = slug.isEmpty() ? QStringLiteral("/") : QStringLiteral("%1/channel/%2").arg(ROOT, slug);
    setProperty(ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QStringLiteral("DefaultChannel"), QVariant::fromValue(QDBusObjectPath(path)));
}
void MixerClient::setCellFollows(const QString &ch, const QString &mix, const QString &followsSlug) {
    const QString path = followsSlug.isEmpty() ? QStringLiteral("/") : QStringLiteral("%1/mix/%2").arg(ROOT, followsSlug);
    m_cells[cellKey(ch, mix)][QStringLiteral("Follows")] = path;
    setProperty(QStringLiteral("%1/cell/%2/%3").arg(ROOT, ch, mix), QStringLiteral("org.kmixdeck1.Cell"), QStringLiteral("Follows"), QVariant::fromValue(QDBusObjectPath(path)));
    Q_EMIT cellChanged(ch, mix);
}
void MixerClient::setMixFallbackOutput(const QString &slug, const QString &node) {
    m_mixes[slug][QStringLiteral("FallbackOutput")] = node;
    setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("FallbackOutput"), node);
}
void MixerClient::toggleMixOutput(const QString &slug, const QString &node) {
    const bool has = mixOutputs(slug).contains(node);
    QDBusInterface(BUS, QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QDBusConnection::sessionBus())
        .asyncCall(has ? QStringLiteral("RemoveOutput") : QStringLiteral("AddOutput"), node);
}
void MixerClient::setMixVolume(const QString &slug, double cubic) {
    const double lin = std::pow(std::clamp(cubic, 0.0, 1.0), 3.0);
    m_mixes[slug][QStringLiteral("Volume")] = lin;
    setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("Volume"), lin);
}
void MixerClient::toggleMixMute(const QString &slug) {
    QDBusInterface(BUS, QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("ToggleMute"));
}
void MixerClient::setChannelIcon(const QString &slug, const QString &icon) { setProperty(QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("Icon"), icon); }
void MixerClient::setMixIcon(const QString &slug, const QString &icon) { setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("Icon"), icon); }
void MixerClient::moveChannel(const QString &slug, int index) { callReportingErrors(QStringLiteral("MoveChannel"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("%1/channel/%2").arg(ROOT, slug))), index); }
void MixerClient::moveMix(const QString &slug, int index) { callReportingErrors(QStringLiteral("MoveMix"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("%1/mix/%2").arg(ROOT, slug))), index); }
void MixerClient::renameChannel(const QString &slug, const QString &name) { setProperty(QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("Name"), name); }
void MixerClient::renameMix(const QString &slug, const QString &name) { setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("Name"), name); }
void MixerClient::moveApp(const QString &appPath, const QString &channelSlug) {
    QDBusInterface(BUS, appPath, QStringLiteral("org.kmixdeck1.App"), QDBusConnection::sessionBus())
        .asyncCall(QStringLiteral("MoveTo"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("%1/channel/%2").arg(ROOT, channelSlug))));
}
// CH-12/UX-11: several channels at once; addOn=true accumulates (drop onto one more row).
void MixerClient::assignApp(const QString &appPath, const QStringList &channelSlugs, bool addOn) {
    QStringList paths;
    for (const QString &s : channelSlugs) paths << QStringLiteral("%1/channel/%2").arg(ROOT, s);
    QDBusInterface(BUS, appPath, QStringLiteral("org.kmixdeck1.App"), QDBusConnection::sessionBus())
        .asyncCall(QStringLiteral("Assign"), paths, addOn);
}
// UX-12: press → Audition(<path>), release → Audition("/"). One round trip each, the daemon restores state.
void MixerClient::audition(const QString &kind, const QString &slug) {
    QDBusInterface(BUS, QString::fromLatin1(ROOT), QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus())
        .asyncCall(QStringLiteral("Audition"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("%1/%2/%3").arg(ROOT, kind, slug))));
}
void MixerClient::stopAudition() {
    QDBusInterface(BUS, QString::fromLatin1(ROOT), QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus())
        .asyncCall(QStringLiteral("Audition"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/"))));
}
QString MixerClient::fxChain(const QString &kind, const QString &slug) const {
    return (kind == QLatin1String("mix") ? m_mixes : m_channels).value(slug).value(QStringLiteral("FxChain")).toString();
}
void MixerClient::setFxChain(const QString &kind, const QString &slug, const QString &chainJson) {
    auto &m = (kind == QLatin1String("mix") ? m_mixes : m_channels)[slug];
    m[QStringLiteral("FxChain")] = chainJson;
    QDBusInterface(BUS, QStringLiteral("%1/%2/%3").arg(ROOT, kind, slug),
                   kind == QLatin1String("mix") ? QStringLiteral("org.kmixdeck1.Mix") : QStringLiteral("org.kmixdeck1.Channel"),
                   QDBusConnection::sessionBus()).asyncCall(QStringLiteral("SetFx"), chainJson);
    Q_EMIT kind == QLatin1String("mix") ? mixChanged(slug) : channelChanged(slug);
}
void MixerClient::setFxControl(const QString &kind, const QString &slug, const QString &control, double value) {
    QDBusInterface(BUS, QStringLiteral("%1/%2/%3").arg(ROOT, kind, slug),
                   kind == QLatin1String("mix") ? QStringLiteral("org.kmixdeck1.Mix") : QStringLiteral("org.kmixdeck1.Channel"),
                   QDBusConnection::sessionBus()).asyncCall(QStringLiteral("SetFxControl"), control, value);
}
void MixerClient::setChannelDevice(const QString &slug, const QString &deviceNode) {
    m_channels[slug][QStringLiteral("InputDevice")] = deviceNode;
    setProperty(QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("InputDevice"), deviceNode);
}

} // namespace kmixdeck::frontend

bool kmixdeck::frontend::MixerClient::fxEnabled(const QString &kind, const QString &slug) const {
    const QJsonObject o = QJsonDocument::fromJson(fxChain(kind, slug).toUtf8()).object();
    return o.value(QStringLiteral("enabled")).toBool(true) && !o.value(QStringLiteral("chain")).toArray().isEmpty();
}
