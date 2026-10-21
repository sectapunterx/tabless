// Dogfood 2026-10-21, items 1 and 3: a picture in a text (a ticket's, a
// note's) opened only as the small copy the text draws. A click or Enter
// opens it whole in the file viewer: fitted or at 100 %, + − and Ctrl+wheel
// zoom, ← → through the other pictures, Esc closes.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "ImageViewer"
    when: windowShown
    visible: true
    width: 1100
    height: 760

    Item { id: host; anchors.fill: parent }
    readonly property url bigFile: Qt.resolvedUrl("data/big-shot.png")
    readonly property url smallFile: Qt.resolvedUrl("data/small-shot.png")

    function stored(url) {
        const added = AppController.importAttachments([url], false);
        compare(added.length, 1);
        return String(added[0].id);
    }
    function viewer() {
        return createTemporaryQmlObject('import TodoCpp; FileViewerDialog {}', host);
    }
    // The first item named `name` (and labelled `label`, when given).
    function child(item, name, label) {
        if (!item) return null;
        if (item.objectName === name && (label === undefined || item.label === label)) return item;
        const kids = item.children || [];
        for (let i = 0; i < kids.length; i++) {
            const f = child(kids[i], name, label);
            if (f) return f;
        }
        return null;
    }

    function test_the_viewer_keys() {
        const big = stored(tc.bigFile);
        const small = stored(tc.smallFile);
        const v = viewer();
        v.showImages([{ attId: big, name: "big-shot.png" }, { attId: small, name: "small-shot.png" }], 0);
        tryCompare(v, "opened", true);
        const pic = findChild(v.contentItem, "file-viewer-image");
        verify(pic !== null);
        tryCompare(pic, "status", Image.Ready);
        // Its own pixels, fitted into the window.
        compare(pic.implicitWidth, 2400);
        verify(v.fit);
        verify(v.scaleNow < 1, "a big picture is not fitted: " + v.scaleNow);
        verify(pic.width <= findChild(v.contentItem, "file-viewer-flick").width + 1);

        keyClick(Qt.Key_1);
        verify(!v.fit);
        compare(v.scaleNow, 1);
        compare(pic.width, 2400);
        const flick = findChild(v.contentItem, "file-viewer-flick");
        verify(flick.interactive, "a picture bigger than the window cannot be dragged about");
        keyClick(Qt.Key_Minus);
        fuzzyCompare(v.scaleNow, 0.8, 0.001);
        keyClick(Qt.Key_Plus);
        fuzzyCompare(v.scaleNow, 1, 0.001);
        keyClick(Qt.Key_0);
        verify(v.fit);

        // Ctrl+wheel zooms; the wheel alone does not.
        const fitted = v.scaleNow;
        mouseWheel(flick, flick.width / 2, flick.height / 2, 0, 120, Qt.NoButton, Qt.ControlModifier);
        verify(!v.fit);
        fuzzyCompare(v.scaleNow, fitted * 1.25, 0.001);
        keyClick(Qt.Key_0);

        // ← → through the text's pictures, round the ends.
        keyClick(Qt.Key_Right);
        compare(v.imageIndex, 1);
        compare(v.attId, small);
        tryCompare(pic, "status", Image.Ready);
        compare(v.scaleNow, 1, "a small picture is never blown up");
        keyClick(Qt.Key_Right);
        compare(v.imageIndex, 0);
        keyClick(Qt.Key_Left);
        compare(v.imageIndex, 1);

        keyClick(Qt.Key_Escape);
        tryCompare(v, "opened", false);
    }

    // A ticket's picture nobody fetched: a readable error, never a blank.
    function test_a_picture_that_cannot_be_fetched_says_why() {
        const v = viewer();
        v.showImages([{ attId: "", name: "x.png", taskId: "no-such-task", attachmentId: "1" }], 0);
        tryCompare(v, "opened", true);
        verify(v.imageError.length > 0, "no error shown");
        const err = findChild(v.contentItem, "file-viewer-image-error");
        verify(err.visible);
        keyClick(Qt.Key_Escape);
        tryCompare(v, "opened", false);
    }

    function richReadOnly(text) {
        const ed = createTemporaryQmlObject(
            'import TodoCpp; MdBlockEditor { width: 900; height: 600; readOnly: true; rich: true; wholeDocument: true; noteType: true }', host);
        ed.text = text;
        return ed;
    }

    // The figure a read text draws is clicked: the picture opens whole.
    function test_a_click_on_a_drawn_picture_opens_it() {
        const big = stored(tc.bigFile);
        const small = stored(tc.smallFile);
        const ed = richReadOnly("Look:\n\n![Big one](attachments/" + big + ")\n\ntext\n\n![](attachments/" + small + ")\n");
        let open = null;
        tryVerify(() => { open = child(ed, "md-live-image-open", "Big one"); return open !== null && open.visible && open.width > 0; },
                  3000, "the figure has nothing to click");
        const other = child(ed, "md-live-image-open", I18n.t("notes.image.open"));
        verify(other !== null);
        // Laid out: the two figures one above the other, not on one spot.
        tryVerify(() => Math.abs(open.mapToItem(ed, 0, 0).y - other.mapToItem(ed, 0, 0).y) >= open.height, 3000,
                  "the figures overlap: " + open.mapToItem(ed, 0, 0).y + " " + other.mapToItem(ed, 0, 0).y + " h " + open.height);
        mouseClick(open);
        const preview = child(ed, "md-live-preview");
        verify(preview !== null);
        tryVerify(() => preview._imageViewer !== null && preview._imageViewer.opened, 3000, "the viewer did not open");
        const v = preview._imageViewer;
        compare(v.images.length, 2, "the text's other picture is not a → away");
        compare(v.attId, big);
        keyClick(Qt.Key_Right);
        compare(v.attId, small);
        keyClick(Qt.Key_Escape);
        tryCompare(v, "opened", false);
    }

    // The caret on a picture's line of a read text: Enter opens it.
    function test_enter_on_a_picture_opens_it() {
        const small = stored(tc.smallFile);
        const ed = richReadOnly("First line\n\n![shot](attachments/" + small + ")\n\nlast\n");
        const field = findChild(ed, "md-block-field");
        tryVerify(() => field.visible);
        field.forceActiveFocus();
        field.cursorPosition = 2;
        keyClick(Qt.Key_Return);
        const preview = child(ed, "md-live-preview");
        verify(!preview._imageViewer || !preview._imageViewer.opened, "Enter on plain text opened a picture");
        field.forceActiveFocus();
        field.cursorPosition = field.text.indexOf("![shot]") + 3;
        keyClick(Qt.Key_Return);
        tryVerify(() => preview._imageViewer !== null && preview._imageViewer.opened, 3000, "Enter did not open the picture");
        compare(preview._imageViewer.attId, small);
        compare(field.text.indexOf("\n\n\n"), -1, "Enter wrote into read-only text");
        keyClick(Qt.Key_Escape);
        tryCompare(preview._imageViewer, "opened", false);
    }
}
