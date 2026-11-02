.pragma library

// "What's new" per release line (R2-054): what the line changed for someone
// coming from the one before, in the UI language. Facts of the release only
// — every entry here is something the build does. A line without an entry
// shows nothing after an update.
//
// line: "0.8" for 0.8.0, 0.8.1, …
var notes = {
    "0.11": {
        sections: [
            { title: { en: "The task, redone", ru: "Задача заново" },
              body: { en: "a task opens as a wide sheet over the board, f for the full window; description, checklist, your notes and comments in one scroll; the sections of the right column fold and stay folded",
                      ru: "задача открывается широким листом поверх доски, f — на всё окно; описание, чеклист, свои заметки и комментарии — одной прокруткой; разделы правой колонки сворачиваются и такими остаются" } },
            { title: { en: "Editing Jira", ru: "Правка Jira" },
              body: { en: "switch it on per tracker: description, checklist, title, due date and assignee go back to Jira, with a conflict shown when Jira changed too; off by default, comments are never sent",
                      ru: "включается для каждого трекера: описание, чеклист, заголовок, срок и исполнитель уходят в Jira, а если там тоже поменяли — покажет конфликт; по умолчанию выключено, комментарии не отправляются никогда" } },
            { title: { en: "Jira, more of it", ru: "Больше из Jira" },
              body: { en: "the issue type with its icon; parent, sub-tasks and links in the task, “↳ PARENT” or “sub-tasks N of M” on the card; checklist statuses as Jira names them",
                      ru: "тип задачи с иконкой; родитель, подзадачи и связи — в задаче, «↳ РОДИТЕЛЬ» или «подзадачи N из M» — на карточке; статусы чеклиста — как они названы в Jira" } },
            { title: { en: "Undo any time", ru: "Отмена в любой момент" },
              body: { en: "Ctrl Z reaches any step of the session, in Notes too",
                      ru: "Ctrl Z дотягивается до любого шага за сессию, и в заметках тоже" } },
            { title: { en: "Weekly recap", ru: "Итоги недели" },
              body: { en: "how each ticket moved, what is in progress now, and the week's meetings",
                      ru: "как двигался каждый тикет, что сейчас в работе и встречи недели" } },
            { title: { en: "The meeting now", ru: "Встреча сейчас" },
              body: { en: "in Today the meeting that is on says “N min left” and fills a thin bar",
                      ru: "в «Сегодня» идущая встреча пишет «ещё N мин» и заполняет тонкую полоску" } },
            { title: { en: "Notes", ru: "Заметки" },
              body: { en: "Import ▾ brings a table from xlsx, csv or the clipboard; tables are edited in their grid, Tab between cells; Ctrl F finds in the text; Alt ↑ / ↓ open the previous / next note; a copied picture pastes as a picture",
                      ru: "«Импорт ▾» — таблица из xlsx, csv или буфера; таблицы правятся в сетке, Tab — между ячейками; Ctrl F — поиск по тексту; Alt ↑ / ↓ — предыдущая / следующая заметка; скопированная картинка вставляется картинкой" } },
            { title: { en: "Also", ru: "Ещё" },
              body: { en: "a tracker switches off and on without signing out · comment authors with their pictures, Ctrl click opens the contact · code highlighted in descriptions · the release date in What's new and About",
                      ru: "трекер выключается и включается без выхода из аккаунта · авторы комментариев с аватарами, Ctrl+клик открывает контакт · подсветка кода в описаниях · дата выхода версии в «Что нового» и «О программе»" } }
        ]
    },
    "0.10": {
        sections: [
            { title: { en: "Waiting for me", ru: "Ждут меня" },
              body: { en: "the sidebar lists the merge requests where someone waits for you — a review asked, a thread without your answer, a mention; i sets a thread aside, s puts a row off until tomorrow",
                      ru: "в сайдбаре — MR, где ждут тебя: просят ревью, тред без твоего ответа, упоминание; i — тред в игнор, s — отложить строку на завтра" } },
            { title: { en: "CI, the same everywhere", ru: "CI везде одинаково" },
              body: { en: "the card, the task and the sidebar read one verdict; c i opens the failed jobs and the end of the log; you hear when a pipeline fails and when it passes",
                      ru: "карточка, задача и сайдбар показывают одно и то же; c i — упавшие джобы и хвост лога; уведомление и когда упало, и когда прошло" } },
            { title: { en: "Calendar of its own", ru: "Календарь отдельным разделом" },
              body: { en: "Ctrl 4 opens the calendar, Ctrl 5 the docs",
                      ru: "Ctrl 4 — календарь, Ctrl 5 — доки" } },
            { title: { en: "Small screens", ru: "Маленькие экраны" },
              body: { en: "drag the sidebar between 160 and 320, narrower it becomes a rail; board columns fold by themselves when the window is tight",
                      ru: "сайдбар тянется от 160 до 320, уже — превращается в рейл; колонки доски сворачиваются сами, когда окну тесно" } },
            { title: { en: "Due dates ahead", ru: "Ближайшие сроки" },
              body: { en: "Today → Deadlines: “Due today” and “Upcoming” for 1 to 30 days",
                      ru: "Сегодня → Сроки: «Срок сегодня» и «Ближайшие» на 1–30 дней" } },
            { title: { en: "Pictures at full size", ru: "Картинки в полный размер" },
              body: { en: "screenshots in notes, tasks and Jira comments open full size; ← / → walk through the ticket's pictures",
                      ru: "скриншоты в заметках, задачах и комментариях Jira открываются в полный размер; ← / → листают картинки тикета" } },
            { title: { en: "Settings", ru: "Настройки" },
              body: { en: "one section per screen, with search; they open about thirteen times faster",
                      ru: "один раздел на экран и поиск; открываются примерно в 13 раз быстрее" } },
            { title: { en: "A calmer network", ru: "Спокойнее с сетью" },
              body: { en: "one retry rule up to 24 hours for every tracker and forge; a service that keeps failing pauses itself and says so; GitLab is no longer hammered",
                      ru: "одно правило повторов до 24 часов для всех трекеров и форджей; сервис, который раз за разом падает, сам встаёт на паузу и говорит об этом; GitLab больше не засыпается запросами" } },
            { title: { en: "Also", ru: "Ещё" },
              body: { en: "CODEOWNERS without gh / glab · repositories over SSH · tables pasted into notes · mouse back / forward",
                      ru: "CODEOWNERS без gh / glab · репозитории по SSH · вставка таблиц в заметки · кнопки мыши «назад / вперёд»" } }
        ],
        moved: { en: "Calendar is Ctrl 4 now, Docs Ctrl 5",
                 ru: "Календарь теперь Ctrl 4, Доки — Ctrl 5" }
    },
    "0.9": {
        sections: [
            { title: { en: "Move to profile", ru: "Перенос в профиль" },
              body: { en: "M → “Move to profile…”, or the selection bar; Ctrl N and quick capture take “+Work” for the profile",
                      ru: "M → «Перенести в профиль…» или панель выделения; Ctrl N и быстрый ввод понимают «+Работа»" } },
            { title: { en: "Repeats", ru: "Повторы" },
              body: { en: "“every 2 weeks”, “every mon, wed”, “every other day”; the next ones show faintly in Week and Month",
                      ru: "«каждые 2 недели», «каждый пн и ср», «через день»; следующие видны бледно в Неделе и Месяце" } },
            { title: { en: "Time zones", ru: "Часовые пояса" },
              body: { en: "a meeting keeps its own zone: pick it in the meeting, or type “15:00 EST”; your local time stays next to it",
                      ru: "у встречи свой пояс: выбрать во встрече или написать «15:00 по Москве»; рядом — ваше время" } },
            { title: { en: "Today for every profile", ru: "Сегодня по всем профилям" },
              body: { en: "Settings → Calendar → “Today shows”: this profile or all of them",
                      ru: "Настройки → Календарь → «Сегодня» показывает: этот профиль или все" } },
            { title: { en: "Hints that show once", ru: "Подсказки один раз" },
              body: { en: "when Tabless does something on its own — folds a merge request into its Jira ticket, keeps a move local, reads dates from text — it says so once, beside the thing, with where to change it",
                      ru: "когда Tabless делает что-то сам — склеивает MR с тикетом Jira, сдвигает карточку только у себя, понимает даты из текста, — он один раз говорит об этом рядом и подсказывает, где это изменить" } },
            { title: { en: "Notes as on the page", ru: "Заметки как на листе" },
              body: { en: "rich mode by default: airier lines, “/” on an empty line for blocks, task links as pills, inline code and pictures drawn in place",
                      ru: "rich-режим по умолчанию: просторные строки, «/» на пустой строке — блоки, ссылки на задачи плашками, код и картинки рисуются на месте" } },
            { title: { en: "Tabless Blue", ru: "Tabless Blue" },
              body: { en: "Settings → Appearance → Themes: “Tabless Blue”, the blue-black palette of the first designs; grey stays the default",
                      ru: "Настройки → Внешний вид → Темы: «Tabless Blue», сине-чёрная палитра первых макетов; серая остаётся по умолчанию" } },
            { title: { en: "Fewer stumbles", ru: "Меньше заминок" },
              body: { en: "about two hundred fixes from a day-long test by twenty kinds of engineers: keys, dates in Russian, the week grid, end of day, undo",
                      ru: "около двухсот исправлений после теста «рабочий день» двадцати разных инженеров: клавиши, даты по-русски, сетка недели, конец дня, отмена" } }
        ],
        moved: { en: "“When” and “Due” are now “Target start” and “Due date” · Scale and Contrast → Settings → Appearance, under More · Docs got a key: g d · n / p step the week",
                 ru: "«Когда» и «Срок» теперь «Целевое начало» и «Срок исполнения» · Масштаб и Контраст → Настройки → Внешний вид, под «ещё» · у Доков своя клавиша: g d · n / p листают неделю" }
    },
    "0.8": {
        sections: [
            { title: { en: "New name", ru: "Новое имя" },
              body: { en: "lowkey is now called Tabless; your data and settings are where you left them",
                      ru: "lowkey теперь называется Tabless; данные и настройки на месте" } },
            { title: { en: "Today", ru: "Сегодня" },
              body: { en: "the home screen: the day by the hour, in progress, deadlines",
                      ru: "главный экран: день по часам, в работе, сроки" } },
            { title: { en: "Tasks", ru: "Задачи" },
              body: { en: "board, list and calendar — one section with one filter",
                      ru: "доска, список и календарь — один раздел с общим фильтром" } },
            { title: { en: "Task as a document", ru: "Задача — документ" },
              body: { en: "a panel on the right, saves by itself, “Done” — d",
                      ru: "панель справа, сохраняется сама, «Готово» — d" } },
            { title: { en: "One input", ru: "Один ввод" },
              body: { en: "“tomorrow 15:00 p1” is understood wherever a task is created",
                      ru: "«завтра 15:00 p1» понимается везде, где создаётся задача" } },
            { title: { en: "Two styles", ru: "Два стиля" },
              body: { en: "“Bold” by default, “Quiet” in Settings → Appearance",
                      ru: "«Насыщенный» по умолчанию, «Тихий» — в Настройки → Внешний вид" } }
        ],
        moved: { en: "“Focus” and “Saved views” → “My views” · Timeline, Week, Month → Tasks · Tweaks → Settings",
                 ru: "«Фокус» и «Сохранённые виды» → «Мои виды» · Лента, Неделя, Месяц → Задачи · Твики → Настройки" }
    }
};

function lineOf(version) {
    var m = /^(\d+)\.(\d+)/.exec(String(version || ""));
    return m ? m[1] + "." + m[2] : "";
}

function forVersion(version) {
    return notes[lineOf(version)] || null;
}
