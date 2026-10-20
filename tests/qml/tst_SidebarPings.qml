// "Кому написать" in the sidebar (APP-321): the open asks beside every view,
// "Написал" moves a person to "жду ответа" (dogfood 2026-10-20) where
// "Ответил" takes them off and "Снова" brings them back, each by key too;
// Undo brings them back, the fold is kept, and "Люди" shows the true state.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "SidebarPings"
    when: windowShown
    visible: true
    width: 320
    height: 600

    Item { id: host; anchors.fill: parent }

    property string savedSettings: ""
    property var made: []

    function init() {
        tc.savedSettings = AppController.appSettingsJson;
        tc.made = [];
    }
    function cleanup() {
        for (const id of tc.made) AppController.deletePerson(id);
        AppController.appSettingsJson = tc.savedSettings;
    }

    function person(name, question) {
        const d = AppController.newPersonDraft();
        d.name = name;
        d.id = AppController.suggestPersonId(name);
        d.question = question;
        d.state = "todo";
        verify(AppController.savePerson(d));
        tc.made.push(d.id);
        return d.id;
    }
    function make() {
        const b = createTemporaryQmlObject('import TodoCpp; SidebarPings { width: 260 }', host);
        verify(b !== null);
        return b;
    }

    function row(b, id) { return findChild(b, "sidebar-ping-" + id); }

    function test_wrote_keeps_them_as_a_wait_and_replied_takes_them_off() {
        const id = person("Pingprobe Viktor", "about the release");
        const b = make();
        tryVerify(() => row(b, id) !== null, 1000, "the ask is not in the sidebar");
        verify(b.visible);
        verify(!row(b, id).waiting);
        const asks = b.todoCount;
        AppController.setPersonState(id, "pinged");
        tryVerify(() => row(b, id) !== null && row(b, id).waiting, 1000, "wrote took them off instead of waiting");
        const sub = findChild(row(b, id), "sidebar-ping-sub");
        verify(sub.text.indexOf(I18n.t("pings.waiting")) === 0, sub.text);
        verify(row(b, id).opacity < 1, "a wait is dimmer");
        compare(b.todoCount, asks - 1, "a wait is not counted as an ask");
        // The waits come after the asks.
        const rows = b.people;
        const at = rows.findIndex(p => p.id === id);
        verify(rows.slice(at).every(p => p.waiting));
        AppController.undo();
        tryVerify(() => row(b, id) !== null && !row(b, id).waiting, 1000, "undo did not bring the ask back");
        AppController.setPersonState(id, "pinged");
        AppController.setPersonState(id, "replied");
        tryVerify(() => row(b, id) === null, 1000, "replied did not take them off");
        compare(AppController.personById(id).state, "replied");
    }

    function test_every_move_has_a_key_on_the_focused_row() {
        const id = person("Pingprobe Keys", "");
        const b = make();
        tryVerify(() => row(b, id) !== null, 1000);
        findChild(row(b, id), "sidebar-ping-open").forceActiveFocus();
        verify(findChild(row(b, id), "sidebar-ping-keys").visible, "the keys are shown on the focused row");
        const opened = createTemporaryQmlObject('import QtTest; SignalSpy { signalName: "personRequested" }', host);
        opened.target = b;
        keyClick(Qt.Key_Return);
        compare(AppController.personById(id).state, "pinged", "Return = wrote");
        compare(opened.count, 0, "Return does not also open the person");
        tryVerify(() => row(b, id) !== null && findChild(row(b, id), "sidebar-ping-open").activeFocus, 1000,
                  "the cursor stays on the person");
        compare(findChild(row(b, id), "sidebar-ping-keys").text, I18n.t("pings.keys.waiting"));
        keyClick(Qt.Key_Backspace);
        compare(AppController.personById(id).state, "todo", "Backspace = write again");
        tryVerify(() => row(b, id) !== null && findChild(row(b, id), "sidebar-ping-open").activeFocus, 1000);
        keyClick(Qt.Key_Return);
        tryVerify(() => row(b, id) !== null && findChild(row(b, id), "sidebar-ping-open").activeFocus, 1000);
        keyClick(Qt.Key_Return);
        compare(AppController.personById(id).state, "replied", "Return on a wait = replied");
    }

    function test_a_time_to_write_shows_on_the_row() {
        const id = person("Pingprobe Time", "");
        const d = AppController.personById(id);
        const w = AppController.parsePingWhen("завтра 15:00");
        verify(w.ok);
        d.remindAt = w.at;
        d.remindHasTime = w.hasTime;
        verify(AppController.savePerson(d));
        const b = make();
        tryVerify(() => row(b, id) !== null, 1000);
        compare(findChild(row(b, id), "sidebar-ping-sub").text, w.label);
        // Written: the time is spent.
        AppController.setPersonState(id, "pinged");
        verify(!AppController.personById(id).remindAt);
    }

    // The person dialog opens on the person asked for and says the true
    // state: a wait, never "replied".
    function test_people_dialog_shows_the_person_and_the_wait() {
        const first = person("Pingprobe Aaa", "");
        const id = person("Pingprobe Zed", "");
        AppController.setPersonState(first, "pinged");
        AppController.setPersonState(first, "replied");
        AppController.setPersonState(id, "pinged");
        const dlg = createTemporaryQmlObject('import TodoCpp; PeopleDialog { }', host);
        dlg.showFor(id);
        tryVerify(() => dlg.opened, 1000);
        wait(50);
        compare(dlg.currentId, id, "another person took over the detail");
        const st = findChild(dlg, "people-dialog-state");
        verify(st.text.indexOf(I18n.t("pings.waitingCap")) === 0, st.text);
        verify(findChild(dlg, "people-dialog-replied").visible);
        verify(!findChild(dlg, "people-dialog-wrote").visible);
        verify(findChild(dlg, "people-dialog-todo").visible);
        // Ctrl+Return in the list closes the wait, Backspace puts it back.
        findChild(dlg, "people-dialog-list").forceActiveFocus();
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        compare(AppController.personById(id).state, "replied");
        keyClick(Qt.Key_Backspace);
        compare(AppController.personById(id).state, "todo");
        dlg.close();
    }

    // A CODEOWNERS row opens the PR from the forge's answer: https only.
    function test_only_a_web_link_is_opened() {
        const b = make();
        verify(b.isWebUrl("https://github.com/team/app/pull/12"));
        verify(b.isWebUrl("HTTPS://code.corp/team/app/-/merge_requests/5"));
        verify(!b.isWebUrl("http://github.com/team/app/pull/12"));
        verify(!b.isWebUrl("file:///C:/Windows/System32/calc.exe"));
        verify(!b.isWebUrl("javascript:alert(1)"));
        verify(!b.isWebUrl("ms-settings:"));
        verify(!b.isWebUrl("https://"));
        verify(!b.isWebUrl(""));
        verify(!b.isWebUrl(undefined));
    }

    function test_the_fold_is_kept() {
        person("Pingprobe Olga", "a certificate");
        const b = make();
        verify(b.open);
        findChild(b, "sidebar-pings-fold").activated();
        verify(!b.open);
        compare(JSON.parse(AppController.appSettingsJson).sidebarPingsOpen, false);
        const again = make();
        verify(!again.open, "a new sidebar opened it again");
        findChild(again, "sidebar-pings-fold").activated();
        verify(again.open);
    }

    function test_back_to_the_list_from_people() {
        const id = person("Pingprobe Dima", "review");
        AppController.setPersonState(id, "pinged");
        compare(AppController.personById(id).state, "pinged");
        AppController.setPersonState(id, "todo");
        compare(AppController.personById(id).state, "todo");
        verify(AppController.openPings().some(p => p.id === id));
    }
}
