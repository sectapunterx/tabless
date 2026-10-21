// The mouse's Back / Forward buttons (dogfood 2026-10-21 #6) in the real
// Main.qml: they step through the views like Ctrl+O / Ctrl+I, Back closes
// what is open over the screen first (like Esc) and never moves the view
// behind it, and a middle-click autoscroll still only ends on them.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "MouseNav"
    when: windowShown

    property var win: null

    function initTestCase() {
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        verify(comp.status === Component.Ready, comp.errorString());
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        tc.win.width = 1456;
        tc.win.height = 939;
        AppController.resetAllShortcuts();
        wait(1200);   // splash
        tc.win.requestActivate();
        AppController.currentView = "board";
        tryVerify(function () { return tc.win.activeViewItem() !== null; }, 3000);
        wait(100);
    }

    function cleanupTestCase() {
        if (tc.win) tc.win.destroy();
        tc.win = null;
    }

    function init() {
        const log = popup("EventLogDialog");
        if (log && log.opened) log.close();
        tryVerify(function () { return !tc.win._layerOpen; }, 2000);
        AppController.currentView = "board";
        tc.win._navBack = [];
        tc.win._navFwd = [];
        tc.win._navCur = "board";
        tc.win.focusActiveView();
        wait(20);
    }

    function typeName(o) {
        const s = String(o);
        const i = s.indexOf("(");
        return i > 0 ? s.slice(0, i) : s;
    }
    function popup(prefix) {
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) {
            const o = cd[i];
            if (o && o.opened !== undefined && o.open !== undefined && typeName(o).indexOf(prefix) === 0) return o;
        }
        return null;
    }
    // A click of a mouse button in the middle of the window.
    function button(b) {
        const item = tc.win.contentItem;
        mouseClick(item, item.width / 2, item.height / 2, b);
        wait(30);
    }

    function test_back_and_forward_step_through_the_views() {
        AppController.currentView = "list";
        AppController.currentView = "settings";
        wait(50);
        button(Qt.BackButton);
        compare(AppController.currentView, "list");
        button(Qt.BackButton);
        compare(AppController.currentView, "board");
        button(Qt.ForwardButton);
        compare(AppController.currentView, "list");
        button(Qt.ForwardButton);
        compare(AppController.currentView, "settings");
        // Nothing further: Forward at the end does nothing.
        button(Qt.ForwardButton);
        compare(AppController.currentView, "settings");
    }

    function test_back_closes_an_open_dialog_first() {
        AppController.currentView = "list";
        wait(50);
        const log = popup("EventLogDialog");
        verify(log !== null);
        log.open();
        tryCompare(log, "opened", true);
        verify(tc.win._layerOpen);

        // Forward never acts behind it.
        button(Qt.ForwardButton);
        verify(log.opened);
        compare(AppController.currentView, "list");

        // Back closes it, like Esc, and the view stays.
        button(Qt.BackButton);
        tryCompare(log, "opened", false, 2000);
        compare(AppController.currentView, "list");

        // Then Back goes back.
        tryVerify(function () { return !tc.win._layerOpen; }, 2000);
        button(Qt.BackButton);
        compare(AppController.currentView, "board");
    }

    Component {
        id: longList
        ListView {
            width: 300
            height: 300
            model: 500
            delegate: Rectangle {
                required property int index
                width: 300
                height: 30
            }
        }
    }

    function test_autoscroll_still_ends_on_them() {
        AppController.currentView = "list";
        wait(50);
        const list = createTemporaryObject(longList, tc.win.contentItem, { z: 10000 });
        verify(list !== null);
        wait(30);
        mouseClick(list, 150, 150, Qt.MiddleButton);
        verify(ScrollFeel.autoscrolling, "a middle click over a list did not start autoscroll");
        mouseClick(list, 150, 150, Qt.BackButton);
        verify(!ScrollFeel.autoscrolling, "Back did not end autoscroll");
        compare(AppController.currentView, "list", "the click that ends autoscroll does nothing else");
    }
}
