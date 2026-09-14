// SPDX-License-Identifier: GPL-3.0-or-later
// A thin vertical peak meter (UX-6). Input is linear 0..1; drawn on a dB scale from -60 to 0 with a slow
// falling peak-hold line — the classic look, cheap to draw (two rectangles).
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: meter
    property double peak: 0            // linear 0..1
    property double floorDb: -60
    implicitWidth: Kirigami.Units.smallSpacing * 1.5

    readonly property double db: peak > 0 ? 20 * Math.log10(peak) : floorDb
    readonly property double frac: Math.max(0, Math.min(1, (db - floorDb) / -floorDb))
    property double hold: 0

    onFracChanged: if (frac > hold) hold = frac
    Timer { interval: 40; running: meter.visible && meter.hold > 0; repeat: true; onTriggered: meter.hold = Math.max(0, meter.hold - 0.02) }

    Rectangle { anchors.fill: parent; radius: width / 2; color: Kirigami.Theme.alternateBackgroundColor; border.width: 1; border.color: Qt.alpha(Kirigami.Theme.textColor, 0.15) }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: parent.height * meter.frac
        radius: width / 2
        color: meter.db > -6 ? Kirigami.Theme.negativeTextColor : meter.db > -18 ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.positiveTextColor
        Behavior on height { NumberAnimation { duration: 30 } }
    }
    Rectangle {   // peak hold
        anchors { left: parent.left; right: parent.right }
        y: parent.height * (1 - meter.hold) - height / 2
        height: 2
        visible: meter.hold > 0.02
        color: Kirigami.Theme.textColor
        opacity: 0.7
    }
}
