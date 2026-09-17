// SPDX-License-Identifier: GPL-3.0-or-later
// Rename a channel or a mix (CH-2 / MX-5). The slug (and every PipeWire node name) stays — only the label changes.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.PromptDialog {
    id: dlg
    property string kind: "channel"
    property string slug
    property bool duplicate: false   // MX-8: same dialog, the OK creates a copy of `slug` under the new name
    title: duplicate ? i18n("Duplicate mix") : kind === "channel" ? i18n("Rename channel") : i18n("Rename mix")
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
    preferredWidth: Kirigami.Units.gridUnit * 22

    function open(k, s, dup) {
        kind = k; slug = s; duplicate = dup === true
        nameField.text = k === "channel" ? Mixer.channelName(s) : Mixer.mixName(s)
        if (duplicate) nameField.text = i18nc("default name for a duplicated mix: <name> copy", "%1 copy", nameField.text)
        dlg.visible = true; nameField.forceActiveFocus(); nameField.selectAll()
    }
    // MX-8 gesture: what OK does, without the dialog
    function commit(text) { nameField.text = text; dlg.accepted() }

    QQC2.TextField {
        id: nameField
        Layout.fillWidth: true
        onAccepted: dlg.accept()
    }
    onAccepted: {
        const n = nameField.text.trim()
        if (n.length === 0) return
        if (duplicate) Mixer.duplicateMix(slug, n)
        else if (kind === "channel") Mixer.renameChannel(slug, n); else Mixer.renameMix(slug, n)
    }
}
