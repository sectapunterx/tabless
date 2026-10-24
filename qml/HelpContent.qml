pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TodoCpp

// The guide ("С чего начать", Settings → Help; Ctrl K "welcome.replay"),
// rebuilt after dogfood 2026-10-20 #12: it was a wall of paragraphs and not
// searchable. Now short parts: a contents list on the left, a search field
// on top (as in Settings), and each part is one line on what it is, the keys
// that run it and a few bullets, with "Show" taking you there.
//
// Keys: "/" or Ctrl+F — search; ↑/↓ — move through the parts; Enter — go to
// the part's screen; Esc — clear the search, then close.
FocusScope {
    id: root

    // A part was picked: the reader scrolls to it (kept for SettingsView).
    signal anchorRequested(string objectName)
    // "Show": a view name (today, board, week, notes, docs) or "settings:<section>".
    signal showRequested(string target)
    // Esc with nothing left to clear.
    signal closeRequested()

    implicitWidth: Theme.px(760)
    implicitHeight: Theme.px(600)
    focus: true

    // The guide is written in both languages side by side; the one shown
    // follows I18n.lang, and switching repaints it.
    function tr2(en, ru) { return I18n.lang === "ru" ? ru : en; }
    // A key chip shows the live binding, so a rebind cannot leave the guide
    // naming the wrong keys.
    function kbd(id) { return AppController.shortcutText(id); }

    // ── The parts ────────────────────────────────────────────────────
    // keys: {id} — a catalogue shortcut as bound now; {k} — a literal key.
    // Each with a word on what it does. show: where "Show" goes ("" = nowhere).
    readonly property var _raw: [
        { anchor: "help-today", show: "today",
          title: ["Today", "Сегодня"],
          line: ["Your day on one screen: meetings, plan, due dates, who to message.",
                 "День на одном экране: встречи, план, сроки и кому написать."],
          keys: [ { id: "section.today", w: ["open", "открыть"] } ],
          bullets: [ ["Meetings and planned tasks in time order; the line across is now.",
                      "Встречи и задачи идут по времени, линия поперёк — сейчас."],
                     ["The arrows step through days.", "Стрелки листают дни."],
                     ["Workday bounds: Settings → Calendar.", "Границы рабочего дня — в Настройки → Календарь."] ] },
        { anchor: "help-tasks", show: "board",
          title: ["Tasks", "Задачи"],
          line: ["The same tasks as a board or a list.",
                 "Одни и те же задачи — доской или списком."],
          keys: [ { id: "section.tasks", w: ["tasks", "задачи"] },
                  { id: "view.board", w: ["board", "доска"] },
                  { id: "view.timeline", w: ["list", "список"] } ],
          bullets: [ ["Board columns are statuses; drag a card between them.",
                      "Колонки доски — статусы, карточку можно перетащить."],
                     ["J K H L or the arrows move the cursor; with Shift they move the card.",
                      "J K H L или стрелки ведут курсор, с Shift — двигают карточку."],
                     ["Enter opens a card, Space selects it. P0–P3 is the priority.",
                      "Enter открывает карточку, Space выделяет. P0–P3 — приоритет."],
                     ["The archive is the list with is:archived.", "Архив — это список с запросом is:archived."] ] },
        { anchor: "help-capture", show: "",
          title: ["Quick capture", "Быстрый ввод"],
          line: ["Write a thought down without leaving what you are doing.",
                 "Записать мысль, не отрываясь от дела."],
          keys: [ { id: "task.new", w: ["new task", "новая задача"] },
                  { id: "quick-capture", w: ["from anywhere", "из любого окна"] } ],
          bullets: [ ["The first line is the title, the rest the description.",
                      "Первая строка — название, остальное — описание."],
                     ["“tomorrow 17:00”, “fri” set the due date, the time too.",
                      "«завтра 17:00», «пт» ставят срок, время тоже."],
                     ["@name links a person, #tag tags it, APP-12 becomes its ID.",
                      "@имя — человек, #тег — тег, APP-12 станет ключом задачи."] ] },
        { anchor: "help-calendar", show: "week",
          title: ["Calendar", "Календарь"],
          line: ["Day, week and month: meetings, timed tasks, reminders and due dates.",
                 "День, неделя и месяц: встречи, задачи со временем, напоминания и сроки."],
          keys: [ { id: "section.calendar", w: ["open", "открыть"] },
                  { id: "view.month", w: ["month", "месяц"] },
                  { id: "cal.today.alt", w: ["today", "сегодня"] } ],
          bullets: [ ["Click an empty slot for an hour-long event, drag for any length.",
                      "Клик по пустому месту — событие на час, протяжка — любой длины."],
                     ["Drop a task from “Without a date” on an hour to plan it.",
                      "Задача из «Без даты», брошенная на час, запланирована на него."],
                     ["Drag an edge to resize an event, the middle to move it.",
                      "Край события меняет длительность, середина — переносит."],
                     ["P and N step a period back and on; the Layers in the sidebar choose what shows.",
                      "P и N — период назад и вперёд; «Слои» в боковой панели — что показывать."] ] },
        { anchor: "help-notes", show: "notes",
          title: ["Notes", "Заметки"],
          line: ["Markdown notes with folders, links and a note for each day.",
                 "Markdown-заметки с папками, ссылками и заметкой дня."],
          keys: [ { id: "section.knowledge", w: ["notes", "заметки"] },
                  { id: "notes.new", w: ["new note", "новая заметка"] },
                  { k: "Ctrl Shift M", w: ["mode", "режим"] } ],
          bullets: [ ["[[Title]] links a note; @ and # link people and tasks.",
                      "[[Название]] — ссылка на заметку, @ и # — на людей и задачи."],
                     ["Ctrl+B, Ctrl+I, Ctrl+E, Ctrl+K: bold, italic, code, link.",
                      "Ctrl+B, Ctrl+I, Ctrl+E, Ctrl+K — жирный, курсив, код, ссылка."],
                     ["Import and export as a folder of .md files, as Obsidian reads them.",
                      "Импорт и экспорт — папкой .md-файлов, как их читает Obsidian."] ] },
        { anchor: "help-docs", show: "docs",
          title: ["Docs", "Доки"],
          line: ["Long documents and a reference of snippets and contacts.",
                 "Длинные документы и справочник: сниппеты и контакты."],
          keys: [ { id: "view.docs", w: ["docs", "доки"] } ],
          bullets: [ ["Pages are a tree of markdown documents.", "Страницы — дерево markdown-документов."],
                     ["The reference holds sections, code snippets and contact cards.",
                      "В справочнике — разделы, сниппеты кода и карточки контактов."],
                     ["The command line searches all of it.", "Командная строка ищет по всему этому."] ] },
        { anchor: "help-people", show: "today",
          title: ["People", "Люди"],
          line: ["Who you need to reply to or write to.", "Кому нужно ответить или написать."],
          keys: [ { id: "people.open", w: ["everyone", "все"] } ],
          bullets: [ ["“Who to message” is on Today.", "«Кому написать» — на экране «Сегодня»."],
                     ["Click the state: to write → pinged → replied.", "Клик по состоянию: написать → написал → ответили."],
                     ["@ in any field suggests the profile's people.", "@ в любом поле подсказывает людей профиля."] ] },
        { anchor: "help-search", show: "",
          title: ["Search and the command line", "Поиск и командная строка"],
          line: ["One box for commands, tasks, notes and people.",
                 "Одно поле для команд, задач, заметок и людей."],
          keys: [ { id: "palette.open", w: ["command line", "командная строка"] },
                  { id: "search.focus", w: ["filter the view", "фильтр вида"] } ],
          bullets: [ ["Words in any order; a typo or two is forgiven.", "Слова в любом порядке, пара опечаток прощается."],
                     ["Filters: status:, priority:, #tag, due:, is:archived.", "Фильтры: status:, priority:, #тег, due:, is:archived."],
                     ["Esc clears the filter; a set of filters can be saved as a view.",
                      "Esc сбрасывает фильтр; набор фильтров можно сохранить как вид."] ] },
        { anchor: "help-profiles", show: "",
          title: ["Profiles", "Профили"],
          line: ["Separate sets of tasks, people and notes: work, a side project.",
                 "Отдельные наборы задач, людей и заметок: работа, пет-проект."],
          keys: [ { id: "profile.next", w: ["next", "следующий"] },
                  { id: "profile.prev", w: ["previous", "предыдущий"] } ],
          bullets: [ ["Nothing mixes between profiles.", "Между профилями ничего не смешивается."],
                     ["A profile exports to JSON and imports back.", "Профиль выгружается в JSON и загружается обратно."],
                     ["Deleting one can be undone like anything else.", "Удаление профиля отменяется, как всё остальное."] ] },
        { anchor: "help-integrations", show: "settings:integrations",
          title: ["Trackers", "Трекеры"],
          line: ["Issues from GitHub, GitLab, Jira and others, as cards in your profile.",
                 "Задачи из GitHub, GitLab, Jira и других — карточками в профиле."],
          keys: [ { id: "sync.all", w: ["sync", "синхронизировать"] } ],
          bullets: [ ["Sign in with the browser or a token; tokens stay in the OS keychain.",
                      "Вход через браузер или токен; токены хранятся в связке ключей ОС."],
                     ["Leave Repo / Project / JQL empty to get what is assigned to you.",
                      "Пустые Repo / Project / JQL — придут задачи, назначенные вам."],
                     ["Nothing is written to a tracker until you turn it on; a sync deletes nothing.",
                      "В трекер ничего не пишется, пока не включите сами; синхронизация ничего не удаляет."] ] },
        { anchor: "help-git", show: "settings:git",
          title: ["Git", "Git"],
          line: ["Switch a branch and its task comes up.", "Переключили ветку — её задача уже под рукой."],
          keys: [],
          bullets: [ ["Pick the repositories in Settings → Git.", "Репозитории выбираются в Настройки → Git."],
                     ["A PR chip on the card: pending, approved, changes requested.",
                      "Чип PR на карточке: ждёт, одобрен, просят правки."],
                     ["Optionally the task moves to In progress by itself.", "По желанию задача сама переходит «в работу»."] ] },
        { anchor: "help-notifications", show: "settings:notifications",
          title: ["Notifications", "Уведомления"],
          line: ["Reminders for due dates and standup, and quiet hours.",
                 "Напоминания о сроках и стендапе, тихие часы."],
          keys: [],
          bullets: [ ["N hours before a due date, and once more when it passes.",
                      "За N часов до срока и ещё раз, когда он прошёл."],
                     ["Quiet hours hold notifications back, they never drop them.",
                      "Тихие часы откладывают уведомления, а не теряют их."],
                     ["Sound is a switch of its own.", "Звук включается отдельно."] ] },
        { anchor: "help-hotkeys", show: "settings:shortcuts",
          title: ["Keys", "Клавиши"],
          line: ["Everything works from the keyboard, and any key can be changed.",
                 "Всё делается с клавиатуры, и любую клавишу можно сменить."],
          keys: [ { id: "hotkeys.open", w: ["cheat sheet", "шпаргалка"] },
                  { id: "hotkeys.edit", w: ["change", "сменить"] } ],
          bullets: [ ["A taken combination shows who holds it.", "Занятое сочетание покажет, кому оно отдано."],
                     ["Esc always takes one step back.", "Esc всегда возвращает на шаг назад."] ] },
        { anchor: "help-tweaks", show: "settings:appearance",
          title: ["Appearance", "Внешний вид"],
          line: ["Theme, accent, density, contrast and motion.", "Тема, акцент, плотность, контраст и анимации."],
          keys: [ { id: "tweaks.open", w: ["settings", "настройки"] },
                  { id: "theme.toggle", w: ["light / dark", "светлая / тёмная"] } ],
          bullets: [ ["Changes apply at once, no restart.", "Меняется сразу, без перезапуска."],
                     ["Minimum turns animations off.", "«Минимум» выключает анимации."] ] },
        { anchor: "help-undo", show: "settings:data",
          title: ["Undo and your data", "Отмена и данные"],
          line: ["Nothing gets lost: everything can be undone, backups make themselves.",
                 "Ничего не теряется: всё отменяется, резервные копии делаются сами."],
          keys: [ { id: "undo", w: ["undo", "отменить"] },
                  { id: "redo", w: ["redo", "вернуть"] } ],
          bullets: [ ["Your data is one file on this computer.", "Данные — один файл на этом компьютере."],
                     ["A backup a day; the latest 20 are kept.", "Резервная копия — раз в день, хранятся последние 20."],
                     ["Restoring one saves the current state first.", "Восстановление сначала сохраняет текущее состояние."] ] }
    ]

    // The parts in the current language, each with what the search reads:
    // both languages and the keys, so "notes" finds Заметки.
    readonly property var sections: {
        const ru = I18n.lang === "ru";
        const out = [];
        for (const p of root._raw) {
            const keys = p.keys.map((k) => ({ keys: k.id ? root.kbd(k.id) : k.k, word: ru ? k.w[1] : k.w[0] }));
            const hay = [p.title[0], p.title[1], p.line[0], p.line[1]]
                .concat(p.bullets.map((b) => b[0] + " " + b[1]))
                .concat(p.keys.map((k) => k.w[0] + " " + k.w[1]))
                .concat(keys.map((k) => k.keys))
                .join(" ").toLowerCase();
            out.push({ anchor: p.anchor, show: p.show,
                       title: ru ? p.title[1] : p.title[0],
                       line: ru ? p.line[1] : p.line[0],
                       keys: keys,
                       bullets: p.bullets.map((b) => ru ? b[1] : b[0]),
                       hay: hay });
        }
        return out;
    }
    // The contents: {anchor, label}.
    readonly property var tocModel: sections.map((s) => ({ anchor: s.anchor, label: s.title }))

    property string query: ""
    // Every word of the query, in any order (as Settings search reads it).
    function matches(s) {
        const words = root.query.toLowerCase().split(/\s+/).filter((w) => w.length > 0);
        for (const w of words) if (s.hay.indexOf(w) < 0) return false;
        return true;
    }
    readonly property var visibleSections: sections.filter((s) => root.matches(s))
    readonly property bool nothingFound: visibleSections.length === 0

    // The part the contents' cursor is on, by anchor (it survives a filter).
    property string currentAnchor: "help-today"
    readonly property int currentIndex: {
        for (let i = 0; i < visibleSections.length; i++)
            if (visibleSections[i].anchor === root.currentAnchor) return i;
        return -1;
    }
    onVisibleSectionsChanged: {
        if (root.currentIndex < 0 && visibleSections.length > 0) root.currentAnchor = visibleSections[0].anchor;
        Qt.callLater(root._scrollToCurrent);
    }

    // ── What the reader calls ────────────────────────────────────────
    // Open on a part (or the top), search cleared, the contents focused.
    function reset(anchor) {
        root.query = "";
        root.currentAnchor = anchor && anchor.length > 0 ? anchor : (sections.length > 0 ? sections[0].anchor : "");
        toc.forceActiveFocus();
        Qt.callLater(() => root.scrollToAnchor(root.currentAnchor));
    }
    function focusSearch() {
        searchField.forceActiveFocus();
        searchField.selectAll();
    }
    function scrollToAnchor(name) {
        if (!name || name.length === 0) { content.contentY = 0; return; }
        const target = root._sectionItem(name);
        if (!target || !target.visible) return;
        root._driving = true;
        const p = target.mapToItem(contentCol, 0, 0);
        content.contentY = Math.max(0, Math.min(p.y, content.contentHeight - content.height));
        root._driving = false;
    }
    // Step the contents' cursor and bring the part into view.
    function move(step) {
        const n = visibleSections.length;
        if (n === 0) return;
        const i = Math.max(0, Math.min(n - 1, (root.currentIndex < 0 ? 0 : root.currentIndex + step)));
        root.currentAnchor = visibleSections[i].anchor;
        root.anchorRequested(root.currentAnchor);
        root.scrollToAnchor(root.currentAnchor);
    }
    // PgUp / PgDn: the text by most of a screen.
    function page(dir) {
        const maxY = Math.max(0, content.contentHeight - content.height);
        content.contentY = Math.max(0, Math.min(maxY, content.contentY + dir * content.height * 0.9));
    }
    // Enter: the part's screen, if it has one.
    function openCurrent() {
        const i = root.currentIndex;
        if (i < 0) return;
        const s = visibleSections[i];
        if (s.show.length > 0) root.showRequested(s.show);
    }

    property bool _driving: false
    function _sectionItem(name) {
        for (let i = 0; i < partsRep.count; i++) {
            const it = partsRep.itemAt(i);
            if (it && it.objectName === name) return it;
        }
        return null;
    }
    function _scrollToCurrent() { root.scrollToAnchor(root.currentAnchor); }
    // Scrolling the text moves the contents' cursor to the part at the top.
    function _followScroll() {
        if (root._driving) return;
        let best = "";
        for (let i = 0; i < partsRep.count; i++) {
            const it = partsRep.itemAt(i);
            if (!it || !it.visible) continue;
            if (it.y <= content.contentY + Theme.spXl) best = it.objectName;
        }
        if (best.length > 0) root.currentAnchor = best;
    }

    // "/" and Ctrl+F search from anywhere in the guide; Esc clears, then closes.
    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier))
                || (event.text === "/" && !searchField.activeFocus)) {
            root.focusSearch();
            event.accepted = true;
        } else if (event.key === Qt.Key_Escape) {
            if (root.query.length > 0) {
                root.query = "";
                toc.forceActiveFocus();
            } else {
                root.closeRequested();
            }
            event.accepted = true;
        }
    }

    // ── Words in chips ───────────────────────────────────────────────
    component Bullet: RowLayout {
        id: bullet
        property string text: ""
        Layout.fillWidth: true
        spacing: Theme.spSm
        Text {
            Layout.alignment: Qt.AlignTop
            text: "–"
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
        }
        Text {
            Layout.fillWidth: true
            text: bullet.text
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            wrapMode: Text.WordWrap
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Title and the search field, as Settings has them.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spLg
            Text {
                objectName: "help-title"
                text: root.tr2("Getting started", "С чего начать")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fsXl
                font.weight: Theme.fwHeading
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: Theme.px(260)
                Layout.preferredHeight: Theme.px(30)
                radius: Theme.radiusMd
                color: Theme.panel
                border.color: searchField.activeFocus ? Theme.focusRing : Theme.border
                border.width: searchField.activeFocus ? 2 : 1
                TextField {
                    id: searchField
                    objectName: "help-search"
                    ContextMenu.menu: TextEditMenu { editor: searchField }
                    anchors.fill: parent
                    leftPadding: Theme.spLg
                    rightPadding: slashHint.visible ? slashHint.width + Theme.spMd : Theme.spMd
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: root.tr2("Search the guide", "Поиск по справке")
                    color: Theme.text
                    placeholderTextColor: Theme.textDim
                    background: Item {}
                    font.pixelSize: Theme.fsSm
                    text: root.query
                    onTextChanged: root.query = text
                    Accessible.name: placeholderText
                    // Enter or ↓: into the contents, on the first part found.
                    onAccepted: {
                        if (root.visibleSections.length > 0) root.currentAnchor = root.visibleSections[0].anchor;
                        toc.forceActiveFocus();
                        root.scrollToAnchor(root.currentAnchor);
                    }
                    Keys.onDownPressed: {
                        if (root.visibleSections.length > 0) root.currentAnchor = root.visibleSections[0].anchor;
                        toc.forceActiveFocus();
                    }
                    Keys.onEscapePressed: (event) => {
                        if (text.length > 0) root.query = "";
                        else root.closeRequested();
                        event.accepted = true;
                    }
                }
                KeyHint {
                    id: slashHint
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spSm
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !searchField.activeFocus && root.query.length === 0
                    keys: "/"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spXl
            spacing: Theme.sp2xl

            // ── Contents ──
            FocusScope {
                id: toc
                objectName: "help-toc"
                Layout.preferredWidth: Theme.px(190)
                Layout.fillHeight: true
                activeFocusOnTab: true
                Accessible.role: Accessible.List
                Accessible.name: root.tr2("Contents", "Оглавление")
                Keys.onUpPressed: root.move(-1)
                Keys.onDownPressed: root.move(1)
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Home) { root.move(-1000); event.accepted = true; }
                    else if (event.key === Qt.Key_End) { root.move(1000); event.accepted = true; }
                    else if (event.key === Qt.Key_PageDown) { root.page(1); event.accepted = true; }
                    else if (event.key === Qt.Key_PageUp) { root.page(-1); event.accepted = true; }
                }
                Keys.onReturnPressed: root.openCurrent()
                Keys.onEnterPressed: root.openCurrent()

                Flickable {
                    id: tocFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: tocCol.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ThinScrollBar {}
                    Column {
                        id: tocCol
                        width: tocFlick.width
                        Repeater {
                            model: root.visibleSections
                            delegate: Rectangle {
                                id: tocRow
                                required property var modelData
                                readonly property bool current: modelData.anchor === root.currentAnchor
                                objectName: "help-toc-" + modelData.anchor
                                width: tocCol.width
                                height: Theme.px(28)
                                radius: Theme.radiusMd
                                color: tocCA.hovered ? Theme.panel : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                Text {
                                    id: tocLabel
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: Theme.spMd
                                    anchors.rightMargin: Theme.spMd
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: tocRow.modelData.title
                                    color: tocRow.current ? Theme.text : Theme.textMuted
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsMd
                                    font.weight: tocRow.current ? Theme.fwHeading : Theme.fwBody
                                    elide: Text.ElideRight
                                    CursorBar {
                                        shown: tocRow.current
                                        anchors.left: parent.left
                                        anchors.top: parent.bottom
                                        anchors.topMargin: Theme.sp2xs
                                    }
                                }
                                // The contents' keyboard cursor, while it has the keys.
                                FocusRing { target: toc; visible: toc.activeFocus && tocRow.current }
                                ClickArea {
                                    id: tocCA
                                    activeFocusOnTab: false
                                    role: Accessible.Link
                                    label: tocRow.modelData.title
                                    showTip: false
                                    onActivated: {
                                        root.currentAnchor = tocRow.modelData.anchor;
                                        root.anchorRequested(tocRow.modelData.anchor);
                                        root.scrollToAnchor(tocRow.modelData.anchor);
                                        toc.forceActiveFocus();
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── The parts ──
            Flickable {
                id: content
                objectName: "help-content"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: contentCol.implicitHeight + Theme.sp3xl
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThinScrollBar {}
                onContentYChanged: root._followScroll()

                ColumnLayout {
                    id: contentCol
                    width: content.width - Theme.spLg
                    spacing: Theme.sp2xl

                    Repeater {
                        id: partsRep
                        model: root.sections
                        delegate: ColumnLayout {
                            id: part
                            required property var modelData
                            objectName: modelData.anchor
                            visible: root.matches(modelData)
                            Layout.fillWidth: true
                            spacing: Theme.spSm

                            SectionHeader {
                                Layout.fillWidth: true
                                title: part.modelData.title
                            }
                            Text {
                                Layout.fillWidth: true
                                text: part.modelData.line
                                color: Theme.text
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsMd
                                wrapMode: Text.WordWrap
                            }
                            // The keys, each with a word on what it does.
                            Flow {
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.sp2xs
                                visible: part.modelData.keys.length > 0
                                spacing: Theme.spLg
                                Repeater {
                                    model: part.modelData.keys
                                    delegate: Row {
                                        id: keyPair
                                        required property var modelData
                                        visible: modelData.keys.length > 0
                                        spacing: Theme.spXs
                                        KeyHint {
                                            keys: keyPair.modelData.keys
                                            always: true
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: keyPair.modelData.word
                                            color: Theme.textDim
                                            font.family: Theme.fontUi
                                            font.pixelSize: Theme.fsSm
                                        }
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.sp2xs
                                spacing: Theme.spXs
                                Repeater {
                                    model: part.modelData.bullets
                                    delegate: Bullet {
                                        required property string modelData
                                        text: modelData
                                    }
                                }
                            }
                            // Go there.
                            Text {
                                id: showLink
                                objectName: "help-show-" + part.modelData.anchor
                                visible: part.modelData.show.length > 0
                                Layout.topMargin: Theme.spXs
                                text: root.tr2("Show", "Показать") + " →"
                                color: showCA.hovered || showCA.keyboardFocused ? Theme.text : Theme.textMuted
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                                ClickArea {
                                    id: showCA
                                    label: root.tr2("Show: ", "Показать: ") + part.modelData.title
                                    showTip: false
                                    onActivated: root.showRequested(part.modelData.show)
                                }
                            }
                        }
                    }

                    EmptyState {
                        objectName: "help-search-empty"
                        visible: root.nothingFound
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.sp2xl
                        compact: true
                        title: root.tr2("Nothing found", "Ничего не нашлось")
                        line: root.tr2("Try another word, or Esc to clear the search.",
                                       "Попробуйте другое слово или Esc, чтобы сбросить поиск.")
                    }
                }
            }
        }
    }
}
