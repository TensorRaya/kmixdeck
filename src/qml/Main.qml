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
            Kirigami.Action {   // UX-3
                objectName: "firstRunAction"
                text: i18n("Set up defaults…")
                icon.name: "tools-wizard"
                onTriggered: { firstRunDialog.done = null; firstRunDialog.open() }
            },
            Kirigami.Action {   // CH-11
                objectName: "hiddenDevicesAction"
                text: i18np("Hidden device… (%1)", "Hidden devices… (%1)", Mixer.hiddenDevices.length)
                icon.name: "view-hidden"
                enabled: Mixer.hiddenDevices.length > 0
                onTriggered: hiddenDevicesDialog.open()
            },
            Kirigami.Action {   // CT-9: save the current mixable state as a named scene
                objectName: "saveSceneAction"
                text: i18n("Save scene…")
                icon.name: "document-save-as"
                onTriggered: { saveSceneDialog.sceneName = ""; saveSceneDialog.open() }
            },
            Kirigami.Action {   // CT-9
                // Opt-in rule (owner 2026-09-18): a feature added after v0.2 is ABSENT by default and the UI
                // hides its controls except the one switch that enables it. For scenes "enabled" simply means
                // "the user saved one", so this submenu does not exist until Scenes is non-empty — and
                // "Save scene…" above is that one always-visible switch.
                // 🔴 The children are built OUTSIDE this block (see sceneMenuBuilder below): a Kirigami.Action
                // accepts only Actions as children, so an Instantiator/Component/Connections parked in here
                // breaks the whole QML load — and a Main.qml that does not load takes EVERY frontend test
                // with it, not just the scene ones (measured 2026-09-19: one mistake, ten red tests).
                id: recallSceneAction
                objectName: "recallSceneAction"
                text: i18np("Recall scene (%1)", "Recall scenes (%1)", Mixer.scenes.length)
                icon.name: "view-presentation"
                visible: Mixer.scenes.length > 0
            },
            Kirigami.Action {   // CT-8: the soundboard panel
                // Without this the panel was reachable ONLY through `kmixdeck --open soundboard` from a shell —
                // there was no way into it from the running window (measured 2026-09-22: nothing called
                // soundboardPanelOpen() except main.cpp). Opt-in rule (owner 2026-09-18): absent until a
                // soundboard channel exists, like "Recall scene" above. No children here — see the warning on
                // recallSceneAction: a non-Action child breaks the whole QML load.
                objectName: "soundboardAction"
                text: i18n("Soundboard…")
                icon.name: "media-playback-start"
                visible: Mixer.soundboardSlugs.length > 0
                onTriggered: applicationWindow().soundboardPanelOpen()
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
    // --self-test hook: force the global drawer to build its actions (incl. the CT-9 scene submenu). A
    // Kirigami.Action that cannot take its children breaks the whole QML load, and nothing opened the
    // drawer during the smoke test before 2026-09-19.
    function openDrawerForSelfTest() { if (root.globalDrawer) { root.globalDrawer.drawerOpen = true; root.rebuildSceneMenu() } return "" }
    // test hooks for the patchbay gestures (--gesture): same calls the drag / the wire click make
    function gestureConnect(fc, fp, tc, tp) { return Mixer.connectJacks(fc, fp, tc, tp) }
    // --probe "<objectName>.<property>": read a property of any item by objectName (MX-10/UX-10 proofs)
    // Wie probe(), aber ab einem beliebigen Item: das Effekt-Panel liegt auf dem Desktop in
    // einem EIGENEN Fenster (pushDialogLayer), dessen contentItem die Shell hier hereingibt.
    function probeIn(startItem, spec) { return startItem ? probe(spec, startItem) : "<probeIn: kein Startpunkt>" }
    function probe(spec, startItem) {
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
        // Kirigami.Actions in the global drawer are NOT in the item tree (find() walks children of visual
        // items), so before CT-9 no test could probe one. Same fallback idea as patchBayPage.probeItem for
        // popups in the Overlay: ask the drawer for its actions by objectName, recursively for submenus.
        function findAction(list) {
            for (let i = 0; i < (list ? list.length : 0); ++i) {
                const a = list[i]; if (!a) continue
                if (a.objectName === name) return a
                const r = findAction(a.children); if (r) return r
            }
            return null
        }
        const it = (startItem ? find(startItem) : null)
                 || find(root.contentItem) || find(root.pageStack) || findOverlay() || (patchBayPage && patchBayPage.probeItem ? patchBayPage.probeItem(name) : null)
                 || (name === "trayOverview" ? trayOverviewWin : find(trayOverviewWin.contentItem))
                 || findAction(root.globalDrawer ? root.globalDrawer.actions : null)
        if (!it) return "<not found: " + name + ">"
        if (prop.startsWith("probe:") && it.probeItem) return String(it.probeItem(prop.slice(6)))   // item-specific diagnostics
        const v = it[prop]
        return v === undefined ? "<no property " + prop + ">" : String(v)
    }
    // Layout check (Michel 2026-09-23: "sehe da teilweise was überlappen"). A picture shows the symptom; this names
    // both items and the overlap in pixels, so a test can assert on it. Collects what a user reads or presses — a
    // text, a button, a dial — and stops there (a button's own background/label are not separate findings). Two
    // kinds of finding: two such items overlap, or one sticks out of the card it belongs to (channel header, mix
    // header, cell). Walks the main window, or the dialog-layer window if one is given.
    function layoutOverlaps(startItem, minPx) {
        const min = minPx || 2
        const out = []
        // Visible part of an item in `start` coordinates: every clipping ancestor cuts it (a ScrollView really hides
        // what scrolled out). null = nothing of it is on screen.
        function sichtbar(it, start) {
            if (it.width <= 0 || it.height <= 0) return null
            const g = it.mapToItem(start, 0, 0)
            let x1 = g.x, y1 = g.y, x2 = g.x + it.width, y2 = g.y + it.height, cutBy = null
            for (let p = it; p; p = p.parent) {
                if (!p.visible || p.opacity === 0) return null
                if (p.clip && p !== it) {
                    const q = p.mapToItem(start, 0, 0)
                    const n1 = Math.max(x1, q.x), m1 = Math.max(y1, q.y), n2 = Math.min(x2, q.x + p.width), m2 = Math.min(y2, q.y + p.height)
                    if (n2 - n1 < 1 || m2 - m1 < 1) return null
                    if (!cutBy && (x2 - x1) - (n2 - n1) + (y2 - y1) - (m2 - m1) >= 2) cutBy = p
                    x1 = n1; y1 = m1; x2 = n2; y2 = m2
                }
                if (p === start) break
            }
            return { x: x1, y: y1, w: x2 - x1, h: y2 - y1, fx: g.x, fy: g.y, fw: it.width, fh: it.height, cutBy: cutBy }
        }
        function name(it) {
            const n = it.objectName || ""
            const txt = (typeof it.text === "string" && it.text.length) ? it.text.slice(0, 28) : ""
            return (n || String(it).split("(")[0]) + (txt ? " \"" + txt + "\"" : "")
        }
        function blatt(it) {
            if (typeof it.text === "string" && it.text.length > 0) return true               // Label, button
            return it.hasOwnProperty("pressed") && it.hasOwnProperty("value")                 // dial, slider
        }
        const karte = /^(channelHeader|mixHeader|cell|trayCard)\//
        const items = []
        function walk(it, fenster, card) {
            if (!it || !it.visible || it.opacity === 0) return
            if (karte.test(it.objectName || "")) card = it
            if (blatt(it)) {
                const r = sichtbar(it, fenster)
                if (r) items.push({ it: it, card: card, x: r.x, y: r.y, w: r.w, h: r.h, fx: r.fx, fy: r.fy, fw: r.fw, fh: r.fh, cutBy: r.cutBy })
                return
            }
            for (let i = 0; i < (it.children ? it.children.length : 0); ++i) walk(it.children[i], fenster, card)
        }
        const start = startItem || root.contentItem
        walk(start, start, null)
        for (let i = 0; i < items.length; ++i) for (let j = i + 1; j < items.length; ++j) {
            const a = items[i], b = items[j]
            const ox = Math.min(a.x + a.w, b.x + b.w) - Math.max(a.x, b.x)
            const oy = Math.min(a.y + a.h, b.y + b.h) - Math.max(a.y, b.y)
            if (ox >= min && oy >= min) out.push(name(a.it) + " @" + Math.round(a.x) + "," + Math.round(a.y) + " ⨯ " + name(b.it) + " @" + Math.round(b.x) + "," + Math.round(b.y) + " = " + Math.round(ox) + "x" + Math.round(oy))
        }
        for (const a of items) {
            if (!a.card) continue
            const c = a.card.mapToItem(start, 0, 0)
            // full size, not the clipped part: a card that clips its own button cuts it in half — that is the finding
            const raus = Math.max(c.x - a.fx, a.fx + a.fw - (c.x + a.card.width), c.y - a.fy, a.fy + a.fh - (c.y + a.card.height))
            if (raus >= min) out.push("OUTSIDE " + name(a.it) + " @" + Math.round(a.fx) + "," + Math.round(a.fy) + " leaves " + a.card.objectName + " by " + Math.round(raus) + " px")
            // cut off INSIDE its own card (the card or something in it clips): half a number is as unreadable as
            // one under a button. A ScrollView outside the card cutting a column at the edge is scrolling, not this.
            else if (a.cutBy) {
                let inCard = false
                for (let p = a.cutBy; p; p = p.parent) if (p === a.card) { inCard = true; break }
                if (inCard) out.push("CLIPPED " + name(a.it) + " @" + Math.round(a.fx) + "," + Math.round(a.fy) + " shows " + Math.round(a.w) + "x" + Math.round(a.h) + " of " + Math.round(a.fw) + "x" + Math.round(a.fh) + " (cut by " + (a.cutBy.objectName || String(a.cutBy).split("(")[0]) + ")")
            }
        }
        // A badge label cut to "M…" says nothing: it must fit, or switch to its symbol (ChannelHeader does that).
        for (const a of items) {
            const p = a.it.parent
            if (a.it.truncated === true && p && /Badge/.test(p.objectName || "")) out.push("ELIDED " + name(a.it) + " in " + p.objectName)
        }
        // Siblings that must line up: every mix header title on one baseline (a band reserved in only one card
        // pushed the Stream title 13 px up, 2026-09-23). Same y for all visible titles of the main window.
        const titles = items.filter(a => /^mixHeaderTitle\//.test(a.it.objectName || ""))
        for (const a of titles) if (Math.abs(a.fy - titles[0].fy) >= min)
            out.push("MISALIGNED " + name(a.it) + " y=" + Math.round(a.fy) + ", " + name(titles[0].it) + " y=" + Math.round(titles[0].fy))
        return out.length + (out.length ? "\n" + out.join("\n") : "")
    }
    // UX-4: the item behind an objectName (visual tree, not QObject parents — findChild() does not see QML items)
    function itemByName(name) {
        function find(item) {
            if (!item) return null
            if (item.objectName === name) return item
            for (let i = 0; i < (item.children ? item.children.length : 0); ++i) { const r = find(item.children[i]); if (r) return r }
            if (item.contentItem && item.contentItem !== item) { const r = find(item.contentItem); if (r) return r }
            if (item.background) { const r = find(item.background); if (r) return r }
            return null
        }
        return find(root.contentItem) || find(root.pageStack) || (patchBayPage && patchBayPage.probeItem ? patchBayPage.probeItem(name) : null)
    }
    function gestureFocus(name) { const it = itemByName(name); if (!it) return "<not found: " + name + ">"; it.forceActiveFocus(); return "" }
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
    // FX-8: das Effekt-Dropdown aufklappen, wie der Nutzer es tut. Eine ComboBox baut ihre
    // Delegates erst beim Oeffnen — ohne diese Geste liefert --probe "<not found>" fuer jeden
    // Eintrag, und die KDE-Seite der Anforderung waere nur behauptet statt geprueft.
    // FX-8: das Effekt-Dropdown aufklappen, wie der Nutzer es tut. Eine ComboBox baut ihre
    // Delegates erst beim Oeffnen — ohne diese Geste liefert --probe leere Werte fuer jeden
    // Eintrag. `wurzel` kommt von der Shell, weil das Panel auf dem Desktop in einem eigenen
    // Fenster liegt (pushDialogLayer) und findByName nur das Hauptfenster kennt.
    function gestureFxAddOpen(wurzel) {
        const b = wurzel ? findIn(wurzel, "fxAddType") : findByName("fxAddType")
        if (!b) return "<no fx add box — is the fx panel open?>"
        b.popup.open()
        return ""
    }
    function findIn(startItem, name) {
        function find(item) {
            if (!item) return null
            if (item.objectName === name) return item
            for (let i = 0; i < (item.children ? item.children.length : 0); ++i) { const r = find(item.children[i]); if (r) return r }
            if (item.contentItem && item.contentItem !== item) { const r = find(item.contentItem); if (r) return r }
            return null
        }
        return find(startItem)
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
        // Auch das Overlay durchsuchen. Alles, was in einem Popup oder DialogLayer sitzt, haengt
        // NICHT unter contentItem — probe() weiss das laengst und hat seinen eigenen
        // Overlay-Walker. findByName hatte ihn nicht, also fand jede Geste, die auf ein Element
        // in einem Dialog zeigt, nichts: "<no fx add box>" bei geoeffnetem Panel (FX-8,
        // 2026-09-21). Statt den Walker zu kopieren die eine Quelle nutzen, sonst driften die
        // beiden Suchen auseinander und der naechste sucht denselben Fehler nochmal.
        const direkt = find(root.contentItem) || find(root.pageStack)
        if (direkt) return direkt
        const ov = QQC2.Overlay.overlay
        const kids = ov ? (ov.contentChildren || ov.children || []) : []
        for (let i = 0; i < kids.length; ++i) {
            const k = kids[i]
            if (k.objectName === name) return k
            if (k.parent && k.parent.objectName === name) return k.parent
            const r = find(k); if (r) return r
        }
        return null
    }

    function gestureFader(channel, mix, value) {
        const f = findByName("cellFader/" + channel + "/" + mix); if (!f) return "<no fader " + channel + "/" + mix + ">"
        f.value = Number(value); f.moved(); return ""
    }
    function gestureTrim(channel, value) {   // CH-7 dial as the user would turn it
        const d = findByName("channelTrim/" + channel); if (!d) return "<no trim dial " + channel + ">"
        d.value = Number(value); d.moved(); return ""
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
    Component.onCompleted: { Mixer.metersEnabled = visible; if (Mixer.firstRun && Mixer.connected && !root.firstRunSuppressed) firstRunDialog.open(); root.rebuildSceneMenu() }   // CT-9 last: a second Component.onCompleted in the same object silently REPLACES this one

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

    // UX-3: first run — opens once when the daemon reports no layout on disk (and can be reopened from the menu)
    FirstRunDialog { id: firstRunDialog }
    property bool firstRunSuppressed: false   // gesture firstrun:skip before the daemon connected must still win
    Connections {
        target: Mixer
        function onFirstRunChanged() { if (Mixer.firstRun && Mixer.connected && !root.firstRunSuppressed) firstRunDialog.open() }
        function onConnectedChanged() { if (Mixer.firstRun && Mixer.connected && !root.firstRunSuppressed) firstRunDialog.open() }
    }
    function gestureFirstRun(what) {
        if (what === "open") { firstRunDialog.open(); return "" }
        if (what === "closeonly") { firstRunDialog.close(); return "closed=" + !firstRunDialog.visible }
        if (what === "hideonly") { firstRunDialog.visible = false; return "hidden=" + !firstRunDialog.visible }
        if (what === "state") return "visible=" + firstRunDialog.visible + " firstRun=" + Mixer.firstRun + " connected=" + Mixer.connected + " suppressed=" + root.firstRunSuppressed
        if (what === "apply") { firstRunDialog.open(); firstRunDialog.refresh(); const r = Mixer.firstRunApply(); if (Object.keys(r).length > 0 || Mixer.lastError === "") firstRunDialog.done = r; return Mixer.lastError }
        if (what === "skip") { root.firstRunSuppressed = true; Mixer.dismissFirstRun(); firstRunDialog.reject(); firstRunDialog.visible = false; return "" }
        return "<unknown " + what + ">"
    }

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
    SaveSceneDialog { id: saveSceneDialog }   // CT-9
    // CT-9: one child Action per saved scene, kept out of the Action block itself (see the note there).
    Component {
        id: sceneEntry
        Kirigami.Action {
            property string sceneName
            objectName: "sceneAction." + sceneName
            text: sceneName
            icon.name: "media-playback-start"
            onTriggered: Mixer.recallScene(sceneName, true)
        }
    }
    function rebuildSceneMenu() {
        const made = []
        for (const n of Mixer.scenes) made.push(sceneEntry.createObject(root, { sceneName: n }))
        recallSceneAction.children = made
    }
    Connections {
        target: Mixer
        function onScenesChanged() { root.rebuildSceneMenu() }
    }
    // CT-9 gestures: what a click does, without a pointer — the frontends_sync suite drives the window
    // through these (same contract as gestureDuplicate above).
    function gestureSaveScene(name) { saveSceneDialog.sceneName = name; saveSceneDialog.open(); saveSceneDialog.commit(name); saveSceneDialog.close(); return "" }
    function gestureRecallScene(name, exclusive) { Mixer.recallScene(name, exclusive !== false); return "" }
    function gestureDeleteScene(name) { Mixer.deleteScene(name); return "" }
    function renameDialogOpen(kind, slug) { renameDialog.open(kind, slug) }
    function duplicateDialogOpen(slug) { renameDialog.open("mix", slug, true) }   // MX-8
    function gestureDuplicate(slug, name) { renameDialog.open("mix", slug, true); renameDialog.commit(name); renameDialog.close(); return "" }
    IconDialog { id: iconDialog }
    // CH-8: name a new group for a channel
    Kirigami.PromptDialog {
        id: groupDialog
        property string channel
        title: i18n("New channel group")
        subtitle: i18n("Channels in one group move together: trim as one dB step for all, mute mirrored.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        QQC2.TextField { id: groupField; objectName: "groupField"; placeholderText: i18n("e.g. Music & Game"); onAccepted: groupDialog.accept() }
        onAccepted: if (groupField.text.trim() !== "") Mixer.setChannelGroup(channel, groupField.text.trim())
        onOpened: { groupField.text = ""; groupField.forceActiveFocus() }
    }
    function groupDialogOpen(slug) { groupDialog.channel = slug; groupDialog.open() }
    function gestureGroup(slug, name) { Mixer.setChannelGroup(slug, name === "none" ? "" : name); return Mixer.lastError }
    function iconDialogOpen(kind, slug) { iconDialog.open(kind, slug) }
    // FX panel opens as a dialog layer over the matrix — narrow windows keep the grid behind them.
    function fxPanelOpen(kind, slug) {
        // createObject gibt bei einem Fehler in der Komponente null zurueck und schreibt den
        // Grund NUR ins Log — pushDialogLayer(null) tut dann lautlos nichts. Genau so war das
        // Effekt-Panel monatelang unoeffenbar, ohne dass irgendwo ein Fehler sichtbar wurde.
        const page = fxPanelComp.createObject(this, {kind: kind, slug: slug,
            title: kind === "mix" ? Mixer.mixName(slug) : Mixer.channelName(slug)})
        if (!page) {
            console.warn("fxPanelOpen: FxPanel konnte nicht erzeugt werden:", fxPanelComp.errorString())
            return "<FxPanel: " + fxPanelComp.errorString() + ">"
        }
        root.pageStack.pushDialogLayer(page)
        return ""
    }
    Component {
        id: fxPanelComp
        FxPanel {}
    }
    // FX-9: ducking is per channel (a mix has no trigger), so this takes a slug and no kind.
    function duckPanelOpen(slug) {
        const page = duckPanelComp.createObject(this, {slug: slug, title: Mixer.channelName(slug)})
        if (!page) {
            console.warn("duckPanelOpen: DuckPanel konnte nicht erzeugt werden:", duckPanelComp.errorString())
            return "<DuckPanel: " + duckPanelComp.errorString() + ">"
        }
        // pushDialogLayer schluckt eine nicht-Page lautlos: verifyPages() lehnt ab und im Fenster passiert
        // sichtbar nichts. Genau so war FxPanel monatelang unoeffenbar, ohne dass ein Test es merkte — das per
        // createObject erzeugte Objekt haengt trotzdem am Fenster und ist damit probebar.
        // layers.depth ist dafuer NICHT der Massstab: auf dem Desktop oeffnet pushDialogLayer ein eigenes
        // QQuickWindow, und dann bleibt depth bei 1, obwohl der Push geklappt hat (gemessen 2026-09-21).
        // Was sich in BEIDEN Faellen aendert, ist der Elternteil: angenommen wird das Panel umgehaengt, und
        // abgelehnt bleibt es dort, wo createObject es hinterlassen hat.
        const elternVorher = page.parent
        root.pageStack.pushDialogLayer(page)
        if (page.parent === elternVorher) {
            console.warn("duckPanelOpen: pushDialogLayer hat nichts gepusht — ist die Wurzel von DuckPanel.qml eine Page?")
            duckPushedAnker.text = "no"
            return "<DuckPanel: pushDialogLayer refused the page>"
        }
        duckPushedAnker.text = "yes"
        return ""
    }
    // Probe-Anker fuer den Test: `visible` ist nur true, wenn das Panel wirklich auf dem Layer-Stack
    // liegt. probe() sucht nach objectName in der Item-Hierarchie, eine Window-Property waere von dort
    // nicht erreichbar — deshalb ein (unsichtbares, nullgrosses) Item statt einer blossen Property.
    Item {
        id: duckPushedAnker
        objectName: "duckPanelPushed"
        // `text` statt `visible`: ein nullgrosses Item in einem Layout ist nicht zuverlaessig sichtbar,
        // und ein Probe auf visible las darum false, obwohl der Push geklappt hatte. Eine eigene
        // String-Property hat keine solche Nebenbedeutung.
        property string text: "no"
        width: 0; height: 0
    }
    Component {
        id: duckPanelComp
        DuckPanel {}
    }
    // CT-8: the soundboard. One page for every board, so no slug is needed — it picks the first board itself and
    // offers a picker when there is more than one. Same push-verification as duckPanelOpen above: pushDialogLayer
    // swallows a non-Page silently, and the parent change is the only signal that works on desktop AND mobile.
    function soundboardPanelOpen() {
        const page = soundboardPanelComp.createObject(this, {})
        if (!page) {
            console.warn("soundboardPanelOpen: SoundboardPanel konnte nicht erzeugt werden:", soundboardPanelComp.errorString())
            return "<SoundboardPanel: " + soundboardPanelComp.errorString() + ">"
        }
        const elternVorher = page.parent
        root.pageStack.pushDialogLayer(page)
        if (page.parent === elternVorher) {
            console.warn("soundboardPanelOpen: pushDialogLayer hat nichts gepusht — ist die Wurzel von SoundboardPanel.qml eine Page?")
            soundboardPushedAnker.text = "no"
            return "<SoundboardPanel: pushDialogLayer refused the page>"
        }
        soundboardPushedAnker.text = "yes"
        return ""
    }
    Item {
        id: soundboardPushedAnker
        objectName: "soundboardPanelPushed"
        property string text: "no"
        width: 0; height: 0
    }
    Component {
        id: soundboardPanelComp
        SoundboardPanel {}
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
