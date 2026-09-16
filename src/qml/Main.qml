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
            Kirigami.Action {   // UX-15
                text: i18n("Routing")
                icon.name: "view-list-tree"
                checked: root.pageStack.currentItem === routingPage
                onTriggered: { root.pageStack.clear(); root.pageStack.push(routingPage) }
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
    MixerPage   { id: mixerPage;   visible: false }
    AppsPage    { id: appsPage;    visible: false }
    RoutingPage { id: routingPage; visible: false }
    pageStack.initialPage: mixerPage
    function showMixer(channel, mix) { root.pageStack.clear(); root.pageStack.push(mixerPage); if (channel) mixerPage.highlightCell(channel, mix) }
    function showApps() { root.pageStack.clear(); root.pageStack.push(appsPage) }
    function showRouting() { root.pageStack.clear(); root.pageStack.push(routingPage) }

    // Meters cost CPU in the daemon (ADR 0006): only while the window is actually shown.
    onVisibleChanged: Mixer.metersEnabled = visible
    Component.onCompleted: Mixer.metersEnabled = visible

    AddDialog { id: addDialog }
    // ADR 0009 / DV-18: pick a port subset of a multichannel device as a mix output (or a channel input)
    Kirigami.Dialog {
        id: portDialog
        property string kind: "mix"
        property string slug: ""
        property string node: ""
        property string description: ""
        property string chosen: ""
        title: kind === "mix" ? i18n("Output ports for %1", Mixer.mixName(slug)) : i18n("Input ports for %1", Mixer.channelName(slug))
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        preferredWidth: Kirigami.Units.gridUnit * 28
        padding: Kirigami.Units.largeSpacing
        function openFor(k, s, n, d) { kind = k; slug = s; node = n; description = d; chosen = ""; portPicker.picked = []; portPicker.expanded = true; visible = true }
        PortPicker {
            id: portPicker
            width: parent.width
            node: portDialog.node
            description: portDialog.description
            direction: portDialog.kind
            current: portDialog.chosen
            onRefChosen: (ref, _name) => portDialog.chosen = ref
        }
        onAccepted: {
            if (chosen.length === 0) return
            if (kind === "mix") Mixer.toggleMixOutput(slug, chosen); else Mixer.setChannelDevice(slug, chosen)
        }
    }
    function portPickerOpen(kind, slug, node, description) { portDialog.openFor(kind, slug, node, description) }
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
    IconDialog { id: iconDialog }
    function iconDialogOpen(kind, slug) { iconDialog.open(kind, slug) }
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
    // review hook (--open channel-ports): open the dialog with the first multi-port device expanded and two ports picked
    function addDialogOpenPorts() { addDialog.open("channel"); addDialog.demoPorts = true }

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
