pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import TodoCpp

// The week arrows clicked a third time in a session (coach mark): the keys
// that do the same, as they are bound now, and where they are changed.
//
//   FASTER FROM THE KEYBOARD
//   Back, forward and today, one key each
//   [p back] [n forward] [t today]
//   [ and ] work too, 0 is today. Change them: Settings → Keys.
// The keys the Calendar's ‹ Today › buttons show under the pointer.
//   Esc close                                        [Got it]
CoachMark {
    id: root
    markId: "weekKeys"
    side: "below"

    // The keys as bound now (Settings → Keys may have moved them).
    readonly property string _back: AppController.shortcutText("cal.prev.alt") || AppController.shortcutText("cal.prev")
    readonly property string _forward: AppController.shortcutText("cal.next.alt") || AppController.shortcutText("cal.next")
    readonly property string _today: AppController.shortcutText("cal.today.alt") || AppController.shortcutText("cal.today")

    badgeText: I18n.t("coach.weekKeys.badge")
    title: I18n.t("coach.weekKeys.title")
    body: I18n.t("coach.weekKeys.body").arg(AppController.shortcutText("cal.prev")).arg(AppController.shortcutText("cal.next"))
                                        .arg(AppController.shortcutText("cal.today"))
    onPrimaryClicked: root.finish()

    RowLayout {
        spacing: Theme.spLg
        KeyStep { keys: root._back; word: I18n.t("coach.weekKeys.back"); objectName: "coachmark-key-back" }
        KeyStep { keys: root._forward; word: I18n.t("coach.weekKeys.forward"); objectName: "coachmark-key-forward" }
        KeyStep { keys: root._today; word: I18n.t("coach.weekKeys.today"); objectName: "coachmark-key-today" }
    }

    component KeyStep: Row {
        id: ks
        property string keys: ""
        property string word: ""
        visible: ks.keys.length > 0
        spacing: Theme.spSm
        KeyHint { keys: ks.keys; always: true; anchors.verticalCenter: parent.verticalCenter }
        Text {
            text: ks.word
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
