// SPDX-License-Identifier: GPL-3.0-or-later
// UX-15 routing view: the whole signal path as ONE picture — sources (apps, inputs) → channels → mixes → outputs
// (devices, capture sources). Live level on every edge (UX-13), listen buttons on channels and mixes (UX-12).
// Read-mostly by design: it explains what happens, it is not a second place to edit routing (the matrix and the
// Applications page are). Clicking a cell edge jumps to its fader on the mixer page.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.ScrollablePage {
    id: page
    title: i18n("Routing")
    padding: Kirigami.Units.largeSpacing

    readonly property var channels: Mixer.channelSlugs
    readonly property var mixes: Mixer.mixSlugs
    readonly property var apps: Mixer.apps
    readonly property int colW: Kirigami.Units.gridUnit * 13
    readonly property int edgeW: Kirigami.Units.gridUnit * 6
    readonly property int nodeH: Kirigami.Units.gridUnit * 3.2
    readonly property int gap: Kirigami.Units.smallSpacing * 2

    // node positions (y centre inside the column), filled by the delegates so the edge layer can draw lines
    property var chY: ({})
    property var mixY: ({})
    property var srcY: ({})
    property var outY: ({})
    signal geometryChanged()

    function db(v) { return v > 0 ? 20 * Math.log10(v) : -60 }
    function setY(map, key, y) { map[key] = y; page.geometryChanged() }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: page.channels.length === 0 || page.mixes.length === 0
        icon.name: "view-list-tree"
        text: i18n("Nothing to route yet")
        explanation: i18n("Add a channel and a mix on the Mixer page; this view then shows the whole signal path.")
    }

    RowLayout {
        id: graph
        visible: page.channels.length > 0 && page.mixes.length > 0
        width: page.flickable ? page.flickable.width : page.width
        spacing: 0

        // ---------------------------------------------------------------- column 1: sources
        ColumnLayout {
            Layout.preferredWidth: page.colW; Layout.minimumWidth: page.colW; Layout.alignment: Qt.AlignTop
            spacing: page.gap
            Kirigami.Heading { level: 4; text: i18n("Sources"); opacity: 0.7 }
            Repeater {
                model: page.apps
                delegate: Node {
                    required property var modelData
                    kind: "app"; key: modelData.path
                    icon: modelData.icon.length > 0 ? modelData.icon : "applications-multimedia"
                    title: modelData.name
                    subtitle: modelData.channel.length > 0 ? i18n("→ %1", (modelData.allChannels ?? [modelData.channel]).map(s => Mixer.channelName(s)).join(", ")) : i18n("not in mixer")
                    meterKey: "app/" + modelData.nodeId
                    dim: modelData.channel.length === 0
                    onClicked: applicationWindow().showApps()
                }
            }
            Repeater {
                model: page.channels.filter(c => Mixer.channelDevice(c).length > 0)
                delegate: Node {
                    required property string modelData
                    kind: "input"; key: modelData
                    icon: "audio-input-microphone"
                    title: Mixer.deviceDescription(Mixer.channelDevice(modelData))
                    subtitle: Mixer.channelInputPresent(modelData) ? i18n("→ %1", Mixer.channelName(modelData)) : i18n("unplugged")
                    meterKey: "in/" + modelData
                    dim: !Mixer.channelInputPresent(modelData)
                }
            }
        }

        EdgeLayer { Layout.preferredWidth: page.edgeW; Layout.fillHeight: true; from: "src"; to: "ch" }

        // ---------------------------------------------------------------- column 2: channels
        ColumnLayout {
            Layout.preferredWidth: page.colW; Layout.minimumWidth: page.colW; Layout.alignment: Qt.AlignTop
            spacing: page.gap
            Kirigami.Heading { level: 4; text: i18n("Channels"); opacity: 0.7 }
            Repeater {
                model: page.channels
                delegate: Node {
                    required property string modelData
                    kind: "channel"; key: modelData
                    icon: Mixer.channelIcon(modelData)
                    title: Mixer.channelName(modelData)
                    subtitle: Mixer.channelMuted(modelData) ? i18n("muted") : (Mixer.fxEnabled("channel", modelData) ? i18n("effects on") : "")
                    meterKey: "channel/" + modelData
                    dim: Mixer.channelMuted(modelData)
                    listenable: true
                    onClicked: applicationWindow().showMixer()
                }
            }
        }

        EdgeLayer { Layout.preferredWidth: page.edgeW * 1.4; Layout.fillHeight: true; from: "ch"; to: "mix" }

        // ---------------------------------------------------------------- column 3: mixes
        ColumnLayout {
            Layout.preferredWidth: page.colW; Layout.minimumWidth: page.colW; Layout.alignment: Qt.AlignTop
            spacing: page.gap
            Kirigami.Heading { level: 4; text: i18n("Mixes"); opacity: 0.7 }
            Repeater {
                model: page.mixes
                delegate: Node {
                    required property string modelData
                    kind: "mix"; key: modelData
                    icon: Mixer.mixIcon(modelData)
                    title: Mixer.mixName(modelData)
                    subtitle: Mixer.mixMuted(modelData) ? i18n("muted") : (Mixer.fxEnabled("mix", modelData) ? i18n("effects on") : "")
                    meterKey: "mix/" + modelData
                    dim: Mixer.mixMuted(modelData)
                    listenable: true
                    onClicked: applicationWindow().showMixer()
                }
            }
        }

        EdgeLayer { Layout.preferredWidth: page.edgeW; Layout.fillHeight: true; from: "mix"; to: "out" }

        // ---------------------------------------------------------------- column 4: outputs
        ColumnLayout {
            Layout.preferredWidth: page.colW; Layout.minimumWidth: page.colW; Layout.alignment: Qt.AlignTop
            spacing: page.gap
            Kirigami.Heading { level: 4; text: i18n("Outputs"); opacity: 0.7 }
            Repeater {
                model: {
                    const rows = []
                    for (const m of page.mixes) {
                        for (const o of Mixer.mixOutputs(m)) rows.push({ key: m + "|" + o, mix: m, dev: o, capture: false })
                        rows.push({ key: m + "|capture", mix: m, dev: Mixer.mixCaptureSource(m), capture: true })
                    }
                    return rows
                }
                delegate: Node {
                    required property var modelData
                    kind: "output"; key: modelData.key
                    icon: modelData.capture ? "camera-video" : (modelData.dev === Mixer.listeningDevice ? "audio-headphones" : "audio-speakers")
                    title: modelData.capture ? i18n("Capture: %1", Mixer.mixName(modelData.mix)) : Mixer.deviceDescription(modelData.dev)
                    subtitle: modelData.capture ? i18n("for OBS / Discord") : (modelData.dev === Mixer.listeningDevice ? i18n("what I hear") : (Mixer.mixOutputPresent(modelData.mix) ? "" : i18n("unplugged")))
                    meterKey: "out/" + modelData.mix
                    dim: !modelData.capture && !Mixer.mixOutputPresent(modelData.mix)
                }
            }
        }
    }

    // ---- one box in the graph: icon tile, title, subtitle, thin live meter along the bottom, optional listen button
    component Node: Rectangle {
        id: node
        property string kind
        property string key
        property string icon
        property string title
        property string subtitle
        property string meterKey
        property bool dim: false
        property bool listenable: false
        signal clicked()
        Layout.fillWidth: true
        Layout.preferredHeight: page.nodeH
        radius: Kirigami.Units.smallSpacing * 1.5
        color: Kirigami.Theme.alternateBackgroundColor
        border.width: 1; border.color: Qt.alpha(Kirigami.Theme.textColor, 0.08)
        opacity: dim ? 0.55 : 1
        // report my vertical centre in page coordinates so the edge layers can aim at me
        function report() {
            const p = node.mapToItem(graph, 0, node.height / 2).y
            if (kind === "channel") page.setY(page.chY, key, p)
            else if (kind === "mix") page.setY(page.mixY, key, p)
            else if (kind === "output") page.setY(page.outY, key, p)
            else page.setY(page.srcY, key, p)
        }
        onYChanged: report(); onHeightChanged: report(); Component.onCompleted: report()
        RowLayout {
            anchors { fill: parent; margins: Kirigami.Units.smallSpacing * 1.5; bottomMargin: Kirigami.Units.smallSpacing * 2.5 }
            spacing: Kirigami.Units.smallSpacing * 1.5
            Kirigami.Icon { source: node.icon; Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium; Layout.preferredHeight: width }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                QQC2.Label { text: node.title; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                QQC2.Label { text: node.subtitle; visible: text.length > 0; opacity: 0.65; font: Kirigami.Theme.smallFont; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            QQC2.ToolButton {   // UX-12 hold-to-listen, same semantics as in the headers
                visible: node.listenable
                icon.name: "audio-headphones"; display: QQC2.AbstractButton.IconOnly
                text: i18n("Listen (hold)")
                onPressed: Mixer.audition(node.kind, node.key)
                onReleased: Mixer.stopAudition(); onCanceled: Mixer.stopAudition()
                QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
            }
        }
        LevelMeter {
            id: nodeMeter
            horizontal: true
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: Kirigami.Units.smallSpacing * 1.5; rightMargin: Kirigami.Units.smallSpacing * 1.5; bottomMargin: 4 }
            height: 3
            Connections { target: Mixer; function onPeaksChanged() { nodeMeter.peak = Mixer.peak(node.meterKey) } }
        }
        TapHandler { onTapped: node.clicked() }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    // ---- the lines between two columns. Drawn on a Canvas; colour = live level of that edge, width = gain.
    component EdgeLayer: Canvas {
        id: layer
        property string from
        property string to
        property var edges: []      // [{y1, y2, key, gain, muted, label}]
        function rebuild() {
            const e = []
            if (from === "src") {
                for (const a of page.apps) {
                    if (!(a.path in page.srcY)) continue
                    for (const c of (a.allChannels ?? (a.channel.length ? [a.channel] : []))) if (c in page.chY) e.push({ y1: page.srcY[a.path], y2: page.chY[c], key: "app/" + a.nodeId, gain: 1, muted: false })
                }
                for (const c of page.channels) if (Mixer.channelDevice(c).length > 0 && c in page.srcY && c in page.chY) e.push({ y1: page.srcY[c], y2: page.chY[c], key: "in/" + c, gain: 1, muted: !Mixer.channelInputPresent(c) })
            } else if (from === "ch") {
                for (const c of page.channels) for (const m of page.mixes) {
                    if (!(c in page.chY) || !(m in page.mixY)) continue
                    const muted = Mixer.cellMuted(c, m) || Mixer.channelMuted(c)
                    e.push({ y1: page.chY[c], y2: page.mixY[m], key: "cell/" + c + "/" + m, gain: Mixer.cellVolume(c, m), muted: muted, cell: [c, m] })
                }
            } else {
                for (const m of page.mixes) {
                    if (!(m in page.mixY)) continue
                    for (const o of Mixer.mixOutputs(m)) if ((m + "|" + o) in page.outY) e.push({ y1: page.mixY[m], y2: page.outY[m + "|" + o], key: "out/" + m, gain: Mixer.mixVolume(m), muted: Mixer.mixMuted(m) || !Mixer.mixOutputPresent(m) })
                    if ((m + "|capture") in page.outY) e.push({ y1: page.mixY[m], y2: page.outY[m + "|capture"], key: "mix/" + m, gain: Mixer.mixVolume(m), muted: Mixer.mixMuted(m) })
                }
            }
            edges = e; requestPaint()
        }
        Connections { target: page; function onGeometryChanged() { layer.rebuild() } }
        Connections { target: Mixer
            function onPeaksChanged() { layer.requestPaint() }
            function onCellChanged() { layer.rebuild() }
            function onChannelChanged() { layer.rebuild() }
            function onMixChanged() { layer.rebuild() }
            function onAppsChanged() { layer.rebuild() }
        }
        Component.onCompleted: rebuild()
        onPaint: {
            const ctx = getContext("2d"); ctx.reset(); ctx.clearRect(0, 0, width, height)
            const off = layer.mapToItem(graph, 0, 0).y
            const hot = Kirigami.Theme.positiveTextColor, idle = Qt.alpha(Kirigami.Theme.textColor, 0.25), off_ = Qt.alpha(Kirigami.Theme.textColor, 0.10)
            for (const e of edges) {
                const y1 = e.y1 - off, y2 = e.y2 - off
                const lvl = Mixer.peak(e.key)
                ctx.beginPath(); ctx.moveTo(0, y1)
                ctx.bezierCurveTo(width * 0.5, y1, width * 0.5, y2, width, y2)
                ctx.lineWidth = e.muted ? 1 : 1.5 + 3 * Math.max(0, e.gain)
                ctx.strokeStyle = e.muted ? off_ : (lvl > 0.001 ? Qt.alpha(hot, 0.45 + 0.55 * Math.min(1, (page.db(lvl) + 60) / 60)) : idle)
                if (e.muted) ctx.setLineDash([4, 4]); else ctx.setLineDash([])
                ctx.stroke()
            }
        }
        // click on an edge → the fader that controls it (cells only; other edges have their control in the headers)
        TapHandler {
            onTapped: (ev) => {
                const off = layer.mapToItem(graph, 0, 0).y
                let best = null, bestD = 12
                for (const e of layer.edges) {
                    if (!e.cell) continue
                    const t = ev.position.x / layer.width          // approximate the bezier by its cubic-in-x centre line
                    const y = (e.y1 - off) * (1 - t) + (e.y2 - off) * t
                    const d = Math.abs(ev.position.y - y)
                    if (d < bestD) { bestD = d; best = e }
                }
                if (best) applicationWindow().showMixer(best.cell[0], best.cell[1])
            }
        }
    }
}
