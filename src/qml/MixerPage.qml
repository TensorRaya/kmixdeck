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

    // One GridLayout for header + rows so columns line up by construction (UX-1).
    GridLayout {
        visible: page.channels.length > 0 && page.mixes.length > 0
        columns: page.mixes.length + 1
        rowSpacing: Kirigami.Units.smallSpacing
        columnSpacing: Kirigami.Units.smallSpacing

        // header row
        Item { Layout.preferredWidth: page.labelW; Layout.fillWidth: false }
        Repeater {
            model: page.mixes
            delegate: MixHeader {
                required property string modelData
                Layout.preferredWidth: page.cellW
                Layout.minimumWidth: page.cellW
                Layout.fillWidth: false
                mix: modelData
            }
        }

        // one row per channel: label + one cell per mix. Repeater children are flattened into the grid.
        Repeater {
            model: page.channels
            delegate: Repeater {
                id: row
                required property string modelData
                model: [""].concat(page.mixes)          // "" = the label column
                delegate: Loader {
                    required property string modelData
                    required property int index
                    Layout.preferredWidth: index === 0 ? page.labelW : page.cellW
                    Layout.minimumWidth: Layout.preferredWidth
                    Layout.fillWidth: false
                    Layout.fillHeight: true
                    property string channel: row.modelData
                    property string mix: modelData
                    sourceComponent: index === 0 ? labelComp : cellComp
                    onLoaded: { item.channel = channel; item.mix = mix }
                }
            }
        }
    }

    Component {
        id: labelComp
        Kirigami.Heading {
            property string channel
            property string mix
            level: 3
            text: Mixer.channelName(channel)
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
    Component {
        id: cellComp
        CellFader { channel: ""; mix: "" }
    }
}
