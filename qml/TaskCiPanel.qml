pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as QQC
import QtQuick.Layouts
import TodoCpp

// CI of a merge / pull request in the task document (APP-318), read only:
// "CI · пайплайн #88412   Открыть ↗", the jobs that failed ("✕ test-linux
// лог ▸") and, a click on one, the end of its log, which can be copied or
// opened in the browser; "7 джоб прошли · скрыты". The reading is ciOf's,
// the card's and the PR row's too (dogfood 2026-10-22); the jobs are asked
// once a run when the document shows a failed one, and kept by the
// controller. Grey words; the only colour is the red ✕ of a failure.
ColumnLayout {
    id: root
    objectName: "task-ci"
    spacing: Theme.spXs

    property string taskId: ""
    property int rev: 0

    readonly property var ci: root.rev >= 0 && root.taskId.length > 0
        ? (AppController.mrRevision, AppController.ciOf(root.taskId)) : ({})
    readonly property string ciState: String(root.ci.state || "")
    visible: root.ciState.length > 0
    // Seen once: the CI feature hint (FeatureHintHost) is moot.
    onVisibleChanged: if (root.visible) AppController.noteFeatureUsed("ciPanel")
    Component.onCompleted: if (root.visible) AppController.noteFeatureUsed("ciPanel")

    property var jobs: []
    property string jobsError: ""
    property bool jobsLoading: false
    property string logJob: ""
    property string logText: ""
    property string logError: ""
    property bool logLoading: false
    readonly property var _openJob: {
        for (let i = 0; i < root.jobs.length; i++) if (root.jobs[i].id === root.logJob) return root.jobs[i];
        return ({});
    }

    onTaskIdChanged: { root._reset(); Qt.callLater(root._autoLoad); }
    onCiStateChanged: Qt.callLater(root._autoLoad)
    // A failed run's jobs, once (the controller answers again from memory).
    function _autoLoad() {
        if (!root.visible || root.ciState !== "failed" || !root.ci.hasRef) return;
        if (root.jobs.length > 0 || root.jobsLoading || root.jobsError.length > 0) return;
        root.loadJobs();
    }
    function _reset() {
        root.jobs = [];
        root.jobsError = "";
        root.jobsLoading = false;
        root.logJob = "";
        root.logText = "";
        root.logError = "";
        root.logLoading = false;
    }
    function loadJobs() {
        root.jobsLoading = true;
        root.jobsError = "";
        AppController.fetchCiJobs(root.taskId);
    }
    function openLog(job) {
        if (root.logJob === job.id && !root.logLoading) {
            root.logJob = "";   // a second click folds it
            return;
        }
        root.logJob = job.id;
        root.logText = "";
        root.logError = "";
        if (!job.hasLog) return;
        root.logLoading = true;
        AppController.fetchCiLog(root.taskId, job.id);
    }

    Connections {
        target: AppController
        function onCiJobsLoaded(taskId, jobs, error) {
            if (taskId !== root.taskId) return;
            root.jobsLoading = false;
            root.jobs = jobs;
            root.jobsError = error;
        }
        function onCiLogLoaded(taskId, jobId, text, error) {
            if (taskId !== root.taskId || jobId !== root.logJob) return;
            root.logLoading = false;
            root.logText = text;
            root.logError = error;
        }
    }

    // "CI · пайплайн #88412                Открыть ↗"
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spSm
        Text {
            objectName: "task-ci-state"
            Layout.fillWidth: true
            text: String(root.ci.pipeline || "").length > 0 ? I18n.t("ci.panel.title").arg(root.ci.pipeline)
                                                           : I18n.t("ci.state." + root.ciState)
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwTitle
        }
        // Always there with a request: the run's link, else the request's
        // pipelines / checks page (GitLab keeps no link without web_url).
        Text {
            objectName: "task-ci-open"
            visible: String(root.ci.url || "").length > 0
            text: I18n.t("ci.pop.open")
            color: openCA.hovered ? Theme.text : Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            font.underline: true
            ClickArea { id: openCA; label: parent.text; onActivated: Qt.openUrlExternally(root.ci.url) }
        }
    }

    Text {
        visible: root.jobsLoading
        text: I18n.t("ci.loading")
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
    }
    Text {
        objectName: "task-ci-jobs-error"
        visible: root.jobsError.length > 0
        Layout.fillWidth: true
        text: I18n.t("ci.jobsUnavailable")
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
    }

    Repeater {
        model: root.jobs
        delegate: ColumnLayout {
            id: jobRow
            required property var modelData
            Layout.fillWidth: true
            spacing: Theme.sp2xs
            readonly property bool open: root.logJob === jobRow.modelData.id
            RowLayout {
                objectName: "task-ci-job-" + jobRow.modelData.id
                Layout.fillWidth: true
                spacing: Theme.spXs
                Text {
                    text: "✕"
                    color: Theme.danger
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    Accessible.ignored: true
                }
                Text {
                    Layout.fillWidth: true
                    text: jobRow.modelData.name + (String(jobRow.modelData.stage || "").length ? "  · " + jobRow.modelData.stage : "")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
                Text {
                    visible: Number(jobRow.modelData.seconds || 0) > 0
                    text: I18n.fmtMinutes(Math.max(1, Math.round(Number(jobRow.modelData.seconds) / 60)))
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    font.features: Theme.tabularNums
                }
                // "лог ▸": the end of this job's log, here.
                Text {
                    text: I18n.t(jobRow.open ? "ci.panel.logOpen" : "ci.panel.log")
                    color: jobCA.hovered || jobRow.open ? Theme.text : Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    ClickArea {
                        id: jobCA
                        objectName: "task-ci-job-open"
                        label: jobRow.modelData.name
                        onActivated: root.openLog(jobRow.modelData)
                    }
                }
            }
            // A check run's own words (GitHub), before its log.
            Text {
                visible: jobRow.open && String(jobRow.modelData.summary || "").length > 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spLg
                text: jobRow.modelData.summary || ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 8
                elide: Text.ElideRight
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
            }
        }
    }

    Text {
        objectName: "task-ci-passed-hidden"
        readonly property int passed: Math.max(0, Number(root.ci.total || 0) - Number(root.ci.failed || 0))
        visible: root.ciState === "failed" && root.jobs.length > 0 && passed > 0
        text: I18n.count(passed, "ci.panel.passedHidden")
        color: Theme.textMuted
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
    }

    // The log's end: monospace, errors louder, copy and open.
    ColumnLayout {
        objectName: "task-ci-log"
        visible: root.logJob.length > 0
        Layout.fillWidth: true
        spacing: Theme.spXs
        Text {
            visible: root.logLoading
            text: I18n.t("ci.loadingLog")
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        Text {
            objectName: "task-ci-log-unavailable"
            visible: !root.logLoading && root.logText.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root._openJob.hasLog === false ? I18n.t("ci.logElsewhere") : root.logError.length > 0 ? I18n.t("ci.logUnavailable") : I18n.t("ci.logEmpty")
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        Rectangle {
            visible: root.logText.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Theme.px(320), logList.contentHeight + 2 * Theme.spSm)
            radius: Theme.radiusSm
            color: Theme.codeBg
            border.width: 1
            border.color: Theme.border
            ListView {
                id: logList
                objectName: "task-ci-log-lines"
                anchors.fill: parent
                anchors.margins: Theme.spSm
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.logText.length > 0 ? root.logText.split("\n") : []
                // The end is what failed: it opens at the bottom.
                onCountChanged: Qt.callLater(logList.positionViewAtEnd)
                QQC.ScrollBar.vertical: ThinScrollBar {}
                delegate: Text {
                    required property string modelData
                    width: logList.width
                    text: modelData
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    color: AppController.isCiErrorLine(modelData) ? Theme.danger : Theme.code
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsXs
                }
            }
        }
        RowLayout {
            visible: root.logText.length > 0 || String(root._openJob.url || "").length > 0
            spacing: Theme.spLg
            Text {
                objectName: "task-ci-log-copy"
                visible: root.logText.length > 0
                text: I18n.t("ci.copyLog")
                color: copyCA.hovered ? Theme.text : Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.underline: true
                ClickArea {
                    id: copyCA
                    label: parent.text
                    onActivated: {
                        AppController.copyToClipboard(root.logText);
                        AppController.toast(I18n.t("ci.logCopied"));
                    }
                }
            }
            Text {
                objectName: "task-ci-log-browser"
                visible: String(root._openJob.url || "").length > 0
                text: I18n.t("ci.openJob")
                color: browserCA.hovered ? Theme.text : Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.underline: true
                ClickArea { id: browserCA; label: parent.text; onActivated: Qt.openUrlExternally(root._openJob.url) }
            }
        }
    }
}
