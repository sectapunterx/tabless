import QtQuick
import TodoCpp

// The sidebar's blocks the first time they show with a row (coach mark
// "waitsIntro", Hints.dc.html): three steps. 1 by "Ждут меня" — what the
// rows and their ages are; 2 by "Кому написать" — the state badges; 3 by
// "Ждут меня" again — the list cleans itself after a sync. "Позже" on the
// first step puts it off a day; the last one reads it.
CoachMark {
    id: root
    markId: "waitsIntro"
    stepCount: 3
    side: "right"
    spotlight: true

    property Item waitsAnchor: null
    property Item pingsAnchor: null

    anchor: root.step === 2 && root.pingsAnchor && root.pingsAnchor.visible ? root.pingsAnchor : root.waitsAnchor
    title: I18n.t("coach.waits.title" + root.step)
    body: I18n.t("coach.waits.body" + root.step)
    primaryText: I18n.t(root.step < root.stepCount ? "coach.next" : "coach.gotIt")
    secondaryText: I18n.t(root.step === 1 ? "coach.later" : "coach.back")
    onPrimaryClicked: {
        if (root.step < root.stepCount) root.step++;
        else root.finish();
    }
    onSecondaryClicked: {
        if (root.step > 1) {
            root.step--;
            return;
        }
        AppController.snoozeCoachMark(root.markId);
        root.close();
    }
}
