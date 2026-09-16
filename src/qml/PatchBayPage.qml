// SPDX-License-Identifier: GPL-3.0-or-later
// DV-24 / ADR 0009 B2: the Loopback-style patchbay. Three columns of cards; each card is a block with a header
// and one row per connector (jack). Wires are drawn as smooth S-curves jack-to-jack between columns. The page
// renders the shared `Mixer.patchbay()` model, so the web UI renders the exact same thing (AR-9).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.ScrollablePage {
    id: page
    title: i18n("Patchbay")
    padding: Kirigami.Units.largeSpacing

    // the shared view model (AR-9); rebuilt on every daemon change, coalesced so a burst of signals = one rebuild
    property var model: Mixer.patchbay()
    readonly property var cards: model.cards
    readonly property var wires: model.wires
    signal remeasure()
    Timer { id: rebuild; interval: 40; onTriggered: { page.model = Mixer.patchbay(); Qt.callLater(page.remeasure) } }
    Connections { target: Mixer
        function onLayoutChanged() { rebuild.restart() }
        function onCellChanged() { rebuild.restart() }
        function onChannelChanged() { rebuild.restart() }
        function onMixChanged() { rebuild.restart() }
        function onAppsChanged() { rebuild.restart() }
        function onInputDevicesChanged() { rebuild.restart() }
        function onOutputDevicesChanged() { rebuild.restart() }
        function onListeningDeviceChanged() { rebuild.restart() }
    }

    // jacks stick out half a ring past the card edge → keep that much air on both sides of the graph
    readonly property int jackAir: Kirigami.Units.gridUnit * 0.6
    readonly property int avail: (page.flickable ? page.flickable.width : page.width) - Kirigami.Units.largeSpacing * 2 - jackAir * 2
    readonly property int colW: Math.max(Kirigami.Units.gridUnit * 10, Math.min(Kirigami.Units.gridUnit * 14, (avail - 2 * Kirigami.Units.gridUnit * 4) / 3))
    readonly property int edgeW: Math.max(Kirigami.Units.gridUnit * 4, (avail - 3 * colW) / 2)
    readonly property int gap: Kirigami.Units.smallSpacing * 2

    property var jackY: ({})      // "<cardId>|<pos>" → y in graph coordinates
    property var jackXY: ({})     // "<cardId>|<pos>|in|out" → {x, y} in graph coordinates (drag targets)
    property var collapsed: ({})  // cardId → true: show only wired connectors (big devices)
    // drag-to-wire state (DV-24): a drag starts on a jack, follows the pointer over the whole graph, ends on a jack
    property string dragFrom: ""          // "<cardId>|<pos>" or ""
    property point dragPos: Qt.point(0, 0)
    property string dragHint: ""
    function jackAt(x, y) {
        let best = "", bestD = Kirigami.Units.gridUnit * 0.9
        for (const k in page.jackXY) { const j = page.jackXY[k]; const d = Math.hypot(j.x - x, j.y - y); if (d < bestD) { bestD = d; best = k } }
        return best
    }
    function endDrag(x, y) {
        const target = page.jackAt(x, y)
        if (page.dragFrom.length > 0 && target.length > 0 && target.split("|")[0] !== page.dragFrom.split("|")[0]) {
            const f = page.dragFrom.split("|"), t = target.split("|")
            const err = Mixer.connectJacks(f[0], f[1], t[0], t[1])
            if (err.length > 0) hint.show(err)
        }
        page.dragFrom = ""; rubber.requestPaint()
    }
    onModelChanged: {
        if (!page.model || !page.model.cards) return
        const cards = page.model.cards, wires = page.model.wires
        const c = Object.assign({}, page.collapsed)
        for (let i = 0; i < cards.length; ++i) { const k = cards[i]
            if (k.rows.length <= 8 || (k.id in c)) continue
            let any = false; for (let j = 0; j < wires.length; ++j) { const w = wires[j]; if (w.from.card === k.id || w.to.card === k.id) { any = true; break } }
            c[k.id] = any
        }
        page.collapsed = c
    }
    function isWired(cardId, pos) { for (const w of page.wires) if ((w.from.card === cardId && w.from.pos === pos) || (w.to.card === cardId && w.to.pos === pos)) return true; return false }
    signal geometryChanged()
    function db(v) { return v > 0 ? 20 * Math.log10(v) : -60 }
    function setY(map, key, y) { map[key] = y; page.geometryChanged() }

    function cardList(kind) {
        const out = []
        for (const c of page.cards) if (c.kind === kind) out.push(c)
        return out
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: page.cards.length === 0
        icon.name: "applications-utilities"
        text: i18n("No devices or channels yet")
        explanation: i18n("Add channels and wire them to devices on the Mixer page; they appear here as cards with connectors.")
    }

    Kirigami.InlineMessage {
        id: hint
        z: 10
        anchors { left: parent.left; right: parent.right; top: parent.top }
        type: Kirigami.MessageType.Information
        showCloseButton: true
        function show(t) { text = t; visible = true; hideTimer.restart() }
        Timer { id: hideTimer; interval: 4000; onTriggered: hint.visible = false }
    }
    // the rubber band while dragging a new wire: drawn above everything, in graph coordinates
    Canvas {
        id: rubber
        parent: graph; anchors.fill: graph; z: 5
        visible: page.dragFrom.length > 0
        onPaint: {
            const ctx = getContext("2d"); ctx.reset(); ctx.clearRect(0, 0, width, height)
            if (page.dragFrom.length === 0) return
            const f = page.jackXY[page.dragFrom + "|out"] || page.jackXY[page.dragFrom + "|in"]; if (!f) return
            const t = page.dragPos
            ctx.beginPath(); ctx.moveTo(f.x, f.y); ctx.bezierCurveTo((f.x + t.x) / 2, f.y, (f.x + t.x) / 2, t.y, t.x, t.y)
            ctx.lineWidth = 2.5; ctx.setLineDash([6, 4]); ctx.strokeStyle = Kirigami.Theme.highlightColor; ctx.stroke()
            const over = page.jackAt(t.x, t.y)
            if (over.length > 0 && over.split("|")[0] !== page.dragFrom.split("|")[0]) { const j = page.jackXY[over]; ctx.beginPath(); ctx.arc(j.x, j.y, Kirigami.Units.gridUnit * 0.6, 0, Math.PI * 2); ctx.lineWidth = 2; ctx.setLineDash([]); ctx.stroke() }
        }
    }
    RowLayout {
        id: graph
        visible: page.cards.length > 0
        x: page.jackAir
        width: (page.flickable ? page.flickable.width : page.width) - page.jackAir * 2
        spacing: 0
        // pointer tracking for the drag: DragHandler on the jacks would steal the tap; a plain MouseArea under everything works with both
        MouseArea {
            id: dragTracker
            parent: graph; anchors.fill: graph; z: 4
            enabled: page.dragFrom.length > 0
            hoverEnabled: enabled
            cursorShape: Qt.CrossCursor
            onPositionChanged: mouse => { page.dragPos = Qt.point(mouse.x, mouse.y); rubber.requestPaint() }
            onReleased: mouse => page.endDrag(mouse.x, mouse.y)
            onClicked: mouse => page.endDrag(mouse.x, mouse.y)
        }

        // column 1: sources (apps + input devices)
        ColumnLayout {
            Layout.preferredWidth: page.colW; Layout.minimumWidth: page.colW; Layout.maximumWidth: page.colW; Layout.alignment: Qt.AlignTop
            spacing: page.gap
            Kirigami.Heading { level: 4; text: i18n("Sources"); opacity: 0.7 }
            Repeater {
                model: page.cardList("app")
                delegate: Card {
                    required property var modelData
                    info: modelData
                    onClicked: applicationWindow().showApps()
                }
            }
            Repeater {
                model: page.cardList("device")
                delegate: Card {
                    required property var modelData
                    info: modelData
                    onClicked: applicationWindow().showMixer()
                }
            }
        }
        EdgeLayer {
            Layout.preferredWidth: page.edgeW; Layout.fillHeight: true
            wires: {
                const out = []
                for (const w of page.wires) if (w.kind === "app" && w.from.card.startsWith("app/") && w.to.card.startsWith("ch/")) out.push(w)
                for (const w of page.wires) if (w.kind === "input" && w.from.card.startsWith("dev/") && w.to.card.startsWith("ch/")) out.push(w)
                return out
            }
        }

        // column 2: channels
        ColumnLayout {
            Layout.preferredWidth: page.colW; Layout.minimumWidth: page.colW; Layout.maximumWidth: page.colW; Layout.alignment: Qt.AlignTop
            spacing: page.gap
            Kirigami.Heading { level: 4; text: i18n("Channels"); opacity: 0.7 }
            Repeater {
                model: page.cardList("channel")
                delegate: Card {
                    required property var modelData
                    info: modelData
                    onClicked: applicationWindow().showMixer()
                }
            }
        }
        EdgeLayer {
            Layout.preferredWidth: page.edgeW; Layout.fillHeight: true
            wires: {
                const out = []
                for (const w of page.wires) if (w.kind === "cell" && w.from.card.startsWith("ch/") && w.to.card.startsWith("mix/")) out.push(w)
                return out
            }
        }

        // column 3: outputs (mixes + outputs + capture)
        ColumnLayout {
            Layout.preferredWidth: page.colW; Layout.minimumWidth: page.colW; Layout.maximumWidth: page.colW; Layout.alignment: Qt.AlignTop
            spacing: page.gap
            Kirigami.Heading { level: 4; text: i18n("Outputs"); opacity: 0.7 }
            Repeater {
                model: page.cardList("mix")
                delegate: Card {
                    required property var modelData
                    info: modelData
                    onClicked: applicationWindow().showMixer()
                }
            }
            Repeater {
                model: { const out = []; for (const c of page.cards) if (c.kind === "output" || c.kind === "capture") out.push(c); return out }
                delegate: Card {
                    required property var modelData
                    info: modelData
                    onClicked: applicationWindow().showMixer()
                }
            }
        }
    }

    // ---- a card = one device/channel/mix block: header (icon, title, listen) above one row per connector.
    // Every jack reports its own y (page coordinates) under "<cardId>|<pos>|in" / "|out" so wires end ON the jack.
    component Card: Rectangle {
        id: card
        property var info
        signal clicked()
        Layout.fillWidth: true
        implicitHeight: body.implicitHeight + Kirigami.Units.smallSpacing * 3
        radius: Kirigami.Units.smallSpacing * 1.5
        color: Kirigami.Theme.alternateBackgroundColor
        border.width: 1; border.color: Qt.alpha(Kirigami.Theme.textColor, 0.08)
        opacity: info.present ? 1 : 0.55
        ColumnLayout {
            id: body
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Kirigami.Units.smallSpacing * 1.5 }
            spacing: Kirigami.Units.smallSpacing
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing * 1.5
                Kirigami.Icon { source: card.info.icon; Layout.preferredWidth: Kirigami.Units.iconSizes.small; Layout.preferredHeight: width }
                QQC2.Label { text: card.info.title; font.weight: Font.DemiBold; elide: Text.ElideMiddle; Layout.fillWidth: true; QQC2.ToolTip.text: card.info.title; QQC2.ToolTip.visible: truncated && th.hovered; HoverHandler { id: th } }
                QQC2.ToolButton {
                    visible: card.info.rows.length > 4
                    icon.name: page.collapsed[card.info.id] ? "expand" : "collapse"; display: QQC2.AbstractButton.IconOnly
                    text: page.collapsed[card.info.id] ? i18n("Show all %1 connectors", card.info.rows.length) : i18n("Show wired connectors only")
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 1.4; Layout.preferredWidth: height
                    onClicked: { const c = Object.assign({}, page.collapsed); c[card.info.id] = !c[card.info.id]; page.collapsed = c }
                    QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
                }
                QQC2.ToolButton {
                    visible: card.info.kind === "channel" || card.info.kind === "mix"
                    icon.name: "audio-headphones"; display: QQC2.AbstractButton.IconOnly
                    text: i18n("Listen (hold)")
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 1.4; Layout.preferredWidth: height
                    onPressed: Mixer.audition(card.info.kind, card.info.id.split("/")[1])
                    onReleased: Mixer.stopAudition(); onCanceled: Mixer.stopAudition()
                    QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
                }
            }
            // big devices (Ui24R: 32 rows) collapse to their wired connectors; header button toggles
            Repeater {
                model: { if (!page.collapsed[card.info.id]) return card.info.rows; const out = []; for (const r of card.info.rows) if (page.isWired(card.info.id, r.pos)) out.push(r); return out }
                delegate: Item {
                    id: rowItem
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: Kirigami.Units.gridUnit * 1.15
                    function report() {
                        const y = rowItem.mapToItem(graph, 0, rowItem.height / 2).y
                        page.jackY[card.info.id + "|" + modelData.pos] = y
                        const key = card.info.id + "|" + modelData.pos
                        if (modelData.jackIn) page.jackXY[key + "|in"] = { x: rowItem.mapToItem(graph, -Kirigami.Units.smallSpacing * 1.5, 0).x, y: y }
                        if (modelData.jackOut) page.jackXY[key + "|out"] = { x: rowItem.mapToItem(graph, rowItem.width + Kirigami.Units.smallSpacing * 1.5, 0).x, y: y }
                        page.geometryChanged()
                    }
                    onYChanged: report(); onWidthChanged: report(); Component.onCompleted: Qt.callLater(report)
                    Connections { target: card; function onYChanged() { rowItem.report() } function onHeightChanged() { rowItem.report() } function onXChanged() { rowItem.report() } }
                    Connections { target: page; function onRemeasure() { rowItem.report() } }
                    Jack {   // leading edge: signal comes IN here
                        visible: rowItem.modelData.jackIn
                        anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: -Kirigami.Units.smallSpacing * 1.5 - width / 2 }
                        used: page.isWired(card.info.id, rowItem.modelData.pos)
                        tip: rowItem.modelData.label
                        jackKey: card.info.id + "|" + rowItem.modelData.pos
                    }
                    QQC2.Label {
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: Kirigami.Units.gridUnit * 0.8; rightMargin: Kirigami.Units.gridUnit * 0.8 }
                        text: (rowItem.modelData.usedBy || "").length > 0 && card.info.kind === "device" ? i18n("%1 → %2", rowItem.modelData.label, rowItem.modelData.usedBy) : rowItem.modelData.label
                        elide: Text.ElideRight; font: Kirigami.Theme.smallFont
                        opacity: card.info.kind === "device" && (rowItem.modelData.usedBy || "").length === 0 ? 0.55 : 1
                    }
                    Jack {   // trailing edge: signal goes OUT here
                        visible: rowItem.modelData.jackOut
                        anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: -Kirigami.Units.smallSpacing * 1.5 - width / 2 }
                        used: page.isWired(card.info.id, rowItem.modelData.pos)
                        tip: rowItem.modelData.label
                        jackKey: card.info.id + "|" + rowItem.modelData.pos
                    }
                    LevelMeter {
                        id: rowMeter
                        horizontal: true
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: Kirigami.Units.gridUnit * 0.8; rightMargin: Kirigami.Units.gridUnit * 0.8 }
                        height: 2
                        Connections { target: Mixer; function onPeaksChanged() { rowMeter.peak = Mixer.peak(rowItem.modelData.meterKey) } }
                    }
                }
            }
        }
        TapHandler { onTapped: card.clicked() }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    // ---- a jack: small ring on the card edge; filled when a wire uses it
    component Jack: Rectangle {
        id: jack
        property bool used: false
        property string tip: ""
        property string jackKey: ""
        width: Kirigami.Units.gridUnit * 0.7; height: width; radius: width / 2
        z: 6
        color: used ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
        border.width: 2; border.color: used ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.4)
        QQC2.ToolTip.visible: jh.hovered && page.dragFrom.length === 0; QQC2.ToolTip.text: i18n("%1 — drag to another jack to wire", tip)
        HoverHandler { id: jh; cursorShape: Qt.CrossCursor }
        MouseArea {   // bigger hit area than the ring itself
            anchors.centerIn: parent; width: parent.width * 2.2; height: width
            onPressed: mouse => { page.dragFrom = jack.jackKey; const p = mapToItem(graph, mouse.x, mouse.y); page.dragPos = Qt.point(p.x, p.y); rubber.requestPaint() }
            onPositionChanged: mouse => { if (page.dragFrom.length === 0) return; const p = mapToItem(graph, mouse.x, mouse.y); page.dragPos = Qt.point(p.x, p.y); rubber.requestPaint() }
            onReleased: mouse => { const p = mapToItem(graph, mouse.x, mouse.y); page.endDrag(p.x, p.y) }
        }
    }

    // ---- a canvas between two columns: every wire as a smooth S-curve from jack to jack, live-coloured
    component EdgeLayer: Canvas {
        id: layer
        property var wires: []
        function db(v) { return v > 0 ? 20 * Math.log10(v) : -60 }

        Connections { target: page; function onGeometryChanged() { layer.requestPaint() } }
        Connections { target: Mixer
            function onPeaksChanged() { layer.requestPaint() }
            function onCellChanged() { layer.requestPaint() }
            function onChannelChanged() { layer.requestPaint() }
            function onMixChanged() { layer.requestPaint() }
            function onAppsChanged() { layer.requestPaint() }
        }
        onYChanged: layer.requestPaint()
        onHeightChanged: layer.requestPaint()
        Component.onCompleted: layer.requestPaint()

        onPaint: {
            const ctx = getContext("2d"); ctx.reset(); ctx.clearRect(0, 0, width, height)
            const off = layer.mapToItem(graph, 0, 0)
            const hot = Kirigami.Theme.positiveTextColor
            const idle = Qt.alpha(Kirigami.Theme.textColor, 0.25)
            const off_ = Qt.alpha(Kirigami.Theme.textColor, 0.10)
            for (const w of wires) {
                const y1 = page.jackY[w.from.card + "|" + w.from.pos], y2 = page.jackY[w.to.card + "|" + w.to.pos]
                if (y1 === undefined || y2 === undefined) continue
                const lvl = Mixer.peak(w.meterKey)
                const y1p = y1 - off.y, y2p = y2 - off.y
                ctx.beginPath(); ctx.moveTo(0, y1p)
                ctx.bezierCurveTo(width * 0.5, y1p, width * 0.5, y2p, width, y2p)
                ctx.lineWidth = w.muted ? 1 : 1.5 + 3 * Math.max(0, w.gain || 1)
                ctx.strokeStyle = w.muted ? off_ : (lvl > 0.001 ? Qt.alpha(hot, 0.45 + 0.55 * Math.min(1, (page.db(lvl) + 60) / 60)) : idle)
                if (w.muted) ctx.setLineDash([4, 4]); else ctx.setLineDash([])
                ctx.stroke()
            }
        }
        // click on a wire → remove it (input/output wires are removed, cell wires are muted, app wires unassigned).
        // Bezier sampled along x; the closest wire within 8 px wins.
        function wireAt(x, y) {
            const off = layer.mapToItem(graph, 0, 0)
            let best = null, bestD = 8
            for (const w of wires) {
                if (w.kind === "capture") continue
                const y1 = page.jackY[w.from.card + "|" + w.from.pos], y2 = page.jackY[w.to.card + "|" + w.to.pos]
                if (y1 === undefined || y2 === undefined) continue
                const t = Math.max(0, Math.min(1, x / layer.width)), mt = 1 - t
                // cubic bezier with control points at (w/2, y1) and (w/2, y2): y(t) = mt^3 y1 + 3 mt^2 t y1 + 3 mt t^2 y2 + t^3 y2
                const yy = (mt * mt * mt + 3 * mt * mt * t) * (y1 - off.y) + (3 * mt * t * t + t * t * t) * (y2 - off.y)
                const d = Math.abs(y - yy)
                if (d < bestD) { bestD = d; best = w }
            }
            return best
        }
        property var hoverWire: null
        HoverHandler {
            enabled: page.dragFrom.length === 0
            onPointChanged: layer.hoverWire = layer.wireAt(point.position.x, point.position.y)
            cursorShape: layer.hoverWire ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
        QQC2.ToolTip.visible: layer.hoverWire !== null && page.dragFrom.length === 0
        QQC2.ToolTip.text: layer.hoverWire ? (layer.hoverWire.kind === "cell" ? i18n("Click: mute this send") : i18n("Click: remove this wire")) : ""
        TapHandler {
            enabled: page.dragFrom.length === 0
            onTapped: (ev) => { const w = layer.wireAt(ev.position.x, ev.position.y); if (w) Mixer.removeWire(w) }
        }
    }
}
