pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as QQC
import QtQuick.Layouts
import TodoCpp

// The card of a "Ждут меня" row under the pointer or the keyboard (dogfood
// 2026-10-22), beside the sidebar: "!2342 · TEL-401198", the title, each
// reason with its age ("✕ CI упал: test-linux, lint  14 мин", "amt ждёт
// ответа в треде  5 дн", "ревью от тебя  2 дн") and the row's keys. Only
// shown: it never takes the keyboard from the row (the keys are the row's).
QQC.Popup {
    id: root
    objectName: "sidebar-wait-detail"
    // A row of AppController.mrWaitRows(); null hides the card.
    property var row: null
    padding: Theme.spLg
    width: Theme.px(300)
    focus: false
    modal: false
    closePolicy: QQC.Popup.NoAutoClose
    background: PopupSurface {}

    readonly property bool hasCard: root.row !== null && String(root.row.taskId || "").length > 0
    readonly property bool ciFailed: root.row !== null && root.row.ci === "failed"
    readonly property var threads: root.row !== null && root.row.threads ? root.row.threads : []
    readonly property string forge: root.row !== null && root.row.flavor === "github" ? "GitHub" : "GitLab"

    function threadLine(t) {
        const who = String(t.who || "");
        if (t.reason === "mention" && who.length > 0) return I18n.t("waits.detail.mention").arg(who);
        if (t.reason === "started" || who.length === 0) return I18n.t("waits.started");
        return I18n.t("waits.detail.replied").arg(who);
    }
    // [{text, ago, failed}]
    readonly property var reasons: {
        const out = [];
        if (root.row === null) return out;
        if (root.ciFailed) {
            const jobs = String(root.row.ciFailed || "");
            out.push({ text: jobs.length > 0 ? I18n.t("waits.detail.ciJobs").arg(jobs) : I18n.t("waits.detail.ci"),
                       ago: String(root.row.ciAgo || ""), failed: true });
        }
        for (let i = 0; i < root.threads.length; i++)
            out.push({ text: root.threadLine(root.threads[i]), ago: String(root.threads[i].ago || ""), failed: false });
        if (root.row.review) out.push({ text: I18n.t("waits.reviewYou"), ago: String(root.row.reviewAgo || ""), failed: false });
        return out;
    }
    // [[key, what]]
    readonly property var keys: {
        const out = [];
        if (root.row === null) return out;
        if (root.hasCard) out.push(["Enter", I18n.t("waits.detail.keyTask")]);
        if (root.ciFailed && root.hasCard) {
            const k = AppController.shortcuts.length >= 0 ? AppController.shortcutText("board.ciPopover") : "";
            if (k.length > 0) out.push([k, I18n.t("waits.detail.keyCi")]);
        }
        if (root.threads.length > 0) {
            out.push(["t", I18n.t("waits.detail.keyThread").arg(root.forge)]);
            out.push([I18n.t("threads.ignoreKeys"), I18n.t("waits.detail.keyIgnore")]);
        } else if (!root.hasCard) {
            out.push(["Enter", I18n.t("waits.detail.keyThread").arg(root.forge)]);
        }
        if (root.row && !root.row.leaving) {
            const s = AppController.shortcuts.length >= 0 ? AppController.shortcutText("task.schedule") : "";
            if (s.length > 0) out.push([s, I18n.t("waits.detail.keySnooze")]);
        }
        out.push(["", I18n.t("waits.detail.keyMenu")]);
        return out;
    }

    contentItem: ColumnLayout {
        spacing: Theme.spSm
        Text {
            objectName: "sidebar-wait-detail-ref"
            Layout.fillWidth: true
            text: root.row === null ? "" : root.row.ref + (String(root.row.ticket || "").length > 0 ? " · " + root.row.ticket : "")
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.textDim
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsXs
            font.features: Theme.tabularNums
        }
        Text {
            objectName: "sidebar-wait-detail-title"
            Layout.fillWidth: true
            text: root.row === null ? "" : String(root.row.title || "")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            font.weight: Theme.fwTitle
        }
        Repeater {
            model: root.reasons
            delegate: RowLayout {
                id: reason
                required property var modelData
                Layout.fillWidth: true
                spacing: Theme.spSm
                Text {
                    visible: !!reason.modelData.failed
                    text: "✕"
                    color: Theme.danger
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
                Text {
                    objectName: "sidebar-wait-detail-reason"
                    Layout.fillWidth: true
                    text: reason.modelData.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
                Text {
                    visible: text.length > 0
                    text: reason.modelData.ago
                    textFormat: Text.PlainText
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    font.features: Theme.tabularNums
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spXs
            implicitHeight: 1
            color: Theme.border
        }
        Flow {
            objectName: "sidebar-wait-detail-keys"
            Layout.fillWidth: true
            spacing: Theme.spMd
            Repeater {
                model: root.keys
                delegate: Row {
                    id: hint
                    required property var modelData
                    spacing: Theme.spXs
                    Text {
                        visible: text.length > 0
                        text: hint.modelData[0]
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fsXs
                    }
                    Text {
                        text: hint.modelData[1]
                        textFormat: Text.PlainText
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                }
            }
        }
    }
}
