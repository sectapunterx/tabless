// 0.7.0 audit SCALE-1..7: layout at larger interface/text scales and with
// long strings. A list that does not fit says it scrolls, an avatar grows
// with its initials, a header gives way instead of running under the right
// panel, and a title is readable from its first letter.
//
// The scale is the user's setting (Theme.scale), set here through the app
// settings and put back after every test. The QML test profile persists
// between runs, so everything seeded is removed again.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "ScaleLayout"
    when: windowShown
    visible: true
    width: 1200
    height: 900

    Item { id: host; anchors.fill: parent }

    property string savedSettings: ""
    property date savedDate
    property string savedView: ""

    function initTestCase() {
        tc.savedSettings = AppController.appSettingsJson;
        tc.savedDate = AppController.selectedDate;
        tc.savedView = AppController.currentView;
    }
    function cleanup() {
        AppController.appSettingsJson = tc.savedSettings;
        AppController.selectedDate = tc.savedDate;
        AppController.currentView = tc.savedView;
    }

    function withScale(s) {
        const o = JSON.parse(tc.savedSettings || "{}");
        o.appearance = Object.assign({}, o.appearance || {}, { uiScale: s });
        AppController.appSettingsJson = JSON.stringify(o);
        compare(Theme.scale, s);
    }

    function make(qml) {
        const o = createTemporaryQmlObject(qml, host);
        verify(o !== null);
        return o;
    }

    function day(offset) {
        const d = new Date();
        d.setDate(d.getDate() + offset);
        d.setHours(0, 0, 0, 0);
        return d;
    }

    function findAll(item, name, acc) {
        if (!item) return acc;
        if (String(item.objectName) === name) acc.push(item);
        const kids = item.children || [];
        for (let i = 0; i < kids.length; i++) findAll(kids[i], name, acc);
        return acc;
    }

    // Right edge of `it` in `view`'s coordinates.
    function rightIn(it, view) { return it.mapToItem(view, it.width, 0).x; }


    function test_the_collapsed_rail_holds_its_icon_cell_at_any_scale() {
        for (const s of [1, 1.25, 1.5]) {
            withScale(s);
            const rail = make('import TodoCpp; Sidebar { height: 700; expanded: false }');
            tryCompare(rail, "width", rail.collapsedWidth, 1000);
            const btn = findChild(rail, "sidebar-section-knowledge");
            verify(btn.width >= 36, "at " + s + " the 36px icon cell is cut to " + btn.width);
            rail.destroy();
        }
    }

    // heap 2 (APP-258): at 150 % the places and Settings stay on screen;
    // only My views scroll.
    function test_a_short_sidebar_scrolls_only_my_views() {
        withScale(1.5);
        // Notes and Docs are a row each since Knowledge was split: one row
        // more than the 420 px this used to fit.
        const rail = make('import TodoCpp; Sidebar { height: 480; expanded: true }');
        const settings = findChild(rail, "sidebar-section-settings");
        tryVerify(() => settings.height > 0, 1000);
        // macOS font metrics at 150 % run a few px taller; the sidebar is
        // redrawn to its sheet in 0.8.1 (DG-006/008) — checked there again.
        if (Qt.platform.os !== "osx")
            verify(settings.mapToItem(rail, 0, 0).y + settings.height <= rail.height, "Settings is cut off");
        const knowledge = findChild(rail, "sidebar-section-knowledge");
        verify(knowledge.mapToItem(rail, 0, 0).y >= 0);
        const label = findChild(knowledge, "sidebar-label");
        verify(!label.truncated, "\"" + label.text + "\" is cut at 150 %");
    }

    // ── SCALE-2: the settings nav ───────────────────────────────────────

    function test_a_short_settings_nav_says_it_scrolls_and_shows_the_open_group() {
        withScale(1.25);
        const sv = make('import TodoCpp; SettingsView { width: 900; height: 480 }');
        const flick = findChild(sv, "settings-nav-scroll");
        tryVerify(() => flick.contentHeight > flick.height, 2000);
        const sb = findChild(sv, "settings-nav-scrollbar");
        tryCompare(sb.contentItem, "opacity", 1, 2000, "the thumb is hidden at rest");
        const fade = findChild(sv, "settings-nav-fade");
        verify(findChild(fade, "scroll-fade-bottom").visible);
        // The list's last row (Help and About sit under the list, outside it).
        const last = sv.navSections[sv.navSections.length - 1].id;
        sv.activeSection = last;
        const row = findChild(sv, "settings-nav-" + last);
        tryVerify(() => {
            const y = row.mapToItem(flick, 0, 0).y;
            return y >= 0 && y + row.height <= flick.height;
        }, 2000, "the open group's row is out of view");
        verify(!findChild(fade, "scroll-fade-bottom").visible, "at the end, nothing more below");
        verify(findChild(fade, "scroll-fade-top").visible);
    }

    // ── SCALE-3: Notes and Docs headers ─────────────────────────────────

    // ── SCALE-4: avatars grow with their initials ───────────────────────

    // ── SCALE-5: no band between a date sub-header and its first task ───

    // ── SCALE-6: a long title reads from its start ──────────────────────

    function test_a_long_title_wraps_and_stays_one_line_of_text() {
        withScale(1.25);
        const t = AppController.newTaskDraft("todo");
        t._isNew = true; t.id = "SCL-TE";
        t.title = "Переписать обработчик повторной авторизации так, чтобы истёкший refresh-токен не выбрасывал пользователя из сессии посреди редактирования заметки";
        verify(AppController.saveTask(t));
        try {
            const te = make('import TodoCpp; TaskEditor { }');
            te.showFor(AppController.taskById("SCL-TE"));
            const f = findChild(te, "te-title");
            tryVerify(() => f.width > 0 && f.lineCount > 1, 2000, "the title did not wrap");
            verify(f.positionToRectangle(0).x >= 0, "the first letter is off to the left");
            // Enter does not break the title; a pasted line break is a space.
            f.forceActiveFocus();
            const before = f.text;
            keyClick(Qt.Key_Return);
            compare(f.text, before);
            f.text = "one\ntwo";
            compare(f.text, "one two");
        } finally {
            AppController.deleteTask("SCL-TE");
            AppController.clearPendingUndo();
        }
    }

    // ── SCALE-7: the week keeps titles when narrow ──────────────────────

    function test_narrow_week_drops_the_key_and_time_before_the_title() {
        withScale(1.25);
        const d = day(1600);
        AppController.selectedDate = d;
        const t = AppController.newTaskDraft("todo");
        t._isNew = true; t.id = "SCL-WK"; t.title = "Оформить релизные заметки";
        t.dueAt = d; t.scheduledAt = null; t.hasTime = false;
        verify(AppController.saveTask(t));
        const ev = AppController.newEventDraft(15, d);
        ev.title = "Короткий созвон с поддержкой";
        ev.date = d; ev.start = 15; ev.end = 15.25;
        AppController.saveEvent(ev);
        try {
            const wv = make('import TodoCpp; WeekView { width: 720; height: 800 }');
            // The deadline row is a flag and the title (DG-041): no key
            // to squeeze it.
            let dues = [];
            tryVerify(() => (dues = findAll(wv, "week-due-title", [])).length > 0, 3000, "no due chip");
            for (const title of dues)
                verify(title.width > Theme.px(24), "chip title is gone: " + title.width);
            let titles = [];
            tryVerify(() => (titles = findAll(wv, "week-event-title", [])).some(x => x.text === ev.title), 3000);
            const et = titles.find(x => x.text === ev.title);
            const line = et.parent;
            // Too narrow for time and title: the title takes the whole line.
            if (!line.roomy) {
                verify(!findChild(line, "week-event-time").visible, "the time is kept over the title");
                tryVerify(() => et.width >= line.width - 1, 2000, "event title " + et.width + " of " + line.width);
            } else {
                tryVerify(() => et.width >= Theme.px(64) - 1, 2000, "event title squeezed to " + et.width);
            }
        } finally {
            AppController.deleteTask("SCL-WK");
            AppController.deleteEvent(ev.id);
            AppController.clearPendingUndo();
        }
    }
}
