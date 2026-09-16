// SPDX-License-Identifier: GPL-3.0-or-later
// ADR 0009 / DV-20: pick a whole device OR a subset of its ports (one = mono, two = stereo, order = L then R).
// Reusable in the Add dialog (input side) and in the mix output picker (output side). Emits `refChosen(ref)` with
// the one reference syntax "node[:POS,POS]" — the same string the CLI takes and layout.json stores.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

ColumnLayout {
    id: picker
    required property string node          // device node.name
    required property string description   // device node.description
    property string current: ""            // currently selected ref (to highlight)
    property bool expanded: false
    // positions picked so far (0..2), in click order
    property var picked: []
    // ADR 0009 A2: with exactly ONE port picked, where it goes — "" = both sides (centre), "L", "R"
    property string side: ""
    // input side ("channel") or output side ("mix") — changes the wording only
    property string direction: "channel"
    signal refChosen(string ref, string suggestedName)

    readonly property var ports: Mixer.devicePortsVersion, Mixer.devicePorts(node)
    spacing: 0

    function emit() {
        const p = picked
        if (p.length === 0) return
        const sd = p.length === 1 ? side : ""
        refChosen(Mixer.makeDeviceRef(node, p, sd), picker.description.split(" ")[0] + " " + p.join("+") + (sd.length > 0 ? " " + sd : ""))
    }
    function toggle(pos) {
        let p = picked.slice()
        const i = p.indexOf(pos)
        if (i >= 0) p.splice(i, 1); else { if (p.length >= 2) p.shift(); p.push(pos) }
        picked = p
        if (p.length !== 1) side = ""
        emit()
    }
    function setSide(sd) { side = sd; emit() }

    // whole-device row
    QQC2.ItemDelegate {
        Layout.fillWidth: true
        highlighted: picker.current === picker.node
        onClicked: { picker.picked = []; picker.refChosen(picker.node, picker.description.split(" ")[0]) }
        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing
            Kirigami.Icon { source: "audio-input-microphone"; Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium; Layout.preferredHeight: width }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                QQC2.Label { text: picker.description; elide: Text.ElideRight; Layout.fillWidth: true; font.weight: Font.DemiBold }
                QQC2.Label {
                    text: picker.ports.length > 2
                        ? (picker.expanded ? i18np("whole device · %1 port", "whole device · %1 ports — or pick single ports below", picker.ports.length)
                                           : i18np("whole device · %1 port", "whole device · %1 ports", picker.ports.length))
                        : i18n("whole device")
                    opacity: 0.65; font: Kirigami.Theme.smallFont; elide: Text.ElideRight; Layout.fillWidth: true
                }
            }
            QQC2.ToolButton {
                visible: picker.ports.length > 2
                icon.name: picker.expanded ? "arrow-up" : "arrow-down"
                text: picker.expanded ? i18n("Hide ports") : i18n("Ports")
                display: QQC2.AbstractButton.TextBesideIcon
                LayoutMirroring.enabled: true   // label left, chevron right
                onClicked: picker.expanded = !picker.expanded
            }
        }
    }
    // port grid
    Flow {
        visible: picker.expanded
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.gridUnit * 2.5
        Layout.rightMargin: Kirigami.Units.largeSpacing
        Layout.bottomMargin: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing
        Repeater {
            model: picker.ports
            QQC2.Button {
                required property var modelData
                readonly property int order: picker.picked.indexOf(modelData.position)
                checkable: true
                checked: order >= 0
                text: order === 0 && picker.picked.length === 2 ? i18n("L · %1", modelData.label)
                    : order === 1 ? i18n("R · %1", modelData.label) : modelData.label
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: modelData.usedBy.length > 0
                    ? i18n("%1 — in use by %2", modelData.port, modelData.usedBy.join(", "))
                    : i18n("%1 — free. One port = mono (both sides), two = stereo (first is left).", modelData.port)
                opacity: modelData.usedBy.length > 0 && order < 0 ? 0.55 : 1
                onClicked: picker.toggle(modelData.position)
            }
        }
    }
    // one port: where does it go? (A2) — both sides is the mixing-desk default, L/R = pan hard
    RowLayout {
        visible: picker.expanded && picker.picked.length === 1
        Layout.leftMargin: Kirigami.Units.gridUnit * 2.5
        Layout.bottomMargin: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing
        QQC2.Label { text: picker.direction === "mix" ? i18nc("@label which side of the mix goes into this port", "From the mix:") : i18nc("@label where a single port lands in the channel", "Into the channel:"); opacity: 0.8 }
        QQC2.Button { text: picker.direction === "mix" ? i18nc("@option whole mix folded into one port", "Both (folded)") : i18nc("@option mono port on both sides", "Centre (both sides)"); checkable: true; checked: picker.side === ""; onClicked: picker.setSide("") }
        QQC2.Button { text: i18nc("@option left side only", "Left only");  checkable: true; checked: picker.side === "L"; onClicked: picker.setSide("L") }
        QQC2.Button { text: i18nc("@option right side only", "Right only"); checkable: true; checked: picker.side === "R"; onClicked: picker.setSide("R") }
    }
    QQC2.Label {
        visible: picker.expanded && picker.picked.length > 0
        Layout.leftMargin: Kirigami.Units.gridUnit * 2.5
        Layout.bottomMargin: Kirigami.Units.smallSpacing
        opacity: 0.7; font: Kirigami.Theme.smallFont
        text: picker.picked.length === 2 ? i18n("Stereo — %1 left, %2 right.", picker.picked[0], picker.picked[1])
            : picker.side === "L" ? i18n("%1 plays on the left side only.", picker.picked[0])
            : picker.side === "R" ? i18n("%1 plays on the right side only.", picker.picked[0])
            : picker.direction === "mix" ? i18n("The whole mix (left + right) goes into %1.", picker.picked[0])
            : i18n("Mono — %1 plays on both sides, centred.", picker.picked[0])
    }
}
