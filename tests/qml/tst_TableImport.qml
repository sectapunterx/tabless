// Notes → Импорт → a table (dogfood 2026-10-23 #9): a table in plain text has
// its separator found, can be switched by hand, every row goes in as one
// table, and text with no table can still go in as it is.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TableImport"
    when: windowShown
    visible: true
    width: 900
    height: 700

    Item { id: host; anchors.fill: parent }

    function make() {
        const d = createTemporaryQmlObject('import TodoCpp; TableImportDialog { }', host);
        verify(d !== null);
        return d;
    }

    function test_a_log_with_pipes_becomes_a_table() {
        AppController.copyToClipboard("trace, build 4411\nslot | ue | cce\n1204 | 17 | 8\n1205 | 3 | 0\n-- end --\n");
        const d = make();
        d.openFor("clipboard", false);
        compare(d.preview.kind, "text");
        compare(d.preview.separator, "pipe");
        verify(d.isTable);
        compare(d.preview.columns, 3);
        compare(d.preview.skipped, 2);
        let got = "";
        d.imported.connect(function (md, into, title) { got = md; });
        d.run(false);
        verify(got.indexOf("| slot | ue | cce |") === 0, got);
        verify(got.indexOf("| 1205 | 3 | 0 |") > 0, got);
    }

    function test_a_chosen_separator_and_letter_headers() {
        AppController.copyToClipboard("1;2;3\n4;5;6\n");
        const d = make();
        d.openFor("clipboard", false);
        compare(d.preview.separator, "semicolon");
        d.firstRowHeader = false;
        compare(d.preview.total, 2, "both lines are data rows");
        compare(d.preview.rows[0], ["A", "B", "C"]);
        d.separator = "comma";
        verify(!d.isTable, "no commas, no table");
        d.separator = "custom";
        d.custom = ";";
        verify(d.isTable);
    }

    function test_every_row_in_one_table() {
        let text = "";
        for (let i = 0; i < 1200; ++i) text += i + "\t" + (i * 2) + "\n";
        AppController.copyToClipboard(text);
        const d = make();
        d.openFor("clipboard", false);
        compare(d.preview.total, 1199);
        compare(d.preview.rows.length, 51, "the preview shows the header and 50 rows");
        let got = "";
        d.imported.connect(function (md) { got = md; });
        d.run(false);
        compare(got.split("\n").length, 1201, "header, rule and 1199 rows");
    }

    function test_text_without_a_table_goes_in_as_text() {
        AppController.copyToClipboard("Just a thought.\nNothing tabular.\n");
        const d = make();
        d.openFor("clipboard", false);
        verify(!d.isTable);
        verify(!findChild(d, "table-import-go").enabled);
        let got = "";
        d.imported.connect(function (md) { got = md; });
        d.run(true);
        compare(got, "Just a thought.\nNothing tabular.\n");
    }
}
