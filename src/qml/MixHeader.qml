// SPDX-License-Identifier: GPL-3.0-or-later
// Column header of the matrix: mix name, master fader/mute (MX-6), where this mix goes (MX-3a/b, DV-2).
// Root is a Control so it has a `background` — the red "muted" backdrop (MX-10) lives there, outside the layout.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

QQC2.Control {
    id: header
    property string mix
    property string outputDevice: Mixer.mixOutputDevice(mix)
    property bool outputPresent: Mixer.mixOutputPresent(mix)
    property double masterValue: Mixer.mixVolume(mix)      // cubic 0..1 (MX-6)
    property bool masterMuted: Mixer.mixMuted(mix)
    padding: Kirigami.Units.smallSpacing

    background: Rectangle {
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.negativeBackgroundColor
        border.color: Kirigami.Theme.negativeTextColor
        border.width: 1
        visible: header.masterMuted
    }

    Connections {
        target: Mixer
        function onMixChanged(slug) {
            if (slug !== header.mix) return
            header.outputDevice = Mixer.mixOutputDevice(slug)
            header.outputPresent = Mixer.mixOutputPresent(slug)
            header.masterValue = Mixer.mixVolume(slug)
            header.masterMuted = Mixer.mixMuted(slug)
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Heading {
                Layout.fillWidth: true
                level: 4
                horizontalAlignment: Text.AlignHCenter
                text: header.masterMuted ? i18nc("@title mix header while muted, %1 mix name", "%1 — MUTED", Mixer.mixName(header.mix)) : Mixer.mixName(header.mix)
                color: header.masterMuted ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
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
        // master fader + mute (MX-6): what the whole mix sends to its outputs and to OBS
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            QQC2.ToolButton {
                id: muteButton
                icon.name: header.masterMuted ? "audio-volume-muted" : "audio-volume-high"
                checkable: true
                checked: header.masterMuted
                onToggled: { checked = Qt.binding(() => header.masterMuted); Mixer.toggleMixMute(header.mix) }
                QQC2.ToolTip.text: i18n("Mute the whole mix (all outputs and the capture source)")
                QQC2.ToolTip.visible: hovered
                Accessible.name: i18n("Mute mix %1", Mixer.mixName(header.mix))
            }
            QQC2.Slider {
                id: master
                Layout.fillWidth: true
                from: 0; to: 1; stepSize: 0.001
                value: header.masterValue
                onMoved: Mixer.setMixVolume(header.mix, value)
                Accessible.name: i18n("Master volume of mix %1", Mixer.mixName(header.mix))
                QQC2.ToolTip.text: {
                    const lin = Math.pow(master.value, 3)
                    return lin <= 0.0005 ? "−∞ dB" : (20 * Math.log10(lin)).toFixed(1) + " dB"
                }
                QQC2.ToolTip.visible: hovered || pressed
                QQC2.ToolTip.delay: 0
            }
        }
        QQC2.ToolButton {
            id: outButton
            Layout.fillWidth: true
            Layout.maximumWidth: header.availableWidth     // long device names elide instead of widening the column
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
}
