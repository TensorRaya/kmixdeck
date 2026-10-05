// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "portalshortcuts.h"
#include "logging.h"
#include <QAction>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QGuiApplication>

namespace {
const QString kPortal = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kIface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");

struct Shortcut {
    QString id;
    QVariantMap props;
};
using Shortcuts = QList<Shortcut>;

QDBusArgument &operator<<(QDBusArgument &a, const Shortcut &s) {
    a.beginStructure();
    a << s.id << s.props;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, Shortcut &s) {
    a.beginStructure();
    a >> s.id >> s.props;
    a.endStructure();
    return a;
}

bool serviceReachable(const QString &name) {
    // activatable counts too: the bus starts the service on the first call (kglobalacceld under Plasma on X11,
    // xdg-desktop-portal everywhere)
    auto *bus = QDBusConnection::sessionBus().interface();
    return bus && (bus->isServiceRegistered(name) || bus->activatableServiceNames().value().contains(name));
}
} // namespace

Q_DECLARE_METATYPE(Shortcut)
Q_DECLARE_METATYPE(Shortcuts)

namespace kmixdeck::frontend {

PortalShortcuts::PortalShortcuts(QObject *parent) : QObject(parent) {
    qDBusRegisterMetaType<Shortcut>();
    qDBusRegisterMetaType<Shortcuts>();
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_debounce, &QTimer::timeout, this, &PortalShortcuts::rebind);
    QDBusConnection::sessionBus().connect(kPortal, kPath, kIface, QStringLiteral("Activated"), this,
                                          SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
}

bool PortalShortcuts::kglobalaccelAvailable() { return serviceReachable(QStringLiteral("org.kde.kglobalaccel")); }
bool PortalShortcuts::portalAvailable() { return serviceReachable(kPortal); }

void PortalShortcuts::setActions(const QList<QAction *> &actions) {
    m_actions.clear();
    for (QAction *a : actions)
        if (a && !a->objectName().isEmpty()) m_actions.insert(a->objectName(), a);
    m_debounce.start();
}

QStringList PortalShortcuts::currentIds() const {
    QStringList ids = m_actions.keys();
    ids.sort();
    return ids;
}

QString PortalShortcuts::requestPath(const QString &token) const {
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

// A portal call answers through a Request object's Response signal, on a path made of our unique name and the
// handle_token we pass. Subscribe BEFORE calling, so a fast answer cannot be missed.
void PortalShortcuts::listen(const QString &token, const char *slot) {
    unlisten();
    m_request = requestPath(token);
    m_requestSlot = slot;
    QDBusConnection::sessionBus().connect(kPortal, m_request, kRequest, QStringLiteral("Response"), this, slot);
}

void PortalShortcuts::unlisten() {
    if (m_request.isEmpty()) return;
    QDBusConnection::sessionBus().disconnect(kPortal, m_request, kRequest, QStringLiteral("Response"), this,
                                             m_requestSlot);
    m_request.clear();
    m_requestSlot = nullptr;
}

void PortalShortcuts::callAsync(const QString &iface, const QString &path, const QString &method,
                                const QVariantList &args, std::function<void(const QDBusMessage &)> onReply) {
    QDBusMessage msg = QDBusMessage::createMethodCall(kPortal, path, iface, method);
    msg.setArguments(args);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 30000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [w, onReply = std::move(onReply)] {
        w->deleteLater();
        if (onReply) onReply(w->reply());
    });
}

void PortalShortcuts::fail(const char *what, const QString &why) {
    qCWarning(lcFrontend, "global shortcuts through the portal: %s failed: %s", what, qPrintable(why));
    unlisten();
    m_busy = false;
    m_bindingIds.clear();
}

void PortalShortcuts::rebind() {
    if (m_busy) { m_debounce.start(); return; }   // picked up again when the running step is done
    if (currentIds() == m_boundIds && !m_session.isEmpty()) return;
    m_busy = true;

    // The portal identifies a host (non-Flatpak) app by its D-Bus connection; without a registered app id
    // xdg-desktop-portal ≥ 1.19 refuses GlobalShortcuts ("An app id is required"). Qt ≥ 6.9 registers on its own
    // when desktopFileName is set; doing it here as well makes the order explicit and covers older Qt. A second
    // Register from the same connection is refused — harmless, the first one counts.
    if (!m_registered) {
        callAsync(QStringLiteral("org.freedesktop.host.portal.Registry"), kPath, QStringLiteral("Register"),
                  {QGuiApplication::desktopFileName(), QVariantMap()}, [this](const QDBusMessage &reply) {
                      if (reply.type() == QDBusMessage::ErrorMessage)
                          qCDebug(lcFrontend, "portal Registry.Register: %s", qPrintable(reply.errorMessage()));
                      m_registered = true;
                      m_busy = false;
                      rebind();
                  });
        return;
    }
    if (!m_session.isEmpty()) {   // one BindShortcuts per session: close it, bind the new set on a fresh one
        callAsync(QStringLiteral("org.freedesktop.portal.Session"), m_session, QStringLiteral("Close"), {}, nullptr);
        m_session.clear();
    }
    const QString token = QStringLiteral("kmixdeck_cs%1").arg(++m_token);
    listen(token, SLOT(onCreateSessionResponse(uint, QVariantMap)));
    callAsync(kIface, kPath, QStringLiteral("CreateSession"),
              {QVariantMap{{QStringLiteral("handle_token"), token},
                           {QStringLiteral("session_handle_token"), QStringLiteral("kmixdeck%1").arg(m_token)}}},
              [this](const QDBusMessage &reply) {
                  if (reply.type() == QDBusMessage::ErrorMessage) fail("CreateSession", reply.errorMessage());
              });
}

void PortalShortcuts::onCreateSessionResponse(uint response, const QVariantMap &results) {
    unlisten();
    if (response != 0) { fail("CreateSession", QStringLiteral("response %1").arg(response)); return; }
    m_session = results.value(QStringLiteral("session_handle")).toString();
    m_bindingIds = currentIds();
    Shortcuts list;
    for (const QString &id : std::as_const(m_bindingIds)) {
        const QAction *a = m_actions.value(id);
        if (!a) continue;
        list.append({id, {{QStringLiteral("description"), a->text().remove(QLatin1Char('&'))}}});
    }
    const QString token = QStringLiteral("kmixdeck_bs%1").arg(++m_token);
    listen(token, SLOT(onBindResponse(uint, QVariantMap)));
    callAsync(kIface, kPath, QStringLiteral("BindShortcuts"),
              {QVariant::fromValue(QDBusObjectPath(m_session)), QVariant::fromValue(list), QString(),
               QVariantMap{{QStringLiteral("handle_token"), token}}},
              [this](const QDBusMessage &reply) {
                  if (reply.type() == QDBusMessage::ErrorMessage) fail("BindShortcuts", reply.errorMessage());
              });
}

void PortalShortcuts::onBindResponse(uint response, const QVariantMap &) {
    unlisten();
    if (response != 0) { fail("BindShortcuts", QStringLiteral("response %1").arg(response)); return; }
    m_boundIds = m_bindingIds;
    m_bindingIds.clear();
    m_busy = false;
    qCInfo(lcFrontend, "global shortcuts bound through the portal: %lld", static_cast<long long>(m_boundIds.size()));
    if (currentIds() != m_boundIds) m_debounce.start();   // the set changed while we were binding
}

void PortalShortcuts::onActivated(const QDBusObjectPath &session, const QString &id, qulonglong, const QVariantMap &) {
    if (session.path() != m_session) return;
    if (QAction *a = m_actions.value(id)) a->trigger();
}

} // namespace kmixdeck::frontend
