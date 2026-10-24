// Markdown drawn as the note is in rich (APP-313): the task's text, the
// tracker's text (read only, Jira Server wiki as markdown), the task dialog
// and the card's line.
import QtQuick
import QtTest
import TodoCpp
import "../../qml/PlainText.js" as MdPlain

TestCase {
    id: tc
    name: "RichMarkdown"
    when: windowShown
    visible: true
    width: 1100
    height: 760

    Item { id: host; anchors.fill: parent }
    property var made: []
    property string importedProfile: ""
    property string homeProfile: ""
    function cleanup() {
        for (const id of tc.made) AppController.deleteTask(id);
        tc.made = [];
        if (tc.importedProfile.length > 0) {
            AppController.activeProfileId = tc.homeProfile;
            AppController.deleteProfile(tc.importedProfile);
            tc.importedProfile = "";
        }
        AppController.setTrackerWikiToMarkdown("jira", true);
        AppController.clearPendingUndo();
    }
    function mkTask(title, desc) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = title;
        d.desc = desc || "";
        AppController.saveTask(d);
        tc.made.push(d.id);
        return d.id;
    }
    function make() {
        const doc = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent }', host);
        verify(doc !== null);
        return doc;
    }
    // A Jira Server card whose description is wiki markup.
    function importTicket(desc) {
        tc.homeProfile = AppController.activeProfileId;
        const json = JSON.stringify({ profile: {
            id: "rich-md", name: "Rich md", color: "#5cc2dd",
            statuses: [{ id: "todo", name: "To Do", color: "#888888" }],
            tasks: [{ id: "TL-9", title: "rich wiki ticket", status: "todo", priority: "P2", desc: desc,
                      externalId: "PLAT-9", externalProvider: "jira",
                      externalUrl: "https://jira.example.invalid/browse/PLAT-9" }] } });
        AppController.importProfileFromJson(json, true);
        tc.importedProfile = AppController.activeProfileId;
        const m = AppController.tasks;
        const rId = m.roleOf("id"), rTitle = m.roleOf("title");
        for (let i = 0; i < m.rowCount(); i++)
            if (m.data(m.index(i, 0), rTitle) === "rich wiki ticket") return m.data(m.index(i, 0), rId);
        fail("the ticket was not imported");
        return "";
    }

    function test_the_card_line_keeps_its_look() {
        const c = { code: "#111111", link: "#222222" };
        compare(MdPlain.inlineHtml("- [ ] a **bold** and *it* and `x<y`", 0, c),
                "a <b>bold</b> and <i>it</i> and <font color=\"#111111\">x&lt;y</font>");
        compare(MdPlain.inlineHtml("see [the **docs**](https://x.test) ~~old~~", 0, c),
                "see <font color=\"#222222\">the <b>docs</b></font> <s>old</s>");
        compare(MdPlain.inlineHtml("2 * 3 * 4 and snake_case_name", 0, c), "2 * 3 * 4 and snake_case_name");
        compare(MdPlain.inlineHtml("<img src=x> & co", 0, c), "&lt;img src=x&gt; &amp; co");
        compare(MdPlain.inlineHtml("**abcdef** ghij", 4, c), "<b>abcd</b>…");
    }

    // The task's text is one field across its lines, drawn: Ctrl+A and
    // Shift+arrows select through it as in a note.
    function test_the_task_text_is_one_drawn_field() {
        const id = mkTask("rich probe", "first **line**\n\n- [ ] box\n\nlast line");
        const doc = make();
        doc.open(id);
        const body = findChild(doc, "task-doc-body");
        const field = findChild(body, "md-block-field");
        tryVerify(() => field.visible, 1000, "the text is not open as a field");
        compare(field.text, "first **line**\n\n- [ ] box\n\nlast line");
        verify(findChild(body, "md-live-decorations") !== null, "nothing is drawn over the source");
        body.focusEditor();
        tryVerify(() => field.activeFocus, 1000);
        field.cursorPosition = 0;
        keyClick(Qt.Key_Down, Qt.ShiftModifier);
        keyClick(Qt.Key_Down, Qt.ShiftModifier);
        keyClick(Qt.Key_Down, Qt.ShiftModifier);
        verify(field.selectedText.indexOf("box") >= 0, "Shift+Down stopped at a block's edge: " + field.selectedText);
        // An undo of the document puts the field back to the source.
        keyClick(Qt.Key_End);
        keyClick("x");
        doc.flush();
        compare(AppController.taskById(id).desc.indexOf("x") >= 0, true);
        keyClick(Qt.Key_Escape);
        verify(!field.activeFocus);
        verify(field.visible, "the drawn text went away with the keyboard");
        doc.close();
    }

    function test_tracker_text_is_read_only_and_wiki_is_markdown() {
        const id = importTicket("h2. Steps\n*bold* and {{mono}}\n* one");
        const doc = make();
        doc.open(id);
        const tracker = findChild(doc, "task-doc-tracker-body");
        verify(tracker !== null && tracker.visible, "no tracker text");
        const field = findChild(tracker, "md-block-field");
        tryVerify(() => field.visible, 1000);
        verify(field.readOnly, "the tracker's text can be edited");
        verify(findChild(doc, "task-doc-tracker-readonly").visible, "nothing says why it does not take typing");
        compare(field.text, "## Steps\n**bold** and `mono`\n- one");
        // Typing goes nowhere, and the stored text is the tracker's own.
        field.forceActiveFocus();
        keyClick("z");
        compare(AppController.taskById(id).desc, "h2. Steps\n*bold* and {{mono}}\n* one");
        // As it is, on the user's word.
        AppController.setTrackerWikiToMarkdown("jira", false);
        compare(AppController.trackerDescription(id), "h2. Steps\n*bold* and {{mono}}\n* one");
        doc.close();
    }

    SignalSpy { id: linkSpy; signalName: "internalLinkActivated" }

    // Ctrl+click opens what is under the pointer; a plain click puts the caret.
    function test_ctrl_click_opens_a_link() {
        const id = mkTask("ctrl click probe", "see [[Some note]] here");
        const doc = make();
        doc.open(id);
        const body = findChild(doc, "task-doc-body");
        const field = findChild(body, "md-block-field");
        tryVerify(() => field.visible, 1000);
        linkSpy.target = doc;
        linkSpy.clear();
        waitForRendering(doc);
        const r = field.positionToRectangle(field.text.indexOf("Some") + 2);
        mouseClick(field, r.x + 1, r.y + r.height / 2, Qt.LeftButton, Qt.ControlModifier);
        tryCompare(linkSpy, "count", 1);
        compare(linkSpy.signalArguments[0][0], "note");
        compare(linkSpy.signalArguments[0][1], "Some note");
        // Without Ctrl it is a click into the text.
        mouseClick(field, r.x + 1, r.y + r.height / 2);
        compare(linkSpy.count, 1);
        tryVerify(() => field.activeFocus, 1000);
        doc.close();
    }

    // A long tracker text does not scroll inside a box of its own any more
    // (dogfood 2026-10-23, Heavy sheet): the wheel over the text itself (the
    // field lies over the view) scrolls the task's one scroll.
    function test_a_long_tracker_text_scrolls_under_the_wheel() {
        let text = "";
        for (let i = 1; i <= 60; i++) text += "Paragraph " + i + " with some words.

";
        const id = importTicket(text);
        const doc = make();
        doc.open(id);
        const tracker = findChild(doc, "task-doc-tracker-body");
        const field = findChild(tracker, "md-block-field");
        const view = findChild(tracker, "md-block-view");
        const scroll = findChild(doc, "task-doc-scroll");
        doc.descExpanded = true;
        tryVerify(() => field.visible && scroll.contentHeight > scroll.height, 2000, "the task does not overflow");
        verify(!view.interactive, "the text scrolls inside itself");
        wait(Theme.durMove + 100);   // the sheet slides in
        const before = scroll.contentY;
        mouseWheel(field, 40, 40, 0, -360);
        tryVerify(() => scroll.contentY > before, 1000, "the wheel over the text did not scroll the task");
        compare(view.contentY, view.originY, "the text moved inside itself");
        doc.close();
    }

    function test_the_task_dialog_draws_its_description() {
        const id = mkTask("dialog probe", "a **bold** line");
        const te = createTemporaryQmlObject('import TodoCpp; TaskEditor {}', host);
        verify(te !== null);
        te.showFor(AppController.taskById(id));
        const desc = findChild(te, "te-desc");
        tryCompare(desc, "text", "a **bold** line");
        verify(findChild(te, "desc-mode-preview") === null, "Text / Preview tabs are still there");
        verify(findChild(te, "md-live-decorations") !== null, "the description is not drawn");
        // Enter goes on with the list, as in a note.
        desc.text = "- one";
        desc.forceActiveFocus();
        desc.cursorPosition = desc.length;
        keyClick(Qt.Key_Return);
        compare(desc.text, "- one\n- ");
        te.close();
    }
}
