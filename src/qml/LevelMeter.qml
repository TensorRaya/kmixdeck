// SPDX-License-Identifier: GPL-3.0-or-later
// A thin peak meter (UX-6), vertical by default, `horizontal: true` for the Wave-Link-style bars under headers.
// Input is linear 0..1; drawn on a dB scale from -60 to 0 with a slowly falling peak-hold line.
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: meter
    property double peak: 0            // linear 0..1
    property double floorDb: -60
    property bool horizontal: false
    implicitWidth: horizontal ? Kirigami.Units.gridUnit * 4 : Kirigami.Units.smallSpacing * 1.5
    implicitHeight: horizontal ? Kirigami.Units.smallSpacing : Kirigami.Units.gridUnit * 4

    readonly property double db: peak > 0 ? 20 * Math.log10(peak) : floorDb
    readonly property double frac: Math.max(0, Math.min(1, (db - floorDb) / -floorDb))
    property double hold: 0
    readonly property color levelColor: db > -6 ? Kirigami.Theme.negativeTextColor : db > -18 ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.positiveTextColor

    onFracChanged: if (frac > hold) hold = frac
    Timer { interval: 40; running: meter.visible && meter.hold > 0; repeat: true; onTriggered: meter.hold = Math.max(0, meter.hold - 0.02) }

    Rectangle { anchors.fill: parent; radius: Math.min(width, height) / 2; color: Qt.alpha(Kirigami.Theme.textColor, 0.12) }
    Rectangle {   // level
        anchors { left: parent.left; bottom: parent.bottom }
        anchors.right: meter.horizontal ? undefined : parent.right
        anchors.top: meter.horizontal ? parent.top : undefined
        width: meter.horizontal ? parent.width * meter.frac : parent.width
        height: meter.horizontal ? parent.height : parent.height * meter.frac
        radius: Math.min(width, height) / 2
        color: meter.levelColor
        Behavior on width { enabled: meter.horizontal; NumberAnimation { duration: 30 } }
        Behavior on height { enabled: !meter.horizontal; NumberAnimation { duration: 30 } }
    }
    Rectangle {   // peak hold
        visible: meter.hold > 0.02
        color: Kirigami.Theme.textColor
        opacity: 0.7
        x: meter.horizontal ? parent.width * meter.hold - width / 2 : 0
        y: meter.horizontal ? 0 : parent.height * (1 - meter.hold) - height / 2
        width: meter.horizontal ? 2 : parent.width
        height: meter.horizontal ? parent.height : 2
    }
}
