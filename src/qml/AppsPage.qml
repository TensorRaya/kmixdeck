// SPDX-License-Identifier: GPL-3.0-or-later
// Running application streams and which channel each one plays into (CH-4, CH-10, CH-12, UX-10, UX-11).
// Every row: app's own icon + running dot (UX-10), picker for the primary channel, chips for extra channels
// (CH-12). Drag the row onto a channel row / mix header to assign there; each drop ADDS that destination,
// the picker replaces the whole assignment (UX-11).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.ScrollablePage {
    id: page
    title: i18n("Applications")

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: Mixer.apps.length === 0
        icon.name: "audio-volume-muted"
        text: i18n("No application is playing audio")
        explanation: i18n("Start a game, a browser tab or a call; it will appear here and you can pick the channel it plays into.")
    }

    ListView {
        id: list
        model: Mixer.apps
        delegate: QQC2.ItemDelegate {
            id: row
            required property var modelData
            width: list.width
            readonly property string appPath: modelData.path
            readonly property var allChannels: modelData.allChannels ?? []

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                // UX-10: the app's own icon; falls back to the generic one when it sets none
                Kirigami.Icon {
                    source: row.modelData.icon.length > 0 ? row.modelData.icon : "applications-multimedia"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                    Layout.preferredHeight: width
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label { text: row.modelData.name; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    QQC2.Label {
                        text: row.modelData.mediaName.length > 0 ? i18n("%1 · %2", row.modelData.binary, row.modelData.mediaName) : row.modelData.binary
                        opacity: 0.7; font: Kirigami.Theme.smallFont; elide: Text.ElideRight; Layout.fillWidth: true
                    }
                }

                // CH-12: extra channels as removable chips (the picker holds the primary)
                Repeater {
                    model: row.allChannels.slice(1)
                    delegate: QQC2.ToolButton {
                        required property var modelData
                        text: Mixer.channelName(modelData)
                        display: QQC2.AbstractButton.TextOnly
                        QQC2.ToolTip.text: i18n("Also playing into %1 — click to remove", Mixer.channelName(modelData))
                        QQC2.ToolTip.visible: hovered
                        onClicked: Mixer.assignApp(row.appPath, row.allChannels.filter(s => s !== modelData), false)
                    }
                }

                // UX-10: running dot — this stream currently produces sound
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: Kirigami.Units.smallSpacing * 1.5
                    Layout.preferredHeight: width
                    radius: width
                    visible: row.modelData.running
                    color: Kirigami.Theme.positiveTextColor
                    QQC2.ToolTip.text: i18n("Playing")
                    QQC2.ToolTip.visible: dotHover.hovered
                    HoverHandler { id: dotHover }
                }

                QQC2.ComboBox {
                    id: target
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    model: [i18nc("@item app is not routed to any kmixdeck channel", "— not in mixer —")].concat(Mixer.channelSlugs.map(s => Mixer.channelName(s)))
                    currentIndex: {
                        const i = Mixer.channelSlugs.indexOf(row.modelData.channel)
                        return i < 0 ? 0 : i + 1
                    }
                    // picker = replace the whole assignment (UX-11: dropping somewhere else replaces)
                    onActivated: (index) => Mixer.assignApp(row.appPath, index > 0 ? [Mixer.channelSlugs[index - 1]] : [], false)
                }
            }

            // UX-11 drag source: payload = the app's object path; rows accept it and add themselves
            Drag.active: dragHandler.active
            Drag.keys: ["x-kmixdeck-app"]
            Drag.hotSpot: Qt.point(width / 2, height / 2)
            Drag.mimeData: { "x-kmixdeck-app": row.appPath }
            DragHandler { id: dragHandler; target: null }
        }
    }
}
