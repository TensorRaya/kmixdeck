// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.ApplicationWindow {
    id: root
    title: i18n("kmixdeck")
    minimumWidth: Kirigami.Units.gridUnit * 40
    minimumHeight: Kirigami.Units.gridUnit * 24
    width: Kirigami.Units.gridUnit * 64
    height: Kirigami.Units.gridUnit * 36

    globalDrawer: Kirigami.GlobalDrawer {
        title: i18n("kmixdeck")
        titleIcon: "audio-card"
        isMenu: true
        actions: [
            Kirigami.Action {
                text: i18n("Add channel…")
                icon.name: "list-add"
                onTriggered: root.addDialogOpen("channel")
            },
            Kirigami.Action {
                text: i18n("Add mix…")
                icon.name: "list-add"
                onTriggered: root.addDialogOpen("mix")
            },
            Kirigami.Action { separator: true },
            Kirigami.Action {
                text: i18n("Mixer")
                icon.name: "view-media-equalizer"
                checked: root.pageStack.currentItem === mixerPage
                onTriggered: { root.pageStack.clear(); root.pageStack.push(mixerPage) }
            },
            Kirigami.Action {
                text: i18n("Applications")
                icon.name: "applications-multimedia"
                checked: root.pageStack.currentItem === appsPage
                onTriggered: { root.pageStack.clear(); root.pageStack.push(appsPage) }
            },
            Kirigami.Action { separator: true },
            Kirigami.Action {
                text: i18n("About kmixdeck")
                icon.name: "help-about"
                onTriggered: root.pageStack.pushDialogLayer(Qt.createComponent("org.kde.kirigami", "AboutPage"))
            }
        ]
    }

    // Pages are kept alive so switching does not lose scroll/fader state.
    MixerPage { id: mixerPage; visible: false }
    AppsPage  { id: appsPage;  visible: false }
    pageStack.initialPage: mixerPage

    // Meters cost CPU in the daemon (ADR 0006): only while the window is actually shown.
    onVisibleChanged: Mixer.metersEnabled = visible
    Component.onCompleted: Mixer.metersEnabled = visible

    AddDialog { id: addDialog }
    Connections { target: Mixer; function onErrorOccurred(message) { root.showPassiveNotification(message, "long") } }
    // CH-9: a removal is never a dead end — the toast carries the way back.
    Connections {
        target: Mixer
        function onUndoChanged() {
            if (Mixer.undoDescription.length > 0)
                root.showPassiveNotification(i18n("Removed %1", Mixer.undoDescription), "long", i18n("Undo"), () => Mixer.undo())
        }
    }
    Shortcut { sequences: [StandardKey.Undo]; enabled: Mixer.undoDescription.length > 0; onActivated: Mixer.undo() }
    RenameDialog { id: renameDialog }
    function renameDialogOpen(kind, slug) { renameDialog.open(kind, slug) }
    // FX panel opens as a dialog layer over the matrix — narrow windows keep the grid behind them.
    function fxPanelOpen(kind, slug) {
        const page = fxPanelComp.createObject(this, {kind: kind, slug: slug,
            title: kind === "mix" ? Mixer.mixName(slug) : Mixer.channelName(slug)})
        root.pageStack.pushDialogLayer(page)
    }
    Component {
        id: fxPanelComp
        FxPanel {}
    }
    function addDialogOpen(kind) { addDialog.open(kind) }

    footer: QQC2.ToolBar {
        visible: !Mixer.serviceAvailable || !Mixer.connected
        contentItem: Kirigami.InlineMessage {
            visible: true
            type: Kirigami.MessageType.Error
            text: !Mixer.serviceAvailable ? i18n("The kmixdeck service (kmixdeckd) is not running.")
                                          : i18n("kmixdeckd has no connection to PipeWire.")
        }
    }
}
