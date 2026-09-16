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
    property string channel                   // unused; Repeater contract
    property string outputDevice: Mixer.mixOutputDevice(mix)
    property bool outputPresent: Mixer.mixOutputPresent(mix)
    property double masterValue: Mixer.mixVolume(mix)      // cubic 0..1 (MX-6)
    property bool masterMuted: Mixer.mixMuted(mix)
    property var outputs: Mixer.mixOutputs(mix)              // MX-9
    property string fallbackOutput: Mixer.mixFallbackOutput(mix)   // DV-15
    property bool hasFx: Mixer.fxEnabled("mix", mix)
    property string iconName: Mixer.mixIcon(mix)
    padding: 0

    Connections {
        target: Mixer
        function onMixChanged(slug) {
            if (slug !== header.mix) return
            header.outputDevice = Mixer.mixOutputDevice(slug)
            header.outputPresent = Mixer.mixOutputPresent(slug)
            header.masterValue = Mixer.mixVolume(slug)
            header.masterMuted = Mixer.mixMuted(slug)
            header.outputs = Mixer.mixOutputs(slug)
            header.fallbackOutput = Mixer.mixFallbackOutput(slug)
            header.hasFx = Mixer.fxEnabled("mix", slug)
            header.iconName = Mixer.mixIcon(slug)
        }
    }

    // header card: darker than the panel, red tint while muted (MX-10); the mix meter is the card's bottom edge
    background: Rectangle {
        radius: Kirigami.Units.smallSpacing * 1.5
        color: header.masterMuted ? Kirigami.Theme.negativeBackgroundColor : Qt.darker(Kirigami.Theme.alternateBackgroundColor, 1.25)
        LevelMeter {
            id: mixMeter
            horizontal: true
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 3; rightMargin: 3; bottomMargin: 2 }
            height: 3
            // UX-13: what actually leaves towards the device (post master fader/mute) — out/<mix>; falls back to the
            // mix sink while the output edge is not metered yet
            Connections { target: Mixer; function onPeaksChanged() { const o = Mixer.peak("out/" + header.mix); mixMeter.peak = header.masterMuted ? 0 : (o > 0 ? o : Mixer.peak("mix/" + header.mix)) } }
        }
    }
    TapHandler { acceptedButtons: Qt.RightButton; onTapped: mixCtxMenu.popup() }

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing
        Item { Layout.preferredWidth: Kirigami.Units.smallSpacing }
        Rectangle {   // icon tile
            Layout.preferredWidth: Kirigami.Units.gridUnit * 2.2
            Layout.preferredHeight: Kirigami.Units.gridUnit * 2.2
            radius: Kirigami.Units.smallSpacing
            color: Qt.darker(Kirigami.Theme.alternateBackgroundColor, 1.6)
            Kirigami.Icon {
                anchors.centerIn: parent
                width: parent.width * 0.6; height: width
                source: header.iconName
                color: header.outputPresent || header.outputs.length === 0 ? Kirigami.Theme.textColor : Kirigami.Theme.neutralTextColor
            }
            TapHandler { onTapped: applicationWindow().iconDialogOpen("mix", header.mix) }
            QQC2.ToolTip.text: i18n("Change icon")
            QQC2.ToolTip.visible: tileHover.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            HoverHandler { id: tileHover; cursorShape: Qt.PointingHandCursor }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 3.5
            Layout.preferredWidth: Kirigami.Units.gridUnit * 5
            spacing: 0
            QQC2.Label {
                Layout.fillWidth: true
                text: header.masterMuted ? i18nc("@title mix header while muted, %1 mix name", "%1 — muted", Mixer.mixName(header.mix)) : Mixer.mixName(header.mix)
                color: header.masterMuted ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            QQC2.Label {   // "1 output" / device name / "No output" — click = output menu
                id: outLabel
                Layout.fillWidth: true
                font: Kirigami.Theme.smallFont
                opacity: header.outputPresent ? 0.6 : 0.9
                color: header.outputPresent ? Kirigami.Theme.textColor : Kirigami.Theme.neutralTextColor
                elide: Text.ElideRight
                text: {
                    if (header.outputs.length === 0) return i18nc("@label mix is not routed to a hardware output", "Capture only")
                    const name = header.outputs.length === 1 ? Mixer.deviceDescription(header.outputs[0])
                               : i18ncp("@label number of hardware outputs of a mix", "%1 output", "%1 outputs", header.outputs.length)
                    return header.outputPresent ? name : i18nc("@label %1 device name(s), unplugged", "%1 — unplugged", name)
                }
                // UX-2: this mix is what I hear → say so, the device name alone is long and gets elided
                readonly property bool heard: header.outputs.indexOf(Mixer.listeningDevice) >= 0 && Mixer.listeningDevice.length > 0
                font.weight: heard ? Font.DemiBold : Font.Normal
                TapHandler { onTapped: outMenu.popup() }
                HoverHandler { id: outHover; cursorShape: Qt.PointingHandCursor }
                QQC2.ToolTip.text: header.outputPresent
                    ? i18n("Where this mix plays — click to change. It is also available to OBS/Discord as the input “%1”.", Mixer.mixCaptureSource(header.mix))
                    : i18n("The device is unplugged. The mix is parked silently and resumes on this device as soon as it is back.")
                QQC2.ToolTip.visible: outHover.hovered
            }
        }
        // master: mute + fader (MX-6)
        QQC2.ToolButton {
            id: muteButton
            icon.name: header.masterMuted ? "audio-volume-muted" : "audio-volume-high"
            icon.color: header.masterMuted ? Kirigami.Theme.negativeTextColor : undefined
            checkable: true; checked: header.masterMuted
            display: QQC2.AbstractButton.IconOnly
            text: header.masterMuted ? i18n("Unmute mix") : i18n("Mute mix")
            onToggled: { checked = Qt.binding(() => header.masterMuted); Mixer.toggleMixMute(header.mix) }
            QQC2.ToolTip.text: i18n("Mute the whole mix (all outputs and the capture source)")
            QQC2.ToolTip.visible: hovered
            Accessible.name: i18n("Mute mix %1", Mixer.mixName(header.mix))
        }
        Fader {
            id: master
            Layout.fillWidth: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
            Layout.minimumWidth: Kirigami.Units.gridUnit * 2.5
            Layout.maximumWidth: Kirigami.Units.gridUnit * 6
            value: header.masterValue
            enabled: !header.masterMuted
            peak: header.masterMuted ? 0 : mixMeter.peak
            onMoved: Mixer.setMixVolume(header.mix, value)
            onReset: Mixer.setMixVolume(header.mix, 1.0)
            Accessible.name: i18n("Master volume of mix %1", Mixer.mixName(header.mix))
            QQC2.ToolTip.text: { const lin = Math.pow(master.value, 3); return lin <= 0.0005 ? "−∞ dB" : (20 * Math.log10(lin)).toFixed(1) + " dB" }
            QQC2.ToolTip.visible: pressed
        }
        // UX-12 listen: hold = only this mix reaches the headphones, release restores everything
        QQC2.ToolButton {
            icon.name: "audio-headphones"
            display: QQC2.AbstractButton.IconOnly
            text: i18n("Listen to this mix")
            onPressed: Mixer.audition("mix", header.mix)
            onReleased: Mixer.stopAudition()
            onCanceled: Mixer.stopAudition()
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
        }
        QQC2.ToolButton {
            icon.name: "view-media-equalizer"
            icon.color: header.hasFx ? Kirigami.Theme.positiveTextColor : undefined
            display: QQC2.AbstractButton.IconOnly
            text: header.hasFx ? i18n("Effects (active)…") : i18n("Effects…")
            onClicked: applicationWindow().fxPanelOpen("mix", header.mix)
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
        }
        QQC2.ToolButton {
            icon.name: "overflow-menu"
            display: QQC2.AbstractButton.IconOnly
            text: i18n("Mix actions")
            onClicked: mixCtxMenu.popup()
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
        }
        Item { Layout.preferredWidth: Kirigami.Units.smallSpacing / 2 }
    }

    QQC2.Menu {
        id: mixCtxMenu
        QQC2.MenuItem { text: i18n("Rename…"); icon.name: "edit-rename"; onTriggered: applicationWindow().renameDialogOpen("mix", header.mix) }
        QQC2.MenuItem { text: i18n("Icon…"); icon.name: "preferences-desktop-icons"; onTriggered: applicationWindow().iconDialogOpen("mix", header.mix) }
        QQC2.MenuItem {   // UX-9
            readonly property int idx: Mixer.mixSlugs.indexOf(header.mix)
            text: i18n("Move left"); icon.name: "go-previous"; enabled: idx > 0
            onTriggered: Mixer.moveMix(header.mix, idx - 1)
        }
        QQC2.MenuItem {
            readonly property int idx: Mixer.mixSlugs.indexOf(header.mix)
            text: i18n("Move right"); icon.name: "go-next"; enabled: idx >= 0 && idx < Mixer.mixSlugs.length - 1
            onTriggered: Mixer.moveMix(header.mix, idx + 1)
        }
        QQC2.MenuItem { text: i18n("Outputs…"); icon.name: "audio-headphones"; onTriggered: outMenu.popup() }
        QQC2.MenuItem { text: i18n("Effects…"); icon.name: "view-media-equalizer"; onTriggered: applicationWindow().fxPanelOpen("mix", header.mix) }
        QQC2.MenuSeparator {}
        QQC2.MenuItem { text: i18n("Remove mix"); icon.name: "edit-delete"; onTriggered: Mixer.removeMix(header.mix) }
    }
            QQC2.Menu {
        id: outMenu
        // MX-9: every entry is a toggle — a mix can play to headphones AND speakers
        QQC2.MenuItem {
            text: i18nc("@item mix output", "No output (capture only)")
            checkable: true; checked: header.outputs.length === 0
            enabled: header.outputs.length > 0
            onTriggered: { const outs = header.outputs.slice(); for (const n of outs) Mixer.toggleMixOutput(header.mix, n) }
        }
        QQC2.MenuSeparator {}
        Repeater {
            model: Mixer.outputDevices
            delegate: QQC2.MenuItem {
                required property var modelData
                text: modelData.description
                checkable: true; checked: header.outputs.indexOf(modelData.nodeName) >= 0
                onTriggered: Mixer.toggleMixOutput(header.mix, modelData.nodeName)
            }
        }
        // configured but unplugged outputs stay visible and de-selectable (DV-9)
        Repeater {
            model: header.outputs.filter(n => !Mixer.outputDevices.some(d => d.nodeName === n))
            delegate: QQC2.MenuItem {
                required property string modelData
                icon.name: "dialog-warning"
                text: i18nc("@item unplugged output kept as selection", "%1 (unplugged)", modelData)
                checkable: true; checked: true
                onTriggered: Mixer.toggleMixOutput(header.mix, modelData)
            }
        }
        QQC2.MenuSeparator {}
        // DV-15: where the mix goes while ALL outputs above are unplugged
        QQC2.Menu {
            title: header.fallbackOutput.length === 0 ? i18n("Fallback: silence")
                 : i18n("Fallback: %1", Mixer.deviceDescription(header.fallbackOutput))
            QQC2.MenuItem {
                text: i18nc("@item no fallback output", "Silence (never the system default)")
                checkable: true; checked: header.fallbackOutput.length === 0
                onTriggered: Mixer.setMixFallbackOutput(header.mix, "")
            }
            QQC2.MenuSeparator {}
            Repeater {
                model: Mixer.outputDevices
                delegate: QQC2.MenuItem {
                    required property var modelData
                    text: modelData.description
                    checkable: true; checked: modelData.nodeName === header.fallbackOutput
                    onTriggered: Mixer.setMixFallbackOutput(header.mix, modelData.nodeName)
                }
            }
        }
    }
}
