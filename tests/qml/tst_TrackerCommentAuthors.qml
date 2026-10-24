// Who wrote a tracker comment (dogfood 2026-10-23, Comments sheet, in the
// quiet look of Redo-Sheet-Quiet): the name, the address (else "@login" in
// the code face), the date on the right, and a round picture — the initials
// on one quiet grey for everyone while there is no picture. No card; mine a
// faint tint. The comments are put in as read; no tracker is asked.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TrackerCommentAuthors"
    when: windowShown
    visible: true
    width: 700
    height: 600

    Item { id: host; width: 700; height: 600 }

    function make(comments) {
        const c = createTemporaryQmlObject('import TodoCpp; TaskTrackerComments { width: 600 }', host);
        verify(c !== null);
        // A ticket no task has: nothing is read or asked for.
        c.taskId = "no-such-ticket";
        c._cached = { comments: comments, total: comments.length };
        return c;
    }
    function test_name_login_and_initials() {
        const at = new Date(2026, 9, 21, 18, 4);
        const c = make([
            { author: "Pavel Babushkin", login: "p.babushkin", createdAt: at, url: "https://jira.example.invalid/browse/X-1", body: "one" },
            { author: "Павел Байбаков", login: "", createdAt: at, url: "", body: "two" },
            { author: "Pavel Babushkin", login: "p.babushkin", createdAt: at, url: "", body: "three", avatar: "" }
        ]);
        c._all = true;
        waitForRendering(c);
        const first = findChild(c, "task-doc-comment-0");
        verify(first !== null);
        compare(findChild(first, "task-doc-comment-author").text, "Pavel Babushkin");
        const login = findChild(first, "task-doc-comment-login");
        verify(login.visible);
        compare(login.text, "@p.babushkin");
        compare(login.font.family, Theme.fontMono);
        const date = findChild(first, "task-doc-comment-date");
        verify(date.visible && date.text.length > 0);
        verify(date.x > login.x, "the date is not after the name");
        // No picture: the initials, on the quiet grey.
        compare(findChild(first, "task-doc-comment-initials").text, "PB");
        verify(!findChild(first, "task-doc-comment-avatar-pic").visible);

        const second = findChild(c, "task-doc-comment-1");
        compare(findChild(second, "task-doc-comment-initials").text, "ПБ");
        verify(!findChild(second, "task-doc-comment-login").visible, "no login, no bare @");

        // One grey for everyone («C · тише»: no colour per person).
        const third = findChild(c, "task-doc-comment-2");
        compare(String(third.avatarFill), String(first.avatarFill));
        compare(String(second.avatarFill), String(first.avatarFill));
        compare(String(first.avatarFill), String(Theme.panel3));
    }

    // The address after the name, dim, and no @login beside it (it is
    // mostly the same); on a narrow card the address goes, the name stays.
    function test_email_in_place_of_the_login() {
        const c = make([{ author: "Pavel Babushkin", login: "p.babushkin", email: "pavel.babushkin@corp.example",
                          createdAt: new Date(2026, 9, 21, 18, 4), url: "https://jira.example.invalid/browse/X-1", body: "one" }]);
        waitForRendering(c);
        const card = findChild(c, "task-doc-comment-0");
        const email = findChild(card, "task-doc-comment-email");
        verify(email.visible);
        compare(email.text, "pavel.babushkin@corp.example");
        compare(email.elide, Text.ElideMiddle);
        compare(String(email.color), String(Theme.textMuted));
        verify(!findChild(card, "task-doc-comment-login").visible, "the login is not shown beside the address");
        verify(email.x > findChild(card, "task-doc-comment-author").x);

        c.width = 230;
        waitForRendering(c);
        verify(!email.visible, "the address gave way first");
        compare(findChild(card, "task-doc-comment-author").text, "Pavel Babushkin");
        verify(findChild(card, "task-doc-comment-author").width > 0, "the name stays");
    }

    // An @login in a comment and a Ctrl+click on the author: nobody known by
    // it, so the offer to add them comes filled in from the comment.
    function test_unknown_author_offers_a_filled_in_contact() {
        const c = make([{ author: "Zed Unknownson", login: "z.unknownson", email: "zed@nowhere.example", createdAt: new Date(),
                         url: "", body: "x", parts: [{ image: false, text: "ask [@z.unknownson](person:z.unknownson)" }] }]);
        waitForRendering(c);
        const body = findChild(findChild(c, "task-doc-comment-0"), "task-doc-comment-body");
        verify(body.text.indexOf("person:z.unknownson") >= 0);
        c.followBodyLink("person:z.unknownson");
        verify(c._personEditor !== null, "no offer to add them");
        tryCompare(c._personEditor, "opened", true);
        compare(c._personEditor.draft.name, "Zed Unknownson");
        compare(c._personEditor.draft.login, "z.unknownson");
        compare(c._personEditor.draft.email, "zed@nowhere.example");
        verify(c._personEditor.isNew);
        c._personEditor.close();
        tryCompare(c._personEditor, "opened", false);

        // A plain click on the name opens nothing; Ctrl+click offers them.
        const name = findChild(findChild(c, "task-doc-comment-0"), "task-doc-comment-author-open");
        mouseClick(name);
        wait(50);
        verify(!c._personEditor.opened);
        mouseClick(name, name.width / 2, name.height / 2, Qt.LeftButton, Qt.ControlModifier);
        tryCompare(c._personEditor, "opened", true);
        c._personEditor.close();
    }

    // A picture not stored (missing file): the initials stay.
    function test_missing_picture_keeps_initials() {
        const c = make([{ author: "Ann", login: "ann", createdAt: new Date(), url: "", body: "x", avatar: "attachments/doesnotexist" }]);
        waitForRendering(c);
        const card = findChild(c, "task-doc-comment-0");
        tryCompare(findChild(card, "task-doc-comment-avatar-pic"), "status", Image.Error);
        verify(!findChild(card, "task-doc-comment-avatar-pic").visible);
        compare(findChild(card, "task-doc-comment-initials").text, "A");
    }

    // No cards; mine a faint tint to tell "me" from "them".
    function test_mine_has_a_faint_tint() {
        const c = make([{ author: "Me", login: "me", mine: true, createdAt: new Date(), url: "", body: "x" },
                        { author: "You", login: "you", createdAt: new Date(), url: "", body: "y" }]);
        waitForRendering(c);
        compare(String(findChild(c, "task-doc-comment-0").color), String(Theme.taskCommentMine));
        compare(findChild(c, "task-doc-comment-1").color.a, 0, "a card around someone else's comment");
        verify(Theme.taskCommentMine.a > 0 && Theme.taskCommentMine.a < 0.1, "mine is more than a tint");
    }
}
