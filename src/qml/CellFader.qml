// SPDX-License-Identifier: GPL-3.0-or-later
// One crosspoint (channel → mix), Wave Link style (MX-2, UX-7): `[mute] [———fader——|—] [link]` in one dense row.
// No dB text (tooltip while dragging), a unity tick on the track, hairline above every row but the first.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Item {
    id: cell
    property string channel
    property string mix
    property bool first: false
    readonly property bool present: Mixer.cellPresent(channel, mix)
    property double value: Mixer.cellVolume(channel, mix)     // cubic 0..1
    property bool muted: Mixer.cellMuted(channel, mix)
    property string follows: Mixer.cellFollows(channel, mix)      // MX-7: "" = independent
    opacity: present ? 1 : 0.35

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
    function toDb(v) { return v <= 0 ? -Infinity : 60 * Math.log10(v) }   // cubic → dB; 1.0 = 0 dB

    Rectangle { visible: !cell.first; anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: Kirigami.Units.largeSpacing; rightMargin: Kirigami.Units.largeSpacing } height: 1; color: Qt.alpha(Kirigami.Theme.textColor, 0.10) }
    HoverHandler { id: hover }

    RowLayout {
        anchors { fill: parent; leftMargin: Kirigami.Units.smallSpacing * 1.5; rightMargin: Kirigami.Units.smallSpacing * 1.5 }
        spacing: Kirigami.Units.smallSpacing

        QQC2.ToolButton {
            icon.name: cell.muted ? "audio-volume-muted" : "audio-volume-high"
            icon.color: cell.muted ? Kirigami.Theme.negativeTextColor : undefined
            checkable: true; checked: cell.muted
            display: QQC2.AbstractButton.IconOnly
            text: cell.muted ? i18n("Unmute") : i18n("Mute")
            onToggled: Mixer.setCellMuted(cell.channel, cell.mix, checked)
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
        }

        Fader {
            id: slider
            Layout.fillWidth: true
            value: cell.value
            enabled: !cell.muted && cell.present
            // UX-13: the cell's own post-fader meter (daemon key cell/<ch>/<mix>) — not an estimate from the channel
            Connections { target: Mixer; function onPeaksChanged() { slider.peak = cell.muted ? 0 : Mixer.peak("cell/" + cell.channel + "/" + cell.mix) } }
            onMoved: Mixer.setCellVolume(cell.channel, cell.mix, value)
            onReset: Mixer.setCellVolume(cell.channel, cell.mix, 1.0)
            QQC2.ToolTip.visible: pressed || hovered
            QQC2.ToolTip.delay: pressed ? 0 : 900
            QQC2.ToolTip.text: isFinite(cell.toDb(value)) ? i18n("%1 dB", cell.toDb(value).toFixed(1)) : i18n("−∞ dB")
        }

        // MX-7 link: visible while linked or hovered; the unlinked state carries no information worth an icon.
        QQC2.ToolButton {
            id: linkButton
            icon.name: "link"
            icon.color: cell.follows.length > 0 ? Kirigami.Theme.highlightColor : undefined
            checkable: true; checked: cell.follows.length > 0
            display: QQC2.AbstractButton.IconOnly
            visible: Mixer.mixSlugs.length > 1
            opacity: cell.follows.length > 0 || hover.hovered ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
            text: cell.follows.length > 0 ? i18n("Follows %1 — click to unlink", Mixer.mixName(cell.follows)) : i18n("Follow another mix…")
            onToggled: {
                checked = Qt.binding(() => cell.follows.length > 0)
                if (cell.follows.length > 0) Mixer.setCellFollows(cell.channel, cell.mix, "")
                else if (Mixer.mixSlugs.length === 2) Mixer.setCellFollows(cell.channel, cell.mix, Mixer.mixSlugs.find(m => m !== cell.mix))
                else linkMenu.popup()
            }
            QQC2.ToolTip.text: text; QQC2.ToolTip.visible: hovered
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
