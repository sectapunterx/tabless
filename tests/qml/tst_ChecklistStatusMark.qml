// A checklist item's status as a soft chip (owner pick «Мягкие чипы»,
// dogfood 2026-10-23, Pick-Checklist): the kind's faint fill and a muted
// tint of it for the name — work, a problem, done, open; cancelled has no
// fill; a name in capitals is not shouted; the ▾ only when asked for.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "ChecklistStatusMark"
    when: windowShown
    visible: true
    width: 300
    height: 200

    Item { id: host; anchors.fill: parent }

    function make(name, category) {
        const m = createTemporaryQmlObject('import TodoCpp; ChecklistStatusMark {}', host);
        verify(m !== null);
        m.statusName = name;
        m.category = category;
        return m;
    }

    function test_each_kind_has_its_tint() {
        const rows = [["In Progress", "progress", Theme.checkWorkFill, Theme.checkWorkText],
                      ["Blocked", "blocked", Theme.checkBlockedFill, Theme.checkBlockedText],
                      ["Done", "done", Theme.checkDoneFill, Theme.checkDoneText],
                      ["To Do", "todo", Theme.checkOpenFill, Theme.checkOpenText]];
        for (const r of rows) {
            const m = make(r[0], r[1]);
            compare(String(m.color), String(r[2]), r[1] + " fill");
            compare(String(m.ink), String(r[3]), r[1] + " text");
            compare(m.text, r[0]);
            verify(m.radius >= m.height / 2 - 0.5, "not a pill");
        }
        const c = make("Cancelled", "cancelled");
        compare(c.color.a, 0, "a cancelled one has a fill");
        compare(String(c.ink), String(Theme.checkCancelledText));
        // Faint: the fill is a tint, never a solid chip.
        verify(Theme.checkWorkFill.a > 0 && Theme.checkWorkFill.a <= 0.15);
    }

    function test_capitals_are_not_shouted() {
        compare(make("IN QA", "progress").text, "In QA");
        compare(make("BLOCKED", "blocked").text, "Blocked");
        compare(make("Code review", "progress").text, "Code review");
    }

    function test_caret_only_when_asked() {
        const m = make("To Do", "todo");
        const caret = findChild(m, "checklist-status-caret");
        verify(!caret.visible);
        m.caret = true;
        verify(caret.visible);
        verify(m.text.indexOf("▾") < 0, "the caret is part of the name");
    }
}
