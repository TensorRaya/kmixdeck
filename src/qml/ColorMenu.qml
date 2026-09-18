// SPDX-License-Identifier: GPL-3.0-or-later
// MX-5 colour code: one palette (from the client, so every frontend agrees), one submenu for channels and mixes.
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

QQC2.Menu {
    id: menu
    required property string kind      // "channel" | "mix"
    required property string slug
    title: i18n("Colour")
    icon.name: "color-picker"
    readonly property string current: kind === "mix" ? Mixer.mixColor(slug) : Mixer.channelColor(slug)
    function apply(c) { if (kind === "mix") Mixer.setMixColor(slug, c); else Mixer.setChannelColor(slug, c) }

    QQC2.MenuItem {
        objectName: "colorNone"
        text: i18n("None (theme)")
        checkable: true; checked: menu.current === ""
        onTriggered: menu.apply("")
    }
    QQC2.MenuSeparator {}
    Repeater {
        model: Mixer.colorPalette()
        QQC2.MenuItem {
            required property string modelData
            objectName: "color/" + modelData
            checkable: true; checked: menu.current === modelData
            text: modelData
            contentItem: Row {
                spacing: Kirigami.Units.smallSpacing
                Rectangle { width: Kirigami.Units.iconSizes.small; height: width; radius: width / 2; color: modelData; anchors.verticalCenter: parent.verticalCenter; border { width: 1; color: Qt.alpha(Kirigami.Theme.textColor, 0.3) } }
                QQC2.Label { text: modelData; anchors.verticalCenter: parent.verticalCenter }
            }
            onTriggered: menu.apply(modelData)
        }
    }
}
