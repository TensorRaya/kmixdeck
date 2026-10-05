// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// ADR 0014 HY-1: global shortcuts through org.freedesktop.portal.GlobalShortcuts, for sessions where nobody owns
// org.kde.kglobalaccel (Hyprland, Sway, GNOME …). Same action ids as the KGlobalAccel path (QAction::objectName:
// mute-channel-<slug>, …), so a bind written once — `hl.dsp.global("org.kmixdeck.kmixdeck:mute-channel-game")` —
// keeps working across restarts and across a channel being added.
//
// The portal allows ONE BindShortcuts per session; when the set of actions changes (a channel added), the session is
// closed and a new one bound. Every call is asynchronous: at login the portal may still be starting, and a blocking
// call there would freeze the window.
//
// Measured in nested Hyprland 0.56.2 with XDPH 1.4.1 / xdg-desktop-portal 1.22.1 (2026-10-05): Registry.Register →
// CreateSession → BindShortcuts → `hyprctl globalshortcuts` lists the ids → a key bound with hl.dsp.global →
// Activated(id) → the action.
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QDBusObjectPath>
#include <QDBusMessage>
#include <functional>

class QAction;

namespace kmixdeck::frontend {

class PortalShortcuts : public QObject {
    Q_OBJECT
public:
    explicit PortalShortcuts(QObject *parent = nullptr);
    // KWin (Plasma) owns org.kde.kglobalaccel; everywhere else the portal has to do it
    static bool kglobalaccelAvailable();
    static bool portalAvailable();
    // the full current set; bound shortly after the last change (one rebuild touches many actions)
    void setActions(const QList<QAction *> &actions);

private Q_SLOTS:
    void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp, const QVariantMap &options);
    void onCreateSessionResponse(uint response, const QVariantMap &results);
    void onBindResponse(uint response, const QVariantMap &results);

private:
    void rebind();
    void fail(const char *what, const QString &why);
    QStringList currentIds() const;
    QString requestPath(const QString &token) const;
    void listen(const QString &token, const char *slot);
    void unlisten();
    void callAsync(const QString &iface, const QString &path, const QString &method, const QVariantList &args,
                   std::function<void(const QDBusMessage &)> onReply);

    QHash<QString, QPointer<QAction>> m_actions;   // id → action
    QStringList m_boundIds;
    QStringList m_bindingIds;                      // the set a running BindShortcuts carries
    QString m_session;
    QString m_request;                             // Request path we listen on, "" when none
    const char *m_requestSlot = nullptr;
    QTimer m_debounce;
    int m_token = 0;
    bool m_registered = false;
    bool m_busy = false;                           // a Register / CreateSession / BindShortcuts is in flight
};

} // namespace kmixdeck::frontend
