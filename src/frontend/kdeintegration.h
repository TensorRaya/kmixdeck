// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// KDE integration for the frontend: global shortcuts (KGlobalAccel, works on Wayland), system tray
// (KStatusNotifierItem), notifications (KNotification). Everything here is a thin client of MixerClient —
// the daemon does the work, this maps KDE events onto D-Bus calls (CT-1, CT-4).
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QAction>
#include <QMenu>
#include <KStatusNotifierItem>
#include "mixerclient.h"

class QQuickWindow;

namespace kmixdeck::frontend {

class KdeIntegration : public QObject {
    Q_OBJECT
public:
    explicit KdeIntegration(MixerClient *client, QObject *parent = nullptr);
    void setMainWindow(QQuickWindow *w);

private:
    void rebuildActions();              // one global-shortcut action per channel + per mix, kept in sync with the layout
    void rebuildTrayMenu();
    void updateTrayIcon();
    void notifyMute(const QString &what, bool muted);

    MixerClient *m_client;
    KStatusNotifierItem *m_tray;
    QMenu *m_trayMenu;
    QHash<QString, QAction *> m_channelMuteActions;   // slug → action
    QHash<QString, QAction *> m_mixMuteActions;       // mix slug → "mute everything in mix" (all cells of that mix)
    QPointer<QQuickWindow> m_window;
};

} // namespace kmixdeck::frontend
