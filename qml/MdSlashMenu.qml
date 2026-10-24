pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as QQC
import TodoCpp

// What "/" inserts (R2-024, sheet X/N-Oth-Knowledge): the six rows, the
// markdown each one types on the right. One menu for the task document's
// field (MdBlockEditor) and the note's rich editor (a-03).
//
// The "/" is typed as it is (sheet: "/" and the caret over the menu); a pick
// replaces it, Esc leaves nothing behind.
AppMenu {
    id: menu
    objectName: "md-slash-menu"

    required property QQC.TextArea field
    // The field is in use; off (a block that was left), closing edits
    // nothing in it.
    property bool live: true
    // A file picker opened from a row is still open: closing leaves the "/"
    // and the caret to it.
    property bool holding: false
    // "Картинка или файл": the host picks the file; slashAt says where.
    signal fileRequested()
    // A position in the table the "/" was typed in (or under), -1 for none:
    // the menu then starts with the table's rows and columns, asked of the
    // host as tableRequested(op, at) once the "/" is gone.
    property int tableAt: -1
    signal tableRequested(string op, int at)
    function pickTable(op) {
        const at = menu.tableAt;
        menu.dropSlash();
        menu.tableRequested(op, at);
    }
    // The table's rows, put at the top while the menu is open in a table and
    // taken out when it closes: hidden rows would still take the keyboard's
    // first place (currentIndex 0) and count as rows of the menu.
    property var _tableItems: []
    function _addTableItems() {
        if (menu.tableAt < 0 || menu._tableItems.length > 0) return;
        const ops = ["rowAbove", "rowBelow", "columnLeft", "columnRight", "deleteRow", "deleteColumn"];
        const made = [];
        for (let i = 0; i < ops.length; i++) {
            const item = tableItem.createObject(null, { op: ops[i] });
            menu.insertItem(i, item);
            made.push(item);
        }
        const sep = tableSeparator.createObject(null);
        menu.insertItem(ops.length, sep);
        made.push(sep);
        menu._tableItems = made;
    }
    function _dropTableItems() {
        for (let i = 0; i < menu._tableItems.length; i++) menu.removeItem(menu._tableItems[i]);
        menu._tableItems = [];
    }
    Component {
        id: tableItem
        AppMenuItem {
            property string op: ""
            objectName: "md-slash-table-" + op
            text: I18n.t("notes.table." + op)
            danger: op.indexOf("delete") === 0
            onTriggered: menu.pickTable(op)
        }
    }
    Component {
        id: tableSeparator
        AppMenuSeparator {}
    }

    // The sheet's "/" menu is 280px wide (R3-093).
    minWidth: Theme.px(280)
    // Where the typed "/" is; a pick replaces it.
    property int slashAt: -1

    // The "/" typed at the caret and the menu under it.
    function openAtCaret() {
        const at = menu.field.cursorPosition;
        menu.field.insert(at, "/");
        menu.field.cursorPosition = at + 1;
        menu.slashAt = at;
        menu._dropTableItems();
        menu._addTableItems();
        menu.popup(menu.field, menu.field.cursorRectangle.x, menu.field.cursorRectangle.y + menu.field.cursorRectangle.height);
        menu.currentIndex = 0;
    }
    function dropSlash() {
        const at = menu.slashAt;
        menu.slashAt = -1;
        if (at >= 0 && menu.field.text.charAt(at) === "/") {
            menu.field.remove(at, at + 1);
            menu.field.cursorPosition = at;
        }
    }
    // A table is a block of its own: under a line of text it read as
    // that paragraph's pipes. The caret goes into the first cell, not
    // under the table where typing made a row (KNOW-6).
    function putTable() {
        menu.dropSlash();
        const at = menu.field.cursorPosition;
        const t = menu.field.text;
        // A blank line above and below it, whatever the caret was in: put
        // into a line of text, the text after the caret ran on the last
        // row's line, and a table under a line read as its paragraph
        // (dogfood 2026-10-20).
        const ls = t.lastIndexOf("\n", at - 1) + 1;
        let le = t.indexOf("\n", at);
        if (le < 0) le = t.length;
        const before = t.substring(ls, at), after = t.substring(at, le);
        const prev = ls > 0 ? t.substring(t.lastIndexOf("\n", ls - 2) + 1, ls - 1) : "";
        const next = le < t.length ? t.substring(le + 1, t.indexOf("\n", le + 1) < 0 ? t.length : t.indexOf("\n", le + 1)) : "";
        const lead = before.trim().length > 0 ? "\n\n" : before.length === 0 && prev.trim().length > 0 ? "\n" : "";
        const tail = after.trim().length > 0 ? "\n\n" : after.length === 0 && next.trim().length > 0 ? "\n" : "";
        const s = lead + "|  |  |\n|---|---|\n|  |  |" + tail;
        menu.field.insert(at, s);
        menu.field.cursorPosition = at + lead.length + 2;
        menu.field.forceActiveFocus();
    }
    function put(s, back) {
        menu.dropSlash();
        const at = menu.field.cursorPosition;
        menu.field.insert(at, s);
        menu.field.cursorPosition = at + s.length - (back || 0);
        menu.field.forceActiveFocus();
    }
    // Closed without a pick (Esc): the "/" it was opened by goes too
    // (IDIOT-KNOW-16) — as it starts to close, before the Esc reaches
    // the field. A pick has already cleared slashAt.
    onAboutToHide: if (!menu.holding && menu.live) menu.dropSlash()
    // The caret back where it was.
    // Not when a second "/" has opened it again in the meantime (seen on
    // Qt 6.9): run that late, this cleared the new slashAt and took the
    // keyboard back, which closed the menu just opened and left the "/" in
    // the text.
    onClosed: {
        // The table's rows go with it, unless it is open again already.
        if (!menu.visible) {
            menu._dropTableItems();
            menu.tableAt = -1;
        }
        Qt.callLater(() => {
            if (menu.holding || menu.visible) return;
            menu.slashAt = -1;
            if (menu.live && menu.field.visible && !menu.field.activeFocus) menu.field.forceActiveFocus();
        });
    }
    AppMenuItem { objectName: "md-slash-check"; text: I18n.t("md.slash.checklist"); keyText: "[]"; onTriggered: menu.put("- [ ] ") }
    AppMenuItem { objectName: "md-slash-code"; text: I18n.t("md.slash.code"); keyText: "```"; onTriggered: menu.put("```\n\n```", 4) }
    AppMenuItem { objectName: "md-slash-task"; text: I18n.t("md.slash.task"); keyText: "[["; onTriggered: menu.put("[[]]", 2) }
    AppMenuItem { objectName: "md-slash-file"; text: I18n.t("md.slash.file"); onTriggered: menu.fileRequested() }
    AppMenuItem { objectName: "md-slash-table"; text: I18n.t("md.slash.table"); onTriggered: menu.putTable() }
    // A reference: a titled link (the knowledge base's "RFC 6585 ·
    // 429 Too Many Requests ↗"); the caret on the title.
    AppMenuItem { objectName: "md-slash-ref"; text: I18n.t("md.slash.reference"); onTriggered: menu.put("[](https://)", 11) }
}
