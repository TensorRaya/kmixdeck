// SPDX-License-Identifier: GPL-3.0-or-later
// UX-17 / ADR 0010 D4: the tray popover. Renders MixerClient.overview() and NOTHING else — if the tray needs a
// new fact, overview() grows and the window's summary shows the same fact. Controls here are the essentials only:
// mix master + mute, channel mute, listening device. Everything else is one double-click away.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.AbstractApplicationWindow {
    id: pop
    objectName: "trayOverview"
    flags: Qt.Popup | Qt.FramelessWindowHint
    color: "transparent"
    width: Kirigami.Units.gridUnit * 22
    height: Math.min(content.implicitHeight + Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 30)
    property var model: Mixer.overview()
    readonly property int nameWidth: Kirigami.Units.gridUnit * 5
    function refresh() { pop.model = Mixer.overview() }
    Connections { target: Mixer; function onLayoutChanged() { pop.refresh() } function onMixChanged() { pop.refresh() } function onChannelChanged() { pop.refresh() } function onServiceAvailableChanged() { pop.refresh() } function onListeningDeviceChanged() { pop.refresh() } }
    onVisibleChanged: { Mixer.metersEnabled = visible || (applicationWindow() ? applicationWindow().visible : false); if (visible) refresh() }
    onActiveChanged: if (!active && visible) pop.close()   // click elsewhere → gone, like every tray popup

    Rectangle {
        anchors.fill: parent
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.backgroundColor
        border.color: Kirigami.Theme.disabledTextColor; border.width: 1
        Kirigami.Theme.colorSet: Kirigami.Theme.Window
        Kirigami.Theme.inherit: false
    }
    QQC2.ScrollView {
        anchors.fill: parent; anchors.margins: Kirigami.Units.largeSpacing
        contentWidth: availableWidth
        ColumnLayout {
            id: content
            width: parent.width
            spacing: Kirigami.Units.smallSpacing

            // --- state line: daemon + what I hear
            RowLayout {
                Kirigami.Icon { source: pop.model.serviceAvailable ? "audio-headphones" : "dialog-error"; Layout.preferredWidth: Kirigami.Units.iconSizes.small; Layout.preferredHeight: width }
                QQC2.Label {
                    objectName: "trayListening"
                    Layout.fillWidth: true; elide: Text.ElideMiddle
                    text: !pop.model.serviceAvailable ? i18n("Service not running")
                        : pop.model.listeningDevice.length === 0 ? i18n("Not listening on any device")
                        : i18n("Listening on %1%2", pop.model.listeningDescription, pop.model.listeningPresent ? "" : i18n(" (unplugged)"))
                    color: pop.model.serviceAvailable && (pop.model.listeningDevice.length === 0 || !pop.model.listeningPresent) ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.textColor
                }
                QQC2.Label { objectName: "trayApps"; text: i18np("%1 app playing", "%1 apps playing", pop.model.runningApps); opacity: 0.7; Layout.rightMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing }
            }
            Kirigami.Separator { Layout.fillWidth: true }

            // --- mixes: master + mute + meter
            QQC2.Label { text: i18n("Mixes"); font.weight: Font.DemiBold; opacity: 0.7 }
            Repeater {
                model: pop.model.mixes
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true; spacing: 2
                    objectName: "trayMix/" + modelData.slug
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { objectName: "trayMixColor/" + modelData.slug; visible: modelData.color.length > 0; color: modelData.color.length > 0 ? modelData.color : "transparent"; Layout.preferredWidth: Kirigami.Units.smallSpacing; Layout.fillHeight: true; radius: width / 2 }   // MX-5
                        Kirigami.Icon { objectName: "trayMixIcon/" + modelData.slug; source: modelData.icon; Layout.preferredWidth: Kirigami.Units.iconSizes.small; Layout.preferredHeight: width; opacity: modelData.present ? 1 : 0.4 }
                        QQC2.Label { text: modelData.name; Layout.preferredWidth: pop.nameWidth; elide: Text.ElideRight; color: modelData.muted ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor }
                        QQC2.Slider {
                            objectName: "trayMixVolume/" + modelData.slug
                            Layout.fillWidth: true
                            from: 0; to: 1; value: modelData.volume
                            opacity: modelData.muted ? 0.4 : 1
                            onMoved: Mixer.setMixVolume(modelData.slug, value)
                            QQC2.ToolTip.text: i18n("Master: %1", value <= 0 ? "-∞ dB" : (20 * Math.log10(Math.pow(value, 3))).toFixed(1) + " dB"); QQC2.ToolTip.visible: hovered || pressed
                        }
                        QQC2.ToolButton {
                            objectName: "trayMixMute/" + modelData.slug
                            icon.name: modelData.muted ? "audio-volume-muted" : "audio-volume-high"
                            icon.color: modelData.muted ? Kirigami.Theme.negativeTextColor : undefined
                            checkable: true; checked: modelData.muted
                            display: QQC2.AbstractButton.IconOnly
                            text: modelData.muted ? i18n("Unmute %1", modelData.name) : i18n("Mute %1", modelData.name)
                            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
                            onToggled: { checked = Qt.binding(() => modelData.muted); Mixer.toggleMixMute(modelData.slug) }
                        }
                    }
                    // the meter sits UNDER the row, full width, like the header bars in the window (UX-13) — a bar next to a
                    // slider read as a second broken slider in the first render (2026-09-17)
                    LevelMeter { id: mm; objectName: "trayMixMeter/" + modelData.slug; horizontal: true; Layout.fillWidth: true; Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing; Layout.rightMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing; Layout.preferredHeight: 3
                                 Connections { target: Mixer; function onPeaksChanged() { mm.peak = modelData.muted ? 0 : Mixer.peak(modelData.meterKey); mm.rms = modelData.muted ? 0 : Mixer.peak("rms/" + modelData.meterKey); mm.clip = !modelData.muted && Mixer.peak("clip/" + modelData.meterKey) > 0 } } }
                }
            }
            Kirigami.Separator { Layout.fillWidth: true }

            // --- channels: mute + meter (no faders: the window has them; the tray answers "who is loud / who is muted")
            QQC2.Label { text: i18n("Channels"); font.weight: Font.DemiBold; opacity: 0.7 }
            Repeater {
                model: pop.model.channels
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true; spacing: 2
                    objectName: "trayChannel/" + modelData.slug
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { objectName: "trayChannelColor/" + modelData.slug; visible: modelData.color.length > 0; color: modelData.color.length > 0 ? modelData.color : "transparent"; Layout.preferredWidth: Kirigami.Units.smallSpacing; Layout.fillHeight: true; radius: width / 2 }   // MX-5
                        Kirigami.Icon { source: modelData.icon; Layout.preferredWidth: Kirigami.Units.iconSizes.small; Layout.preferredHeight: width; opacity: modelData.inputPresent ? 1 : 0.4 }
                        QQC2.Label { text: modelData.name; Layout.preferredWidth: pop.nameWidth; elide: Text.ElideRight; color: modelData.muted ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor }
                        QQC2.Slider {   // CH-7 trim — same column and width as the mix masters so the grid stays one grid
                            objectName: "trayChannelTrim/" + modelData.slug
                            Layout.fillWidth: true
                            from: 0; to: 1; value: modelData.trim
                            opacity: modelData.muted ? 0.4 : 1
                            onMoved: Mixer.setChannelTrim(modelData.slug, value)
                            QQC2.ToolTip.text: !modelData.inputPresent ? i18n("Input unplugged") : i18n("Trim: %1", value <= 0 ? "-∞ dB" : (20 * Math.log10(Math.pow(value, 3))).toFixed(1) + " dB"); QQC2.ToolTip.visible: hovered || pressed
                        }
                        QQC2.ToolButton {
                            objectName: "trayChannelMute/" + modelData.slug
                            icon.name: modelData.muted ? "audio-volume-muted" : "audio-volume-high"
                            icon.color: modelData.muted ? Kirigami.Theme.negativeTextColor : undefined
                            checkable: true; checked: modelData.muted
                            display: QQC2.AbstractButton.IconOnly
                            text: modelData.muted ? i18n("Unmute %1", modelData.name) : i18n("Mute %1", modelData.name)
                            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
                            onToggled: { checked = Qt.binding(() => modelData.muted); Mixer.toggleChannelMute(modelData.slug) }
                        }
                    }
                    LevelMeter { id: cm; objectName: "trayChannelMeter/" + modelData.slug; horizontal: true; Layout.fillWidth: true; Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing; Layout.rightMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing; Layout.preferredHeight: 3
                                 Connections { target: Mixer; function onPeaksChanged() { cm.peak = modelData.muted ? 0 : Mixer.peak(modelData.meterKey); cm.rms = modelData.muted ? 0 : Mixer.peak("rms/" + modelData.meterKey); cm.clip = !modelData.muted && Mixer.peak("clip/" + modelData.meterKey) > 0 } } }
                }
            }
            Kirigami.Separator { Layout.fillWidth: true }
            RowLayout {
                Item { Layout.fillWidth: true }
                QQC2.Button { objectName: "trayOpenWindow"; icon.name: "view-fullscreen"; text: i18n("Open mixer"); onClicked: { pop.close(); applicationWindow().raiseFromTray() } }
            }
        }
    }
}
