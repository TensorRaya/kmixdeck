// SPDX-License-Identifier: GPL-3.0-or-later
// The matrix: channels as rows, mixes as columns, one fader per cell (UX-1).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.ScrollablePage {
    id: page
    title: i18n("Mixer")

    actions: [
        Kirigami.Action { text: i18n("Add channel"); icon.name: "list-add"; onTriggered: applicationWindow().addDialogOpen("channel") },
        Kirigami.Action { text: i18n("Add mix");     icon.name: "list-add"; onTriggered: applicationWindow().addDialogOpen("mix") }
    ]

    readonly property var channels: Mixer.channelSlugs
    readonly property var mixes: Mixer.mixSlugs
    readonly property int cellW: Kirigami.Units.gridUnit * 9
    readonly property int labelW: Kirigami.Units.gridUnit * 10

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: page.channels.length === 0 || page.mixes.length === 0
        icon.name: "audio-card"
        text: i18n("No channels or mixes yet")
        explanation: i18n("A channel is where applications play into (Game, Voice, Music …). A mix is what you or your stream hear. Add one of each to start.")
        helpfulAction: Kirigami.Action { text: i18n("Add channel"); icon.name: "list-add"; onTriggered: applicationWindow().addDialogOpen("channel") }
    }

    ColumnLayout {
        visible: page.channels.length > 0 && page.mixes.length > 0
        spacing: Kirigami.Units.smallSpacing

        // header row: mix names
        RowLayout {
            spacing: Kirigami.Units.smallSpacing
            Item { Layout.preferredWidth: page.labelW }
            Repeater {
                model: page.mixes
                delegate: Kirigami.Heading {
                    required property string modelData
                    Layout.preferredWidth: page.cellW
                    level: 4
                    horizontalAlignment: Text.AlignHCenter
                    text: Mixer.mixName(modelData)
                    elide: Text.ElideRight
                }
            }
        }

        Repeater {
            model: page.channels
            delegate: RowLayout {
                id: row
                required property string modelData
                spacing: Kirigami.Units.smallSpacing
                Layout.fillWidth: true

                Kirigami.Heading {
                    Layout.preferredWidth: page.labelW
                    Layout.alignment: Qt.AlignVCenter
                    level: 3
                    text: Mixer.channelName(row.modelData)
                    elide: Text.ElideRight
                }
                Repeater {
                    model: page.mixes
                    delegate: CellFader {
                        required property string modelData
                        Layout.preferredWidth: page.cellW
                        channel: row.modelData
                        mix: modelData
                    }
                }
            }
        }
    }
}
