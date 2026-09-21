// SPDX-FileCopyrightText: 2026 Raya Elena Solano
// SPDX-License-Identifier: GPL-3.0-or-later
// FX-9: ducking for one channel — turn it down while another channel talks. Same four values as
// `kmixdeck duck set` and duck.js: {duckedBy, depth, threshold, attack, release}. "How hard" is depth.
//
// Wurzel ist eine ScrollablePage, KEINE FormLayout — aus demselben Grund wie bei FxPanel.qml:
// Main.qml schiebt die Datei mit pushDialogLayer() auf den Stack, und das erwartet eine Page.
// Eine FormLayout hier laesst sich im KDE-Fenster GAR NICHT oeffnen, ohne dass irgendwo ein
// Fehler sichtbar wird (PageRow.qml stirbt an "Value is null", layers.depth bleibt 1).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.ScrollablePage {
    id: panel
    property string slug: ""
    property var cfg: ({duckedBy: "", depth: -12, threshold: -40, attack: 10, release: 200})

    // Die Grenzen sind die des Daemons (Mixer::setDucking). Ein Regler, der einen abgelehnten
    // Wert erzeugen kann, ist ein kaputter Regler — deshalb stehen sie hier genauso wie dort.
    readonly property var grenzen: ({
        depth:     {von: -60, bis: 0,   einheit: "dB"},
        threshold: {von: -60, bis: 0,   einheit: "dBFS"},
        attack:    {von: 2,   bis: 400, einheit: "ms"},
        release:   {von: 2,   bis: 800, einheit: "ms"}
    })

    function load() {
        const d = Mixer.ducking(slug)
        cfg = {duckedBy: d.duckedBy || "", depth: d.depth === undefined ? -12 : d.depth,
               threshold: d.threshold === undefined ? -40 : d.threshold,
               attack: d.attack === undefined ? 10 : d.attack,
               release: d.release === undefined ? 200 : d.release}
    }
    // Der Grund einer Ablehnung muss beim Benutzer ankommen, nicht nur im Log — sonst raet er,
    // welcher der fuenf Werte nicht gepasst hat (dieselbe Lehre wie FX-8 mit setFxChain).
    function save() {
        if (!Mixer.setDucking(slug, cfg))
            fehler.text = Mixer.lastError.length ? Mixer.lastError : i18n("The daemon refused the change.")
        else
            fehler.text = ""
        load()
    }
    function setzeWert(key, wert) { cfg[key] = wert; save() }

    Component.onCompleted: load()

    // Die laufende Absenkung mitschreiben: "wie hart" soll sichtbar sein, waehrend es passiert,
    // nicht nur eingestellt.
    Timer {
        interval: 200; running: panel.visible && panel.cfg.duckedBy.length > 0; repeat: true
        onTriggered: jetzt.wert = Mixer.duckReduction(panel.slug)
    }

    Kirigami.FormLayout {
        id: form

        QQC2.Label {
            text: i18n("Ducking on %1", panel.title); font.bold: true
            Kirigami.FormData.isSection: true
        }

        QQC2.ComboBox {
            id: trigger
            objectName: "duckBy"
            Kirigami.FormData.label: i18nc("@label:listbox channel that triggers the ducking", "Triggered by:")
            // Der eigene Kanal fehlt bewusst: der Daemon lehnt Selbst-Ducking ab, also soll die
            // Oberflaeche es nicht anbieten.
            model: [i18nc("@item:inlistbox no ducking configured", "Not ducked")].concat(
                Mixer.channelSlugs.filter((s) => s !== panel.slug).map((s) => Mixer.channelName(s)))
            property var slugs: [""].concat(Mixer.channelSlugs.filter((s) => s !== panel.slug))   // Property, nicht Methode
            currentIndex: Math.max(0, slugs.indexOf(panel.cfg.duckedBy))
            onActivated: panel.setzeWert("duckedBy", slugs[currentIndex])
        }

        QQC2.Label {
            id: jetzt
            objectName: "duckReduction"
            property real wert: 0
            Kirigami.FormData.label: i18nc("@label the reduction being applied right now", "Right now:")
            text: i18nc("@info reduction in decibels", "%1 dB", wert.toFixed(1))
            opacity: panel.cfg.duckedBy.length ? 1 : 0.5
        }

        Repeater {
            model: [
                {key: "depth",     label: i18nc("@label:slider how far the channel drops", "Depth (dB):"),
                 hilfe: i18n("How far the channel drops while the trigger is active.")},
                {key: "threshold", label: i18nc("@label:slider trigger level", "Threshold (dBFS):"),
                 hilfe: i18n("How loud the trigger has to be before ducking starts.")},
                {key: "attack",    label: i18nc("@label:slider ramp down", "Attack (ms):"),
                 hilfe: i18n("How fast it ducks once the trigger opens.")},
                {key: "release",   label: i18nc("@label:slider ramp up", "Release (ms):"),
                 hilfe: i18n("How long it takes to come back up.")}
            ]
            RowLayout {
                required property var modelData
                Kirigami.FormData.label: modelData.label
                QQC2.Slider {
                    objectName: "duckParam/" + parent.modelData.key
                    Layout.fillWidth: true
                    from: panel.grenzen[parent.modelData.key].von
                    to: panel.grenzen[parent.modelData.key].bis
                    stepSize: 1
                    snapMode: QQC2.Slider.SnapAlways
                    enabled: panel.cfg.duckedBy.length > 0
                    value: panel.cfg[parent.modelData.key]
                    QQC2.ToolTip.text: parent.modelData.hilfe
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    // moved: erst beim Loslassen schreiben — sonst ein Bus-Aufruf pro Pixel
                    onMoved: panel.setzeWert(parent.modelData.key, Math.round(value))
                }
                QQC2.Label {
                    text: Math.round(panel.cfg[parent.modelData.key]) + " " + panel.grenzen[parent.modelData.key].einheit
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 3
                }
            }
        }

        Kirigami.InlineMessage {
            id: fehler
            objectName: "duckError"
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: text.length > 0
        }

        QQC2.Button {
            objectName: "duckClear"
            text: i18nc("@action:button", "Stop ducking")
            icon.name: "edit-delete"
            enabled: panel.cfg.duckedBy.length > 0
            onClicked: { panel.cfg = {duckedBy: "", depth: -12, threshold: -40, attack: 10, release: 200}; panel.save() }
        }
    }
}
