// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kmixdeck

Kirigami.PromptDialog {
    id: dlg
    property string kind: "channel"
    title: kind === "channel" ? i18n("Add channel") : i18n("Add mix")
    subtitle: kind === "channel"
        ? i18n("Applications play into a channel. Every mix gets its own fader for it.")
        : i18n("A mix is an output: your headphones, or a capture device for OBS. Add as many as you need.")
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
    preferredWidth: Kirigami.Units.gridUnit * 24

    function open(k) { kind = k; nameField.text = ""; dlg.visible = true; nameField.forceActiveFocus() }

    QQC2.TextField {
        id: nameField
        Layout.fillWidth: true
        placeholderText: dlg.kind === "channel" ? i18n("e.g. Game, Voice, Music") : i18n("e.g. Monitor, Stream, Recording")
        onAccepted: dlg.accept()
    }
    onAccepted: {
        if (nameField.text.trim().length === 0) return
        if (kind === "channel") Mixer.addChannel(nameField.text.trim()); else Mixer.addMix(nameField.text.trim())
    }
}
