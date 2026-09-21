// SPDX-License-Identifier: GPL-3.0-or-later
// Effects editor for one channel or mix (ADR 0008). Reads the chain JSON from the client, edits a local copy,
// writes it back on every change — the daemon validates and rebuilds the filter-chain live.
//
// Wurzel ist eine ScrollablePage, KEINE FormLayout. Bis zum 2026-09-21 war sie eine
// Kirigami.FormLayout, und Main.qml schob sie mit pushDialogLayer() auf den Stack —
// pushDialogLayer erwartet aber eine Page. Ergebnis: das Effekt-Panel liess sich im
// KDE-Fenster GAR NICHT oeffnen. Der Klick auf "Effects…" im Kanalkopf erzeugte das Objekt,
// PageRow.qml:1112 starb an "Value is null", layers.depth blieb 1, und im Fenster passierte
// sichtbar nichts. Kein Test hat es gemerkt, weil kein Test das Panel je geoeffnet hat: die
// FX-Tests gingen ueber CLI und Browser, und --self-test laedt die Datei nur, ohne sie zu
// pushen. Aufgefallen ist es erst, als FX-8 einen Probe GEGEN DAS FENSTER brauchte.
// Eine Page ist ausserdem das, was jede andere gepushte Datei hier ist (AddDialog, IconDialog,
// die PromptDialogs) — FxPanel war die einzige Ausnahme.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.ScrollablePage {
    id: panel
    property string kind: "channel"           // "channel" | "mix"
    property string slug: ""
    // `title` kommt von Page selbst — hier keine eigene Property deklarieren, sonst
    // verdeckt sie die der Page und der Kopf des Dialogs bleibt leer.
    property var chain: ({enabled: true, chain: []})

    function load() {
        const raw = Mixer.fxChain(kind, slug)
        chain = raw.length ? JSON.parse(raw) : {enabled: true, chain: []}
    }
    function save() { Mixer.setFxChain(kind, slug, JSON.stringify(chain)) }
    function preset(name) { chain = Mixer.fxPresets()[name]; save() }
    function addEffect(type) { chain.chain.push({type: type, enabled: true, params: {}}); save(); load() }
    function removeEffect(i) { chain.chain.splice(i, 1); save(); load() }
    function moveUp(i) { if (i > 0) { const a = chain.chain; [a[i-1], a[i]] = [a[i], a[i-1]] } save(); load() }

    Component.onCompleted: load()

    Kirigami.FormLayout {
        id: form
        Item {
            Layout.fillWidth: true
            implicitHeight: head.implicitHeight
            RowLayout {
                id: head
                width: parent.width
                QQC2.Label { text: i18n("Effects on %1", panel.title); font.bold: true; Layout.fillWidth: true }
                QQC2.CheckBox {
                    objectName: "fxEnabled"
                    text: i18nc("@action enable/disable whole effect chain", "Enabled")
                    // === statt roher Zuweisung: bei leerer Kette ist chain.enabled undefined, und
                    // QML warnte bei jedem Oeffnen "Unable to assign [undefined] to bool".
                    checked: panel.chain.enabled === true
                    onToggled: { panel.chain.enabled = checked; panel.save() }
                }
            }
        }

        Repeater {
            model: panel.chain.chain
            delegate: Kirigami.AbstractCard {
                id: card
                required property int index
                required property var modelData
                Layout.fillWidth: true
                contentItem: ColumnLayout {
                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.Label { text: card.modelData.type; font.bold: true }
                        Item { Layout.fillWidth: true }
                        QQC2.CheckBox {
                            text: i18nc("@action bypass one effect", "On")
                            checked: card.modelData.enabled !== false
                            onToggled: { card.chainEdit("enabled", checked); }
                        }
                        QQC2.ToolButton { icon.name: "arrow-up"; onClicked: panel.moveUp(card.index) }
                        QQC2.ToolButton { icon.name: "remove"; onClicked: panel.removeEffect(card.index) }
                    }
                    Repeater {
                        model: {
                            const spec = Mixer.fxTypes().find(t => t.type === card.modelData.type)
                            return spec ? spec.params : []
                        }
                        delegate: RowLayout {
                            id: prow
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            QQC2.Label { text: prow.modelData.label; Layout.preferredWidth: Kirigami.Units.gridUnit * 9 }
                            QQC2.Slider {
                                objectName: "fxParam/" + card.index + "/" + prow.modelData.key
                                Layout.fillWidth: true
                                from: prow.modelData.min; to: prow.modelData.max
                                value: (card.paramsOf(prow.modelData.key) ?? prow.modelData.def)
                                onMoved: card.paramEdit(prow.modelData.key, value)
                            }
                            QQC2.Label { text: (card.paramsOf(prow.modelData.key) ?? prow.modelData.def) + (prow.modelData.unit ? " " + prow.modelData.unit : ""); font: Kirigami.Theme.smallFont }
                        }
                    }
                }
                function paramsOf(key) {
                    const p = card.modelData.params ?? {}
                    return key in p ? p[key] : undefined
                }
                function paramEdit(key, v) {
                    const c = JSON.parse(JSON.stringify(panel.chain))
                    const e = c.chain[card.index]; e.params = e.params ?? {}; e.params[key] = Math.round(v * 100) / 100
                    panel.chain = c; panel.save()
                }
                function chainEdit(key, v) {
                    const c = JSON.parse(JSON.stringify(panel.chain))
                    c.chain[card.index][key] = v
                    panel.chain = c; panel.save()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            QQC2.ComboBox {
                id: addBox
                objectName: "fxAddType"
                Layout.fillWidth: true
                textRole: "label"; valueRole: "type"
                model: Mixer.fxTypes()
                onActivated: panel.addEffect(currentValue)
                // FX-8: an effect whose LADSPA plugin is missing must not look pickable. Before
                // 2026-09-21 the entry sat in the list like any other, the user picked it, and the
                // daemon refused the whole chain — the package name only reached the log. Greyed
                // out with the package in the tooltip puts the answer where the question is asked.
                delegate: QQC2.ItemDelegate {
                    id: opt
                    required property var modelData
                    required property int index
                    width: addBox.width
                    // probe-Name, damit der KDE-Zweig dieser Anforderung PRUEFBAR ist und nicht nur
                    // behauptet: qmllint sieht Syntax, --self-test nur, dass die Datei laedt.
                    objectName: "fxAddType/" + opt.modelData.type
                    enabled: opt.modelData.available !== false
                    text: opt.modelData.available === false
                          ? i18nc("@item effect needing an uninstalled plugin", "%1 — needs %2", opt.modelData.label, opt.modelData.package)
                          : opt.modelData.label
                    QQC2.ToolTip.visible: hovered && opt.modelData.available === false
                    QQC2.ToolTip.text: opt.modelData.available === false
                                       ? i18nc("@info:tooltip", "Install %1 to use this effect", opt.modelData.package) : ""
                    onClicked: { addBox.currentIndex = opt.index; addBox.activated(opt.index); addBox.popup.close() }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            QQC2.Label { text: i18nc("@label", "Preset:"); font: Kirigami.Theme.smallFont }
            Repeater {
                model: Object.keys(Mixer.fxPresets())
                delegate: QQC2.Button {
                    required property string modelData
                    objectName: "fxPreset/" + modelData
                    text: modelData
                    onClicked: panel.preset(modelData)
                }
            }
        }
    }
}
