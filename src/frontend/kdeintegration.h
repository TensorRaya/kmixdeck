// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
// KDE integration for the frontend: global shortcuts (KGlobalAccel, works on Wayland), system tray
// (KStatusNotifierItem), notifications (KNotification). Everything here is a thin client of MixerClient —
// the daemon does the work, this maps KDE events onto D-Bus calls (CT-1, CT-4).
#include <QObject>
#include <QTimer>
#include <QPoint>
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
    void trayClick(const QPoint &pos);
    QStringList trayMenuTexts() const;   // test hook: the context menu's entries, "[x] " prefix when checked   // simulates a tray click (tests: one = popover, two within the interval = window)

private:
    void rebuildActions();              // one global-shortcut action per channel + per mix, kept in sync with the layout
    void rebuildTrayMenu();
    void updateTrayIcon();
    void notifyMute(const QString &what, bool muted);

    MixerClient *m_client;
    KStatusNotifierItem *m_tray;
    QMenu *m_trayMenu;
    QHash<QString, QAction *> m_channelMuteActions;   // slug → action
    QHash<QString, QAction *> m_mixMuteActions;       // mix slug → Mix.ToggleMute (master, MX-6)
    QHash<QString, QAction *> m_mixUpActions, m_mixDownActions;   // mix slug → master ±3 dB (CT-1 "volume up/down")
    QAction *m_listenNextAction = nullptr;            // UX-2/CT-1 "switch monitoring mix": the headphones follow
    void listenNext();
    QString listeningMix() const;
    QTimer m_clickTimer; QPoint m_clickPos;                     // the mix currently on the user's headphones (first with a present output), "" if none
    QPointer<QQuickWindow> m_window;
};

} // namespace kmixdeck::frontend
