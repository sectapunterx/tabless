// Feature hints (dogfood 2026-10-20 #6, mockup 6A) in the real Main.qml:
// a hint never takes the keys while the user types (idiot test) — it waits,
// the typing lands where it was going, and the card comes once the field is
// left; Esc reads it and the keyboard goes back to the view; "Try it" does
// the thing once and changes no setting.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "FeatureHints"
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
        AppController.setCoachMarksEnabled(true);
        tc.win.coachMarksAwake = true;
        wait(100);
    }

    function cleanupTestCase() {
        AppController.forgetCoachMarks("");
        AppController.setCoachMarksEnabled(false);
        if (tc.win) tc.win.destroy();
        tc.win = null;
    }

    function init() {
        AppController.forgetCoachMarks("");
        tc.win.activeViewItem().forceActiveFocus();
    }
    // Forgetting takes the offer back: its card goes with it.
    function cleanup() { AppController.forgetCoachMarks(""); }

    function find(root, name) {
        if (!root) return null;
        if (root.objectName === name) return root;
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) {
            const r = find(kids[i], name);
            if (r) return r;
        }
        return null;
    }
    // The feature card on screen, if any (its title inside the overlay).
    // The window's overlay (an attached property is not reachable through
    // a var): a sibling of the content item.
    function overlay() {
        let r = tc.win.contentItem;
        while (r.parent) r = r.parent;
        const kids = r.children;
        for (let i = 0; i < kids.length; i++)
            if (String(kids[i]).indexOf("QQuickOverlay") === 0) return kids[i];
        return null;
    }
    function card() {
        const ov = overlay();
        const kids = ov ? ov.children : [];
        for (let i = 0; i < kids.length; i++) {
            const t = find(kids[i], "coachmark-title");
            if (t && t.visible && t.text === I18n.t("coach.f.cheatSheet.title")) return kids[i];
        }
        return null;
    }
    function inOverlay(item) {
        for (let p = item; p; p = p.parent)
            if (p === overlay()) return true;
        return false;
    }
    function settingsNow() {
        function clean(o) {
            if (!o || typeof o !== "object") return o;
            const r = Array.isArray(o) ? [] : {};
            for (const k in o) if (!/^hint|^featureUse$/.test(k)) r[k] = clean(o[k]);
            return r;
        }
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}"); } catch (e) { s = { raw: AppController.appSettingsJson }; }
        return JSON.stringify({ s: clean(s), safety: AppController.safety, theme: AppController.theme });
    }

    // Idiot test: the user is typing in the search when a hint comes due.
    function test_never_steals_keys_while_typing() {
        const field = find(tc.win.contentItem, "topbar-search");
        verify(field, "the header search");
        tc.win._focusSearch();
        tryVerify(function () { return tc.win._typing; }, 1000, "typing in the search");
        verify(AppController.offerCoachMark("cheatSheet"));
        keyClick(Qt.Key_A);
        wait(700);
        keyClick(Qt.Key_B);
        wait(700);
        verify(card() === null, "a hint opened over typing");
        verify(field.activeFocus, "the field lost the caret");
        verify(String(field.text).indexOf("ab") >= 0, "typed: " + field.text);
        // Typing ends: the field let go (Esc clears, Esc hands back).
        field.text = "";
        tc.win.activeViewItem().forceActiveFocus();
        let c = null;
        tryVerify(function () { c = card(); return c !== null; }, 4000, "the hint once the typing stopped");
        // Esc reads it, and the keyboard is back where it was — not in a popup.
        keyClick(Qt.Key_Escape);
        tryVerify(function () { return card() === null; }, 1000);
        verify(AppController.coachMarkSeen("cheatSheet"));
        tryVerify(function () { return !!tc.win.activeFocusItem && !inOverlay(tc.win.activeFocusItem); }, 1000,
                  "focus stayed in a popup");
    }

    // "Try it" opens the cheat sheet once; no setting moves.
    function test_try_does_it_once_and_changes_no_setting() {
        const before = settingsNow();
        verify(AppController.offerCoachMark("cheatSheet"));
        tryVerify(function () { return card() !== null; }, 4000);
        keyClick(Qt.Key_Return);
        tryVerify(function () { return card() === null; }, 1000);
        tryVerify(function () { return tc.win._overlayOpen; }, 2000, "the cheat sheet opened");
        compare(settingsNow(), before, "a setting changed");
        verify(AppController.coachMarkSeen("cheatSheet"));
        tc.win.openCheatSheet();   // toggles it closed
        tryVerify(function () { return !tc.win._overlayOpen; }, 2000);
    }
}
