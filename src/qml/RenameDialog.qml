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
    title: kind === "channel" ? i18n("Rename channel") : i18n("Rename mix")
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
    preferredWidth: Kirigami.Units.gridUnit * 22

    function open(k, s) {
        kind = k; slug = s
        nameField.text = k === "channel" ? Mixer.channelName(s) : Mixer.mixName(s)
        dlg.visible = true; nameField.forceActiveFocus(); nameField.selectAll()
    }

    QQC2.TextField {
        id: nameField
        Layout.fillWidth: true
        onAccepted: dlg.accept()
    }
    onAccepted: {
        const n = nameField.text.trim()
        if (n.length === 0) return
        if (kind === "channel") Mixer.renameChannel(slug, n); else Mixer.renameMix(slug, n)
    }
}
