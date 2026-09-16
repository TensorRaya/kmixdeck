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
    property string iconName: Mixer.channelIcon(channel)
    readonly property bool compact: width < Kirigami.Units.gridUnit * 21   // only the FX button folds into ⋮; listen stays
    property bool dropActive: false           // UX-11: a drag hovers this row

    Connections {
        target: Mixer
        function onChannelChanged(slug) {
            if (slug !== header.channel) return
            header.inputDevice = Mixer.channelDevice(slug)
            header.inputPresent = Mixer.channelInputPresent(slug)
            header.muted = Mixer.channelMuted(slug)
            header.hasFx = Mixer.fxEnabled("channel", slug)
            header.iconName = Mixer.channelIcon(slug)
        }
    }

    QQC2.ToolTip.text: i18n("Hardware input feeding this channel — click the input line to change it. Applications can be routed here regardless.")
    QQC2.ToolTip.visible: inHover.hovered
    QQC2.ToolTip.delay: 800
    Rectangle { visible: !header.first; anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: Kirigami.Units.largeSpacing; rightMargin: Kirigami.Units.largeSpacing } height: 1; color: Qt.alpha(Kirigami.Theme.textColor, 0.10) }
    TapHandler { acceptedButtons: Qt.RightButton; onTapped: ctxMenu.popup() }

    // UX-11: drop an application row here to add this channel to its assignment (CH-12 keeps the others)
    DropArea {
        anchors.fill: parent
        keys: ["x-kmixdeck-app"]
        onEntered: header.dropActive = true
        onExited: header.dropActive = false
        onDropped: (drop) => { Mixer.assignApp(drop.text, [header.channel], true); header.dropActive = false }
    }
    // faint highlight while a drag hovers the row
    Rectangle {
        anchors.fill: parent
        visible: header.dropActive
        color: Qt.alpha(Kirigami.Theme.highlightColor, 0.18)
        radius: Kirigami.Units.smallSpacing
    }

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
                source: header.iconName
            }
            TapHandler { onTapped: applicationWindow().iconDialogOpen("channel", header.channel) }
            QQC2.ToolTip.text: i18n("Change icon")
            QQC2.ToolTip.visible: tileHover.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            HoverHandler { id: tileHover; cursorShape: Qt.PointingHandCursor
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
        // the channel's level (ADR 0006) — gain lives in the crosspoints (ADR 0002), so no fader here.
        // Vertical and clearly a meter: the 3 px horizontal bar read as a broken/empty fader on the laptop
        // screenshot (2026-09-16) whenever the channel was silent.
        LevelMeter {
            id: chMeter
            Layout.preferredWidth: Kirigami.Units.smallSpacing * 1.5
            Layout.preferredHeight: Kirigami.Units.gridUnit * 1.8
            Layout.alignment: Qt.AlignVCenter
            horizontal: false
            opacity: peak > 0.001 ? 1 : 0.5
            QQC2.ToolTip.text: i18n("Channel level (what every mix receives)")
            QQC2.ToolTip.visible: chMeterHover.hovered
            HoverHandler { id: chMeterHover }
            Connections { target: Mixer; function onPeaksChanged() { chMeter.peak = header.muted ? 0 : Mixer.peak("channel/" + header.channel) } }
        }

        // UX-12 listen: hold = only this channel reaches the main output, release restores everything.
        // Headphones like in the mix header — it sat next to the mute button with the SAME speaker glyph (laptop
        // screenshot 2026-09-16: two identical icons, one of them meaning "solo").
        QQC2.ToolButton {
            icon.name: "audio-headphones"
            display: QQC2.AbstractButton.IconOnly
            text: i18n("Listen to this channel")
            onPressed: Mixer.audition("channel", header.channel)
            onReleased: Mixer.stopAudition()
            onCanceled: Mixer.stopAudition()
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
        }

        // effects — highlighted when a chain is active (ADR 0008)
        QQC2.ToolButton {
            visible: !header.compact || header.hasFx
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
        QQC2.MenuItem { text: i18n("Icon…"); icon.name: "preferences-desktop-icons"; onTriggered: applicationWindow().iconDialogOpen("channel", header.channel) }
        QQC2.MenuItem {   // UX-9
            readonly property int idx: Mixer.channelSlugs.indexOf(header.channel)
            text: i18n("Move up"); icon.name: "go-up"; enabled: idx > 0
            onTriggered: Mixer.moveChannel(header.channel, idx - 1)
        }
        QQC2.MenuItem {
            readonly property int idx: Mixer.channelSlugs.indexOf(header.channel)
            text: i18n("Move down"); icon.name: "go-down"; enabled: idx >= 0 && idx < Mixer.channelSlugs.length - 1
            onTriggered: Mixer.moveChannel(header.channel, idx + 1)
        }
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
