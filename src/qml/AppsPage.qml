// SPDX-License-Identifier: GPL-3.0-or-later
// Running application streams and which channel each one plays into (CH-4, CH-10).
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
            required property var modelData
            width: list.width
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon { source: "applications-multimedia"; Layout.preferredWidth: Kirigami.Units.iconSizes.medium; Layout.preferredHeight: width }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label { text: modelData.name; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    QQC2.Label {
                        text: modelData.mediaName.length > 0 ? i18n("%1 · %2", modelData.binary, modelData.mediaName) : modelData.binary
                        opacity: 0.7; font: Kirigami.Theme.smallFont; elide: Text.ElideRight; Layout.fillWidth: true
                    }
                }
                QQC2.ComboBox {
                    id: target
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    model: [i18nc("@item app is not routed to any kmixdeck channel", "— not in mixer —")].concat(Mixer.channelSlugs.map(s => Mixer.channelName(s)))
                    currentIndex: {
                        const i = Mixer.channelSlugs.indexOf(modelData.channel)
                        return i < 0 ? 0 : i + 1
                    }
                    onActivated: (index) => { if (index > 0) Mixer.moveApp(modelData.path, Mixer.channelSlugs[index - 1]) }
                }
            }
        }
    }
}
