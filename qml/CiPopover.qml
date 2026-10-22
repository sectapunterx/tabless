pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as QQC
import QtQuick.Layouts
import TodoCpp

// CI of a card's merge / pull request, under the card (dogfood 2026-10-22):
// "Пайплайн #88412 · a1f3c9e · 14 мин назад", the jobs that failed with
// their time, the end of the first one's log, "Открыть джобу ↗" and "Весь
// лог". Opened by a click on the card's CI chip or by its key; Esc closes.
// Read from AppController.ciOf, the reading the card, the document and the
// sidebar share; the jobs and the log are asked once a run, when it opens.
// Quiet: grey words, the only colour a red ✕ for what failed.
QQC.Popup {
    id: root
    objectName: "ci-popover"
    padding: Theme.spMd
    width: Theme.px(300)
    focus: true
    background: PopupSurface {}
    closePolicy: QQC.Popup.CloseOnEscape | QQC.Popup.CloseOnPressOutside
    PopupArrival { popup: root; dialog: false }

    property string taskId: ""
    readonly property var ci: root.taskId.length > 0 && root.visible
        ? (AppController.mrRevision, AppController.ciOf(root.taskId)) : ({})
    readonly property string ciState: String(root.ci.state || "")

    property var jobs: []
    property bool jobsLoading: false
    property string jobsError: ""
    property string logJob: ""
    property string logText: ""
    property bool logLoading: false
    property string logError: ""
    readonly property var firstJob: root.jobs.length > 0 ? root.jobs[0] : ({})
    // GitLab's raw log; a GitHub check's page has its log.
    readonly property string fullLogUrl: {
        const u = String(root.firstJob.url || "");
        if (u.length === 0) return "";
        return root.ci.gitlab ? u + "/raw" : u;
    }

    // Where it opens, relative to the card `a` ({x, y, w, h} in the window)
    // for a popover `pw` × `ph` in a window `ww` × `wh`, `m` kept clear at
    // the edges: under the card; with no room at the right (the last
    // column), beside the card on its left, not over the card below it;
    // with no room under it, above it. Whatever still does not fit is moved
    // inside the window (dogfood 2026-10-22: it ran past the right edge).
    function place(a, pw, ph, ww, wh, m) {
        let x = Theme.spXl;
        let y = a.h + Theme.spXs;
        if (a.x + x + pw > ww - m) {
            const beside = -pw - Theme.spXs;
            if (a.x + beside >= m) {
                x = beside;
                y = 0;
            } else {
                x = a.w - pw;
            }
        }
        if (y > 0 && a.y + y + ph > wh - m && a.y - ph - Theme.spXs >= m)
            y = -ph - Theme.spXs;
        x = Math.max(m - a.x, Math.min(x, ww - m - pw - a.x));
        y = Math.max(m - a.y, Math.min(y, wh - m - ph - a.y));
        return Qt.point(x, y);
    }
    property Item _anchor: null
    function _placeNow() {
        const a = root._anchor;
        if (!a || !a.Window.window) return;
        const at = a.mapToItem(null, 0, 0);
        const p = root.place({ x: at.x, y: at.y, w: a.width, h: a.height }, root.width, root.height,
                             a.Window.width, a.Window.height, Theme.spMd);
        root.x = p.x;
        root.y = p.y;
    }
    // The jobs come in after it opens and it grows: placed again.
    onHeightChanged: if (root.visible) root._placeNow()

    function openAt(anchor, id) {
        root.taskId = id;
        root.parent = anchor;
        root._anchor = anchor;
        root._placeNow();
        root.jobs = [];
        root.jobsError = "";
        root.logJob = "";
        root.logText = "";
        root.logError = "";
        root.logLoading = false;
        root.open();
        // The jobs once a run (the controller keeps what it read): only when
        // there is something to say about them.
        const now = AppController.ciOf(id);
        if (now.hasRef && (now.state === "failed" || now.state === "running")) {
            root.jobsLoading = true;
            AppController.fetchCiJobs(root.taskId);
        }
    }

    function duration(sec) {
        const n = Math.max(0, Math.round(Number(sec) || 0));
        if (n === 0) return "";
        return n >= 60 ? I18n.t("ci.dur.ms").arg(Math.floor(n / 60)).arg(n % 60) : I18n.t("ci.dur.s").arg(n);
    }

    Connections {
        target: AppController
        enabled: root.visible
        function onCiJobsLoaded(taskId, jobs, error) {
            if (taskId !== root.taskId) return;
            root.jobsLoading = false;
            root.jobs = jobs;
            root.jobsError = error;
            // The end of the first failed job's log, where there is one.
            const first = root.jobs.length > 0 ? root.jobs[0] : null;
            if (first && first.hasLog) {
                root.logJob = first.id;
                root.logLoading = true;
                AppController.fetchCiLog(root.taskId, first.id);
            }
        }
        function onCiLogLoaded(taskId, jobId, text, error) {
            if (taskId !== root.taskId || jobId !== root.logJob) return;
            root.logLoading = false;
            root.logText = text;
            root.logError = error;
        }
    }

    // Return: the failed job, else the run, in the browser.
    function openFirst() {
        const u = String(root.firstJob.url || root.ci.url || "");
        if (u.length > 0) Qt.openUrlExternally(u);
    }

    contentItem: ColumnLayout {
        spacing: Theme.spSm
        focus: true
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                root.openFirst();
                e.accepted = true;
            }
        }

        // "Пайплайн #88412 · a1f3c9e · 14 мин назад"
        Text {
            objectName: "ci-pop-head"
            Layout.fillWidth: true
            text: {
                const parts = [];
                parts.push(String(root.ci.pipeline || "").length > 0 ? I18n.t("ci.pop.pipeline").arg(root.ci.pipeline)
                                                                     : I18n.t("ci.state." + (root.ciState || "unknown")));
                if (String(root.ci.sha || "").length > 0) parts.push(root.ci.sha);
                if (String(root.ci.ago || "").length > 0) parts.push(I18n.t("ci.pop.ago").arg(root.ci.ago));
                return parts.join(" · ");
            }
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
            font.features: Theme.tabularNums
        }
        // How far a running one got, or what an old push's run said.
        Text {
            objectName: "ci-pop-state"
            visible: text.length > 0
            Layout.fillWidth: true
            text: root.ciState === "running" && (root.ci.total || 0) > 0
                  ? I18n.t("ci.pop.progress").arg(root.ci.done || 0).arg(root.ci.total)
                  : root.ciState === "success" ? I18n.t("ci.state.success")
                  : root.ciState === "canceled" ? I18n.t("ci.state.canceled")
                  : root.ciState === "unknown" ? I18n.t("ci.state.unknown")
                  : root.ciState === "running" ? I18n.t("ci.state.running") : ""
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        Text {
            visible: root.jobsLoading
            text: I18n.t("ci.loading")
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        Text {
            objectName: "ci-pop-jobs-error"
            visible: !root.jobsLoading && root.jobsError.length > 0
            Layout.fillWidth: true
            text: I18n.t("ci.jobsUnavailable")
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }

        // "✕ test-linux            4 м 12 с"
        Repeater {
            model: root.jobs
            delegate: RowLayout {
                id: jobRow
                required property var modelData
                objectName: "ci-pop-job"
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
                    text: jobRow.modelData.name
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    Accessible.name: I18n.t("ci.state.failed") + ": " + jobRow.modelData.name
                }
                Text {
                    text: root.duration(jobRow.modelData.seconds)
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    font.features: Theme.tabularNums
                }
                // A check with no log here (`gh pr checks`): its own page.
                Text {
                    objectName: "ci-pop-job-link"
                    readonly property string url: String(jobRow.modelData.url || "")
                    visible: jobRow.modelData.hasLog === false && url.length > 0
                    text: I18n.t("ci.pop.open")
                    color: rowCA.hovered ? Theme.text : Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    font.underline: true
                    ClickArea { id: rowCA; label: jobRow.modelData.name; onActivated: Qt.openUrlExternally(parent.url) }
                }
            }
        }

        // The end of the log: the last lines, monospace.
        Text {
            visible: root.logLoading
            text: I18n.t("ci.loadingLog")
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        Rectangle {
            objectName: "ci-pop-log"
            visible: root.logText.length > 0
            Layout.fillWidth: true
            implicitHeight: logT.implicitHeight + 2 * Theme.spSm
            radius: Theme.radiusSm
            color: Theme.codeBg
            Text {
                id: logT
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: Theme.spSm
                text: {
                    const lines = root.logText.split("\n");
                    return lines.slice(Math.max(0, lines.length - 8)).join("\n");
                }
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 10
                elide: Text.ElideRight
                color: Theme.code
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsXs
            }
        }
        Text {
            visible: !root.logLoading && root.logJob.length > 0 && root.logText.length === 0 && root.logError.length > 0
            Layout.fillWidth: true
            text: I18n.t("ci.logUnavailable")
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }

        // "Открыть джобу ↗   Весь лог                Esc"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spLg
            Text {
                objectName: "ci-pop-open-job"
                readonly property string url: String(root.firstJob.url || "")
                visible: url.length > 0
                text: I18n.t("ci.pop.openJob")
                color: jobCA.hovered ? Theme.text : Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                font.underline: true
                ClickArea { id: jobCA; label: parent.text; onActivated: Qt.openUrlExternally(parent.url) }
            }
            Text {
                objectName: "ci-pop-full-log"
                visible: root.fullLogUrl.length > 0
                text: I18n.t("ci.pop.fullLog")
                color: logCA.hovered ? Theme.text : Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                font.underline: true
                ClickArea { id: logCA; label: parent.text; onActivated: Qt.openUrlExternally(root.fullLogUrl) }
            }
            // No job to open: the run, or the request's pipelines page.
            Text {
                objectName: "ci-pop-open"
                readonly property string url: String(root.ci.url || "")
                visible: String(root.firstJob.url || "").length === 0 && url.length > 0
                text: I18n.t("ci.pop.open")
                color: runCA.hovered ? Theme.text : Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                font.underline: true
                ClickArea { id: runCA; label: parent.text; onActivated: Qt.openUrlExternally(parent.url) }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "Esc"
                color: Theme.textDim
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsXs
            }
        }
    }
}
