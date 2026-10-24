pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import TodoCpp

// Above the Tasks view (X/N-Err-Tracker, R2-035/036): one line per tracker
// problem that is not solved yet — what happened, what it means, one action,
// and × to put the line away until the problem changes. A toast says it
// once when it happens; this line stays. Below the lines, a tracker's first
// pull, while it runs: "загружаем ваши тикеты" with placeholder rows.
//
// Nothing here sends anything: the actions sign in (Settings), pull again,
// or open the log.
//
// A feature paused after failing again and again (net/FeatureGate.h,
// 2026-10-21) gets a line too: what fails and since when, "requests
// stopped", and two answers — switch the feature off (its own setting, the
// connection stays) or try once now. × hides the line until the state
// changes; the feature stays paused.
ColumnLayout {
    id: root
    objectName: "tracker-strip"
    spacing: Theme.spSm

    signal signInRequested(string providerId)
    signal logRequested()
    // A paused feature's answers, after the controller has them.
    signal featureRetried(string featureId)
    signal featureSwitchedOff(string featureId)

    // Bound to the controller; a test can set it by hand.
    property var sources: AppController.syncSources
    property var paused: AppController.pausedFeatures
    // "<id>:<failedAtMs>" put away with ×; a new failure shows again.
    property var hidden: ({})

    // One sentence for a paused feature: what, who answers how, since when.
    function pausedFact(p) {
        const name = I18n.t("pause.name." + p.feature).arg(p.nameArg || "");
        const times = I18n.count(p.count || 0, "pause.times");
        const host = p.host && p.host.length > 0 ? p.host : I18n.t("pause.server");
        if (p.kind === "noAccess")
            return I18n.t("pause.noAccess").arg(name).arg(host).arg(p.status).arg(p.since).arg(times);
        if (p.status < 0)
            return I18n.t("pause.cli").arg(name).arg(p.since).arg(times);
        if (p.status === 0)
            return I18n.t("pause.noAnswer").arg(name).arg(host).arg(p.since).arg(times);
        return I18n.t("pause.errorStatus").arg(name).arg(host).arg(p.status).arg(p.since).arg(times);
    }

    readonly property var rows: {
        const out = [];
        const src = root.sources || [];
        const paused = root.paused || [];
        const pausedIds = {};
        for (let j = 0; j < paused.length; j++) {
            const p = paused[j];
            pausedIds[p.id] = true;
            if (p.hidden) continue;
            out.push({ key: "pause:" + p.id, id: p.id, icon: "pending", action: "featureRetry",
                       fact: root.pausedFact(p),
                       more: I18n.t(p.kind === "noAccess" ? "pause.stoppedNoAccess" : "pause.stopped"),
                       actionText: I18n.t("pause.retry"),
                       action2: "featureOff", actionText2: I18n.t("pause.off") });
        }
        let offline = false, waiting = 0, offlineKey = "";
        for (let i = 0; i < src.length; i++) {
            const s = src[i];
            // Its paused line says it already.
            if (pausedIds["sync:" + s.id]) continue;
            if (s.offline || s.kind === "network") {
                offline = true;
                waiting += s.waiting || 0;
                offlineKey += s.id + ",";
                continue;
            }
            if (!s.failing) continue;
            const key = s.id + ":" + (s.failedAtMs || 0);
            if (root.hidden[key]) continue;
            if (s.kind === "auth") {
                out.push({ key: key, id: s.id, icon: "info", action: "signIn",
                           fact: s.failedAt ? I18n.t("trk.strip.auth").arg(s.name).arg(s.failedAt)
                                            : I18n.t("trk.strip.authNoTime").arg(s.name),
                           more: I18n.t("trk.strip.stale").arg(s.name),
                           actionText: I18n.t("trk.strip.signIn") });
            } else if (s.kind === "rateLimited") {
                out.push({ key: key, id: s.id, icon: "pending", action: "retry",
                           fact: I18n.t("trk.strip.rate").arg(s.name),
                           more: I18n.t("trk.strip.rateNext"),
                           actionText: I18n.t("trk.strip.retryNow") });
            } else {
                const e = String(s.error || "");
                out.push({ key: key, id: s.id, icon: "info", action: "retry",
                           fact: I18n.t("trk.strip.failed").arg(s.name)
                                 .arg(e.length > 0 ? e.charAt(0).toLowerCase() + e.slice(1) : ""),
                           more: I18n.t("trk.strip.stale").arg(s.name),
                           actionText: I18n.t("trk.strip.retry") });
            }
        }
        if (offline && !root.hidden["offline:" + offlineKey + waiting]) {
            out.push({ key: "offline:" + offlineKey + waiting, id: "", icon: "pending", action: "log",
                       fact: I18n.t("trk.strip.offline"),
                       more: waiting > 0 ? I18n.count(waiting, "trk.strip.offlineWaiting") : I18n.t("trk.strip.offlineSaved"),
                       actionText: I18n.t("trk.strip.log") });
        }
        return out;
    }
    // A tracker that has never answered and is pulling now (R2-036).
    readonly property var firstLoads: {
        const out = [];
        const src = root.sources || [];
        for (let i = 0; i < src.length; i++)
            if (src[i].inFlight && !src[i].everOk && !src[i].failing) out.push(src[i]);
        return out;
    }

    // `row` is a row or its key.
    function hide(row) {
        const key = typeof row === "string" ? row : row.key;
        // A paused feature's line stays hidden across restarts, until its
        // state changes (the controller keeps that).
        if (key.indexOf("pause:") === 0) {
            AppController.hidePausedFeature(key.slice(6));
            return;
        }
        const h = Object.assign({}, root.hidden);
        h[key] = true;
        root.hidden = h;
    }
    function act(row, which) {
        const action = which || row.action;
        if (action === "signIn") root.signInRequested(row.id);
        else if (action === "retry") AppController.syncProvider(row.id);
        else if (action === "log") root.logRequested();
        else if (action === "featureRetry") {
            AppController.retryFeature(row.id);
            root.featureRetried(row.id);
        } else if (action === "featureOff") {
            AppController.disableFeature(row.id);
            root.featureSwitchedOff(row.id);
        }
    }

    visible: rows.length > 0 || firstLoads.length > 0

    Repeater {
        model: root.rows
        delegate: Rectangle {
            id: line
            required property var modelData
            required property int index
            objectName: "tracker-strip-row-" + line.index
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.px(900)
            implicitHeight: Math.max(Theme.chipH + Theme.spMd, lineRow.implicitHeight + 2 * Theme.spMd)
            radius: Theme.radiusMd
            color: "transparent"
            border.width: 1
            border.color: Theme.border
            RowLayout {
                id: lineRow
                anchors.fill: parent
                anchors.leftMargin: Theme.spLg
                anchors.rightMargin: Theme.spMd
                spacing: Theme.spMd
                Icon {
                    name: line.modelData.icon
                    // N-Err-Tracker: an error ring in red, a waiting one
                    // (429, offline) in amber; quiet keeps both neutral (R4-107).
                    color: !Style.urgency ? (line.modelData.icon === "info" ? Theme.text : Theme.textMuted)
                         : line.modelData.icon === "info" ? Theme.danger : Theme.warning
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    objectName: "tracker-strip-fact"
                    text: line.modelData.fact
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    font.weight: Theme.fwTitle
                    // A long fact (a paused feature's) gives way before the
                    // answers do: they stay on the line, it elides.
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.maximumWidth: implicitWidth
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    objectName: "tracker-strip-more"
                    Layout.fillWidth: true
                    text: line.modelData.more
                    textFormat: Text.PlainText
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    elide: Text.ElideRight
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    id: actT
                    objectName: "tracker-strip-action"
                    text: line.modelData.actionText
                    color: actCA.hovered ? Theme.textMuted : Theme.text
                    Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    font.weight: Theme.fwTitle
                    font.underline: true
                    Layout.alignment: Qt.AlignVCenter
                    ClickArea {
                        id: actCA
                        anchors.margins: -Theme.spXs
                        label: actT.text
                        showTip: false
                        onActivated: root.act(line.modelData, line.modelData.action)
                    }
                }
                // A paused feature's second answer: switch it off.
                Text {
                    id: act2T
                    objectName: "tracker-strip-action2"
                    visible: !!line.modelData.action2
                    text: line.modelData.actionText2 || ""
                    color: act2CA.hovered ? Theme.textMuted : Theme.text
                    Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    font.weight: Theme.fwTitle
                    font.underline: true
                    Layout.alignment: Qt.AlignVCenter
                    ClickArea {
                        id: act2CA
                        anchors.margins: -Theme.spXs
                        enabled: act2T.visible
                        label: act2T.text
                        showTip: false
                        onActivated: root.act(line.modelData, line.modelData.action2)
                    }
                }
                Item {
                    implicitWidth: Theme.chipH * 0.8
                    implicitHeight: Theme.chipH * 0.8
                    Layout.alignment: Qt.AlignVCenter
                    Icon {
                        anchors.centerIn: parent
                        name: "close"
                        size: Theme.px(10)
                        color: hideCA.hovered ? Theme.text : Theme.textMuted
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    }
                    ClickArea {
                        id: hideCA
                        objectName: "tracker-strip-hide"
                        label: I18n.t("trk.strip.dismiss")
                        onActivated: root.hide(line.modelData)
                    }
                }
            }
        }
    }

    // A tracker's first pull: the panel stays while it runs (R2-036).
    Repeater {
        model: root.firstLoads
        delegate: Rectangle {
            id: panel
            required property var modelData
            objectName: "tracker-first-load"
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.px(620)
            Layout.topMargin: Theme.spSm
            implicitHeight: panelCol.implicitHeight + 2 * Theme.spLg
            radius: Theme.radius
            color: "transparent"
            border.width: 1
            border.color: Theme.border
            ColumnLayout {
                id: panelCol
                anchors.fill: parent
                anchors.margins: Theme.spLg
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.spMd
                    spacing: Theme.spMd
                    Icon { name: "progress"; color: Style.urgency ? Theme.warning : Theme.text }
                    Text {
                        objectName: "tracker-first-load-title"
                        Layout.fillWidth: true
                        text: I18n.t("trk.first.title").arg(panel.modelData.name)
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                        font.weight: Theme.fwTitle
                    }
                }
                Repeater {
                    model: 3
                    delegate: Item {
                        id: skel
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: Theme.chipH + Theme.spSm
                        Rectangle {
                            anchors.top: parent.top
                            width: parent.width; height: 1
                            color: Theme.border
                        }
                        Rectangle {
                            id: skelDot
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.statusRingSize; height: width; radius: width / 2
                            color: Theme.withAlpha(Theme.text, 0.08)
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: skelDot.right; anchors.leftMargin: Theme.spMd
                            width: parent.width * [0.55, 0.42, 0.62][skel.index]
                            height: Theme.spSm
                            radius: height / 2
                            color: Theme.withAlpha(Theme.text, 0.08)
                        }
                    }
                }
                Text {
                    Layout.topMargin: Theme.spMd
                    text: I18n.t("trk.first.note")
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
            }
        }
    }
}
