// SPDX-License-Identifier: GPL-3.0-or-later
// The one widget that makes the desk look like a desk: a horizontal fader whose track IS the meter.
//  - track: rounded pill, dark; the filled part up to the knob shows the gain
//  - inside the fill, a brighter segment shows the live peak (ADR 0006) — so level and gain read at one glance
//  - a unity tick at 0 dB; double-click snaps back to it
// Colours come from Kirigami.Theme so it follows Breeze / Breeze Dark; positive = green, like every meter.
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.Slider {
    id: fader
    property double peak: 0                 // linear 0..1 of the signal AFTER this fader
    signal reset()
    from: 0; to: 1; stepSize: 0.005
    implicitHeight: Kirigami.Units.gridUnit * 1.6
    implicitWidth: Kirigami.Units.gridUnit * 6
    leftPadding: knobD / 2; rightPadding: knobD / 2

    readonly property double trackH: Kirigami.Units.smallSpacing * 2
    readonly property double knobD: Kirigami.Units.gridUnit * 0.9
    readonly property double peakDb: peak > 0 ? 20 * Math.log10(peak) : -60
    readonly property double peakFrac: Math.max(0, Math.min(1, (peakDb + 60) / 60))

    background: Item {
        implicitWidth: fader.implicitWidth
        implicitHeight: fader.knobD
        x: 0; width: fader.width
        Rectangle {   // track — spans the knob's travel plus half a knob on each side, and STAYS inside the item.
            // Before 2026-09-16 it was `width: parent.width + knobD`, i.e. it stuck out knobD/2 on both sides and ran
            // into the neighbouring icon (visible in every mix header and cell row on the laptop).
            id: track
            anchors.verticalCenter: parent.verticalCenter
            x: 0; width: parent.width; height: fader.trackH; radius: height / 2
            color: Qt.alpha(Kirigami.Theme.textColor, fader.enabled ? 0.18 : 0.08)
            Rectangle {   // gain fill — up to the knob centre
                width: fader.knobD / 2 + fader.visualPosition * (parent.width - fader.knobD); height: parent.height; radius: height / 2
                color: fader.enabled ? Kirigami.Theme.positiveTextColor : Qt.alpha(Kirigami.Theme.textColor, 0.2)
                opacity: 0.45
            }
            Rectangle {   // live level inside the fill — brighter green, clipped at the knob
                id: liveFill
                width: fader.knobD / 2 + Math.min(fader.peakFrac, fader.visualPosition) * (parent.width - fader.knobD); height: parent.height; radius: height / 2
                // UX-16: same bands + hysteresis as LevelMeter (red only when it really clips the top band)
                property int band: 0
                Connections { target: fader; function onPeakDbChanged() { const d = fader.peakDb; let b = liveFill.band; if (b === 0 && d > -18) b = d > -6 ? 2 : 1; else if (b === 1) { if (d > -6) b = 2; else if (d < -20) b = 0 } else if (b === 2 && d < -8) b = d < -20 ? 0 : 1; liveFill.band = b } }
                color: liveFill.band === 2 ? Kirigami.Theme.negativeTextColor : liveFill.band === 1 ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.positiveTextColor
                visible: fader.enabled
                Behavior on width { NumberAnimation { duration: 40 } }
            }
            Rectangle {   // unity tick at the knob's centre for value 1.0 (0 dB)
                x: parent.width - fader.knobD / 2 - width / 2; anchors.verticalCenter: parent.verticalCenter
                width: 2; height: fader.trackH * 1.8; radius: 1
                color: Qt.alpha(Kirigami.Theme.textColor, 0.55)
            }
        }
    }
    handle: Rectangle {
        x: fader.leftPadding + fader.visualPosition * (fader.availableWidth - width)
        y: fader.topPadding + fader.availableHeight / 2 - height / 2
        width: fader.knobD; height: fader.knobD; radius: width / 2
        color: fader.pressed ? Qt.lighter(Kirigami.Theme.highlightColor, 1.2) : Kirigami.Theme.highlightColor
        border.width: 2; border.color: Kirigami.Theme.backgroundColor
        scale: fader.hovered || fader.pressed ? 1.1 : 1
        Behavior on scale { NumberAnimation { duration: 80 } }
    }
    TapHandler { onDoubleTapped: { fader.value = 1.0; fader.reset() } }
}
