// SPDX-License-Identifier: GPL-3.0-or-later
// One (channel, mix) cell: vertical fader + mute + dB readout (MX-2, UX-7).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.AbstractCard {
    id: cell
    required property string channel
    required property string mix
    readonly property bool present: Mixer.cellPresent(channel, mix)
    property double value: Mixer.cellVolume(channel, mix)     // cubic 0..1
    property bool muted: Mixer.cellMuted(channel, mix)

    Connections {
        target: Mixer
        function onCellChanged(ch, mx) {
            if (ch === cell.channel && mx === cell.mix) {
                if (!slider.pressed) cell.value = Mixer.cellVolume(ch, mx)
                cell.muted = Mixer.cellMuted(ch, mx)
            }
        }
    }

    // cubic 0..1 → dB for display: 20*log10(v^3). 1.0 = 0 dB, 0.5 ≈ −18 dB
    function toDb(v) { return v <= 0 ? -Infinity : 60 * Math.log10(v) }

    enabled: present
    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.Slider {
            id: slider
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 8
            orientation: Qt.Vertical
            from: 0; to: 1; stepSize: 0.01
            value: cell.value
            enabled: !cell.muted
            onMoved: Mixer.setCellVolume(cell.channel, cell.mix, value)
            QQC2.ToolTip.visible: pressed
            QQC2.ToolTip.text: isFinite(cell.toDb(value)) ? i18n("%1 dB", cell.toDb(value).toFixed(1)) : i18n("−∞ dB")
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            text: cell.muted ? i18n("muted") : (isFinite(cell.toDb(cell.value)) ? i18n("%1 dB", cell.toDb(cell.value).toFixed(1)) : "−∞")
            opacity: cell.muted ? 0.6 : 1
            font: Kirigami.Theme.smallFont
        }

        QQC2.ToolButton {
            Layout.alignment: Qt.AlignHCenter
            icon.name: cell.muted ? "audio-volume-muted" : "audio-volume-high"
            checkable: true
            checked: cell.muted
            display: QQC2.AbstractButton.IconOnly
            text: cell.muted ? i18n("Unmute") : i18n("Mute")
            onToggled: Mixer.setCellMuted(cell.channel, cell.mix, checked)
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
        }
    }
}
