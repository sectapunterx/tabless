// A key that is an action of its own and also the start of a sequence live
// where the person is (the task's d beside the document's d d), review
// 2026-10-19: the key alone, or followed by a key that continues nothing,
// runs NOTHING - a slow d d closed the task, and d then a in type-to-search
// marked it done and searched "da". Where the sequence is not live, d is
// Done at once. A standalone router with a recording handler.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "KeyRouterPrefix"
    when: windowShown
    width: 200
    height: 100

    property var ran: []
    property var typed: []
    // Which ids are live where the "person" is.
    property var live: ({})

    Item { id: holder; focus: true; anchors.fill: parent }

    KeyRouter {
        id: router
        layout: "latin"
        timeoutMs: 150
        bindings: [
            { id: "task.done", sequence: "D" },
            { id: "editor.deleteBlock", sequence: "D, D" },
            { id: "task.priority", sequence: "1" }
        ]
        handler: function (ids, dry) {
            const list = String(ids).split(",");
            for (let i = 0; i < list.length; i++) {
                if (tc.live[list[i]] === true) {
                    if (!dry) tc.ran = tc.ran.concat([list[i]]);
                    return list[i];
                }
            }
            return "";
        }
        onTypedText: (text) => { tc.typed = tc.typed.concat([text]); }
    }

    function init() {
        router.cancel();
        router.holdUnlive = false;
        router.typingHoldMs = 0;
        tc.ran = [];
        tc.typed = [];
        tc.live = { "task.done": true, "editor.deleteBlock": true, "task.priority": true };
        holder.forceActiveFocus();
    }

    function test_d_alone_runs_nothing_when_d_d_is_live() {
        keyClick(Qt.Key_D);
        compare(router.pending, "D", "d waits for its second d");
        wait(400);
        compare(router.pending, "", "the wait is over");
        compare(tc.ran, [], "and Done did not run on the timeout");
    }

    function test_d_d_runs_the_sequence_only() {
        keyClick(Qt.Key_D);
        keyClick(Qt.Key_D);
        compare(tc.ran, ["editor.deleteBlock"]);
        wait(300);
        compare(tc.ran, ["editor.deleteBlock"], "no Done after it");
    }

    function test_d_then_another_key_cancels_and_the_key_is_its_own() {
        keyClick(Qt.Key_D);
        keyClick(Qt.Key_1);
        compare(tc.ran, ["task.priority"], "d let go, 1 ran as itself");
        compare(router.pending, "");
        wait(300);
        compare(tc.ran, ["task.priority"]);
    }

    function test_d_is_done_where_d_d_is_not_live() {
        tc.live = { "task.done": true, "editor.deleteBlock": false };
        keyClick(Qt.Key_D);
        compare(router.pending, "", "nothing to wait for");
        compare(tc.ran, ["task.done"], "Done at once");
    }

    // Type-to-search live: d (a live prefix) then a letter that is nobody's
    // is a word, typed into the search - and Done never runs.
    function test_d_then_a_letter_is_a_word_not_done() {
        router.holdUnlive = true;
        router.typingHoldMs = 200;
        keyClick(Qt.Key_D);
        keyClick(Qt.Key_A);
        wait(400);
        compare(tc.ran, [], "nothing ran");
        compare(tc.typed, ["da"], "the two letters went to the search");
    }
}
