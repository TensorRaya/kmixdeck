// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "kdeintegration.h"
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KNotification>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QProcess>

namespace kmixdeck::frontend {

KdeIntegration::KdeIntegration(MixerClient *client, QObject *parent)
    : QObject(parent), m_client(client),
      m_tray(new KStatusNotifierItem(QStringLiteral("kmixdeck"), this)),
      m_trayMenu(new QMenu) {
    m_tray->setTitle(i18n("kmixdeck"));
    m_tray->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_tray->setStatus(KStatusNotifierItem::Active);
    m_tray->setIconByName(QStringLiteral("audio-card"));
    m_tray->setContextMenu(m_trayMenu);
    // left click: raise/hide the window (KSNI default when a window is associated)
    connect(m_client, &MixerClient::layoutChanged, this, [this] { rebuildActions(); rebuildTrayMenu(); updateTrayIcon(); });
    connect(m_client, &MixerClient::channelChanged, this, [this](const QString &) { rebuildTrayMenu(); updateTrayIcon(); });
    connect(m_client, &MixerClient::cellChanged, this, [this](const QString &, const QString &) { updateTrayIcon(); });
    connect(m_client, &MixerClient::serviceAvailableChanged, this, [this] { updateTrayIcon(); });
    rebuildActions(); rebuildTrayMenu(); updateTrayIcon();
}

void KdeIntegration::setMainWindow(QQuickWindow *w) { m_window = w; m_tray->setAssociatedWindow(w); }

void KdeIntegration::rebuildActions() {
    // Global shortcuts are identified by (component = app name, action objectName). Keep names stable per slug so
    // the user's key assignment (stored by kglobalacceld) survives restarts and renames (DV-7 for hotkeys).
    const QStringList channels = m_client->channelSlugs();
    for (const QString &slug : channels) {
        if (m_channelMuteActions.contains(slug)) { m_channelMuteActions[slug]->setText(i18n("Mute channel: %1", m_client->channelName(slug))); continue; }
        auto *a = new QAction(i18n("Mute channel: %1", m_client->channelName(slug)), this);
        a->setObjectName(QStringLiteral("mute-channel-") + slug);
        a->setProperty("componentDisplayName", i18n("kmixdeck"));
        connect(a, &QAction::triggered, this, [this, slug] {
            m_client->toggleChannelMute(slug);
            notifyMute(m_client->channelName(slug), !m_client->channelMuted(slug));
        });
        KGlobalAccel::self()->setGlobalShortcut(a, QList<QKeySequence>{});   // no default key; user assigns in System Settings → Shortcuts → kmixdeck
        m_channelMuteActions.insert(slug, a);
    }
    for (auto it = m_channelMuteActions.begin(); it != m_channelMuteActions.end();) {
        if (!channels.contains(it.key())) { KGlobalAccel::self()->removeAllShortcuts(it.value()); it.value()->deleteLater(); it = m_channelMuteActions.erase(it); } else ++it;
    }
    const QStringList mixes = m_client->mixSlugs();
    for (const QString &slug : mixes) {
        if (m_mixMuteActions.contains(slug)) { m_mixMuteActions[slug]->setText(i18n("Mute all in mix: %1", m_client->mixName(slug))); continue; }
        auto *a = new QAction(i18n("Mute all in mix: %1", m_client->mixName(slug)), this);
        a->setObjectName(QStringLiteral("mute-mix-") + slug);
        connect(a, &QAction::triggered, this, [this, slug] {
            // "mute the mix" = mute every cell feeding it; unmute if all were muted
            bool allMuted = true; for (const auto &ch : m_client->channelSlugs()) if (m_client->cellPresent(ch, slug) && !m_client->cellMuted(ch, slug)) allMuted = false;
            for (const auto &ch : m_client->channelSlugs()) if (m_client->cellPresent(ch, slug)) m_client->setCellMuted(ch, slug, !allMuted);
            notifyMute(i18n("mix %1", m_client->mixName(slug)), !allMuted);
        });
        KGlobalAccel::self()->setGlobalShortcut(a, QList<QKeySequence>{});
        m_mixMuteActions.insert(slug, a);
    }
    for (auto it = m_mixMuteActions.begin(); it != m_mixMuteActions.end();) {
        if (!mixes.contains(it.key())) { KGlobalAccel::self()->removeAllShortcuts(it.value()); it.value()->deleteLater(); it = m_mixMuteActions.erase(it); } else ++it;
    }
}

void KdeIntegration::rebuildTrayMenu() {
    m_trayMenu->clear();
    m_trayMenu->addSection(i18n("Channels"));
    for (const QString &slug : m_client->channelSlugs()) {
        auto *a = m_trayMenu->addAction(m_client->channelName(slug));
        a->setCheckable(true); a->setChecked(m_client->channelMuted(slug));
        a->setIcon(QIcon::fromTheme(m_client->channelMuted(slug) ? QStringLiteral("audio-volume-muted") : QStringLiteral("audio-volume-high")));
        a->setToolTip(i18n("Toggle mute for %1 in every mix", m_client->channelName(slug)));
        connect(a, &QAction::triggered, this, [this, slug] { m_client->toggleChannelMute(slug); });
    }
    m_trayMenu->addSection(i18n("Mixes"));
    for (const QString &slug : m_client->mixSlugs()) {
        if (auto *ga = m_mixMuteActions.value(slug)) m_trayMenu->addAction(ga);
    }
    m_trayMenu->addSeparator();
    auto *shortcuts = m_trayMenu->addAction(QIcon::fromTheme(QStringLiteral("configure-shortcuts")), i18n("Configure Shortcuts…"));
    connect(shortcuts, &QAction::triggered, this, [] {
        // KDE's own shortcut KCM lists our component; no custom dialog needed
        QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_keys")});
    });
    // KStatusNotifierItem adds its own "Quit" entry to the context menu.
}

void KdeIntegration::updateTrayIcon() {
    if (!m_client->serviceAvailable()) { m_tray->setIconByName(QStringLiteral("audio-card-symbolic")); m_tray->setStatus(KStatusNotifierItem::Passive); m_tray->setToolTip(QStringLiteral("audio-card"), i18n("kmixdeck"), i18n("Service not running")); return; }
    int muted = 0; const auto channels = m_client->channelSlugs();
    for (const auto &c : channels) if (m_client->channelMuted(c)) ++muted;
    m_tray->setStatus(KStatusNotifierItem::Active);
    m_tray->setIconByName(muted == 0 ? QStringLiteral("audio-volume-high") : (muted == channels.size() ? QStringLiteral("audio-volume-muted") : QStringLiteral("audio-volume-medium")));
    m_tray->setToolTip(QStringLiteral("audio-card"), i18n("kmixdeck"),
                       muted == 0 ? i18n("%1 channels, %2 mixes", channels.size(), m_client->mixSlugs().size()) : i18np("%1 channel muted", "%1 channels muted", muted));
}

void KdeIntegration::notifyMute(const QString &what, bool muted) {
    auto *n = new KNotification(QStringLiteral("muteToggled"), KNotification::CloseOnTimeout, this);
    n->setComponentName(QStringLiteral("kmixdeck"));
    n->setTitle(i18n("kmixdeck"));
    n->setText(muted ? i18n("%1 muted", what) : i18n("%1 unmuted", what));
    n->setIconName(muted ? QStringLiteral("audio-volume-muted") : QStringLiteral("audio-volume-high"));
    n->sendEvent();
}

} // namespace kmixdeck::frontend
