// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "kdeintegration.h"
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KNotification>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QStyleHints>
#include <QTimer>
#include <QProcess>
#include <algorithm>
#include <cmath>
#ifdef KMIXDECK_LAYER_SHELL
#include <LayerShellQt/Window>
#include <QScreen>
#include <wayland-client.h>
#endif

namespace kmixdeck::frontend {

#ifdef KMIXDECK_LAYER_SHELL
namespace {
// LayerShellQt cannot be asked whether the compositor has wlr-layer-shell: without it, it logs a warning and the
// window silently becomes a normal toplevel, which Hyprland tiles over half the screen. One registry round trip on a
// private queue answers it without touching Qt's own event queue.
bool compositorHasLayerShell() {
    auto *wl = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wl || !wl->display()) return false;
    wl_display *display = wl->display();
    wl_event_queue *queue = wl_display_create_queue(display);
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
    wl_registry *registry = wl_display_get_registry(wrapper);
    static const wl_registry_listener listener = {
        [](void *found, wl_registry *, uint32_t, const char *interface, uint32_t) {
            if (qstrcmp(interface, "zwlr_layer_shell_v1") == 0) *static_cast<bool *>(found) = true;
        },
        [](void *, wl_registry *, uint32_t) {},
    };
    bool found = false;
    wl_registry_add_listener(registry, &listener, &found);
    wl_display_roundtrip_queue(display, queue);
    wl_registry_destroy(registry);
    wl_proxy_wrapper_destroy(wrapper);
    wl_event_queue_destroy(queue);
    return found;
}
} // namespace
#endif

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
    connect(m_client, &MixerClient::mixChanged, this, [this](const QString &) { rebuildTrayMenu(); });   // mute state / listening mix
    connect(m_client, &MixerClient::cellChanged, this, [this](const QString &, const QString &) { updateTrayIcon(); });
    connect(m_client, &MixerClient::serviceAvailableChanged, this, [this] { updateTrayIcon(); });
    rebuildActions(); rebuildTrayMenu(); updateTrayIcon();
}

// UX-17 (ADR 0010 D4): left click → the overview popover, double-click → the full window. KStatusNotifierItem has no
// double-click signal: activateRequested fires per click, so a click starts a short timer; a second click inside the
// double-click interval cancels it and raises the window instead. The associated-window toggle is disabled on purpose
// — it would hide the window on the very click that should open the popover.
void KdeIntegration::setMainWindow(QQuickWindow *w) {
    m_window = w;
    m_tray->setAssociatedWindow(nullptr);
    m_clickTimer.setSingleShot(true); m_clickTimer.setInterval(QGuiApplication::styleHints()->mouseDoubleClickInterval());
    connect(&m_clickTimer, &QTimer::timeout, this, [this] { showPopover(m_clickPos); });
    connect(m_tray, &KStatusNotifierItem::activateRequested, this, [this](bool, const QPoint &pos) {
        if (m_clickTimer.isActive()) { m_clickTimer.stop(); if (m_window) QMetaObject::invokeMethod(m_window, "raiseFromTray"); return; }
        m_clickPos = pos; m_clickTimer.start();
    });
    connect(m_tray, &KStatusNotifierItem::secondaryActivateRequested, this, [this](const QPoint &) {   // middle click: mute/unmute the listening mix
        const QString cur = listeningMix(); if (!cur.isEmpty()) m_client->toggleMixMute(cur);
    });
}
// UX-17 on Wayland (ADR 0014 HY-2). The Qt.Popup path is an xdg_popup, and that needs an input serial of one of OUR
// surfaces; a click on the tray belongs to the panel. QtWayland refuses ("Failed to create grabbing popup") and nothing
// appears, measured 2026-10-05 under Hyprland 0.56 and KWin 6.7 alike. A wlr-layer-shell surface needs no serial.
// Anchored to the screen edge nearest the click with exclusive zone 0, it is kept clear of the bar by the protocol
// itself. Without layer-shell the click opens the window: never a tiled "popover".
void KdeIntegration::showPopover(const QPoint &pos) {
    if (!m_window) return;
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        QMetaObject::invokeMethod(m_window, "showTrayOverview", Q_ARG(QVariant, pos.x()), Q_ARG(QVariant, pos.y()));
        return;
    }
#ifdef KMIXDECK_LAYER_SHELL
    if (m_layerShell < 0) m_layerShell = compositorHasLayerShell() ? 1 : 0;
    auto *pop = m_window->property("trayOverview").value<QQuickWindow *>();
    if (pop && m_layerShell == 1) {
        if (pop->isVisible()) { pop->close(); return; }   // no grab on a layer surface: a second click closes it
        QScreen *screen = QGuiApplication::screenAt(pos);
        if (!screen) screen = QGuiApplication::primaryScreen();
        const QRect g = screen->geometry();
        const bool known = !pos.isNull() && g.contains(pos);   // some tray hosts send (0,0)
        const bool bottom = known && pos.y() > g.center().y();
        const int gap = 8;
        using LS = LayerShellQt::Window;
        LS::Anchors anchors = bottom ? LS::AnchorBottom : LS::AnchorTop;
        QMargins margins(0, bottom ? 0 : gap, 0, bottom ? gap : 0);
        if (known) {
            anchors |= LS::AnchorLeft;
            margins.setLeft(std::clamp(pos.x() - g.x() - pop->width() / 2, 0, std::max(0, g.width() - pop->width())));
        } else {
            anchors |= LS::AnchorRight;
            margins.setRight(gap);
        }
        LS *ls = LS::get(pop);
        ls->setScope(QStringLiteral("kmixdeck-tray"));   // the layer namespace: `hyprctl layers`, Hyprland layer rules
        ls->setLayer(LS::LayerTop);
        ls->setKeyboardInteractivity(LS::KeyboardInteractivityOnDemand);
        ls->setExclusiveZone(0);
        ls->setAnchors(anchors);
        ls->setMargins(margins);
        ls->setScreen(screen);
        pop->setFlags(Qt::FramelessWindowHint);   // not Qt::Popup: that is the xdg_popup path that fails
        pop->show();
        pop->requestActivate();
        return;
    }
#endif
    QMetaObject::invokeMethod(m_window, "raiseFromTray");
}
void KdeIntegration::trayClick(const QPoint &pos) { m_tray->activate(pos); }
QStringList KdeIntegration::trayMenuTexts() const {
    QStringList out;
    for (QAction *a : m_trayMenu->actions()) {
        if (a->isSeparator()) continue;
        out << (a->isCheckable() ? (a->isChecked() ? QStringLiteral("[x] ") : QStringLiteral("[ ] ")) : QString()) + a->text().remove(QLatin1Char('&'));
    }
    return out;
}   // test hook (--gesture trayclick)

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
        auto *a = new QAction(i18n("Mute mix: %1", m_client->mixName(slug)), this);
        a->setObjectName(QStringLiteral("mute-mix-") + slug);
        connect(a, &QAction::triggered, this, [this, slug] {
            const bool willMute = !m_client->mixMuted(slug);   // master mute (MX-6): outputs AND capture source
            m_client->toggleMixMute(slug);
            notifyMute(i18n("mix %1", m_client->mixName(slug)), willMute);
        });
        KGlobalAccel::self()->setGlobalShortcut(a, QList<QKeySequence>{});
        m_mixMuteActions.insert(slug, a);
        // CT-1 volume up/down: master of the mix in 3 dB steps, cubic domain like the slider
        auto step = [this, slug](double db) {
            const double lin = std::pow(m_client->mixVolume(slug), 3.0);
            const double next = std::clamp(lin * std::pow(10.0, db / 20.0), 0.0, 1.0);
            m_client->setMixVolume(slug, std::cbrt(next));
        };
        auto *up = new QAction(i18n("Mix %1: volume up", m_client->mixName(slug)), this);
        up->setObjectName(QStringLiteral("volume-up-mix-") + slug);
        connect(up, &QAction::triggered, this, [step] { step(+3.0); });
        KGlobalAccel::self()->setGlobalShortcut(up, QList<QKeySequence>{});
        m_mixUpActions.insert(slug, up);
        auto *down = new QAction(i18n("Mix %1: volume down", m_client->mixName(slug)), this);
        down->setObjectName(QStringLiteral("volume-down-mix-") + slug);
        connect(down, &QAction::triggered, this, [step] { step(-3.0); });
        KGlobalAccel::self()->setGlobalShortcut(down, QList<QKeySequence>{});
        m_mixDownActions.insert(slug, down);
    }
    for (auto *map : {&m_mixMuteActions, &m_mixUpActions, &m_mixDownActions})
        for (auto it = map->begin(); it != map->end();) {
            if (!mixes.contains(it.key())) { KGlobalAccel::self()->removeAllShortcuts(it.value()); it.value()->deleteLater(); it = map->erase(it); } else ++it;
        }
    if (!m_listenNextAction) {
        // UX-2 / CT-1 "switch monitoring mix": move the headphones (= the output the current mix plays to) to the next mix
        m_listenNextAction = new QAction(i18n("Listen to next mix"), this);
        m_listenNextAction->setObjectName(QStringLiteral("listen-next-mix"));
        connect(m_listenNextAction, &QAction::triggered, this, &KdeIntegration::listenNext);
        KGlobalAccel::self()->setGlobalShortcut(m_listenNextAction, QList<QKeySequence>{});
    }
}

QString KdeIntegration::listeningMix() const {
    for (const auto &m : m_client->mixSlugs())
        if (!m_client->mixOutputDevice(m).isEmpty() && m_client->mixOutputPresent(m)) return m;
    return {};
}

void KdeIntegration::listenNext() {
    const QStringList mixes = m_client->mixSlugs();
    if (mixes.size() < 2) return;
    const QString cur = listeningMix();
    if (cur.isEmpty()) return;                                   // nothing on the headphones → nothing to move
    const QString device = m_client->mixOutputDevice(cur);
    const QString next = mixes.at((mixes.indexOf(cur) + 1) % mixes.size());
    // hand the device over: the old mix keeps its OTHER outputs (MX-9), only the headphones travel
    m_client->toggleMixOutput(cur, device);
    if (!m_client->mixOutputs(next).contains(device)) m_client->toggleMixOutput(next, device);
    auto *n = new KNotification(QStringLiteral("muteToggled"), KNotification::CloseOnTimeout, this);
    n->setComponentName(QStringLiteral("kmixdeck"));
    n->setTitle(i18n("Now listening to: %1", m_client->mixName(next)));
    n->setText(m_client->deviceDescription(device));
    n->setIconName(QStringLiteral("audio-headphones"));
    n->sendEvent();
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
        if (auto *ga = m_mixMuteActions.value(slug)) {
            ga->setCheckable(true); ga->setChecked(m_client->mixMuted(slug));
            ga->setIcon(QIcon::fromTheme(m_client->mixMuted(slug) ? QStringLiteral("audio-volume-muted") : QStringLiteral("audio-volume-high")));
            m_trayMenu->addAction(ga);
        }
    }
    // UX-2: "what am I hearing" — the listening mix is checked; picking another one moves the headphones there
    const QString cur = listeningMix();
    if (!cur.isEmpty()) {
        m_trayMenu->addSection(i18n("Listening to"));
        const QString device = m_client->mixOutputDevice(cur);
        for (const QString &slug : m_client->mixSlugs()) {
            auto *a = m_trayMenu->addAction(QIcon::fromTheme(QStringLiteral("audio-headphones")), m_client->mixName(slug));
            a->setCheckable(true); a->setChecked(slug == cur);
            connect(a, &QAction::triggered, this, [this, slug, cur, device] {
                if (slug == cur) return;
                m_client->toggleMixOutput(cur, device);
                if (!m_client->mixOutputs(slug).contains(device)) m_client->toggleMixOutput(slug, device);
            });
        }
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
