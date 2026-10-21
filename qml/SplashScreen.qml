// Tabless launch splash (N/X-Ntf-Focus "Запуск", R3-017): the wordmark and
// the version on the window background, nothing else. After an unclean exit
// (R3-018, "Запуск после сбоя") it says so under the wordmark and waits for
// an answer: "Посмотреть отчёт" opens the report form the person sends
// themselves, "Не отправлять" goes on. Nothing is ever sent from here.
import QtQuick
import TodoCpp

FocusScope {
    id: root

    // The window has drawn its first frame (Main.qml): a normal launch leaves
    // at once, never on a timer (Review 2, R3-017).
    property bool ready: false
    readonly property string version: AppController.appVersion
    readonly property bool crashed: AppController.lastExitUnclean
    // finished() was sent; the splash is on its way out.
    property bool _done: false
    // The crash card is up and not answered yet: it is the only layer. Main
    // turns every global key off while this holds (A7-2: Ctrl+2 switched the
    // hidden view, Ctrl+N opened a task over the card).
    readonly property bool waiting: root.crashed && !root._done

    // The splash is done and may fade out.
    signal finished()
    // "Посмотреть отчёт".
    signal reportRequested()

    // Asked to leave: after a crash it stays until the card is answered.
    function dismiss() {
        if (!root.crashed) root._finish();
    }
    function _finish() {
        if (root._done) return;
        root._done = true;
        root.finished();
    }
    onReadyChanged: if (root.ready) root.dismiss()
    Component.onCompleted: {
        if (root.crashed) root.forceActiveFocus();
        else if (root.ready) root.dismiss();
    }

    // A press ends a normal splash and still reaches the app under it; only
    // the crash card takes the press (it waits for an answer).
    MouseArea {
        anchors.fill: parent
        z: -1
        onPressed: (mouse) => {
            if (root.crashed) return;
            mouse.accepted = false;
            root.dismiss();
        }
    }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.sp3xl, Theme.px(400))
        spacing: Theme.spMd

        BrandLogo {
            anchors.horizontalCenter: parent.horizontalCenter
            variant: "wordmark"
            theme: Theme.dark ? "dark" : "light"
            height: Theme.px(32)
        }
        Text {
            objectName: "splash-version"
            visible: !root.crashed
            anchors.horizontalCenter: parent.horizontalCenter
            // "Стабильная 0.9.5" / "Feature-сборка · abc1234 · 20 окт. 2026".
            text: Brand.buildLine()
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        Text {
            objectName: "splash-crash-title"
            visible: root.crashed
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: I18n.t("crash.title")
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
        }
        Text {
            objectName: "splash-crash-fact"
            visible: root.crashed
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            lineHeight: 1.5
            text: {
                const t = AppController.lastSaveTime;
                const when = t && !isNaN(t.getTime()) ? I18n.fmtTime(t) : "";
                return (when ? I18n.t("crash.fact").arg(when) : I18n.t("crash.factNoTime")) + " " + I18n.t("crash.ask");
            }
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        Row {
            visible: root.crashed
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.spSm
            Text {
                objectName: "splash-crash-report"
                text: I18n.t("crash.view")
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.underline: viewCA.hovered
                ClickArea {
                    id: viewCA
                    label: parent.text
                    onActivated: {
                        AppController.dismissUncleanExit();
                        root._finish();
                        root.reportRequested();
                    }
                }
            }
            Text { text: "·"; color: Theme.textDim; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
            Text {
                objectName: "splash-crash-skip"
                text: I18n.t("crash.skip")
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.underline: skipCA.hovered
                ClickArea {
                    id: skipCA
                    label: parent.text
                    onActivated: {
                        AppController.dismissUncleanExit();
                        root._finish();
                    }
                }
            }
        }
    }

    // Enter / Esc answer the card from the keyboard: Esc = "Не отправлять".
    // A normal splash never holds the focus, so the first key goes to the app.
    focus: root.crashed
    onCrashedChanged: if (root.crashed) root.forceActiveFocus()
    // The view built under the card took the keyboard, and Esc / Enter no
    // longer reached it (A7-2): while it waits, the card takes it back.
    onActiveFocusChanged: {
        if (root.activeFocus || !root.waiting) return;
        Qt.callLater(function () {
            if (root.waiting && !root.activeFocus) root.forceActiveFocus();
        });
    }
    Keys.onEscapePressed: if (root.crashed) skipCA.activated()
    Keys.onReturnPressed: if (root.crashed) viewCA.activated()
    Keys.onEnterPressed: if (root.crashed) viewCA.activated()
    // Tab walks the card's two answers, not the hidden app under it.
    Keys.onTabPressed: (event) => root._cycle(event)
    Keys.onBacktabPressed: (event) => root._cycle(event)
    function _cycle(event) {
        if (!root.waiting) { event.accepted = false; return; }
        event.accepted = true;
        if (viewCA.activeFocus) skipCA.forceActiveFocus(Qt.TabFocusReason);
        else viewCA.forceActiveFocus(Qt.TabFocusReason);
    }
    // Any other key is the card's too: nothing behind it moves.
    Keys.onPressed: (event) => { if (root.waiting) event.accepted = true; }
}
