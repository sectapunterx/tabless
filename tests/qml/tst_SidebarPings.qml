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
        // The block is folded by default (dogfood 2026-10-23 #11); these
        // tests read its rows, so open it.
        const s = JSON.parse(AppController.appSettingsJson || "{}");
        s.sidebarPingsOpen = true;
        AppController.appSettingsJson = JSON.stringify(s);
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

    function test_wrote_keeps_them_as_a_wait_and_replied_goes_last() {
        const id = person("Pingprobe Viktor", "about the release");
        const b = make();
        tryVerify(() => row(b, id) !== null, 1000, "the ask is not in the sidebar");
        verify(b.visible);
        verify(!row(b, id).waiting);
        const asks = b.todoCount;
        AppController.setPersonState(id, "pinged");
        tryVerify(() => row(b, id) !== null && row(b, id).waiting, 1000, "wrote took them off instead of waiting");
        const sub = findChild(row(b, id), "sidebar-ping-sub");
        // "жду ответа · about the release", the age on the name's line
        // (dogfood 2026-10-22).
        compare(sub.text, I18n.t("pings.st.pinged") + " · about the release");
        verify(findChild(row(b, id), "sidebar-ping-age").text.length > 0, "how long it has been a wait");
        compare(findChild(row(b, id), "sidebar-ping-initials").text, "PV", "two initials");
        compare(row(b, id).opacity, 1, "a wait is not dimmed, an answer is");
        compare(b.todoCount, asks - 1, "a wait is not counted as an ask");
        // The asks, then the waits, then the answers.
        const order = { todo: 0, pinged: 1, replied: 2 };
        const items = b.items;
        for (let i = 1; i < items.length; i++)
            verify(order[items[i - 1].state] <= order[items[i].state], "out of order at " + i);
        AppController.undo();
        tryVerify(() => row(b, id) !== null && !row(b, id).waiting, 1000, "undo did not bring the ask back");
        AppController.setPersonState(id, "pinged");
        AppController.setPersonState(id, "replied");
        tryVerify(() => row(b, id) !== null && row(b, id).replied, 1000, "replied is not shown as answered");
        tryVerify(() => row(b, id).opacity < 1, 1000, "an answer is not dimmed");
        compare(findChild(row(b, id), "sidebar-ping-sub").text, I18n.t("pings.st.replied") + " · about the release");
        compare(b.items[b.items.length - 1].state, "replied", "answers go last");
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
        compare(findChild(row(b, id), "sidebar-ping-keys").text, I18n.t("pings.keys.waiting") + " · " + I18n.t("pings.keys.done"));
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
        compare(findChild(row(b, id), "sidebar-ping-sub").text, I18n.t("pings.st.todo") + " · " + w.label);
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

    function test_folded_until_opened() {
        const s = JSON.parse(AppController.appSettingsJson || "{}");
        delete s.sidebarPingsOpen;
        AppController.appSettingsJson = JSON.stringify(s);
        person("Pingprobe Lena", "a deploy");
        const b = make();
        verify(!b.open, "Кому написать is open on a fresh profile");
        findChild(b, "sidebar-pings-fold").activated();
        verify(b.open);
        compare(JSON.parse(AppController.appSettingsJson).sidebarPingsOpen, true);
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

    // The reviewers of my own request, as the forge says (dogfood
    // 2026-10-22): asked and silent is a wait, a word since is an answer.
    function test_reviewers_of_my_request_wait_or_answered() {
        AppController.clearMrThreadsForTest();
        const url = "https://gitlab.company.ru/tel/app/-/merge_requests/2342";
        const raw = JSON.stringify([{ id: "d1", individual_note: false, notes: [
            { id: 301, type: "DiffNote", body: "nit", author: { username: "irareviewer" },
              created_at: new Date(Date.now() - 3600 * 1000).toISOString(), system: false, resolvable: true, resolved: false }] }]);
        verify(AppController.setMrThreadsForTest("gitlab", url, "Retry", "fin", ["irareviewer", "maxreviewer"], "fin", raw));
        const b = make();
        tryVerify(() => b.reviews.length === 2, 1000, "no reviewer rows");
        const max = b.items.find(it => it.kind === "review" && it.name === "maxreviewer");
        const ira = b.items.find(it => it.kind === "review" && it.name === "irareviewer");
        compare(max.state, "pinged", "asked and silent is a wait");
        compare(max.reason, I18n.t("pings.review").arg("!2342"));
        compare(ira.state, "replied", "a word since the ask is an answer");
        verify(b.items.indexOf(max) < b.items.indexOf(ira), "the answer goes below the wait");
        AppController.clearMrThreadsForTest();
        tryVerify(() => b.reviews.length === 0, 1000);
    }

    // Leaves by itself (dogfood 2026-10-22): an answer once opened from its
    // row; my own ask with Del or the menu's "Готово", Undo brings it back.
    function test_done_with_del_and_an_answer_read_leaves() {
        const ada = tc.person("Ada Qa", "");
        const bob = tc.person("Bob Qa", "");
        AppController.setPersonState(bob, "replied");
        const b = make();
        tryVerify(() => row(b, ada) !== null && row(b, bob) !== null, 1000);

        // The menu says "Готово" with its key.
        mouseClick(findChild(row(b, ada), "sidebar-ping-open"), 20, 10, Qt.RightButton);
        const menu = findChild(b, "person-menu");
        tryVerify(() => menu.opened, 1000, "no menu on a right click");
        const done = findChild(menu, "person-menu-done");
        verify(done.visible);
        compare(done.text, I18n.t("pings.menu.done"));
        compare(done.keyText, "Del");
        menu.close();
        tryVerify(() => !menu.visible, 1000);

        findChild(row(b, ada), "sidebar-ping-open").forceActiveFocus();
        keyClick(Qt.Key_Delete);
        tryVerify(() => row(b, ada) === null, 1000, "Del did not take the ask off");
        compare(AppController.personById(ada).state, "idle");
        AppController.undo();
        tryVerify(() => row(b, ada) !== null, 1000, "undo did not bring it back");

        // The answer opened from its row is read: the row goes.
        // The rows were rebuilt by the undo: the click goes to the live one.
        tryVerify(() => row(b, bob) !== null && row(b, bob).visible && row(b, bob).height > 0, 1000);
        wait(50);
        mouseClick(findChild(row(b, bob), "sidebar-ping-open"));
        tryVerify(() => row(b, bob) === null, 1000, "a read answer stays");
    }
}
