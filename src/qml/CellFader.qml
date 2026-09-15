// SPDX-License-Identifier: GPL-3.0-or-later
// One (channel, mix) cell: vertical fader + mute + dB readout (MX-2, UX-7).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.AbstractCard {
    id: cell
    property string channel
    property string mix
    readonly property bool present: Mixer.cellPresent(channel, mix)
    property double value: Mixer.cellVolume(channel, mix)     // cubic 0..1
    property bool muted: Mixer.cellMuted(channel, mix)
    property string follows: Mixer.cellFollows(channel, mix)      // MX-7: "" = independent

    Connections {
        target: Mixer
        function onCellChanged(ch, mx) {
            if (ch === cell.channel && mx === cell.mix) {
                if (!slider.pressed) cell.value = Mixer.cellVolume(ch, mx)
                cell.muted = Mixer.cellMuted(ch, mx)
                cell.follows = Mixer.cellFollows(ch, mx)
            }
        }
    }

    // cubic 0..1 → dB for display: 20*log10(v^3). 1.0 = 0 dB, 0.5 ≈ −18 dB
    function toDb(v) { return v <= 0 ? -Infinity : 60 * Math.log10(v) }

    enabled: present
    implicitWidth: Kirigami.Units.gridUnit * 9
    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: Kirigami.Units.gridUnit * 8
            spacing: Kirigami.Units.smallSpacing

        QQC2.Slider {
            id: slider
            Layout.fillHeight: true
            orientation: Qt.Vertical
            from: 0; to: 1; stepSize: 0.01
            value: cell.value
            enabled: !cell.muted
            onMoved: Mixer.setCellVolume(cell.channel, cell.mix, value)
            QQC2.ToolTip.visible: pressed
            QQC2.ToolTip.text: isFinite(cell.toDb(value)) ? i18n("%1 dB", cell.toDb(value).toFixed(1)) : i18n("−∞ dB")
        }
        // What this cell contributes to the mix: channel peak × cell gain (ADR 0006 — meters are per node).
        LevelMeter {
            id: cellMeter
            Layout.fillHeight: true
            property double channelPeak: 0
            peak: cell.muted ? 0 : channelPeak * Math.pow(cell.value, 3)
            Connections { target: Mixer; function onPeaksChanged() { cellMeter.channelPeak = Mixer.peak("channel/" + cell.channel) } }
        }
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            text: cell.muted ? i18n("muted") : (isFinite(cell.toDb(cell.value)) ? i18n("%1 dB", cell.toDb(cell.value).toFixed(1)) : "−∞")
            opacity: cell.muted ? 0.6 : 1
            font: Kirigami.Theme.smallFont
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 0
            QQC2.ToolButton {
                icon.name: cell.muted ? "audio-volume-muted" : "audio-volume-high"
                checkable: true
                checked: cell.muted
                display: QQC2.AbstractButton.IconOnly
                text: cell.muted ? i18n("Unmute") : i18n("Mute")
                onToggled: Mixer.setCellMuted(cell.channel, cell.mix, checked)
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
            }
            // MX-7: follow another mix's cell for this channel ("Stream follows Monitor"). Highlighted while linked;
            // any move of this fader breaks the link, exactly like Wave Link.
            QQC2.ToolButton {
                id: linkButton
                icon.name: cell.follows.length > 0 ? "link" : "remove-link"
                checkable: true
                checked: cell.follows.length > 0
                display: QQC2.AbstractButton.IconOnly
                visible: Mixer.mixSlugs.length > 1
                text: cell.follows.length > 0 ? i18n("Follows %1 — click to unlink", Mixer.mixName(cell.follows)) : i18n("Follow another mix…")
                onToggled: {
                    checked = Qt.binding(() => cell.follows.length > 0)
                    if (cell.follows.length > 0) Mixer.setCellFollows(cell.channel, cell.mix, "")
                    else if (Mixer.mixSlugs.length === 2) Mixer.setCellFollows(cell.channel, cell.mix, Mixer.mixSlugs.find(m => m !== cell.mix))
                    else linkMenu.popup()
                }
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.Menu {
                    id: linkMenu
                    Repeater {
                        model: Mixer.mixSlugs.filter(m => m !== cell.mix)
                        delegate: QQC2.MenuItem {
                            required property string modelData
                            text: i18n("Follow %1", Mixer.mixName(modelData))
                            onTriggered: Mixer.setCellFollows(cell.channel, cell.mix, modelData)
                        }
                    }
                }
            }
        }
    }
}
