// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
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
            Kirigami.Action {   // DV-24
                text: i18n("Patchbay")
                icon.name: "applications-utilities"
                checked: root.pageStack.currentItem === patchBayPage
                onTriggered: { root.pageStack.clear(); root.pageStack.push(patchBayPage) }
            },
            Kirigami.Action { separator: true },
            Kirigami.Action {   // CH-11
                objectName: "hiddenDevicesAction"
                text: i18np("Hidden device… (%1)", "Hidden devices… (%1)", Mixer.hiddenDevices.length)
                icon.name: "view-hidden"
                enabled: Mixer.hiddenDevices.length > 0
                onTriggered: hiddenDevicesDialog.open()
            },
            Kirigami.Action {   // CT-7
                objectName: "exportAction"
                text: i18n("Export settings…")
                icon.name: "document-export"
                onTriggered: exportDialog.open()
            },
            Kirigami.Action {   // CT-7
                objectName: "importAction"
                text: i18n("Import settings…")
                icon.name: "document-import"
                onTriggered: importDialog.open()
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
    AppsPage    { id: appsPage;     visible: false }
    RoutingPage { id: routingPage;  visible: false }
    // DV-24: Loopback-style patchbay — cards with jacks, wires as S-curves
    PatchBayPage { id: patchBayPage; visible: false }
    pageStack.initialPage: mixerPage
    // UX-17: the tray popover is part of this window's QML so it shares the Mixer singleton (ADR 0010 D2).
    property alias trayOverview: trayOverviewWin
    TrayOverview { id: trayOverviewWin; visible: false }
    function showTrayOverview(x, y) { trayOverviewWin.x = x - trayOverviewWin.width / 2; trayOverviewWin.y = y - trayOverviewWin.height - 8; trayOverviewWin.show(); trayOverviewWin.requestActivate() }
    function raiseFromTray() { root.show(); root.raise(); root.requestActivate() }
    // closing the window keeps the tray alive (the daemon keeps mixing anyway; the icon is the way back)
    onClosing: (close) => { if (Mixer.hideToTray) { close.accepted = false; root.hide() } }
    function showMixer(channel, mix) { root.pageStack.clear(); root.pageStack.push(mixerPage); if (channel) mixerPage.highlightCell(channel, mix) }
    function showApps() { root.pageStack.clear(); root.pageStack.push(appsPage) }
    function showRouting() { root.pageStack.clear(); root.pageStack.push(routingPage) }
    function showPatchbay() { root.pageStack.clear(); root.pageStack.push(patchBayPage) }
    // test hooks for the patchbay gestures (--gesture): same calls the drag / the wire click make
    function gestureConnect(fc, fp, tc, tp) { return Mixer.connectJacks(fc, fp, tc, tp) }
    // --probe "<objectName>.<property>": read a property of any item by objectName (MX-10/UX-10 proofs)
    function probe(spec) {
        const dot = spec.lastIndexOf("."); const name = spec.slice(0, dot), prop = spec.slice(dot + 1)
        function find(item) {
            if (!item) return null
            if (item.objectName === name) return item
            for (let i = 0; i < (item.children ? item.children.length : 0); ++i) { const r = find(item.children[i]); if (r) return r }
            if (item.contentItem && item.contentItem !== item) { const r = find(item.contentItem); if (r) return r }
            if (item.background) { const r = find(item.background); if (r) return r }
            return null
        }
        // Popups live in the window's Overlay, not under contentItem — search it too (DV-14 wire popover).
        // Popups live in the window's Overlay: its popups are not "children" — walk contentChildren + each popup's
        // contentItem/background (DV-14 wire popover; the QQC2.Popup itself carries the objectName).
        function findOverlay() {
            const ov = QQC2.Overlay.overlay; if (!ov) return null
            const kids = ov.contentChildren || ov.children || []
            for (let i = 0; i < kids.length; ++i) {
                const k = kids[i]
                if (k.objectName === name) return k
                if (k.parent && k.parent.objectName === name) return k.parent           // popup's contentItem → the popup
                const r = find(k); if (r) return r
            }
            return null
        }
        const it = find(root.contentItem) || find(root.pageStack) || findOverlay() || (patchBayPage && patchBayPage.probeItem ? patchBayPage.probeItem(name) : null)
                 || (name === "trayOverview" ? trayOverviewWin : find(trayOverviewWin.contentItem))
        if (!it) return "<not found: " + name + ">"
        const v = it[prop]
        return v === undefined ? "<no property " + prop + ">" : String(v)
    }
    function gestureMonitors(on) { if (patchBayPage) patchBayPage.showMonitors = (on === "on" || on === "true" || on === "1"); return "" }
    function gestureDrop(appPath, channel) {
        function find(item) {
            if (!item) return null
            if (item.objectName === "channelDrop/" + channel) return item
            for (let i = 0; i < (item.children ? item.children.length : 0); ++i) { const r = find(item.children[i]); if (r) return r }
            if (item.contentItem && item.contentItem !== item) { const r = find(item.contentItem); if (r) return r }
            return null
        }
        const d = find(root.contentItem)
        if (!d) return "<no drop target for " + channel + ">"
        return d.parent.gestureDrop(appPath)
    }
    // DV-14 probe path: open the wire popover for a device wire as a click on it would (kind input|output).
    function gestureWirePopup(kind, owner, ref) {
        if (!patchBayPage) return "<no patchbay page>"
        const w = kind === "input" ? { kind: "input", channel: owner, ref: ref } : { kind: "output", mix: owner, ref: ref }
        return patchBayPage.openWirePopup(w)
    }
    // UX-14: drive the mixer page's own handlers — the fader's onMoved, the header's Outputs menu entry, the "I hear" box
    function findByName(name) {
        function find(item) {
            if (!item) return null
            if (item.objectName === name) return item
            for (let i = 0; i < (item.children ? item.children.length : 0); ++i) { const r = find(item.children[i]); if (r) return r }
            if (item.contentItem && item.contentItem !== item) { const r = find(item.contentItem); if (r) return r }
            return null
        }
        return find(root.contentItem) || find(root.pageStack)
    }
    function gestureFader(channel, mix, value) {
        const f = findByName("cellFader/" + channel + "/" + mix); if (!f) return "<no fader " + channel + "/" + mix + ">"
        f.value = Number(value); f.moved(); return ""
    }
    function gestureHear(nodeName) { const h = findByName("hearingBar"); return h ? h.pickDevice(nodeName) : "<no hearing bar>" }
    function gestureMixOutput(mix, nodeName) { const h = findByName("mixHeader/" + mix); return h ? h.triggerOutput(nodeName) : "<no mix header " + mix + ">" }
    function gestureExport(path) { root.exportTo(Qt.resolvedUrl("file://" + path)); return Mixer.lastError.length ? Mixer.lastError : "" }
    function gestureImport(path) { root.importFrom(Qt.resolvedUrl("file://" + path)); return Mixer.lastError.length ? Mixer.lastError : "" }
    function gestureMute(kind, slug) {
        const b = findByName((kind === "mix" ? "mixMute/" : "channelMute/") + slug); if (!b) return "<no mute button " + kind + "/" + slug + ">"
        b.toggle(); b.toggled(); return ""
    }
    function gestureRemove(kind, owner, ref) {
        const w = kind === "input" ? { kind: "input", channel: owner, ref: ref } : kind === "output" ? { kind: "output", mix: owner, ref: ref } : kind === "cell" ? { kind: "cell", channel: owner, mix: ref } : { kind: "app", app: owner, channel: ref }
        Mixer.removeWire(w); return ""
    }

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

    // CH-11: bring hidden devices back
    Kirigami.PromptDialog {
        id: hiddenDevicesDialog
        objectName: "hiddenDevicesDialog"
        title: i18n("Hidden devices")
        standardButtons: Kirigami.Dialog.Close
        preferredWidth: Kirigami.Units.gridUnit * 26
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            QQC2.Label { text: i18n("These devices are left out of the pickers. Routing that uses them is untouched."); wrapMode: Text.WordWrap; Layout.fillWidth: true; opacity: 0.7 }
            Repeater {
                model: Mixer.hiddenDevices
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Kirigami.Icon { source: modelData.present ? "audio-card" : "audio-card-symbolic"; opacity: modelData.present ? 1 : 0.5; Layout.preferredWidth: Kirigami.Units.iconSizes.small; Layout.preferredHeight: width }
                    QQC2.Label { text: modelData.description + (modelData.present ? "" : i18n(" (not connected)")); elide: Text.ElideMiddle; Layout.fillWidth: true }
                    QQC2.Button { objectName: "unhide/" + modelData.nodeName; text: i18n("Show again"); icon.name: "view-visible"; onClicked: Mixer.setDeviceHidden(modelData.nodeName, false) }
                }
            }
        }
    }
    function gestureHide(node, hidden) { Mixer.setDeviceHidden(node, hidden === "1" || hidden === "true"); return "" }

    // CT-7 backup / restore — the daemon owns the document, the window only picks the file
    FileDialog {
        id: exportDialog
        title: i18n("Export kmixdeck settings")
        fileMode: FileDialog.SaveFile
        nameFilters: [i18n("kmixdeck settings (*.kmixdeck.json)"), i18n("JSON (*.json)")]
        defaultSuffix: "kmixdeck.json"
        currentFile: "file:///" + StandardPaths.writableLocation(StandardPaths.DocumentsLocation).toString().replace("file:///", "") + "/kmixdeck-" + Qt.formatDate(new Date(), "yyyy-MM-dd") + ".kmixdeck.json"
        onAccepted: root.exportTo(selectedFile)
    }
    FileDialog {
        id: importDialog
        title: i18n("Import kmixdeck settings")
        fileMode: FileDialog.OpenFile
        nameFilters: [i18n("kmixdeck settings (*.kmixdeck.json *.json)")]
        onAccepted: root.importFrom(selectedFile)
    }
    function exportTo(url) {
        const ok = Mixer.exportToFile(url)
        root.showPassiveNotification(ok ? i18n("Settings exported to %1", Mixer.displayPath(url)) : i18n("Export failed: %1", Mixer.lastError), "long")
    }
    function importFrom(url) {
        const ok = Mixer.importFromFile(url)
        root.showPassiveNotification(ok ? i18n("Settings imported from %1", Mixer.displayPath(url)) : i18n("Import failed: %1", Mixer.lastError), "long")
    }
    RenameDialog { id: renameDialog }
    function renameDialogOpen(kind, slug) { renameDialog.open(kind, slug) }
    function duplicateDialogOpen(slug) { renameDialog.open("mix", slug, true) }   // MX-8
    function gestureDuplicate(slug, name) { renameDialog.open("mix", slug, true); renameDialog.commit(name); renameDialog.close(); return "" }
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
