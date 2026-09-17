// SPDX-License-Identifier: GPL-3.0-or-later
// The mixer body, Wave Link style (UX-1): channels are ROWS (icon + name + own fader), mixes are COLUMNS with a
// header card; every crosspoint is a short horizontal fader with a mute. Dense rows, no dB text, no cards-in-cards.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.ScrollablePage {
    id: page
    title: i18n("Mixes")
    padding: Kirigami.Units.largeSpacing

    actions: [
        Kirigami.Action { text: i18n("Add channel"); icon.name: "list-add"; onTriggered: applicationWindow().addDialogOpen("channel") },
        Kirigami.Action { text: i18n("Add mix");     icon.name: "list-add"; onTriggered: applicationWindow().addDialogOpen("mix") }
    ]

    // UX-15: the routing view asks us to show one cell; the CellFader registers itself under "ch|mix"
    property var cellItems: ({})
    function highlightCell(channel, mix) { const c = cellItems[channel + "|" + mix]; if (c) c.pulse() }

    readonly property var channels: Mixer.channelSlugs
    readonly property var mixes: Mixer.mixSlugs
    readonly property int rowH: Kirigami.Units.gridUnit * 3.6          // ≈ 66 px @ 18 px gridUnit, like Wave Link
    readonly property int gap: Kirigami.Units.smallSpacing * 2
    readonly property int channelColW: Kirigami.Units.gridUnit * 19
    readonly property int mixColW: Kirigami.Units.gridUnit * 17     // MX-5: the header (icon, name, mute, master, listen, ⋮) needs this much; below it the master handle left the card (2026-09-17 render)

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: page.channels.length === 0 || page.mixes.length === 0
        icon.name: "audio-card"
        text: i18n("No channels or mixes yet")
        explanation: i18n("A channel is where applications play into (Game, Voice, Music …). A mix is what you or your stream hear. Add one of each to start.")
        helpfulAction: Kirigami.Action { text: i18n("Add channel"); icon.name: "list-add"; onTriggered: applicationWindow().addDialogOpen("channel") }
    }

    // ScrollablePage puts this into a Flickable; the Flickable only scrolls vertically when contentWidth <= width,
    // so bind our width to the page and let the mix panels share what is left of it.
    ColumnLayout {
        width: page.flickable ? page.flickable.width : page.width
        spacing: page.gap
        visible: page.channels.length > 0 && page.mixes.length > 0

    // ---- UX-2 "what am I hearing": my listening device + the mix that plays there. One click switches the mix.
    // The device list is the same as in every mix's Outputs menu; changing the device here re-points the
    // currently heard mix (or the first mix) to it, so this bar and the mix headers never disagree (CT-5 spirit).
    Panel {
        id: hearing
        objectName: "hearingBar"
        readonly property string hearLabelText: hearLabel.text   // UX-5 probe: which catalog is active
        readonly property string deviceNames: Mixer.outputDevices.map(d => d.description).join("|")   // CH-11 probe
        Layout.fillWidth: true
        // UX-14 gesture: choose a listening device exactly as the combo box's onActivated does
        function pickDevice(nodeName) { const i = Mixer.outputDevices.findIndex(d => d.nodeName === nodeName); if (i < 0) return "<no device " + nodeName + ">"; devBox.activated(i); return "" }
        // remembered per user session in the layout-independent UI settings; default: the first device that any mix plays to
        property string device: Mixer.listeningDevice
        readonly property var hearingMixes: page.mixes.filter(m => Mixer.mixOutputs(m).indexOf(hearing.device) >= 0)
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing * 2
            spacing: Kirigami.Units.largeSpacing
            Kirigami.Icon { source: "audio-headphones"; Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium; Layout.preferredHeight: width }
            QQC2.Label { id: hearLabel; objectName: "hearLabel"; text: i18nc("@label what am I hearing", "I hear:"); font.bold: true }
            // mix selector: exactly the mixes as toggle buttons, checked = plays on my device
            Repeater {
                model: page.mixes
                delegate: QQC2.ToolButton {
                    required property string modelData
                    text: Mixer.mixName(modelData)
                    Accessible.name: i18n("Listen to %1 on %2", text, Mixer.deviceDescription(hearing.device))
                    icon.name: Mixer.mixIcon(modelData).length > 0 ? Mixer.mixIcon(modelData) : "audio-speakers"
                    checkable: true
                    checked: hearing.hearingMixes.indexOf(modelData) >= 0
                    enabled: hearing.device.length > 0
                    QQC2.ToolTip.text: checked ? i18n("%1 plays on %2", text, Mixer.deviceDescription(hearing.device))
                                               : i18n("Listen to %1 on %2", text, Mixer.deviceDescription(hearing.device))
                    QQC2.ToolTip.visible: hovered
                    // one click = this mix on my device, every other mix off it (exclusive by default; MX-9 stays
                    // reachable through the mix header for the "headphones AND speakers" case)
                    onClicked: {
                        for (const m of hearing.hearingMixes) if (m !== modelData) Mixer.toggleMixOutput(m, hearing.device)
                        if (!checked) Mixer.toggleMixOutput(modelData, hearing.device)
                        checked = Qt.binding(() => hearing.hearingMixes.indexOf(modelData) >= 0)
                    }
                }
            }
            Item { Layout.fillWidth: true }
            QQC2.Label { text: i18nc("@label", "on"); opacity: 0.7 }
            QQC2.ComboBox {
                id: devBox
                objectName: "listeningDeviceBox"   // AR-12 probe: displayText names the listening device
                Accessible.name: i18nc("@label the device I listen on", "Listening device")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                model: Mixer.outputDevices
                textRole: "description"
                valueRole: "nodeName"
                displayText: hearing.device.length > 0 ? Mixer.deviceDescription(hearing.device) : i18nc("@item no listening device chosen", "Choose your headphones…")
                currentIndex: Mixer.outputDevices.findIndex(d => d.nodeName === hearing.device)
                onActivated: (index) => {
                    const dev = Mixer.outputDevices[index].nodeName
                    const heard = hearing.hearingMixes.length > 0 ? hearing.hearingMixes : [page.mixes[0]]
                    for (const m of heard) { if (hearing.device.length > 0 && Mixer.mixOutputs(m).indexOf(hearing.device) >= 0) Mixer.toggleMixOutput(m, hearing.device); if (Mixer.mixOutputs(m).indexOf(dev) < 0) Mixer.toggleMixOutput(m, dev) }
                    Mixer.listeningDevice = dev
                }
                QQC2.ToolTip.text: i18n("The device you listen on. Mixes are sent to it from here; the same choice appears in each mix's Outputs menu.")
                QQC2.ToolTip.visible: hovered
            }
        }
    }

    RowLayout {
        id: desk
        Layout.fillWidth: true
        spacing: page.gap

        // ---- channel panel: one dark panel, rows separated by hairlines
        Panel {
            Layout.preferredWidth: page.channelColW
            Layout.alignment: Qt.AlignTop
                // header row of the channel panel is empty in Wave Link; we use it for the count + hint
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.rowH
                    QQC2.Label {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: Kirigami.Units.largeSpacing }
                        text: i18np("%1 channel", "%1 channels", page.channels.length)
                        opacity: 0.6
                    }
                }
                Repeater {
                    model: page.channels
                    delegate: ChannelHeader {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.rowH
                        channel: modelData; mix: ""
                        first: index === 0
                    }
                }
        }

        // ---- one panel per mix: header card on top, then one crosspoint per channel, rows aligned 1:1.
        // MX-5 ≥ 8 mixes: the channel panel stays put, the mix strip scrolls horizontally (console layout) — every
        // fader keeps its full size, nothing is squeezed or hidden; the strip fills the width when there is room.
        QQC2.ScrollView {
            id: mixStrip
            objectName: "mixStrip"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            // each mix panel is mixColW wide at least; with room to spare they share the width (fillWidth)
            readonly property int needed: page.mixes.length * (page.mixColW + page.gap) - page.gap
            Layout.preferredHeight: mixRow.implicitHeight + (needed > availableWidth ? QQC2.ScrollBar.horizontal.height : 0)
            contentWidth: Math.max(availableWidth, needed)
            contentHeight: mixRow.implicitHeight
            QQC2.ScrollBar.vertical.policy: QQC2.ScrollBar.AlwaysOff
            QQC2.ScrollBar.horizontal.policy: needed > availableWidth ? QQC2.ScrollBar.AlwaysOn : QQC2.ScrollBar.AlwaysOff
            clip: true
            RowLayout {
                id: mixRow
                width: Math.max(mixStrip.availableWidth, mixStrip.needed)
                spacing: page.gap
        Repeater {
            model: page.mixes
            delegate: Panel {
                id: mixPanel
                required property string modelData
                Layout.fillWidth: true
                Layout.minimumWidth: page.mixColW
                Layout.preferredWidth: page.mixColW
                Layout.alignment: Qt.AlignTop
                    MixHeader {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.rowH
                        mix: mixPanel.modelData; channel: ""
                    }
                    Repeater {
                        model: page.channels
                        delegate: CellFader {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            Layout.preferredHeight: page.rowH
                            channel: modelData; mix: mixPanel.modelData
                            first: index === 0
                            Component.onCompleted: page.cellItems[channel + "|" + mix] = this
                            Component.onDestruction: delete page.cellItems[channel + "|" + mix]
                        }
                    }
            }
        }
            }   // mixRow
        }   // mixStrip

        // ---- add-mix, square, aligned with the header row
        QQC2.ToolButton {
            Layout.alignment: Qt.AlignTop
            Layout.preferredHeight: page.rowH
            Layout.preferredWidth: page.rowH
            icon.name: "list-add"
            display: QQC2.AbstractButton.IconOnly
            text: i18n("Add mix")
            onClicked: applicationWindow().addDialogOpen("mix")
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
            Accessible.name: text
        }
    }
    }   // ColumnLayout (hearing bar + desk)

    // A dark rounded panel — the only container in this UI. Kirigami colours so it follows Breeze/BreezeDark.
    // Children go into the ColumnLayout; the panel takes the layout's implicit size (no anchors.fill cycle).
    component Panel: Rectangle {
        default property alias content: col.data
        implicitHeight: col.implicitHeight
        implicitWidth: col.implicitWidth
        radius: Kirigami.Units.smallSpacing * 1.5
        color: Kirigami.Theme.alternateBackgroundColor
        border.width: 1
        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.08)
        ColumnLayout { id: col; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; spacing: 0 }
    }
}
