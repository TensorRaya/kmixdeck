// SPDX-License-Identifier: GPL-3.0-or-later
// Effects editor for one channel or mix (ADR 0008). Reads the chain JSON from the client, edits a local copy,
// writes it back on every change — the daemon validates and rebuilds the filter-chain live.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.FormLayout {
    id: panel
    property string kind: "channel"           // "channel" | "mix"
    property string slug: ""
    property string title: ""
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

    Item {
        Layout.fillWidth: true
        implicitHeight: head.implicitHeight
        RowLayout {
            id: head
            width: parent.width
            QQC2.Label { text: i18n("Effects on %1", panel.title); font.bold: true; Layout.fillWidth: true }
            QQC2.CheckBox {
                text: i18nc("@action enable/disable whole effect chain", "Enabled")
                checked: panel.chain.enabled
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
                    QQC2.Label { text: modelData.type; font.bold: true }
                    Item { Layout.fillWidth: true }
                    QQC2.CheckBox {
                        text: i18nc("@action bypass one effect", "On")
                        checked: modelData.enabled !== false
                        onToggled: { card.chainEdit("enabled", checked); }
                    }
                    QQC2.ToolButton { icon.name: "arrow-up"; onClicked: panel.moveUp(index) }
                    QQC2.ToolButton { icon.name: "remove"; onClicked: panel.removeEffect(index) }
                }
                Repeater {
                    model: {
                        const spec = Mixer.fxTypes().find(t => t.type === modelData.type)
                        return spec ? spec.params : []
                    }
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        QQC2.Label { text: modelData.label; Layout.preferredWidth: Kirigami.Units.gridUnit * 9 }
                        QQC2.Slider {
                            Layout.fillWidth: true
                            from: modelData.min; to: modelData.max
                            value: (card.paramsOf(modelData.key) ?? modelData.def)
                            onMoved: card.paramEdit(modelData.key, value)
                        }
                        QQC2.Label { text: (card.paramsOf(modelData.key) ?? modelData.def) + (modelData.unit ? " " + modelData.unit : ""); font: Kirigami.Theme.smallFont }
                    }
                }
            }
            function paramsOf(key) {
                const p = modelData.params ?? {}
                return key in p ? p[key] : undefined
            }
            function paramEdit(key, v) {
                const c = JSON.parse(JSON.stringify(panel.chain))
                const e = c.chain[index]; e.params = e.params ?? {}; e.params[key] = Math.round(v * 100) / 100
                panel.chain = c; panel.save()
            }
            function chainEdit(key, v) {
                const c = JSON.parse(JSON.stringify(panel.chain))
                c.chain[index][key] = v
                panel.chain = c; panel.save()
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        QQC2.ComboBox {
            id: addBox
            Layout.fillWidth: true
            textRole: "label"; valueRole: "type"
            model: Mixer.fxTypes()
            onActivated: panel.addEffect(currentValue)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        QQC2.Label { text: i18nc("@label", "Preset:"); font: Kirigami.Theme.smallFont }
        Repeater {
            model: Object.keys(Mixer.fxPresets())
            delegate: QQC2.Button {
                required property string modelData
                text: modelData
                onClicked: panel.preset(modelData)
            }
        }
    }
}
