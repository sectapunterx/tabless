// Contract + behaviour tests for qml/HelpContent.qml, the guide ("С чего
// начать") the Settings help reader shows. Since dogfood 2026-10-20 #12 it is
// short parts with a contents list, a search field and "Show" links, all
// driven from the keyboard.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "HelpContent"
    when: windowShown
    visible: true
    width: 900
    height: 700

    Item { id: host; anchors.fill: parent }

    function make(qml) {
        const o = createTemporaryQmlObject(qml || 'import TodoCpp; HelpContent { anchors.fill: parent }', host);
        verify(o !== null);
        return o;
    }

    // Every text item under `it`, for language checks.
    function texts(it, out) {
        out = out || [];
        if (!it) return out;
        if (typeof it.text === "string" && it.contentWidth !== undefined && it.visible) out.push(it);
        const kids = it.children || [];
        for (let k = 0; k < kids.length; k++) texts(kids[k], out);
        return out;
    }

    function test_smoke_load() {
        const hc = make();
        verify(findChild(hc, "help-search") !== null);
        verify(findChild(hc, "help-toc") !== null);
    }

    // tocModel: {anchor, label} pairs, unique anchors, each one a part.
    function test_every_toc_anchor_is_a_part() {
        const hc = make();
        const toc = hc.tocModel;
        verify(toc.length >= 10, "parts: " + toc.length);
        const seen = ({});
        for (let i = 0; i < toc.length; ++i) {
            verify(toc[i].anchor.length > 0 && toc[i].label.length > 0);
            verify(seen[toc[i].anchor] === undefined, "duplicate anchor " + toc[i].anchor);
            seen[toc[i].anchor] = true;
            verify(findChild(hc, toc[i].anchor) !== null, "no part for " + toc[i].anchor);
        }
    }

    // Short: one line and at most four bullets per part, no paragraph.
    function test_parts_are_short() {
        const hc = make();
        for (const s of hc.sections) {
            verify(s.line.length <= 90, s.anchor + " line too long: " + s.line);
            verify(s.bullets.length >= 1 && s.bullets.length <= 4, s.anchor + " bullets: " + s.bullets.length);
            for (const b of s.bullets) verify(b.length <= 110, s.anchor + " bullet too long: " + b);
            verify(s.keys.length <= 3, s.anchor + " keys: " + s.keys.length);
        }
    }

    // The search filters the parts, in either language, and says so when
    // nothing is left.
    function test_search_filters_parts() {
        const saved = AppController.language;
        AppController.language = "ru";
        const hc = make();
        const all = hc.visibleSections.length;
        hc.query = "заметк";
        verify(hc.visibleSections.length > 0 && hc.visibleSections.length < all);
        verify(findChild(hc, "help-notes").visible);
        verify(!findChild(hc, "help-today").visible);
        hc.query = "notes";   // English words find the Russian part too
        verify(findChild(hc, "help-notes").visible);
        hc.query = "календарь неделя";   // every word, any order
        compare(hc.visibleSections.length, 1);
        compare(hc.visibleSections[0].anchor, "help-calendar");
        hc.query = "zzqqxx";
        verify(hc.nothingFound);
        verify(findChild(hc, "help-search-empty").visible);
        AppController.language = saved;
    }

    // "/" and Ctrl+F focus the search; Esc clears it, a second Esc closes.
    function test_keys_search_and_escape() {
        const hc = make();
        hc.reset("");
        const field = findChild(hc, "help-search");
        verify(!field.activeFocus);
        keyClick(Qt.Key_Slash);
        tryCompare(field, "activeFocus", true);
        keyClick(Qt.Key_G);
        keyClick(Qt.Key_I);
        keyClick(Qt.Key_T);
        compare(hc.query, "git");
        let closed = 0;
        hc.closeRequested.connect(() => closed++);
        keyClick(Qt.Key_Escape);
        compare(hc.query, "");
        compare(closed, 0, "the first Esc only clears");
        keyClick(Qt.Key_Escape);
        compare(closed, 1);
        hc.reset("");
        keyClick(Qt.Key_F, Qt.ControlModifier);
        tryCompare(field, "activeFocus", true);
    }

    // Arrows walk the contents; Enter goes to the part's screen.
    function test_arrows_and_enter() {
        const hc = make();
        hc.reset("");
        const toc = findChild(hc, "help-toc");
        tryCompare(toc, "activeFocus", true);
        compare(hc.currentAnchor, hc.sections[0].anchor);
        keyClick(Qt.Key_Down);
        compare(hc.currentAnchor, hc.sections[1].anchor);
        keyClick(Qt.Key_Up);
        compare(hc.currentAnchor, hc.sections[0].anchor);
        let shown = "";
        hc.showRequested.connect((t) => shown = t);
        keyClick(Qt.Key_Return);
        compare(shown, hc.sections[0].show);
        keyClick(Qt.Key_End);
        compare(hc.currentAnchor, hc.sections[hc.sections.length - 1].anchor);
        // A filtered list: ↓ from the search lands on the first part found.
        hc.focusSearch();
        hc.query = "branch";
        keyClick(Qt.Key_Down);
        tryCompare(toc, "activeFocus", true);
        compare(hc.currentAnchor, "help-git");
        keyClick(Qt.Key_Return);
        compare(shown, "settings:git");
    }

    // "Show" is a link that runs from the mouse too.
    function test_show_link() {
        const hc = make();
        const link = findChild(hc, "help-show-help-notes");
        verify(link && link.visible);
        let shown = "";
        hc.showRequested.connect((t) => shown = t);
        hc.scrollToAnchor("help-notes");
        mouseClick(link);
        compare(shown, "notes");
        verify(!findChild(hc, "help-show-help-capture").visible, "a part with no screen has no Show");
    }

    // The guide reads in Russian when the app does, and flips back.
    function test_guide_follows_the_language() {
        const saved = AppController.language;
        AppController.language = "ru";
        const hc = make();
        const english = [];
        for (const t of texts(hc)) {
            const v = t.text;
            if (v.length < 24 || /^[A-Za-z0-9+\-\/ .,:]*$/.test(v) && v.indexOf(" ") < 0) continue;
            if (!/[А-Яа-яЁё]/.test(v)) english.push(v.slice(0, 60));
        }
        const ru = hc.tocModel[0].label;
        AppController.language = "en";
        const en = hc.tocModel[0].label;
        AppController.language = saved;
        compare(english.length, 0, "English left in the Russian guide: " + english.join(" | "));
        verify(/[А-Яа-я]/.test(ru) && !/[А-Яа-я]/.test(en), ru + " / " + en);
    }

    // Key chips show the live bindings; the calendar part names Month.
    function test_keys_are_live() {
        const hc = make();
        compare(hc.kbd("tweaks.open"), AppController.shortcutText("tweaks.open"));
        let cal = null;
        for (const s of hc.sections) if (s.anchor === "help-calendar") cal = s;
        verify(cal !== null);
        const month = cal.keys.filter((k) => k.keys === AppController.shortcutText("view.month"));
        compare(month.length, 1);
    }

    // The same words as the app: "Due", not "deadline"; "Notes", not
    // "Knowledge"; "резервная копия", not "бэкап"; the command line, not a palette.
    function test_guide_speaks_the_glossary() {
        const saved = AppController.language;
        const banned = {
            en: [/\bdeadlines?\b/i, /\bknowledge\b/i, /who to write/i, /command palette/i, /\bpalette\b/i],
            ru: [/дедлайн/i, /знани[яй]/i, /бэкап/i, /палитр/i]
        };
        const bad = [];
        for (const lang of ["en", "ru"]) {
            AppController.language = lang;
            const hc = make();
            for (const s of hc.sections) {
                const words = [s.title, s.line].concat(s.bullets).concat(s.keys.map((k) => k.word));
                for (const w of words)
                    for (const rx of banned[lang])
                        if (rx.test(w)) bad.push(lang + ": " + w);
            }
        }
        AppController.language = saved;
        compare(bad.length, 0, "old terms in the guide:\n" + bad.join("\n"));
    }
}
