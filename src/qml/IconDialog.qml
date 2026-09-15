// SPDX-License-Identifier: GPL-3.0-or-later
// Icon for a channel or a mix (UX-8). A curated grid first — the icon most people want is one click away —
// then two doors to everything else: the full icon theme (KIconDialog) or any image file.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.Dialog {
    id: dlg
    property string kind: "channel"
    property string slug
    property string current
    title: kind === "channel" ? i18n("Channel icon") : i18n("Mix icon")
    standardButtons: Kirigami.Dialog.Close
    preferredWidth: Kirigami.Units.gridUnit * 26
    padding: Kirigami.Units.largeSpacing

    function open(k, s) {
        kind = k; slug = s
        current = k === "channel" ? Mixer.channelIconRaw(s) : Mixer.mixIconRaw(s)
        dlg.visible = true
    }
    function apply(icon) {
        if (kind === "channel") Mixer.setChannelIcon(slug, icon); else Mixer.setMixIcon(slug, icon)
        current = icon
        dlg.close()
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // the shortlist
        GridLayout {
            id: grid
            columns: 6
            rowSpacing: Kirigami.Units.smallSpacing
            columnSpacing: Kirigami.Units.smallSpacing
            Repeater {
                model: IconPicker.suggestions(dlg.kind)
                delegate: QQC2.AbstractButton {
                    id: tile
                    required property string modelData
                    readonly property bool selected: modelData === dlg.current
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 3.4
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 3.4
                    hoverEnabled: true
                    onClicked: dlg.apply(modelData)
                    QQC2.ToolTip.text: modelData
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    background: Rectangle {
                        radius: Kirigami.Units.smallSpacing
                        color: tile.selected ? Qt.alpha(Kirigami.Theme.highlightColor, 0.35)
                             : tile.hovered  ? Qt.alpha(Kirigami.Theme.textColor, 0.08)
                             : Qt.darker(Kirigami.Theme.alternateBackgroundColor, 1.25)
                        border.width: tile.selected ? 2 : 0
                        border.color: Kirigami.Theme.highlightColor
                    }
                    contentItem: Kirigami.Icon {
                        source: tile.modelData
                        anchors.centerIn: parent
                        width: Kirigami.Units.iconSizes.medium; height: width
                    }
                }
            }
        }

        // custom icon preview (only when the current one is not in the shortlist)
        RowLayout {
            visible: dlg.current.length > 0 && IconPicker.suggestions(dlg.kind).indexOf(dlg.current) < 0
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Icon { source: dlg.current; Layout.preferredWidth: Kirigami.Units.iconSizes.medium; Layout.preferredHeight: width }
            QQC2.Label { text: dlg.current; elide: Text.ElideMiddle; Layout.fillWidth: true; opacity: 0.7 }
        }

        Kirigami.Separator { Layout.fillWidth: true }

        RowLayout {
            spacing: Kirigami.Units.smallSpacing
            QQC2.Button {
                text: i18n("More icons…")
                icon.name: "preferences-desktop-icons"
                onClicked: { const i = IconPicker.pickFromTheme(dlg.current); if (i.length) dlg.apply(i) }
            }
            QQC2.Button {
                text: i18n("Image file…")
                icon.name: "document-open"
                onClicked: { const f = IconPicker.pickFile(); if (f.length) dlg.apply(f) }
            }
            Item { Layout.fillWidth: true }
            QQC2.Button {
                text: i18n("Default")
                flat: true
                enabled: dlg.current.length > 0
                onClicked: dlg.apply("")
            }
        }
    }
}
