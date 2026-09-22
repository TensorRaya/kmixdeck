// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "mixerclient.h"
#include "../logging.h"
Q_LOGGING_CATEGORY(lcFrontend, "kmixdeck.frontend")
#include <QFile>
#include <KLocalizedString>
#include <QDBusInterface>
#include <QSet>
#include <QDBusReply>
#include <QDBusMetaType>
#include <QDBusArgument>
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
        m_available = false; qCWarning(lcFrontend) << "kmixdeckd not reachable:" << r.error().message();
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
        if (props.contains(QStringLiteral("FirstRun"))) { const bool f = props.value(QStringLiteral("FirstRun")).toBool(); if (f != m_firstRun) { m_firstRun = f; Q_EMIT firstRunChanged(); } }   // UX-3
        if (props.contains(QStringLiteral("DefaultSink")) || props.contains(QStringLiteral("DefaultSource"))) {
            const QString sk = props.value(QStringLiteral("DefaultSink"), m_defaultSink).toString(), sr = props.value(QStringLiteral("DefaultSource"), m_defaultSource).toString();
            if (sk != m_defaultSink || sr != m_defaultSource) { m_defaultSink = sk; m_defaultSource = sr; Q_EMIT defaultDevicesChanged(); }
        }
        if (props.contains(QStringLiteral("HiddenDevices"))) {   // CH-11
            const QStringList h = props.value(QStringLiteral("HiddenDevices")).toStringList();
            if (h != m_hiddenDevices) { m_hiddenDevices = h; Q_EMIT hiddenDevicesChanged(); Q_EMIT outputDevicesChanged(); Q_EMIT inputDevicesChanged(); }
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
        if (props.contains(QStringLiteral("Scenes"))) {   // CT-9
            const QStringList sc = props.value(QStringLiteral("Scenes")).toStringList();
            if (sc != m_scenes) { m_scenes = sc; Q_EMIT scenesChanged(); }
        }
        if (rawProps.contains(QStringLiteral("InputDevices"))) {
            QVariant v = rawProps.value(QStringLiteral("InputDevices"));
            if (v.userType() == qMetaTypeId<QDBusVariant>()) v = v.value<QDBusVariant>().variant();
            const StringMap d = qdbus_cast<StringMap>(v);
            for (auto it = d.cbegin(); it != d.cend(); ++it) m_inputDevices.insert(it.key(), it.value());
            Q_EMIT inputDevicesChanged();
        }
        if (rawProps.contains(QStringLiteral("Samples"))) {   // CT-8
            // aa{sv} — needs rawProps and qdbus_cast like DevicePorts below, because the demarshalled QVariant
            // would otherwise stay a QDBusArgument and every field would read empty.
            QVariant v = rawProps.value(QStringLiteral("Samples"));
            if (v.userType() == qMetaTypeId<QDBusVariant>()) v = v.value<QDBusVariant>().variant();
            const QList<QVariantMap> rows = qdbus_cast<QList<QVariantMap>>(v);
            QVariantList neu;
            for (const auto &r : rows) neu.append(r);
            if (neu != m_samples) { m_samples = neu; Q_EMIT samplesChanged(); }
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

void MixerClient::setChannelPan(const QString &slug, double pan) {   // DV-22
    setProperty(QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("Pan"), std::clamp(pan, -1.0, 1.0));
}
void MixerClient::setChannelTrim(const QString &slug, double cubic) {   // CH-7
    const double c = std::clamp(cubic, 0.0, 1.0);
    setProperty(QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("Trim"), c * c * c);
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
        if (r.isError()) { qCWarning(lcFrontend) << "kmixdeck: request refused:" << r.error().message(); Q_EMIT errorOccurred(r.error().message()); }
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
        if (r.isError()) { qCWarning(lcFrontend) << "kmixdeck: request refused:" << r.error().message(); Q_EMIT errorOccurred(r.error().message()); return; }
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
    for (auto it = m_outputDevices.cbegin(); it != m_outputDevices.cend(); ++it) {
        // CH-11: hidden devices leave every picker — except one that is in use (the user must still see where a mix
        // plays / what they listen on; hiding is about the list, not about the routing)
        if (m_hiddenDevices.contains(it.key()) && !deviceInUse(it.key())) continue;
        out.push_back(QVariantMap{{QStringLiteral("nodeName"), it.key()}, {QStringLiteral("description"), it.value()}, {QStringLiteral("hidden"), m_hiddenDevices.contains(it.key())}});
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) { return a.toMap().value(QStringLiteral("description")).toString().localeAwareCompare(b.toMap().value(QStringLiteral("description")).toString()) < 0; });
    return out;
}
// ADR 0009 D4 / DV-20: ports of one device, each with who uses it (scanned from the layout the client already has).
QVariantList MixerClient::devicePorts(const QString &nodeName) const {
    QVariantList out;
    QHash<QString, QStringList> users;   // position ("*" = whole device) → "Voice" / "Stream"
    auto note = [&](const QString &ref, const QString &who) {
        if (ref.section(QLatin1Char(':'), 0, 0) != nodeName) return;
        const QString posList = ref.section(QLatin1Char(':'), 1).section(QLatin1Char('>'), 0, 0);
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
    QString r = dev + QStringLiteral(" · ") + labels.join(QStringLiteral("+"));
    const QString side = refSide(ref);
    if (side == QLatin1String("L")) r += i18nc("@label ref bound to the left side only", " → L");
    else if (side == QLatin1String("R")) r += i18nc("@label ref bound to the right side only", " → R");
    return r;
}
QString MixerClient::deviceRefShort(const QString &ref) const {
    const QStringList pos = refPositions(ref);
    QString dev = deviceDescription(refNode(ref));
    dev.remove(QStringLiteral(" (virtual) outputs")).remove(QStringLiteral(" (virtual)"));
    if (pos.isEmpty()) return dev;
    QStringList labels;
    for (const auto &p : pos) { QString l = p; for (const auto &pt : devicePorts(refNode(ref))) if (pt.toMap().value(QStringLiteral("position")) == p) { l = pt.toMap().value(QStringLiteral("label")).toString(); break; } labels << l; }
    return labels.join(QStringLiteral("+")) + QStringLiteral(" · ") + dev;
}
void MixerClient::addChannelInput(const QString &slug, const QString &ref) {
    QDBusInterface(BUS, QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("AddInput"), ref);
}
void MixerClient::removeChannelInput(const QString &slug, const QString &ref) {
    QDBusInterface(BUS, QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("RemoveInput"), ref);
}

QString MixerClient::connectJacks(const QString &fromCard0, const QString &fromPos0, const QString &toCard0, const QString &toPos0) {
    // normalise direction: signal flows source → channel → mix → output; accept the drag either way round
    auto rank = [](const QString &c) { return c.startsWith(QLatin1String("app/")) || c.startsWith(QLatin1String("dev/")) ? 0 : c.startsWith(QLatin1String("ch/")) ? 1 : c.startsWith(QLatin1String("mix/")) ? 2 : 3; };
    QString fromCard = fromCard0, fromPos = fromPos0, toCard = toCard0, toPos = toPos0;
    if (rank(fromCard) > rank(toCard)) { std::swap(fromCard, toCard); std::swap(fromPos, toPos); }
    if (rank(fromCard) == rank(toCard)) return i18n("Connect a source to a channel, a channel to a mix, or a mix to an output.");
    const QString fromId = fromCard.section(QLatin1Char('/'), 1), toId = toCard.section(QLatin1Char('/'), 1);
    if (fromCard.startsWith(QLatin1String("app/")) && toCard.startsWith(QLatin1String("ch/"))) {
        assignApp(fromId.startsWith(QLatin1Char('/')) ? fromId : QLatin1Char('/') + fromId, {toId}, true); return {};
    }
    if (fromCard.startsWith(QLatin1String("dev/")) && toCard.startsWith(QLatin1String("ch/"))) {
        // one connector → one side of the channel (L or R); the side is where the drag ended
        addChannelInput(toId, makeDeviceRef(fromId, {fromPos}, toPos == QLatin1String("L") || toPos == QLatin1String("R") ? toPos : QString())); return {};
    }
    if (fromCard.startsWith(QLatin1String("ch/")) && toCard.startsWith(QLatin1String("mix/"))) { setCellMuted(fromId, toId, false); return {}; }
    if (fromCard.startsWith(QLatin1String("mix/")) && toCard.startsWith(QLatin1String("outdev/"))) {
        QDBusInterface(BUS, QStringLiteral("%1/mix/%2").arg(ROOT, fromId), QStringLiteral("org.kmixdeck1.Mix"), QDBusConnection::sessionBus())
            .asyncCall(QStringLiteral("AddOutput"), makeDeviceRef(toId, {toPos}, fromPos == QLatin1String("L") || fromPos == QLatin1String("R") ? fromPos : QString()));
        return {};
    }
    if (fromCard.startsWith(QLatin1String("mix/")) && toCard.startsWith(QLatin1String("out/"))) return i18n("The capture device of a mix is always connected.");
    if (fromCard.startsWith(QLatin1String("dev/")) && toCard.startsWith(QLatin1String("mix/"))) return i18n("A device feeds a channel, not a mix — add a channel first.");
    return i18n("These two cannot be wired.");
}
static QDBusInterface wireOwner(const QVariantMap &w) {
    const QString kind = w.value(QStringLiteral("kind")).toString();
    return kind == QLatin1String("input")
        ? QDBusInterface(BUS, QStringLiteral("%1/channel/%2").arg(ROOT, w.value(QStringLiteral("channel")).toString()), QStringLiteral("org.kmixdeck1.Channel"), QDBusConnection::sessionBus())
        : QDBusInterface(BUS, QStringLiteral("%1/mix/%2").arg(ROOT, w.value(QStringLiteral("mix")).toString()), QStringLiteral("org.kmixdeck1.Mix"), QDBusConnection::sessionBus());
}
double MixerClient::wireTrim(const QVariantMap &w) const {
    const QString kind = w.value(QStringLiteral("kind")).toString();
    if (kind != QLatin1String("input") && kind != QLatin1String("output")) return 1.0;
    QDBusReply<double> r = wireOwner(w).call(QStringLiteral("WireTrim"), w.value(QStringLiteral("ref")).toString());
    return (r.isValid() && r.value() >= 0) ? r.value() : 1.0;
}
bool MixerClient::wireMuted(const QVariantMap &w) const {
    const QString kind = w.value(QStringLiteral("kind")).toString();
    if (kind != QLatin1String("input") && kind != QLatin1String("output")) return false;
    QDBusReply<bool> r = wireOwner(w).call(QStringLiteral("WireMuted"), w.value(QStringLiteral("ref")).toString());
    return r.isValid() && r.value();
}
void MixerClient::setWireTrim(const QVariantMap &w, double trim, bool muted) {
    const QString kind = w.value(QStringLiteral("kind")).toString();
    if (kind != QLatin1String("input") && kind != QLatin1String("output")) return;
    wireOwner(w).asyncCall(QStringLiteral("SetWireTrim"), w.value(QStringLiteral("ref")).toString(), trim, muted);
}
void MixerClient::removeWire(const QVariantMap &w) {
    const QString kind = w.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("input")) removeChannelInput(w.value(QStringLiteral("channel")).toString(), w.value(QStringLiteral("ref")).toString());
    else if (kind == QLatin1String("output")) QDBusInterface(BUS, QStringLiteral("%1/mix/%2").arg(ROOT, w.value(QStringLiteral("mix")).toString()), QStringLiteral("org.kmixdeck1.Mix"), QDBusConnection::sessionBus()).asyncCall(QStringLiteral("RemoveOutput"), w.value(QStringLiteral("ref")).toString());
    else if (kind == QLatin1String("cell")) setCellMuted(w.value(QStringLiteral("channel")).toString(), w.value(QStringLiteral("mix")).toString(), true);
    else if (kind == QLatin1String("app")) {
        const QString app = w.value(QStringLiteral("app")).toString(), ch = w.value(QStringLiteral("channel")).toString();
        QStringList rest = m_apps.value(app).value(QStringLiteral("Channels")).toStringList(); rest.removeAll(ch);
        assignApp(app, rest, false);
    }
}

QVariantMap MixerClient::patchbay() const {
    QVariantList cards, wires;
    auto row = [](const QString &pos, const QString &label, const QString &meterKey, bool jackIn, bool jackOut, const QString &usedBy = QString()) {
        return QVariantMap{{QStringLiteral("pos"), pos}, {QStringLiteral("label"), label}, {QStringLiteral("meterKey"), meterKey},
                           {QStringLiteral("jackIn"), jackIn}, {QStringLiteral("jackOut"), jackOut}, {QStringLiteral("usedBy"), usedBy}};
    };
    auto card = [&](const QString &id, const QString &kind, const QString &title, const QString &icon, bool on, bool present, const QVariantList &rows, const QString &node = QString()) {
        cards.push_back(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("kind"), kind}, {QStringLiteral("title"), title}, {QStringLiteral("icon"), icon},
                                    {QStringLiteral("on"), on}, {QStringLiteral("present"), present}, {QStringLiteral("rows"), rows}, {QStringLiteral("node"), node}});
    };
    // --- who uses which device port (for "usedBy")
    QHash<QString, QString> used;   // "node|POS" → "Channel name"
    for (const QString &c : m_channelOrder) for (const QString &ref : channelInputs(c)) for (const QString &p : refPositions(ref)) used.insert(refNode(ref) + QLatin1Char('|') + p, channelName(c));
    for (const QString &m : m_mixOrder) for (const QString &ref : mixOutputs(m)) for (const QString &p : refPositions(ref)) used.insert(refNode(ref) + QLatin1Char('|') + p, mixName(m));

    // --- column 1: sources = running/known apps + every input device (all connectors)
    for (const QVariant &av : apps()) {
        const QVariantMap a = av.toMap();
        const QString id = QStringLiteral("app/") + a.value(QStringLiteral("path")).toString();
        const QString mk = QStringLiteral("app/") + a.value(QStringLiteral("nodeId")).toString();
        card(id, QStringLiteral("app"), a.value(QStringLiteral("name")).toString(), a.value(QStringLiteral("icon")).toString().isEmpty() ? QStringLiteral("applications-multimedia") : a.value(QStringLiteral("icon")).toString(),
             true, a.value(QStringLiteral("running")).toBool(),
             {row(QStringLiteral("L"), i18nc("@label stereo row", "1 (L)"), mk, false, true), row(QStringLiteral("R"), i18nc("@label stereo row", "2 (R)"), mk, false, true)});
        const QStringList chans = a.value(QStringLiteral("allChannels")).toStringList().isEmpty() ? (a.value(QStringLiteral("channel")).toString().isEmpty() ? QStringList{} : QStringList{a.value(QStringLiteral("channel")).toString()}) : a.value(QStringLiteral("allChannels")).toStringList();
        for (const QString &c : chans) for (const QString &side : {QStringLiteral("L"), QStringLiteral("R")})
            wires.push_back(QVariantMap{{QStringLiteral("from"), QVariantMap{{QStringLiteral("card"), id}, {QStringLiteral("pos"), side}}}, {QStringLiteral("to"), QVariantMap{{QStringLiteral("card"), QStringLiteral("ch/") + c}, {QStringLiteral("pos"), side}}},
                                        {QStringLiteral("ref"), QString()}, {QStringLiteral("kind"), QStringLiteral("app")}, {QStringLiteral("muted"), false}, {QStringLiteral("meterKey"), mk}, {QStringLiteral("app"), a.value(QStringLiteral("path"))}, {QStringLiteral("channel"), c}});
    }
    for (auto it = m_inputDevices.cbegin(); it != m_inputDevices.cend(); ++it) {
        const QString node = it.key();
        if (m_hiddenDevices.contains(node) && !deviceInUse(node)) continue;   // CH-11: hidden and unused → no card
        QVariantList rows;
        const QVariantList ports = devicePorts(node);
        if (ports.isEmpty()) { rows << row(QStringLiteral("FL"), QStringLiteral("L"), QStringLiteral("dev/") + node, false, true, used.value(node + QStringLiteral("|FL"))) << row(QStringLiteral("FR"), QStringLiteral("R"), QStringLiteral("dev/") + node, false, true, used.value(node + QStringLiteral("|FR"))); }
        else for (const QVariant &pv : ports) { const auto pm = pv.toMap(); const QString pos = pm.value(QStringLiteral("position")).toString(); rows << row(pos, pm.value(QStringLiteral("label")).toString(), QStringLiteral("dev/") + node, false, true, used.value(node + QLatin1Char('|') + pos)); }
        card(QStringLiteral("dev/") + node, QStringLiteral("device"), it.value(), QStringLiteral("audio-input-microphone"), true, true, rows, node);
    }
    // --- column 2: channels (jacks both sides)
    for (const QString &c : m_channelOrder) {
        card(QStringLiteral("ch/") + c, QStringLiteral("channel"), channelName(c), channelIcon(c), !channelMuted(c), true,
             {row(QStringLiteral("L"), QStringLiteral("L"), QStringLiteral("channel/") + c, true, true), row(QStringLiteral("R"), QStringLiteral("R"), QStringLiteral("channel/") + c, true, true)});
        for (const QString &ref : channelInputs(c)) {
            const QString node = refNode(ref); const QStringList pos = refPositions(ref); const QString side = refSide(ref);
            const bool present = m_inputDevices.contains(node);
            auto wire = [&](const QString &fromPos, const QString &toSide) {
                wires.push_back(QVariantMap{{QStringLiteral("from"), QVariantMap{{QStringLiteral("card"), QStringLiteral("dev/") + node}, {QStringLiteral("pos"), fromPos}}}, {QStringLiteral("to"), QVariantMap{{QStringLiteral("card"), QStringLiteral("ch/") + c}, {QStringLiteral("pos"), toSide}}},
                                            {QStringLiteral("ref"), ref}, {QStringLiteral("kind"), QStringLiteral("input")}, {QStringLiteral("muted"), !present}, {QStringLiteral("meterKey"), QStringLiteral("in/") + c}, {QStringLiteral("channel"), c}});
            };
            if (pos.isEmpty()) { wire(QStringLiteral("FL"), QStringLiteral("L")); wire(QStringLiteral("FR"), QStringLiteral("R")); }
            else if (pos.size() == 2) { wire(pos[0], QStringLiteral("L")); wire(pos[1], QStringLiteral("R")); }
            else if (!side.isEmpty()) wire(pos[0], side);
            else { wire(pos[0], QStringLiteral("L")); wire(pos[0], QStringLiteral("R")); }   // mono → centre = both sides
        }
        for (const QString &m : m_mixOrder) {   // channel → mix cells (gain wires)
            const bool muted = cellMuted(c, m) || channelMuted(c);
            for (const QString &side : {QStringLiteral("L"), QStringLiteral("R")})
                wires.push_back(QVariantMap{{QStringLiteral("from"), QVariantMap{{QStringLiteral("card"), QStringLiteral("ch/") + c}, {QStringLiteral("pos"), side}}}, {QStringLiteral("to"), QVariantMap{{QStringLiteral("card"), QStringLiteral("mix/") + m}, {QStringLiteral("pos"), side}}},
                                            {QStringLiteral("ref"), QString()}, {QStringLiteral("kind"), QStringLiteral("cell")}, {QStringLiteral("muted"), muted}, {QStringLiteral("gain"), cellVolume(c, m)}, {QStringLiteral("meterKey"), QStringLiteral("cell/") + c + QLatin1Char('/') + m}, {QStringLiteral("channel"), c}, {QStringLiteral("mix"), m}});
        }
    }
    // --- column 3: mixes (jacks both sides), then ONE card per output device with ALL its connectors (DV-24/26),
    //     plus one capture card per mix. Wires: mix L/R → device rows.
    QSet<QString> outDevs;
    for (const QString &m : m_mixOrder) {
        card(QStringLiteral("mix/") + m, QStringLiteral("mix"), mixName(m), mixIcon(m), !mixMuted(m), true,
             {row(QStringLiteral("L"), QStringLiteral("L"), QStringLiteral("mix/") + m, true, true), row(QStringLiteral("R"), QStringLiteral("R"), QStringLiteral("mix/") + m, true, true)});
        for (const QString &ref : mixOutputs(m)) {
            const QString node = refNode(ref); const QStringList pos = refPositions(ref); const QString side = refSide(ref);
            const bool present = m_outputDevices.contains(node);
            outDevs.insert(node);
            const QString oid = QStringLiteral("outdev/") + node;
            auto wire = [&](const QString &fromSide, const QString &toPos) {
                wires.push_back(QVariantMap{{QStringLiteral("from"), QVariantMap{{QStringLiteral("card"), QStringLiteral("mix/") + m}, {QStringLiteral("pos"), fromSide}}}, {QStringLiteral("to"), QVariantMap{{QStringLiteral("card"), oid}, {QStringLiteral("pos"), toPos}}},
                                            {QStringLiteral("ref"), ref}, {QStringLiteral("kind"), QStringLiteral("output")}, {QStringLiteral("muted"), mixMuted(m) || !present}, {QStringLiteral("meterKey"), QStringLiteral("out/") + m}, {QStringLiteral("mix"), m}});
            };
            if (pos.isEmpty()) { wire(QStringLiteral("L"), QStringLiteral("FL")); wire(QStringLiteral("R"), QStringLiteral("FR")); }
            else if (pos.size() == 2) { wire(QStringLiteral("L"), pos[0]); wire(QStringLiteral("R"), pos[1]); }
            else if (!side.isEmpty()) wire(side, pos[0]);
            else { wire(QStringLiteral("L"), pos[0]); wire(QStringLiteral("R"), pos[0]); }   // whole mix folded into one port
        }
        const QString cid = QStringLiteral("out/") + m + QStringLiteral("/capture");
        card(cid, QStringLiteral("capture"), i18n("Capture: %1", mixName(m)), QStringLiteral("camera-video"), true, true,
             {row(QStringLiteral("L"), QStringLiteral("L"), QStringLiteral("mix/") + m, true, false), row(QStringLiteral("R"), QStringLiteral("R"), QStringLiteral("mix/") + m, true, false)});
        for (const QString &side : {QStringLiteral("L"), QStringLiteral("R")})
            wires.push_back(QVariantMap{{QStringLiteral("from"), QVariantMap{{QStringLiteral("card"), QStringLiteral("mix/") + m}, {QStringLiteral("pos"), side}}}, {QStringLiteral("to"), QVariantMap{{QStringLiteral("card"), cid}, {QStringLiteral("pos"), side}}},
                                        {QStringLiteral("ref"), QString()}, {QStringLiteral("kind"), QStringLiteral("capture")}, {QStringLiteral("muted"), mixMuted(m)}, {QStringLiteral("meterKey"), QStringLiteral("mix/") + m}, {QStringLiteral("mix"), m}});
    }
    // every known output device gets a card (wired or not) — that is the patchbay's point: you see the free jacks
    for (auto it = m_outputDevices.cbegin(); it != m_outputDevices.cend(); ++it) outDevs.insert(it.key());
    QStringList outList = outDevs.values(); std::sort(outList.begin(), outList.end(), [this](const QString &a, const QString &b) { return deviceDescription(a).localeAwareCompare(deviceDescription(b)) < 0; });
    for (const QString &node : outList) {
        const bool present = m_outputDevices.contains(node);
        if (m_hiddenDevices.contains(node) && !deviceInUse(node)) continue;   // CH-11
        QVariantList rows;
        const QVariantList ports = devicePorts(node);
        if (ports.isEmpty()) { rows << row(QStringLiteral("FL"), QStringLiteral("L"), QStringLiteral("dev/") + node, true, false, used.value(node + QStringLiteral("|FL"))) << row(QStringLiteral("FR"), QStringLiteral("R"), QStringLiteral("dev/") + node, true, false, used.value(node + QStringLiteral("|FR"))); }
        else for (const QVariant &pv : ports) { const auto pm = pv.toMap(); const QString pos = pm.value(QStringLiteral("position")).toString(); rows << row(pos, pm.value(QStringLiteral("label")).toString(), QStringLiteral("dev/") + node, true, false, used.value(node + QLatin1Char('|') + pos)); }
        card(QStringLiteral("outdev/") + node, QStringLiteral("output"), deviceDescription(node), node == m_listeningDevice ? QStringLiteral("audio-headphones") : QStringLiteral("audio-speakers"), true, present, rows, node);
    }
    // DV-27 "Monitors": what the user is listening on (UX-2) and which mixes reach it — one row per mix on the
    // listening device with its master level, so the patchbay can answer "why do I hear X" without the mixer page.
    // Hidden behind a button in the UI; the model always carries it.
    QVariantList monitors;
    if (!m_listeningDevice.isEmpty()) {
        for (const QString &m : m_mixOrder) {
            bool onIt = false;
            for (const QString &ref : mixOutputs(m)) if (ref.section(QLatin1Char(':'), 0, 0).section(QLatin1Char('>'), 0, 0) == m_listeningDevice) onIt = true;
            if (!onIt) continue;
            monitors.push_back(QVariantMap{{QStringLiteral("id"), QStringLiteral("monitor/") + m}, {QStringLiteral("mix"), m}, {QStringLiteral("title"), mixName(m)},
                                           {QStringLiteral("icon"), mixIcon(m)}, {QStringLiteral("volume"), mixVolume(m)}, {QStringLiteral("muted"), mixMuted(m)},
                                           {QStringLiteral("meterKey"), QStringLiteral("out/") + m}});
        }
    }
    return {{QStringLiteral("cards"), cards}, {QStringLiteral("wires"), wires},
            {QStringLiteral("monitors"), QVariantMap{{QStringLiteral("device"), m_listeningDevice}, {QStringLiteral("description"), m_listeningDevice.isEmpty() ? QString() : deviceDescription(m_listeningDevice)},
                                                     {QStringLiteral("present"), m_outputDevices.contains(m_listeningDevice)}, {QStringLiteral("mixes"), monitors}}}};
}
QVariantMap MixerClient::overview() const {
    QVariantList mixes, channels;
    for (const QString &m : m_mixOrder)
        mixes.push_back(QVariantMap{{QStringLiteral("slug"), m}, {QStringLiteral("name"), mixName(m)}, {QStringLiteral("icon"), mixIcon(m)}, {QStringLiteral("color"), mixColor(m)},
                                    {QStringLiteral("volume"), mixVolume(m)}, {QStringLiteral("muted"), mixMuted(m)}, {QStringLiteral("present"), mixOutputPresent(m)},
                                    {QStringLiteral("outputs"), mixOutputs(m)}, {QStringLiteral("meterKey"), QStringLiteral("mix/") + m},
                                    // UX-18: the tray shows the integrated value — the one number that answers "am I
                                    // at target?". M/S belong in the window, where there is room to watch them move.
                                    {QStringLiteral("loudness"), mixLoudness(m)}, {QStringLiteral("loudnessTarget"), mixLoudnessTarget(m)}});
    for (const QString &c : m_channelOrder)
        channels.push_back(QVariantMap{{QStringLiteral("slug"), c}, {QStringLiteral("name"), channelName(c)}, {QStringLiteral("icon"), channelIcon(c)}, {QStringLiteral("color"), channelColor(c)},
                                       {QStringLiteral("muted"), channelMuted(c)}, {QStringLiteral("trim"), channelTrim(c)}, {QStringLiteral("inputPresent"), m_channels.value(c).value(QStringLiteral("InputPresent"), true).toBool()},
                                       {QStringLiteral("inputs"), channelInputs(c)}, {QStringLiteral("meterKey"), QStringLiteral("channel/") + c}, {QStringLiteral("group"), channelGroup(c)}});
    int running = 0; for (const auto &a : m_apps) if (a.value(QStringLiteral("Running"), true).toBool()) ++running;
    return {{QStringLiteral("serviceAvailable"), serviceAvailable()}, {QStringLiteral("connected"), connected()},
            {QStringLiteral("listeningDevice"), m_listeningDevice}, {QStringLiteral("listeningDescription"), m_listeningDevice.isEmpty() ? QString() : deviceDescription(m_listeningDevice)},
            {QStringLiteral("listeningPresent"), m_outputDevices.contains(m_listeningDevice)},
            {QStringLiteral("mixes"), mixes}, {QStringLiteral("channels"), channels}, {QStringLiteral("runningApps"), running}};
}
QVariantList MixerClient::inputDevices() const {
    QVariantList out;
    for (auto it = m_inputDevices.cbegin(); it != m_inputDevices.cend(); ++it) {
        if (m_hiddenDevices.contains(it.key()) && !deviceInUse(it.key())) continue;   // CH-11
        out.push_back(QVariantMap{{QStringLiteral("nodeName"), it.key()}, {QStringLiteral("description"), it.value()}, {QStringLiteral("hidden"), m_hiddenDevices.contains(it.key())}});
    }
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
        bus.connect(BUS, ROOT, QStringLiteral("org.kmixdeck1.Levels"), QStringLiteral("Loudness"), this, SLOT(onLoudness(QDBusMessage)));   // UX-18
        lv.asyncCall(QStringLiteral("Subscribe"));
    } else {
        bus.disconnect(BUS, ROOT, QStringLiteral("org.kmixdeck1.Levels"), QStringLiteral("Peaks"), this, SLOT(onPeaks(QVariantMap)));
        bus.disconnect(BUS, ROOT, QStringLiteral("org.kmixdeck1.Levels"), QStringLiteral("Loudness"), this, SLOT(onLoudness(QDBusMessage)));
        lv.asyncCall(QStringLiteral("Unsubscribe"));
        m_peaks.clear(); Q_EMIT peaksChanged();
        m_loudness.clear(); Q_EMIT loudnessChanged();
    }
}
void MixerClient::onPeaks(const QVariantMap &peaks) {
    for (auto it = peaks.cbegin(); it != peaks.cend(); ++it) m_peaks[it.key()] = it.value().toDouble();
    Q_EMIT peaksChanged();
}
// UX-18. Unlike Peaks (a{sd} → QVariantMap for free) the Loudness signature is a{sad}: a map to an ARRAY.
// Qt cannot hand that to a slot as a typed argument without the metatype, and even with it the nested
// QDBusArgument has to be demarshalled by hand — hence the raw QDBusMessage. Reading it any other way
// yields an empty map, which looks exactly like "the daemon sends nothing".
void MixerClient::onLoudness(const QDBusMessage &msg) {
    if (msg.arguments().isEmpty()) return;
    const QDBusArgument arg = msg.arguments().constFirst().value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::MapType) return;
    QHash<QString, QList<double>> fresh;
    arg.beginMap();
    while (!arg.atEnd()) {
        QString slug; QList<double> vals;
        arg.beginMapEntry();
        arg >> slug;
        arg.beginArray();
        while (!arg.atEnd()) { double d; arg >> d; vals << d; }
        arg.endArray();
        arg.endMapEntry();
        fresh.insert(slug, vals);
    }
    arg.endMap();
    if (fresh.isEmpty()) return;
    for (auto it = fresh.cbegin(); it != fresh.cend(); ++it) m_loudness[it.key()] = it.value();
    Q_EMIT loudnessChanged();
}
void MixerClient::setMixOutputDevice(const QString &slug, const QString &nodeName) {
    m_mixes[slug][QStringLiteral("OutputDevice")] = nodeName;
    setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("OutputDevice"), nodeName);
}
// UX-18: the per-mix analyser toggle and the target line, written straight onto the Mix object —
// same route as every other mix property, so layout.json persists them without extra code.
void MixerClient::setMixLoudness(const QString &slug, bool on) {
    m_mixes[slug][QStringLiteral("Loudness")] = on;
    setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("Loudness"), on);
    if (!on) { m_loudness.remove(slug); Q_EMIT loudnessChanged(); }   // stale numbers must not linger on screen
    Q_EMIT mixChanged(slug);
}
void MixerClient::setMixLoudnessTarget(const QString &slug, double lufs) {
    const double t = std::clamp(lufs, -40.0, 0.0);
    m_mixes[slug][QStringLiteral("LoudnessTarget")] = t;
    setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("LoudnessTarget"), t);
    Q_EMIT mixChanged(slug);
}
void MixerClient::undo() { callReportingErrors(QStringLiteral("Undo"), QVariant()); }
// CT-9. callReportingErrors surfaces the daemon's error in lastError, which is why RecallScene had to stop
// swallowing its return value (fixed 2026-09-19) — otherwise a typo'd scene name looked like success here too.
void MixerClient::saveScene(const QString &name) { callReportingErrors(QStringLiteral("SaveScene"), name); }
void MixerClient::deleteScene(const QString &name) { callReportingErrors(QStringLiteral("DeleteScene"), name); }
void MixerClient::recallScene(const QString &name, bool exclusive) {
    QDBusInterface iface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
    const QDBusMessage r = iface.call(QStringLiteral("RecallScene"), name, exclusive);
    if (r.type() == QDBusMessage::ErrorMessage) { m_lastError = r.errorMessage(); Q_EMIT lastErrorChanged(); Q_EMIT errorOccurred(m_lastError); return; }
    m_lastError.clear(); Q_EMIT lastErrorChanged();
}
QVariantMap MixerClient::firstRunPlan() const {
    QDBusInterface iface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
    const QDBusReply<QString> r = iface.call(QStringLiteral("FirstRunPlan"));
    return r.isValid() ? QJsonDocument::fromJson(r.value().toUtf8()).object().toVariantMap() : QVariantMap{};
}
QVariantMap MixerClient::firstRunApply() {
    QDBusInterface iface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
    const QDBusMessage r = iface.call(QStringLiteral("FirstRunApply"));
    if (r.type() == QDBusMessage::ErrorMessage) { m_lastError = r.errorMessage(); Q_EMIT lastErrorChanged(); Q_EMIT errorOccurred(m_lastError); return {}; }
    m_lastError.clear(); Q_EMIT lastErrorChanged();
    if (m_firstRun) { m_firstRun = false; Q_EMIT firstRunChanged(); }
    return QJsonDocument::fromJson(r.arguments().value(0).toString().toUtf8()).object().toVariantMap();
}
bool MixerClient::exportToFile(const QUrl &url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QDBusInterface iface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
    const QDBusReply<QString> r = iface.call(QStringLiteral("Export"));
    if (!r.isValid()) { m_lastError = r.error().message(); Q_EMIT lastErrorChanged(); return false; }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { m_lastError = f.errorString(); Q_EMIT lastErrorChanged(); return false; }
    f.write(r.value().toUtf8()); m_lastError.clear(); Q_EMIT lastErrorChanged(); return true;
}
bool MixerClient::importFromFile(const QUrl &url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { m_lastError = f.errorString(); Q_EMIT lastErrorChanged(); return false; }
    QDBusInterface iface(BUS, ROOT, QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
    const QDBusMessage r = iface.call(QStringLiteral("Import"), QString::fromUtf8(f.readAll()));
    if (r.type() == QDBusMessage::ErrorMessage) { m_lastError = r.errorMessage(); Q_EMIT lastErrorChanged(); Q_EMIT errorOccurred(m_lastError); return false; }
    m_lastError.clear(); Q_EMIT lastErrorChanged(); return true;
}
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
void MixerClient::duplicateMix(const QString &slug, const QString &name) { callReportingErrors(QStringLiteral("DuplicateMix"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("%1/mix/%2").arg(ROOT, slug))), name); }
bool MixerClient::deviceInUse(const QString &node) const {
    if (m_listeningDevice == node) return true;
    for (auto it = m_mixes.cbegin(); it != m_mixes.cend(); ++it) for (const QString &o : it->value(QStringLiteral("Outputs")).toStringList()) if (o.section(QLatin1Char(':'), 0, 0) == node) return true;
    for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it) for (const QString &i : it->value(QStringLiteral("Inputs")).toStringList()) if (i.section(QLatin1Char(':'), 0, 0) == node) return true;
    return false;
}
QVariantList MixerClient::hiddenDevices() const {
    QVariantList out;
    for (const QString &n : m_hiddenDevices) {
        const QString d = m_outputDevices.contains(n) ? m_outputDevices.value(n) : m_inputDevices.value(n, n);
        out.push_back(QVariantMap{{QStringLiteral("nodeName"), n}, {QStringLiteral("description"), d}, {QStringLiteral("present"), m_outputDevices.contains(n) || m_inputDevices.contains(n)}});
    }
    return out;
}
void MixerClient::setDeviceHidden(const QString &node, bool hidden) { callReportingErrors(QStringLiteral("SetDeviceHidden"), node, hidden); }
void MixerClient::setChannelColor(const QString &slug, const QString &color) { setProperty(QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("Color"), color); }
void MixerClient::setChannelGroup(const QString &slug, const QString &group) { setProperty(QStringLiteral("%1/channel/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Channel"), QStringLiteral("Group"), group); }
QStringList MixerClient::channelGroups() const {
    QStringList out;
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) { const QString g = it.value().value(QStringLiteral("Group")).toString(); if (!g.isEmpty() && !out.contains(g)) out << g; }
    out.sort(Qt::CaseInsensitive); return out;
}
void MixerClient::setMixColor(const QString &slug, const QString &color) { setProperty(QStringLiteral("%1/mix/%2").arg(ROOT, slug), QStringLiteral("org.kmixdeck1.Mix"), QStringLiteral("Color"), color); }
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

QVariantMap kmixdeck::frontend::MixerClient::ducking(const QString &slug) const {
    const QJsonObject o = QJsonDocument::fromJson(m_channels.value(slug).value(QStringLiteral("Ducking")).toString().toUtf8()).object();
    return o.toVariantMap();
}
double kmixdeck::frontend::MixerClient::duckReduction(const QString &slug) const {
    return m_channels.value(slug).value(QStringLiteral("DuckReduction")).toDouble();
}
bool kmixdeck::frontend::MixerClient::setDucking(const QString &slug, const QVariantMap &cfg) {
    const QString json = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(cfg)).toJson(QJsonDocument::Compact));
    // Blocking on purpose: the caller shows the reason on failure, and a fire-and-forget asyncCall would leave
    // an out-of-range value looking accepted in the window while the daemon refused it.
    const QDBusMessage r = QDBusInterface(BUS, QStringLiteral("%1/channel/%2").arg(ROOT, slug),
                                          QStringLiteral("org.kmixdeck1.Channel"), QDBusConnection::sessionBus())
                               .call(QStringLiteral("SetDucking"), json);
    if (r.type() == QDBusMessage::ErrorMessage) { m_lastError = r.errorMessage(); return false; }
    m_lastError.clear();
    m_channels[slug][QStringLiteral("Ducking")] = json;
    Q_EMIT channelChanged(slug);
    return true;
}
bool kmixdeck::frontend::MixerClient::fxEnabled(const QString &kind, const QString &slug) const {
    const QJsonObject o = QJsonDocument::fromJson(fxChain(kind, slug).toUtf8()).object();
    return o.value(QStringLiteral("enabled")).toBool(true) && !o.value(QStringLiteral("chain")).toArray().isEmpty();
}

// ---- CT-8 soundboard --------------------------------------------------------------------------------------
// All of these block and return a result, like setDucking above and for the same reason: the panel puts the
// daemon's refusal on screen. callReportingErrors() is async and returns void — with it, an unreadable file
// would look registered until the next property update contradicted it.
namespace {
QDBusMessage callMixer(const QString &method, const QVariantList &args) {
    QDBusInterface iface(kmixdeck::frontend::BUS, kmixdeck::frontend::ROOT,
                         QStringLiteral("org.kmixdeck1.Mixer"), QDBusConnection::sessionBus());
    return iface.callWithArgumentList(QDBus::Block, method, args);
}
}   // namespace

QVariantList kmixdeck::frontend::MixerClient::samples(const QString &channel) const {
    if (channel.isEmpty()) return m_samples;
    QVariantList out;
    for (const QVariant &v : m_samples)
        if (v.toMap().value(QStringLiteral("channel")).toString() == channel) out.append(v);
    return out;
}
bool kmixdeck::frontend::MixerClient::isSoundboard(const QString &slug) const {
    return m_channels.value(slug).value(QStringLiteral("Kind")).toString() == QLatin1String("soundboard");
}
// CT-8: every soundboard channel in layout order — the window's "Soundboard…" action and the panel's board picker
// both need the LIST, not a per-slug question, and a binding needs a property (a Q_INVOKABLE is evaluated once).
QStringList kmixdeck::frontend::MixerClient::soundboardSlugs() const {
    QStringList out;
    const QStringList alle = channelSlugs();
    for (const QString &s : alle) if (isSoundboard(s)) out << s;
    return out;
}
QString kmixdeck::frontend::MixerClient::addSoundboard(const QString &name) {
    const QDBusMessage r = callMixer(QStringLiteral("AddSoundboard"), {name});
    if (r.type() == QDBusMessage::ErrorMessage) { m_lastError = r.errorMessage(); Q_EMIT lastErrorChanged(); return {}; }
    m_lastError.clear();
    return r.arguments().value(0).value<QDBusObjectPath>().path().section(QLatin1Char('/'), -1);
}
QString kmixdeck::frontend::MixerClient::addSample(const QString &channel, const QString &path, const QString &name) {
    const QDBusMessage r = callMixer(QStringLiteral("AddSample"), {channel, path, name});
    if (r.type() == QDBusMessage::ErrorMessage) { m_lastError = r.errorMessage(); Q_EMIT lastErrorChanged(); return {}; }
    m_lastError.clear();
    return r.arguments().value(0).toString();
}
bool kmixdeck::frontend::MixerClient::removeSample(const QString &channel, const QString &name) {
    const QDBusMessage r = callMixer(QStringLiteral("RemoveSample"), {channel, name});
    if (r.type() == QDBusMessage::ErrorMessage) { m_lastError = r.errorMessage(); Q_EMIT lastErrorChanged(); return false; }
    m_lastError.clear(); return true;
}
bool kmixdeck::frontend::MixerClient::playSample(const QString &channel, const QString &name) {
    // PlaySampleOn: the name is unique per board, not globally — a pad must never fire another board's sample.
    const QDBusMessage r = callMixer(QStringLiteral("PlaySampleOn"), {channel, name});
    if (r.type() == QDBusMessage::ErrorMessage) { m_lastError = r.errorMessage(); Q_EMIT lastErrorChanged(); return false; }
    m_lastError.clear(); return true;
}
bool kmixdeck::frontend::MixerClient::stopSample(const QString &name) {
    const QDBusMessage r = callMixer(QStringLiteral("StopSample"), {name});
    if (r.type() == QDBusMessage::ErrorMessage) { m_lastError = r.errorMessage(); Q_EMIT lastErrorChanged(); return false; }
    m_lastError.clear(); return true;
}
bool kmixdeck::frontend::MixerClient::setSampleGain(const QString &channel, const QString &name, double gain) {
    const QDBusMessage r = callMixer(QStringLiteral("SetSampleGain"), {channel, name, gain});
    if (r.type() == QDBusMessage::ErrorMessage) { m_lastError = r.errorMessage(); Q_EMIT lastErrorChanged(); return false; }
    m_lastError.clear(); return true;
}
