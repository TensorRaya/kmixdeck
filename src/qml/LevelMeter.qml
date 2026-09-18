// SPDX-License-Identifier: GPL-3.0-or-later
// A thin peak meter (UX-6), vertical by default, `horizontal: true` for the Wave-Link-style bars under headers.
// Input is linear 0..1; drawn on a dB scale from -60 to 0 with a slowly falling peak-hold line.
import QtQuick
import org.kde.kirigami as Kirigami
import org.kmixdeck

Item {
    id: meter
    property double peak: 0            // linear 0..1
    property double rms: 0             // CH-7: linear 0..1, drawn as a solid core inside the peak bar (0 = not shown)
    property bool clip: false          // CH-7: red cap at the top/right end while lit (the daemon holds it ≈ 1.5 s)
    // Bind a daemon key and the meter feeds itself from Mixer.peak/rms/clip on every tick — one place, every frontend
    property string meterKey: ""
    Connections { target: Mixer; enabled: meter.meterKey.length > 0; function onPeaksChanged() { meter.peak = Mixer.peak(meter.meterKey); meter.rms = Mixer.peak("rms/" + meter.meterKey); meter.clip = Mixer.peak("clip/" + meter.meterKey) > 0 } }
    property double floorDb: -60
    property bool horizontal: false
    implicitWidth: horizontal ? Kirigami.Units.gridUnit * 4 : Kirigami.Units.smallSpacing * 1.5
    implicitHeight: horizontal ? Kirigami.Units.smallSpacing : Kirigami.Units.gridUnit * 4

    readonly property double db: peak > 0 ? 20 * Math.log10(peak) : floorDb
    readonly property double frac: Math.max(0, Math.min(1, (db - floorDb) / -floorDb))
    readonly property double rmsDb: rms > 0 ? 20 * Math.log10(rms) : floorDb
    readonly property double rmsFrac: Math.max(0, Math.min(1, (rmsDb - floorDb) / -floorDb))
    property double hold: 0
    // UX-16 colour bands with 2 dB hysteresis: a level hovering around a threshold must not flicker between two
    // colours (laptop 2026-09-16: "orange und grün wechselt sich zu schnell ab"). Ballistics (hold/fall) come from the
    // daemon, so every frontend agrees; here only the band decision is smoothed.
    property int band: 0            // 0 green, 1 amber, 2 red
    onDbChanged: {
        if (band === 0 && db > -18) band = db > -6 ? 2 : 1
        else if (band === 1) { if (db > -6) band = 2; else if (db < -20) band = 0 }
        else if (band === 2 && db < -8) band = db < -20 ? 0 : 1
    }
    readonly property color levelColor: band === 2 ? Kirigami.Theme.negativeTextColor : band === 1 ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.positiveTextColor

    // peak-hold marker: sits on the highest value for ~1 s, then slides down — the daemon already holds the bar itself
    onFracChanged: if (frac >= hold) { hold = frac; holdTimer.restart() }
    Timer { id: holdTimer; interval: 1000 }
    Timer { interval: 40; running: meter.visible && meter.hold > 0 && !holdTimer.running; repeat: true; onTriggered: meter.hold = Math.max(meter.frac, meter.hold - 0.01) }

    Rectangle { anchors.fill: parent; radius: Math.min(width, height) / 2; color: Qt.alpha(Kirigami.Theme.textColor, 0.12) }
    Rectangle {   // level
        anchors { left: parent.left; bottom: parent.bottom }
        anchors.right: meter.horizontal ? undefined : parent.right
        anchors.top: meter.horizontal ? parent.top : undefined
        width: meter.horizontal ? parent.width * meter.frac : parent.width
        height: meter.horizontal ? parent.height : parent.height * meter.frac
        radius: Math.min(width, height) / 2
        color: meter.levelColor
        Behavior on width { enabled: meter.horizontal; NumberAnimation { duration: 40; easing.type: Easing.Linear } }
        Behavior on height { enabled: !meter.horizontal; NumberAnimation { duration: 40; easing.type: Easing.Linear } }
    }
    Rectangle {   // CH-7 RMS core: the "how loud does it feel" part, brighter than the peak envelope
        visible: meter.rms > 0
        anchors { left: parent.left; bottom: parent.bottom }
        anchors.right: meter.horizontal ? undefined : parent.right
        anchors.top: meter.horizontal ? parent.top : undefined
        width: meter.horizontal ? parent.width * meter.rmsFrac : parent.width
        height: meter.horizontal ? parent.height : parent.height * meter.rmsFrac
        radius: Math.min(width, height) / 2
        color: Qt.lighter(meter.levelColor, 1.35)
        Behavior on width { enabled: meter.horizontal; NumberAnimation { duration: 40; easing.type: Easing.Linear } }
        Behavior on height { enabled: !meter.horizontal; NumberAnimation { duration: 40; easing.type: Easing.Linear } }
    }
    Rectangle {   // CH-7 clip indicator: a red cap at the 0 dBFS end, set off by a light gap so it reads as its own
        objectName: "clipIndicator"   // element even when the bar below it is red too — stays lit as long as the daemon says so
        visible: meter.clip
        color: Kirigami.Theme.negativeTextColor
        border { width: 1; color: Kirigami.Theme.backgroundColor }
        anchors.right: parent.right
        anchors.top: parent.top
        width: meter.horizontal ? Math.max(5, parent.height * 1.5) : parent.width
        height: meter.horizontal ? parent.height : Math.max(5, parent.width * 1.5)
        radius: 1
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
