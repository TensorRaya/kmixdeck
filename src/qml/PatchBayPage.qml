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

    readonly property var model: Mixer.patchbay()
    readonly property var cards: model.cards
    readonly property var wires: model.wires

    readonly property int avail: (page.flickable ? page.flickable.width : page.width) - Kirigami.Units.largeSpacing * 2
    readonly property int colW: Math.max(Kirigami.Units.gridUnit * 10, Math.min(Kirigami.Units.gridUnit * 14, (avail - 2 * Kirigami.Units.gridUnit * 4) / 3))
    readonly property int edgeW: Math.max(Kirigami.Units.gridUnit * 4, (avail - 3 * colW) / 2)
    readonly property int gap: Kirigami.Units.smallSpacing * 2

    property var jackY: ({})      // "<cardId>|<pos>" → y in graph coordinates
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

    RowLayout {
        id: graph
        visible: page.cards.length > 0
        width: page.flickable ? page.flickable.width : page.width
        spacing: 0

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
                QQC2.Label { text: card.info.title; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
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
            Repeater {
                model: card.info.rows
                delegate: Item {
                    id: rowItem
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: Kirigami.Units.gridUnit * 1.15
                    function report() {
                        const y = rowItem.mapToItem(graph, 0, rowItem.height / 2).y
                        page.setY(page.jackY, card.info.id + "|" + modelData.pos, y)
                    }
                    onYChanged: report(); Component.onCompleted: report()
                    Connections { target: card; function onYChanged() { rowItem.report() } function onHeightChanged() { rowItem.report() } }
                    Jack {   // leading edge: signal comes IN here
                        visible: rowItem.modelData.jackIn
                        anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: -Kirigami.Units.smallSpacing * 1.5 - width / 2 }
                        used: (rowItem.modelData.usedBy || "").length > 0
                        tip: rowItem.modelData.label
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
                        used: (rowItem.modelData.usedBy || "").length > 0
                        tip: rowItem.modelData.label
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
        property bool used: false
        property string tip: ""
        width: Kirigami.Units.gridUnit * 0.7; height: width; radius: width / 2
        color: used ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
        border.width: 2; border.color: used ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.4)
        QQC2.ToolTip.visible: jh.hovered; QQC2.ToolTip.text: tip
        HoverHandler { id: jh; cursorShape: Qt.PointingHandCursor }
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
        TapHandler {
            onTapped: (ev) => {
                for (const w of wires) {
                    if (w.kind !== "cell" && w.kind !== "input") continue
                    const y1 = page.jackY[w.from.card + "|" + w.from.pos] || 0
                    const y2 = page.jackY[w.to.card + "|" + w.to.pos] || 0
                    const t = ev.position.x / layer.width
                    const y = y1 * (1 - t) + y2 * t
                    if (Math.abs(ev.position.y - y) < 12) { applicationWindow().showMixer(); break }
                }
            }
        }
    }
}
