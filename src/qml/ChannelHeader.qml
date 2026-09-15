// SPDX-License-Identifier: GPL-3.0-or-later
// Row header of the matrix: channel name + which hardware input feeds it (CH-3, ADR 0007).
// Greys out when the configured input is unplugged (DV-9); the value is kept and comes back on its own.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

ColumnLayout {
    id: header
    property string channel
    property string mix                       // unused; keeps the Loader contract with CellFader
    property string inputDevice: Mixer.channelDevice(channel)
    property bool inputPresent: Mixer.channelInputPresent(channel)
    spacing: 0

    Connections {
        target: Mixer
        function onChannelChanged(slug) {
            if (slug !== header.channel) return
            header.inputDevice = Mixer.channelDevice(slug)
            header.inputPresent = Mixer.channelInputPresent(slug)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 0
        Kirigami.Heading {
            Layout.fillWidth: true
            level: 3
            text: Mixer.channelName(header.channel)
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            TapHandler { acceptedButtons: Qt.RightButton; onTapped: ctxMenu.popup() }
        }
        QQC2.ToolButton {
            icon.name: "overflow-menu"
            display: QQC2.AbstractButton.IconOnly
            text: i18n("Channel actions")
            onClicked: ctxMenu.popup()
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
        }
        QQC2.Menu {
            id: ctxMenu
            QQC2.MenuItem { text: i18n("Rename…"); icon.name: "edit-rename"; onTriggered: applicationWindow().renameDialogOpen("channel", header.channel) }
            QQC2.MenuSeparator {}
            QQC2.MenuItem {
                text: i18n("Remove channel"); icon.name: "edit-delete"
                onTriggered: Mixer.removeChannel(header.channel)   // undo is offered by the toast (CH-9)
            }
        }
    }
    QQC2.ToolButton {
        id: inButton
        Layout.fillWidth: true
        // absent device: same text, dimmed — the user must still see *what* is missing
        opacity: header.inputPresent ? 1.0 : 0.45
        icon.name: header.inputDevice.length === 0 ? "list-add"
                 : header.inputPresent ? "audio-input-microphone" : "network-disconnect"
        text: {
            if (header.inputDevice.length === 0) return i18nc("@label channel has no hardware input, apps only", "Apps only")
            const d = Mixer.inputDevices.find(x => x.nodeName === header.inputDevice)
            const name = d ? d.description : header.inputDevice
            return header.inputPresent ? name : i18nc("@label %1 device name, device is unplugged", "%1 (unplugged)", name)
        }
        font: Kirigami.Theme.smallFont
        display: QQC2.AbstractButton.TextBesideIcon
        onClicked: inMenu.popup()
        QQC2.ToolTip.text: i18n("Hardware input feeding this channel, e.g. a microphone or a capture card. Applications can be routed here regardless.")
        QQC2.ToolTip.visible: hovered

        QQC2.Menu {
            id: inMenu
            QQC2.MenuItem {
                text: i18nc("@item channel input", "No hardware input")
                checkable: true; checked: header.inputDevice.length === 0
                onTriggered: Mixer.setChannelDevice(header.channel, "")
            }
            QQC2.MenuSeparator {}
            Repeater {
                model: Mixer.inputDevices
                delegate: QQC2.MenuItem {
                    required property var modelData
                    text: modelData.description
                    checkable: true; checked: modelData.nodeName === header.inputDevice
                    onTriggered: Mixer.setChannelDevice(header.channel, modelData.nodeName)
                }
            }
            QQC2.MenuSeparator {}
            QQC2.MenuItem {
                text: i18n("New applications start here")
                icon.name: "go-jump"
                checkable: true
                checked: Mixer.defaultChannel === header.channel
                onTriggered: Mixer.defaultChannel = checked ? header.channel : ""
                QQC2.ToolTip.text: i18n("Applications kmixdeck has never seen before are placed on this channel. Ones you have moved keep their place.")
                QQC2.ToolTip.visible: hovered
            }
            // the configured device is unplugged → keep it selectable so the user sees the current choice
            QQC2.MenuItem {
                visible: !header.inputPresent && header.inputDevice.length > 0
                height: visible ? implicitHeight : 0
                text: i18nc("@item unplugged device kept as selection", "%1 (unplugged)", header.inputDevice)
                checkable: true; checked: true; enabled: false
            }
        }
    }
}
