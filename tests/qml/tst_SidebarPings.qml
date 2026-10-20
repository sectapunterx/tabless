// "Кому написать" in the sidebar (APP-321): the open asks beside every view,
// "Написал" takes a person off (Undo brings them back), the fold is kept,
// and "Люди" has a way back to the list.
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

    function test_lists_open_asks_and_wrote_takes_them_off() {
        const id = person("Pingprobe Viktor", "about the release");
        const b = make();
        tryVerify(() => findChild(b, "sidebar-ping-" + id) !== null, 1000, "the ask is not in the sidebar");
        verify(b.visible);
        AppController.setPersonState(id, "pinged");
        tryVerify(() => findChild(b, "sidebar-ping-" + id) === null, 1000, "wrote did not take them off");
        AppController.undo();
        tryVerify(() => findChild(b, "sidebar-ping-" + id) !== null, 1000, "undo did not bring them back");
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
