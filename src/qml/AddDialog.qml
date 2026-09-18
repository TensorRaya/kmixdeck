// SPDX-License-Identifier: GPL-3.0-or-later
// Add channel / add mix. For channels this is the ONE picker for every source (CH-13): running applications
// (icon, name, live level), hardware inputs, or "apps only". Choosing an app creates the channel and assigns the
// app (CH-4); choosing a device creates the channel and its input edge (DV-9). No second dialog, no Apps page detour.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.Dialog {
    id: dlg
    property string kind: "channel"
    // selected source: kind "app" | "device" | "" and its reference (app path / input node.name)
    property string srcKind: ""
    property string srcRef: ""
    property bool demoPorts: false   // review only: expand the first multi-port device and pick two ports
    title: kind === "channel" ? i18n("Add channel") : i18n("Add mix")
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
    preferredWidth: Kirigami.Units.gridUnit * 30
    maximumHeight: Kirigami.Units.gridUnit * 34
    padding: 0

    function open(k) { kind = k; srcKind = ""; srcRef = ""; nameField.text = ""; dlg.visible = true; nameField.forceActiveFocus() }
    function pick(k, ref, suggestedName) {
        if (srcKind === k && srcRef === ref) { srcKind = ""; srcRef = ""; return }   // click again = deselect
        srcKind = k; srcRef = ref
        if (nameField.text.trim().length === 0 || nameField.autoNamed) { nameField.text = suggestedName; nameField.autoNamed = true }
    }

    ColumnLayout {
        spacing: 0
        QQC2.Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            wrapMode: Text.WordWrap
            opacity: 0.8
            text: dlg.kind === "channel"
                ? i18n("Pick what plays into the new channel — a running application, a microphone or line input — or leave it empty for apps you route later.")
                : i18n("A mix is an output: your headphones, or a capture device for OBS. Add as many as you need.")
        }
        QQC2.TextField {
            id: nameField
            property bool autoNamed: false
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing; Layout.rightMargin: Kirigami.Units.largeSpacing
            placeholderText: dlg.kind === "channel" ? i18n("Name — e.g. Game, Voice, Music") : i18n("Name — e.g. Monitor, Stream, Recording")
            onTextEdited: autoNamed = false
            onAccepted: dlg.accept()
        }

        // ---- CH-13 source list (channels only)
        QQC2.ScrollView {
            visible: dlg.kind === "channel"
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 20
            Layout.topMargin: Kirigami.Units.largeSpacing
            clip: true
            ListView {
                id: sources
                spacing: 0
                model: {
                    const rows = []
                    rows.push({ section: i18n("Applications playing right now"), header: true })
                    const apps = Mixer.apps
                    if (apps.length === 0) rows.push({ section: "", empty: i18n("No application is playing audio.") })
                    for (const a of apps) rows.push({ kind: "app", ref: a.path, name: a.name, sub: a.mediaName.length > 0 ? a.mediaName : a.binary,
                                                      icon: a.icon.length > 0 ? a.icon : "applications-multimedia", nodeId: a.nodeId,
                                                      taken: a.channel.length > 0 ? Mixer.channelName(a.channel) : "" })
                    rows.push({ section: i18n("Microphones and inputs"), header: true })
                    const devs = Mixer.inputDevices
                    if (devs.length === 0) rows.push({ section: "", empty: i18n("No input device found.") })
                    for (const d of devs) rows.push({ kind: "device", ref: d.nodeName, name: d.description, sub: d.nodeName, icon: "audio-input-microphone", nodeId: 0, taken: "" })
                    rows.push({ section: i18n("Or start empty"), header: true })
                    rows.push({ kind: "", ref: "", name: i18n("Empty channel (apps only)"), sub: i18n("Route applications to it later, by drag & drop or from the Applications page."), icon: "list-add", nodeId: 0, taken: "" })
                    return rows
                }
                delegate: Loader {
                    required property var modelData
                    required property int index
                    width: sources.width
                    sourceComponent: modelData.header ? headerRow : (modelData.empty ? emptyRow : (modelData.kind === "device" ? deviceRow : sourceRow))
                    Component {
                        id: deviceRow   // ADR 0009 / DV-20: whole device or a port subset
                        PortPicker {
                            id: pp
                            width: sources.width
                            node: modelData.ref
                            description: modelData.name
                            current: dlg.srcKind === "device" ? dlg.srcRef : ""
                            Component.onCompleted: if (dlg.demoPorts && ports.length > 2) { dlg.demoPorts = false; expanded = true; toggle(ports[1].position); toggle(ports[2].position) }
                            onRefChosen: (ref, suggestedName) => { dlg.srcKind = "device"; dlg.srcRef = ref
                                if (nameField.text.trim().length === 0 || nameField.autoNamed) { nameField.text = suggestedName; nameField.autoNamed = true } }
                        }
                    }
                    Component {
                        id: headerRow
                        Kirigami.ListSectionHeader { text: modelData.section; width: sources.width; visible: text.length > 0; height: text.length > 0 ? implicitHeight : Kirigami.Units.smallSpacing }
                    }
                    Component {
                        id: emptyRow
                        QQC2.Label { text: modelData.empty; opacity: 0.6; leftPadding: Kirigami.Units.largeSpacing * 2; topPadding: Kirigami.Units.smallSpacing; bottomPadding: Kirigami.Units.smallSpacing; width: sources.width }
                    }
                    Component {
                        id: sourceRow
                        QQC2.ItemDelegate {
                            width: sources.width
                            highlighted: dlg.srcKind === modelData.kind && dlg.srcRef === modelData.ref
                            onClicked: dlg.pick(modelData.kind, modelData.ref, modelData.kind === "device" ? modelData.name.split(" ")[0] : modelData.name)
                            contentItem: RowLayout {
                                spacing: Kirigami.Units.largeSpacing
                                Kirigami.Icon { source: modelData.icon; Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium; Layout.preferredHeight: width }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 0
                                    QQC2.Label { text: modelData.name; elide: Text.ElideRight; Layout.fillWidth: true; font.weight: Font.DemiBold }
                                    QQC2.Label {
                                        text: modelData.taken.length > 0 ? i18n("%1 · currently on %2", modelData.sub, modelData.taken) : modelData.sub
                                        opacity: 0.65; font: Kirigami.Theme.smallFont; elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                }
                                // live level for apps (UX-13 key app/<id>); devices are not metered here — that would
                                // open every microphone while the dialog is up
                                LevelMeter {
                                    id: rowMeter
                                    visible: modelData.kind === "app"
                                    horizontal: true
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                                    Layout.preferredHeight: Kirigami.Units.smallSpacing * 1.5
                                    Connections { target: Mixer; enabled: rowMeter.visible; function onPeaksChanged() { rowMeter.peak = Mixer.peak("app/" + modelData.nodeId) } }
                                }
                            }
                        }
                    }
                }
            }
        }
        Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
    }

    onAccepted: {
        const name = nameField.text.trim()
        if (name.length === 0) return
        if (kind === "channel") Mixer.addChannelWithSource(name, srcKind, srcRef); else Mixer.addMix(name)
    }
}
