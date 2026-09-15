// SPDX-License-Identifier: GPL-3.0-or-later
// Column header of the matrix: mix name + where this mix goes (hardware output, capture source) (MX-3a/b, DV-2).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

ColumnLayout {
    id: header
    property string mix
    property string outputDevice: Mixer.mixOutputDevice(mix)
    property bool outputPresent: Mixer.mixOutputPresent(mix)
    spacing: 0

    Connections {
        target: Mixer
        function onMixChanged(slug) {
            if (slug !== header.mix) return
            header.outputDevice = Mixer.mixOutputDevice(slug)
            header.outputPresent = Mixer.mixOutputPresent(slug)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        Kirigami.Heading {
            Layout.fillWidth: true
            level: 4
            horizontalAlignment: Text.AlignHCenter
            text: Mixer.mixName(header.mix)
            elide: Text.ElideRight
        }
        // horizontal mix meter next to the name
        LevelMeter {
            id: mixMeter
            Layout.preferredWidth: Kirigami.Units.smallSpacing * 1.5
            Layout.preferredHeight: Kirigami.Units.gridUnit * 1.2
            Layout.alignment: Qt.AlignVCenter
            Connections { target: Mixer; function onPeaksChanged() { mixMeter.peak = Mixer.peak("mix/" + header.mix) } }
        }
    }
    QQC2.ToolButton {
        id: outButton
        Layout.alignment: Qt.AlignHCenter
        opacity: header.outputPresent ? 1.0 : 0.45      // DV-9: unplugged → dimmed, value kept
        icon.name: header.outputDevice.length === 0 ? "network-disconnect"
                 : header.outputPresent ? "audio-headphones" : "dialog-warning"
        text: {
            if (header.outputDevice.length === 0) return i18nc("@label mix is not routed to a hardware output", "No output")
            const d = Mixer.outputDevices.find(x => x.nodeName === header.outputDevice)
            const name = d ? d.description : header.outputDevice
            return header.outputPresent ? name : i18nc("@label %1 device name, device is unplugged", "%1 (unplugged)", name)
        }
        font: Kirigami.Theme.smallFont
        display: QQC2.AbstractButton.TextBesideIcon
        onClicked: outMenu.popup()
        QQC2.ToolTip.text: header.outputPresent
            ? i18n("Where this mix plays. It is also available to OBS/Discord as the input “%1”.", Mixer.mixCaptureSource(header.mix))
            : i18n("The device is unplugged. The mix is parked silently and resumes on this device as soon as it is back.")
        QQC2.ToolTip.visible: hovered

        QQC2.Menu {
            id: outMenu
            QQC2.MenuItem {
                text: i18nc("@item mix output", "No output (capture only)")
                checkable: true; checked: header.outputDevice.length === 0
                onTriggered: Mixer.setMixOutputDevice(header.mix, "")
            }
            QQC2.MenuSeparator {}
            Repeater {
                model: Mixer.outputDevices
                delegate: QQC2.MenuItem {
                    required property var modelData
                    text: modelData.description
                    checkable: true; checked: modelData.nodeName === header.outputDevice
                    onTriggered: Mixer.setMixOutputDevice(header.mix, modelData.nodeName)
                }
            }
        }
    }
}
