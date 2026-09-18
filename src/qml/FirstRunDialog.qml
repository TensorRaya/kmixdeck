// SPDX-License-Identifier: GPL-3.0-or-later
// UX-3 first run. No logic here: the daemon's FirstRunPlan says what it would do, this shows it in words and the
// "Set up" button calls FirstRunApply. Result lines come back from the daemon too, so CLI (`kmixdeck setup`) and
// window always tell the same story (ADR 0010).
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kmixdeck

Kirigami.PromptDialog {
    id: dlg
    objectName: "firstRunDialog"
    property var plan: ({})
    property var done: null
    readonly property bool canApply: (plan.defaultSink || "") !== "" && plan.sinkKnown === true
    readonly property string summary: {   // probe: one line a test can read
        if (done) return "done:" + Object.keys(done).filter(k => k !== "apps").join(",") + ":apps=" + (done.apps ? done.apps.length : 0)
        return "plan:" + (plan.defaultSink || "-") + "|" + (plan.defaultSource || "-") + "|apps=" + (plan.apps ? plan.apps.length : 0)
    }
    title: done ? i18n("kmixdeck is set up") : i18n("Welcome to kmixdeck")
    preferredWidth: Kirigami.Units.gridUnit * 30
    standardButtons: Kirigami.Dialog.NoButton
    function refresh() { plan = Mixer.firstRunPlan() }
    onOpened: refresh()
    Connections { target: Mixer; function onDefaultDevicesChanged() { if (dlg.visible && !dlg.done) dlg.refresh() } }

    ColumnLayout {
        objectName: "firstRunBody"                       // probe anchor: Kirigami.Dialog's own objectName is not reachable from the overlay walk
        readonly property bool dialogVisible: dlg.visible
        readonly property string summary: dlg.summary
        spacing: Kirigami.Units.largeSpacing
        QQC2.Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: dlg.done ? i18n("Your desk is wired. Change anything on the Mixer page — every fader, output and input is one click away.")
                           : i18n("A channel is where applications play into (Game, System, Voice). A mix is what you or your stream hear (Monitor, Stream). kmixdeck can wire the basics now:")
        }
        Kirigami.FormLayout {
            visible: !dlg.done
            Layout.fillWidth: true
            QQC2.Label {
                Kirigami.FormData.label: i18n("Monitor mix plays on:")
                objectName: "firstRunSink"
                text: dlg.plan.defaultSink ? (dlg.plan.sinkDescription || dlg.plan.defaultSink) + (dlg.plan.sinkKnown ? "" : i18n(" (not seen yet)")) : i18n("no default output — is the sound server running?")
                color: dlg.canApply ? Kirigami.Theme.textColor : Kirigami.Theme.negativeTextColor
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Voice channel listens to:")
                objectName: "firstRunSource"
                text: dlg.plan.defaultSource ? (dlg.plan.sourceDescription || dlg.plan.defaultSource) : i18n("no microphone found — add one later via the channel's Hardware input… menu")
                opacity: dlg.plan.defaultSource ? 1 : 0.7
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Running applications:")
                objectName: "firstRunApps"
                text: {
                    const apps = dlg.plan.apps || []
                    if (apps.length === 0) return i18n("none playing right now — they land in Game when they start")
                    return apps.map(a => a.assigned ? i18n("%1 (already in %2)", a.name, Mixer.channelName(a.channel)) : i18n("%1 → %2", a.name, Mixer.channelName(a.channel))).join("\n")
                }
            }
        }
        ColumnLayout {
            visible: dlg.done !== null
            Layout.fillWidth: true
            Repeater {
                model: dlg.done ? [
                    dlg.done.monitorOutput ? i18n("Monitor mix → %1", Mixer.deviceDescription(dlg.done.monitorOutput)) : "",
                    dlg.done.listeningDevice ? i18n("You hear on %1", Mixer.deviceDescription(dlg.done.listeningDevice)) : "",
                    dlg.done.voiceInput ? i18n("Voice ← %1", dlg.done.voiceInput) : ""
                ].concat((dlg.done.apps || []).map(a => i18n("%1 → %2", a.name, Mixer.channelName(a.channel)))).filter(s => s !== "") : []
                QQC2.Label { required property string modelData; text: "✓ " + modelData }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            QQC2.Button {
                objectName: "firstRunSkip"
                visible: !dlg.done
                text: i18n("I'll set it up myself")
                onClicked: { Mixer.dismissFirstRun(); dlg.reject(); dlg.visible = false }
            }
            QQC2.Button {
                objectName: "firstRunApply"
                visible: !dlg.done
                text: i18n("Set up")
                icon.name: "dialog-ok-apply"
                enabled: dlg.canApply
                highlighted: true
                onClicked: { const r = Mixer.firstRunApply(); if (Object.keys(r).length > 0 || Mixer.lastError === "") dlg.done = r }
            }
            QQC2.Button {
                objectName: "firstRunClose"
                visible: dlg.done !== null
                text: i18n("Start mixing")
                icon.name: "dialog-ok"
                highlighted: true
                onClicked: { dlg.accept(); dlg.visible = false }
            }
        }
    }
}
