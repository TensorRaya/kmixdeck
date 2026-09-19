// SPDX-License-Identifier: GPL-3.0-or-later
// CT-9: save the current mixable state (per-cell faders and mutes, mix masters and mutes, FX bypass,
// listening device) under a name. Deliberately NOT the channel/mix set and not the wiring — a scene is a
// snapshot of what you mix, not of how it is built. The list of existing scenes is offered for overwrite so
// "Stream" stays one scene instead of becoming "Stream", "Stream 2", "Stream 2 final".
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.PromptDialog {
    id: dlg
    property string sceneName: ""
    title: i18n("Save scene")
    subtitle: i18n("Stores the current faders, mutes and FX bypass states. Channels, mixes and wiring stay untouched.")
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
    preferredWidth: Kirigami.Units.gridUnit * 24

    function open() {
        nameField.text = dlg.sceneName
        dlg.visible = true; nameField.forceActiveFocus(); nameField.selectAll()
    }
    // Test hook, same idea as RenameDialog.commit(): do what OK does, without driving the dialog.
    function commit(text) { nameField.text = text; dlg.accepted() }

    ColumnLayout {
        QQC2.TextField {
            id: nameField
            objectName: "sceneNameField"
            Layout.fillWidth: true
            placeholderText: i18n("Scene name")
            onAccepted: dlg.accept()
        }
        QQC2.Label {
            objectName: "sceneOverwriteHint"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: Mixer.scenes.indexOf(nameField.text.trim()) >= 0
            text: i18n("A scene named “%1” exists and will be overwritten.", nameField.text.trim())
        }
        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: Mixer.scenes.length > 0
            opacity: 0.7
            font: Kirigami.Theme.smallFont
            text: i18np("Saved scene: %2", "Saved scenes: %2", Mixer.scenes.length, Mixer.scenes.join(", "))
        }
    }
    onAccepted: {
        const n = nameField.text.trim()
        if (n.length === 0) return
        Mixer.saveScene(n)
    }
}
