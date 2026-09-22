// SPDX-FileCopyrightText: 2026 Raya Elena Solano
// SPDX-License-Identifier: GPL-3.0-or-later
// CT-8: soundboard — jingles and stingers on a button. Same data as `kmixdeck sample list` and soundboard.js:
// rows of {channel, name, path, length, gain, sounding} from Mixer.Samples.
//
// Wurzel ist eine ScrollablePage, KEINE FormLayout — derselbe Grund wie bei DuckPanel.qml und FxPanel.qml:
// Main.qml schiebt die Datei mit pushDialogLayer() auf den Stack, und das erwartet eine Page.
//
// Ein Board ist Opt-in: ohne Soundboard-Kanal zeigt die Seite, wie man einen anlegt, statt ein leeres Raster.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as Dialogs
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.ScrollablePage {
    objectName: "soundboardPanel"
    id: panel
    title: i18n("Soundboard")

    // Der Kanal, auf den „+ Sample" legt. Leer = noch keiner da.
    property string slug: ""
    property var rows: []

    function boards() {
        const out = []
        for (const s of Mixer.channelSlugs)
            if (Mixer.isSoundboard(s)) out.push(s)
        return out
    }
    function load() {
        const bs = boards()
        if (bs.length && (slug === "" || bs.indexOf(slug) < 0)) slug = bs[0]
        if (!bs.length) slug = ""
        rows = slug.length ? Mixer.samples(slug) : []
    }

    Component.onCompleted: load()
    // Die Liste muss dem Daemon folgen, waehrend man sie ansieht: ein Sample, das gerade klingt, ist ein
    // anderer Zustand als eines, das bereitliegt, und genau das will man auf der Buehne sehen.
    Connections { target: Mixer; function onSamplesChanged() { panel.load() } }
    Connections { target: Mixer; function onLayoutChanged() { panel.load() } }

    actions: [
        Kirigami.Action {
            text: i18n("Add sample…")
            icon.name: "list-add"
            enabled: panel.slug.length > 0
            onTriggered: dateiWahl.open()
        },
        Kirigami.Action {
            text: i18n("Stop all")
            icon.name: "media-playback-stop"
            // Panikknopf: „was auch immer laeuft, aus" darf nicht erst das leuchtende Pad suchen muessen.
            onTriggered: Mixer.stopSample("")
        }
    ]

    Dialogs.FileDialog {
        id: dateiWahl
        title: i18n("Choose an audio file")
        nameFilters: [i18n("Audio (*.wav *.flac *.ogg *.oga *.opus *.mp3)"), i18n("All files (*)")]
        onAccepted: {
            // Der Daemon oeffnet die Datei selbst, also braucht er einen Pfad im Dateisystem — deshalb
            // toLocalFile(). Eine nicht lesbare Datei lehnt er hier ab, nicht erst beim Druck aufs Pad.
            const name = Mixer.addSample(panel.slug, dateiWahl.selectedFile.toString().replace("file://", ""), "")
            if (!name.length) fehler.text = Mixer.lastError.length ? Mixer.lastError : i18n("The daemon refused the file.")
            else fehler.text = ""
            panel.load()
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // Opt-in-Hinweis statt leerem Raster: sagen, wie man ein Board bekommt.
        Kirigami.PlaceholderMessage {
            visible: panel.slug.length === 0
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 3
            icon.name: "media-playlist-append"
            text: i18n("No soundboard yet")
            explanation: i18n("A soundboard is a channel that plays files, with a fader in every mix like any other channel.")
            helpfulAction: Kirigami.Action {
                text: i18n("Create soundboard")
                icon.name: "list-add"
                onTriggered: { const s = Mixer.addSoundboard(i18n("Board")); if (s.length) panel.slug = s; panel.load() }
            }
        }

        // Board-Auswahl nur, wenn es mehr als eines gibt — ein Auswahlfeld mit einem Eintrag ist Rauschen.
        RowLayout {
            visible: panel.boards().length > 1
            Layout.fillWidth: true
            QQC2.Label { text: i18n("Board:") }
            QQC2.ComboBox {
                id: boardWahl
                objectName: "soundboardPick"
                Layout.fillWidth: true
                model: panel.boards()
                currentIndex: Math.max(0, panel.boards().indexOf(panel.slug))
                onActivated: { panel.slug = panel.boards()[currentIndex]; panel.load() }
                displayText: Mixer.channelName(panel.slug)
            }
        }

        QQC2.Label {
            id: fehler
            objectName: "soundboardError"
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.negativeTextColor
        }

        QQC2.Label {
            visible: panel.slug.length > 0 && panel.rows.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: i18n("No samples on this board yet — “Add sample…” takes a file from disk.")
            color: Kirigami.Theme.disabledTextColor
        }

        // Die Pads. Grosszuegig bemessen: das ist die Flaeche, die man mitten im Satz trifft, ohne zu zielen.
        GridLayout {
            Layout.fillWidth: true
            columns: Math.max(1, Math.floor(panel.width / (Kirigami.Units.gridUnit * 9)))
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Repeater {
                model: panel.rows
                delegate: ColumnLayout {
                    required property var modelData
                    readonly property bool klingt: modelData.sounding === true
                    spacing: Kirigami.Units.smallSpacing
                    Layout.fillWidth: true

                    QQC2.Button {
                        // Probebarer Name wie channelMeter/<slug> im Rest der UI: tests/integration/
                        // test_frontends_sync.py fragt damit genau EIN Pad ab (--probe samplePad/board:jingle.text).
                        objectName: "samplePad/" + modelData.channel + ":" + modelData.name
                        Layout.fillWidth: true
                        Layout.minimumHeight: Kirigami.Units.gridUnit * 4
                        // Ein Knopf fuer beide Richtungen: waehrend ein 30-s-Bett laeuft, will man keinen
                        // zweiten Knopf suchen muessen.
                        // Nur der Name, kein Statuswort im Label: `highlighted` + Stop-Icon zeigen den Zustand
                        // schon, und ein Umbruch im Knopftext macht den Knopf bei langen Namen unlesbar.
                        text: modelData.name
                        highlighted: klingt
                        icon.name: klingt ? "media-playback-stop" : "media-playback-start"
                        QQC2.ToolTip.text: modelData.path + (modelData.length > 0 ? i18n(" — %1 s", modelData.length.toFixed(1)) : "")
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        onClicked: {
                            const ok = klingt ? Mixer.stopSample(modelData.name)
                                              : Mixer.playSample(modelData.channel, modelData.name)
                            if (!ok) fehler.text = Mixer.lastError
                            panel.load()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        // Gain pro Sample (linear 0…4) wie in der CLI: ein leises Jingle anheben, ohne den
                        // Kanalfader zu bewegen — der wuerde jedes Sample des Boards mitnehmen.
                        QQC2.Slider {
                            objectName: "sampleGain/" + modelData.channel + ":" + modelData.name
                            Layout.fillWidth: true
                            from: 0; to: 4; stepSize: 0.05
                            value: modelData.gain === undefined ? 1 : modelData.gain
                            onMoved: { if (!Mixer.setSampleGain(modelData.channel, modelData.name, value)) fehler.text = Mixer.lastError }
                            QQC2.ToolTip.text: i18n("Gain for this sample only")
                            QQC2.ToolTip.visible: hovered
                        }
                        QQC2.ToolButton {
                            objectName: "sampleRemove/" + modelData.channel + ":" + modelData.name
                            icon.name: "edit-delete"
                            QQC2.ToolTip.text: i18n("Remove this sample")
                            QQC2.ToolTip.visible: hovered
                            onClicked: { if (!Mixer.removeSample(modelData.channel, modelData.name)) fehler.text = Mixer.lastError; panel.load() }
                        }
                    }
                }
            }
        }
    }
}
