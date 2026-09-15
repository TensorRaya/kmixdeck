// SPDX-License-Identifier: GPL-3.0-or-later
// One channel row in the channel panel, Wave Link style: `[icon tile] [name / input] [mute] [own fader] [fx] [⋮]`
// (CH-3, ADR 0007). The input line dims when the configured device is unplugged (DV-9); the value is kept.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Item {
    id: header
    property string channel
    property string mix                       // unused; Loader/Repeater contract with CellFader
    property bool first: false
    property string inputDevice: Mixer.channelDevice(channel)
    property bool inputPresent: Mixer.channelInputPresent(channel)
    property bool muted: Mixer.channelMuted(channel)
    property bool hasFx: Mixer.fxEnabled("channel", channel)

    Connections {
        target: Mixer
        function onChannelChanged(slug) {
            if (slug !== header.channel) return
            header.inputDevice = Mixer.channelDevice(slug)
            header.inputPresent = Mixer.channelInputPresent(slug)
            header.muted = Mixer.channelMuted(slug)
            header.hasFx = Mixer.fxEnabled("channel", slug)
        }
    }

    Rectangle { visible: !header.first; anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: Kirigami.Units.largeSpacing; rightMargin: Kirigami.Units.largeSpacing } height: 1; color: Qt.alpha(Kirigami.Theme.textColor, 0.10) }
    TapHandler { acceptedButtons: Qt.RightButton; onTapped: ctxMenu.popup() }

    RowLayout {
        anchors { fill: parent; leftMargin: Kirigami.Units.smallSpacing * 1.5; rightMargin: Kirigami.Units.smallSpacing / 2 }
        spacing: Kirigami.Units.smallSpacing * 1.5

        // icon tile — the channel's icon on a darker rounded square
        Rectangle {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 2.2
            Layout.preferredHeight: Kirigami.Units.gridUnit * 2.2
            radius: Kirigami.Units.smallSpacing
            color: Qt.darker(Kirigami.Theme.alternateBackgroundColor, 1.25)
            Kirigami.Icon {
                anchors.centerIn: parent
                width: parent.width * 0.6; height: width
                source: Mixer.channelIcon(header.channel)
            }
        }

        // name + input line
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 5
            spacing: 0
            QQC2.Label {
                Layout.fillWidth: true
                text: Mixer.channelName(header.channel)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            QQC2.Label {
                id: inLabel
                Layout.fillWidth: true
                font: Kirigami.Theme.smallFont
                opacity: header.inputPresent ? 0.6 : 0.4
                elide: Text.ElideRight
                text: {
                    if (header.inputDevice.length === 0) return i18nc("@label channel has no hardware input, apps only", "Apps only")
                    const d = Mixer.inputDevices.find(x => x.nodeName === header.inputDevice)
                    const name = d ? d.description : header.inputDevice
                    return header.inputPresent ? name : i18nc("@label %1 device name, device is unplugged", "%1 (unplugged)", name)
                }
                TapHandler { onTapped: inMenu.popup() }
                HoverHandler { id: inHover; cursorShape: Qt.PointingHandCursor }
                QQC2.ToolTip.text: i18n("Hardware input feeding this channel — click to change. Applications can be routed here regardless.")
                QQC2.ToolTip.visible: inHover.hovered
            }
        }

        // mute + the channel's own fader (what all mixes receive)
        QQC2.ToolButton {
            icon.name: header.muted ? "audio-volume-muted" : "audio-volume-high"
            icon.color: header.muted ? Kirigami.Theme.negativeTextColor : undefined
            checkable: true; checked: header.muted
            display: QQC2.AbstractButton.IconOnly
            text: header.muted ? i18n("Unmute channel") : i18n("Mute channel")
            onToggled: { checked = Qt.binding(() => header.muted); Mixer.toggleChannelMute(header.channel) }
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
        }
        // the channel's level (ADR 0006) — gain lives in the crosspoints (ADR 0002), so no fader here
        LevelMeter {
            id: chMeter
            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
            Layout.preferredHeight: Kirigami.Units.smallSpacing
            horizontal: true
            QQC2.ToolTip.text: i18n("Channel level"); QQC2.ToolTip.visible: chHover.hovered
            HoverHandler { id: chHover }
            Connections { target: Mixer; function onPeaksChanged() { chMeter.peak = header.muted ? 0 : Mixer.peak("channel/" + header.channel) } }
        }

        // effects — highlighted when a chain is active (ADR 0008)
        QQC2.ToolButton {
            icon.name: "view-media-equalizer"
            icon.color: header.hasFx ? Kirigami.Theme.positiveTextColor : undefined
            display: QQC2.AbstractButton.IconOnly
            text: header.hasFx ? i18n("Effects (active)…") : i18n("Effects…")
            onClicked: applicationWindow().fxPanelOpen("channel", header.channel)
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
        }
        QQC2.ToolButton {
            icon.name: "overflow-menu"
            display: QQC2.AbstractButton.IconOnly
            text: i18n("Channel actions")
            onClicked: ctxMenu.popup()
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
        }
    }

    QQC2.Menu {
        id: ctxMenu
        QQC2.MenuItem { text: i18n("Rename…"); icon.name: "edit-rename"; onTriggered: applicationWindow().renameDialogOpen("channel", header.channel) }
        QQC2.MenuItem { text: i18n("Hardware input…"); icon.name: "audio-input-microphone"; onTriggered: inMenu.popup() }
        QQC2.MenuItem { text: i18n("Effects…"); icon.name: "view-media-equalizer"; onTriggered: applicationWindow().fxPanelOpen("channel", header.channel) }
        QQC2.MenuItem {
            text: i18n("New applications start here")
            icon.name: "go-jump"
            checkable: true
            checked: Mixer.defaultChannel === header.channel
            onTriggered: Mixer.defaultChannel = checked ? header.channel : ""
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem { text: i18n("Remove channel"); icon.name: "edit-delete"; onTriggered: Mixer.removeChannel(header.channel) }   // undo via toast (CH-9)
    }
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
        QQC2.MenuItem {   // configured but unplugged → stays visible so the user sees the current choice (DV-9)
            visible: !header.inputPresent && header.inputDevice.length > 0
            height: visible ? implicitHeight : 0
            text: i18nc("@item unplugged device kept as selection", "%1 (unplugged)", header.inputDevice)
            checkable: true; checked: true; enabled: false
        }
    }
}
