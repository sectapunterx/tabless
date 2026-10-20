// PersonEditor (X-Dlg-Small "Человек", DG-126): Name and What to ask.
// Covers the showFor() seeding paths, Escape, a nameless person refused with
// a message, a new person's handle made free from the name (SHELL-24), and
// an edit that keeps role, state and colour.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "PersonEditor"
    when: windowShown
    visible: true
    width: 500
    height: 500

    Item { id: host; anchors.fill: parent }

    function make(qml) {
        const o = createTemporaryQmlObject(qml, host);
        verify(o !== null);
        return o;
    }
    function field(pe, name) {
        const f = findChild(pe.contentItem, name);
        verify(f !== null, name);
        return f;
    }

    function test_smoke_load() {
        const pe = make('import TodoCpp; PersonEditor { }');
        compare(pe.isNew, false);
        verify(!pe.visible);
        compare(pe.title, I18n.t("editor.person.title"));
        compare(pe.fact, I18n.t("editor.person.fact"));
    }

    function test_two_fields_only() {
        const pe = make('import TodoCpp; PersonEditor { }');
        pe.showFor({ _isNew: true });
        tryCompare(pe, "opened", true);
        field(pe, "pe-name");
        field(pe, "pe-question");
        field(pe, "pe-when");
        compare(findChild(pe.contentItem, "pe-id"), null, "no handle field");
        tryVerify(function () { return field(pe, "pe-name").activeFocus; });
        pe.close();
        tryCompare(pe, "opened", false);
    }

    function test_showfor_existing_focuses_the_question() {
        const pe = make('import TodoCpp; PersonEditor { }');
        pe.showFor({ id: "x.y", name: "X Y", question: "q?" });
        tryCompare(pe, "opened", true);
        compare(pe.isNew, false);
        compare(field(pe, "pe-name").text, "X Y");
        compare(field(pe, "pe-question").text, "q?");
        tryVerify(function () { return field(pe, "pe-question").activeFocus; });
        pe.close();
        tryCompare(pe, "opened", false);
    }

    function test_escape_closes() {
        const pe = make('import TodoCpp; PersonEditor { }');
        pe.showFor({ _isNew: true });
        tryCompare(pe, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(pe, "opened", false);
    }

    function test_new_person_without_a_name_says_so() {
        const before = AppController.people.rowCount();
        const pe = make('import TodoCpp; PersonEditor { }');
        pe.showFor({ _isNew: true });
        tryCompare(pe, "opened", true);
        field(pe, "pe-name").text = "   ";
        pe._save();
        verify(pe.opened, "the dialog stays open on the draft");
        const err = findChild(pe.contentItem, "pe-error");
        verify(err.visible);
        compare(err.text, I18n.t("editor.person.err.name"));
        compare(AppController.people.rowCount(), before);
        field(pe, "pe-name").text = "Named";
        verify(!err.visible, "typing a name clears the message");
        pe.close();
        tryCompare(pe, "opened", false);
    }

    // SHELL-24: a new person never takes someone else's handle — it is made
    // free from the name.
    function test_new_person_never_replaces_another() {
        const victim = "dg126.victim";
        AppController.deletePerson(victim);
        verify(AppController.savePerson({ _isNew: true, id: victim, name: "Dg126 Victim",
                                          role: "Tech Lead", question: "keep me", state: "todo" }));
        const before = AppController.people.rowCount();
        const expected = AppController.suggestPersonId("Dg126 Victim", "");
        verify(expected !== victim);
        const pe = make('import TodoCpp; PersonEditor { }');
        pe.showFor({ _isNew: true });
        tryCompare(pe, "opened", true);
        field(pe, "pe-name").text = "Dg126 Victim";
        field(pe, "pe-question").text = "repro-trace";
        pe._save();
        tryCompare(pe, "opened", false);
        compare(AppController.people.rowCount(), before + 1);
        compare(AppController.personById(victim).question, "keep me");
        compare(AppController.personById(expected).question, "repro-trace");
        AppController.deletePerson(expected);
        AppController.deletePerson(victim);
    }

    function test_edit_keeps_role_state_colour() {
        const pid = "dg126.edit";
        AppController.deletePerson(pid);
        verify(AppController.savePerson({ _isNew: true, id: pid, name: "Edit Me", role: "QA",
                                          question: "old", state: "pinged", color: "#7cc492" }));
        const pe = make('import TodoCpp; PersonEditor { }');
        pe.showFor(AppController.personById(pid));
        tryCompare(pe, "opened", true);
        field(pe, "pe-question").text = "new question";
        pe._save();
        tryCompare(pe, "opened", false);
        const p = AppController.personById(pid);
        compare(p.question, "new question");
        compare(p.role, "QA");
        compare(p.state, "pinged");
        compare(String(p.color).toLowerCase(), "#7cc492");
        AppController.deletePerson(pid);
    }

    // "Когда написать" (dogfood 2026-10-20): read as typed, saved with the
    // person; a past moment is refused, an empty field clears it, and a time
    // given to a wait puts the ask back on the list.
    function test_when_to_write() {
        const pid = "dg1020.when";
        AppController.deletePerson(pid);
        verify(AppController.savePerson({ _isNew: true, id: pid, name: "When Me", question: "q", state: "pinged" }));
        const pe = make('import TodoCpp; PersonEditor { }');
        pe.showFor(AppController.personById(pid));
        tryCompare(pe, "opened", true);
        compare(field(pe, "pe-when").text, "", "a wait has no time to write");
        field(pe, "pe-when").text = "вчера 15:00";
        const read = findChild(pe.contentItem, "pe-when-read");
        compare(read.text, I18n.t("editor.person.err.past"));
        pe._save();
        verify(pe.opened, "a past moment is refused");
        field(pe, "pe-when").text = "завтра 15:00";
        verify(read.text.indexOf("→") === 0, read.text);
        pe._save();
        tryCompare(pe, "opened", false);
        let p = AppController.personById(pid);
        verify(p.remindHasTime);
        compare(p.state, "todo", "a time to write is an ask to write");
        const tomorrow = new Date(); tomorrow.setDate(tomorrow.getDate() + 1);
        compare(new Date(p.remindAt).getDate(), tomorrow.getDate());
        // Opened again, the field says it; untouched, nothing moves.
        pe.showFor(p);
        tryCompare(pe, "opened", true);
        verify(field(pe, "pe-when").text.length > 0);
        compare(read.text, "");
        pe._save();
        tryCompare(pe, "opened", false);
        compare(String(AppController.personById(pid).remindAt), String(p.remindAt));
        // Cleared.
        pe.showFor(AppController.personById(pid));
        tryCompare(pe, "opened", true);
        field(pe, "pe-when").text = "";
        pe._save();
        tryCompare(pe, "opened", false);
        p = AppController.personById(pid);
        verify(!p.remindAt);
        verify(!p.remindHasTime);
        AppController.deletePerson(pid);
    }
}
