pragma ComponentBehavior: Bound
import QtQuick
import TodoCpp

// Feature hints (dogfood 2026-10-20 #6, mockup 6A): a one-time card on the
// real thing — something new, or something never used after a while. The
// rules for when one is due are in src/hints/CoachMarks.h
// (featureHintRules, noteHintVisit); this is where each one stands and what
// "Try it" does. One host for all of them, in Main.qml:
//
//   NEW · NOT IN JIRA                                Esc got it
//   Why CI failed, right here
//   The failed jobs and the log tail, without opening GitLab.
//   [ Enter ] on the selected card
//   Skip all                                    [Later] [Try it]
//
// The card waits for a quiet window like every coach mark (CoachMarkHost):
// nobody typing, no modal, no focus mode. Its anchor is looked up by
// objectName among what is on screen; when none shows up it goes back
// unread and comes again on a later visit. "Try it" does the thing once —
// what its key would do — and never turns a setting on. "Later" puts it off
// to another day, once.
Item {
    id: fh
    // Main.qml's window: where the anchors are looked for, and its actions
    // (runCommand, showTask, navStep).
    property var win: null

    // Through the command line when the action has no key of its own.
    function _viaPalette(cmdKey) {
        return { keysId: "palette.open", caption: I18n.t("coach.f.viaPalette").arg(I18n.t(cmdKey)) };
    }
    function _run(cmd) {
        return function (w) { if (w && w.runCommand) w.runCommand(cmd); };
    }

    // id → where it stands, its key, and what "Try it" does.
    readonly property var catalogue: ({
        ciPanel: { anchors: ["tc-ci"], side: "right", isNew: true, unique: true, keysId: "board.open",
                   caption: I18n.t("coach.f.ciPanel.caption"),
                   tryIt: function (w, a) {
                       for (let p = a; p; p = p.parent)
                           if (p.task && p.task.id) { if (w && w.showTask) w.showTask(p.task); return; }
                   } },
        standupDraft: Object.assign({ anchors: ["today-header", "view-header-title"], side: "below",
                                      tryIt: fh._run("standup.draft") }, fh._viaPalette("palette.cmd.standupDraft")),
        endOfDay: Object.assign({ anchors: ["today-header", "view-header-title"], side: "below",
                                  tryIt: fh._run("endOfDay.open") }, fh._viaPalette("palette.cmd.endOfDay")),
        weekRecap: { anchors: ["recap-button", "today-header", "view-header-title"], side: "below", keysId: "recap.open",
                     tryIt: fh._run("recap.open") },
        timeMachineOpen: Object.assign({ anchors: ["today-header", "view-header-title"], side: "below",
                                         tryIt: fh._run("timeMachine.open") }, fh._viaPalette("palette.cmd.timeMachine")),
        savedViews: { anchors: ["save-view", "query-save-view", "view-header-title"], side: "below", keysId: "savedView.1",
                      caption: I18n.t("coach.f.savedViews.caption"), tryIt: fh._run("savedview.save") },
        quickCapture: { anchors: ["view-header-title"], side: "below", keysId: "quick-capture",
                        caption: I18n.t("coach.f.quickCapture.caption"), tryIt: fh._run("quick-capture") },
        commandLine: { anchors: ["topbar-search", "view-header-title"], side: "below", keysId: "palette.open",
                       tryIt: fh._run("palette.open") },
        navBack: { anchors: ["view-header-title"], side: "below", keysId: "nav.back",
                   caption: I18n.t("coach.f.navBack.caption").arg(AppController.shortcutText("nav.forward")),
                   canTry: function (w) { return !!w && !!w._navBack && w._navBack.length > 0; },
                   tryIt: function (w) { if (w && w.navStep) w.navStep(-1); } },
        eventLog: { anchors: ["view-header-title"], side: "below", keysId: "log.open", tryIt: fh._run("log.open") },
        // Ctrl+Tab is the field's own key (NotesView), not a catalogue one.
        notesRegions: { anchors: ["notes-list-pane"], side: "right", keys: "Ctrl+Tab",
                        caption: I18n.t("coach.f.notesRegions.caption") },
        cheatSheet: { anchors: ["view-header-title"], side: "below", keysId: "hotkeys.open.alt",
                      tryIt: fh._run("hotkeys.open") }
    })

    readonly property string markId: String(AppController.coachMark.id || "")
    readonly property var entry: fh.catalogue[fh.markId] || null
    property Item anchorItem: null
    property int _tries: 0

    onEntryChanged: {
        fh.anchorItem = null;
        fh._tries = 0;
        if (fh.entry && !AppController.coachMark.shown) finder.restart();
        else finder.stop();
    }

    // The first one of its anchors that is on screen, outside every popup.
    function findAnchor(names) {
        const w = fh.win;
        if (!w || !w.contentItem) return null;
        const ww = w.width, wh = w.height;
        function on(item) {
            if (!item.visible || item.width <= 0 || item.height <= 0) return false;
            const p = item.mapToItem(null, 0, 0);
            return p.x >= 0 && p.y >= 0 && p.x + item.width <= ww + 1 && p.y < wh;
        }
        function walk(item, name) {
            if (!item || !item.visible) return null;
            if (item.objectName === name && on(item)) return item;
            const kids = item.children || [];
            for (let i = 0; i < kids.length; i++) {
                const r = walk(kids[i], name);
                if (r) return r;
            }
            return null;
        }
        for (let i = 0; i < names.length; i++) {
            const r = walk(w.contentItem, names[i]);
            if (r) return r;
        }
        return null;
    }

    // A view just switched builds its items a moment later: look a few times,
    // then hand the card back unread.
    Timer {
        id: finder
        interval: 250
        repeat: true
        onTriggered: {
            if (!fh.entry) { finder.stop(); return; }
            const a = fh.findAnchor(fh.entry.anchors);
            if (a) {
                fh.anchorItem = a;
                finder.stop();
            } else if (++fh._tries >= 8) {
                finder.stop();
                AppController.dropCoachMark(fh.markId);
            }
        }
    }

    CoachMarkHost {
        id: host
        markId: fh.entry ? fh.markId : "-"
        when: fh.anchorItem !== null && fh.anchorItem.visible
        anchorItem: fh.anchorItem
        sourceComponent: cardComp
    }

    Component {
        id: cardComp
        CoachMark {
            id: card
            // Kept as made: the offer clears while the card closes.
            property var e: fh.entry || ({})
            readonly property bool tryable: !!card.e.tryIt && (!card.e.canTry || card.e.canTry(fh.win))
            // Made only once its anchor was found (the host's `when`): stand
            // by it from the start, and keep it while the offer clears.
            Component.onCompleted: {
                card.markId = String(card.markId);
                card.e = card.e;
                card.anchor = fh.anchorItem;
            }

            markId: fh.markId
            side: card.e.side || "right"
            spotlight: true
            badgeText: card.e.isNew ? (card.e.unique ? I18n.t("coach.f.newUnique") : I18n.t("coach.new"))
                                    : I18n.t("coach.f.untried")
            title: I18n.t("coach.f." + card.markId + ".title")
            body: I18n.t("coach.f." + card.markId + ".body")
            keys: card.e.keys || (card.e.keysId ? AppController.shortcutText(card.e.keysId) : "")
            keysCaption: card.e.caption || ""
            secondaryText: I18n.t("coach.later")
            primaryText: card.tryable ? I18n.t("coach.f.try") : I18n.t("coach.gotIt")

            onSecondaryClicked: {
                AppController.snoozeCoachMark(card.markId);
                card.close();
            }
            // Closed first, so the keyboard is free for what the action opens;
            // then the action, once.
            onPrimaryClicked: {
                const act = card.tryable ? card.e.tryIt : null;
                const a = card.anchor;
                const w = fh.win;
                card.close();
                if (act) Qt.callLater(function () { act(w, a); });
            }
        }
    }
}
