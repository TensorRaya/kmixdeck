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

    readonly property var channels: Mixer.channelSlugs
    readonly property var mixes: Mixer.mixSlugs
    readonly property int rowH: Kirigami.Units.gridUnit * 3.6          // ≈ 66 px @ 18 px gridUnit, like Wave Link
    readonly property int gap: Kirigami.Units.smallSpacing * 2
    readonly property int channelColW: Kirigami.Units.gridUnit * 19
    readonly property int mixColW: Kirigami.Units.gridUnit * 12

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
    RowLayout {
        id: desk
        visible: page.channels.length > 0 && page.mixes.length > 0
        width: page.flickable ? page.flickable.width : page.width
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

        // ---- one panel per mix: header card on top, then one crosspoint per channel, rows aligned 1:1
        Repeater {
            model: page.mixes
            delegate: Panel {
                id: mixPanel
                required property string modelData
                Layout.fillWidth: true
                Layout.minimumWidth: page.mixColW
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
                        }
                    }
            }
        }

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
        }
    }

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
