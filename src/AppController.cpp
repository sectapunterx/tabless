#include "AppController.h"
#include "Logger.h"
#include "RecoveryLog.h"
#include "SampleData.h"
#include "StateSerializer.h"
#include "TaskDefer.h"
#include "ViewNames.h"

#include "board/ColumnCategory.h"
#include "board/Rank.h"
#include "cal/EventClamp.h"
#include "cal/EventSpan.h"
#include "cal/IcsCodec.h"
#include "cal/IcsSubscription.h"
#include "cal/MeetingChimes.h"
#include "cal/Occurrences.h"
#include "cal/OutlookDesktop.h"
#include "cal/OutlookWatch.h"
#include "cal/Reminders.h"
#include "cal/ZoneNames.h"
#include "capture/CaptureParse.h"
#include "chrono/ChronoParser.h"
#include "diag/FrameLog.h"
#include "diag/IssueReport.h"
#include "diag/PerfLog.h"
#include "git/BranchTaskMatcher.h"
#include "git/BranchTaskResolve.h"
#include "git/GitWatcher.h"
#include "hints/CoachMarks.h"
#include "hints/ShortcutHints.h"
#include "history/TaskHistory.h"
#include "integrations/AutoSync.h"
#include "integrations/CiStatus.h"
#include "integrations/CommentCache.h"
#include "integrations/IntegrationI18n.h"
#include "integrations/JiraProvider.h"
#include "integrations/MattermostClient.h"
#include "integrations/OAuthManager.h"
#include "integrations/OAuthRefresh.h"
#include "integrations/ProviderDescriptor.h"
#include "integrations/ProviderRegistry.h"
#include "integrations/RestIssueProvider.h"
#include "integrations/SecretStore.h"
#include "integrations/Sprints.h"
#include "integrations/StatusMap.h"
#include "integrations/StatusTime.h"
#include "integrations/SyncState.h"
#include "integrations/TrackerEstimate.h"
#include "integrations/TrackerFields.h"
#include "integrations/TrackerImages.h"
#include "integrations/TrackerMerge.h"
#include "keys/KeyNames.h"
#include "local/Effective.h"
#include "local/Sessions.h"
#include "markdown/JiraWiki.h"
#include "markdown/MdHtml.h"
#include "markdown/MdOutline.h"
#include "notes/ChecklistItems.h"
#include "notes/MdVault.h"
#include "notes/NoteChain.h"
#include "notes/NoteGraph.h"
#include "notes/NoteLinks.h"
#include "notify/NotificationCenter.h"
#include "people/AttendeeMatch.h"
#include "plan/Carry.h"
#include "plan/DayPlan.h"
#include "plan/DueHorizon.h"
#include "plan/FreeWindow.h"
#include "platform/Accessibility.h"
#include "platform/Brand.h"
#include "platform/GlobalHotkey.h"
#include "platform/Paths.h"
#include "platform/Sound.h"
#include "platform/WindowFrame.h"
#include "query/CommandQuery.h"
#include "query/TaskQuery.h"
#include "recap/WeeklyRecap.h"
#include "recur/RecurrenceEngine.h"
#include "safety/Immersion.h"
#include "safety/SafetyText.h"
#include "storage/AsyncSaver.h"
#include "storage/Attachments.h"
#include "storage/Snapshots.h"
#include "storage/StateIO.h"
#include "text/LocaleFormat.h"
#include "text/PersonMatch.h"
#include "text/TaskTextUtils.h"
#include "text/UiLanguage.h"
#include "update/UpdateInstall.h"
#include "update/Updater.h"
#include "views/UiScale.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QInputMethod>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPair>
#include <QPointerEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QScreen>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTime>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace {

// Bumped when the *meaning* of settings.shortcuts changes. 1 stored every
// binding; 2 stores only the ones the user rebound; 3 is the heap 2 shell
// (APP-258), where Ctrl+1..3 open the sidebar's sections and Ctrl+, Settings;
// 4 is the Vim-based keymap of 0.8.0 (keymap.md, APP-272): g/y/z/c prefixes,
// single letters on the task under the cursor, Ctrl+4..9 for My views;
// 5 stores keymapScheme beside the overrides (Classic or Vim, dogfood
// 2026-10-19), the scheme their defaults are read against.
constexpr int kShortcutsSchema = 5;

// Defined with the other load helpers below.
bool slugLegacyPersonIds(Profile& pr);

// The keys whose meaning a returning user's hands still remember (APP-281
// A4, keymap.md "Что меняется для пользователей 0.7.x"): the old default
// sequence, the action it ran, and the schema it changed in. The first press
// of one after the update says once where that action went.
struct LegacyKey {
  const char* sequence;
  const char* oldId;
  int changedIn;
};

constexpr LegacyKey kLegacyKeys[] = {
    {"Ctrl+1", "view.board", 3},
    {"Ctrl+2", "view.timeline", 3},
    {"Ctrl+3", "view.week", 3},
    {"Ctrl+4", "view.month", 3},
    {"Ctrl+5", "view.archive", 3},
    {"Ctrl+6", "view.docs", 3},
    {"Ctrl+7", "view.notes", 3},
    {"Ctrl+8", "view.settings", 3},
    {"O", "task.openExternal", 4},
    {"Z", "board.collapseColumn", 4},
    {"T", "cal.today", 4},
    {"G", "cal.goToDate", 4},
    {"Alt+1", "savedView.1", 4},
    {"Alt+2", "savedView.2", 4},
    {"Alt+3", "savedView.3", 4},
};

// Defined with the shortcut conflict rules below.
bool surfacesApart(const QString& a, const QString& b, bool prefix);

// "section.today.alt" and "savedView.3.alt2" are a second key for the action
// before the suffix: they share its name.
QString baseShortcutId(const QString& id) {
  static const QRegularExpression alt(QStringLiteral("\\.alt\\d*$"));
  QString base = id;
  base.remove(alt);
  return base;
}

// True when `sequence` is exactly what `id` was bound to before the view
// shortcuts were renumbered to follow the side rail — i.e. a stored default
// rather than a deliberate rebind, in a settings file written by an older
// build. Ids outside this table never had their default changed, so nothing
// stored for them can be mistaken for one; in particular a cleared binding
// (an empty sequence) is always a real choice.
bool isLegacyShortcutDefault(const QString& id, const QString& sequence) {
  static const QHash<QString, QString> kDefaults = {
      {QStringLiteral("view.board"), QStringLiteral("Ctrl+1")},
      {QStringLiteral("view.timeline"), QStringLiteral("Ctrl+2")},
      {QStringLiteral("view.week"), QStringLiteral("Ctrl+3")},
      {QStringLiteral("view.docs"), QStringLiteral("Ctrl+4")},
      {QStringLiteral("view.notes"), QStringLiteral("Ctrl+5")},
      {QStringLiteral("view.settings"), QStringLiteral("Ctrl+6")},
      {QStringLiteral("view.archive"), QStringLiteral("Ctrl+7")},
  };
  const auto it = kDefaults.constFind(id);
  return it != kDefaults.constEnd() && *it == sequence;
}

}  // namespace

// Version is injected by CMake (PROJECT_VERSION); this fallback keeps standalone
// test targets that compile AppController.cpp directly building without it.
#ifndef HEAP_VERSION
#define HEAP_VERSION "0.0.0-dev"
#endif

namespace {
constexpr int kBackupIntervalSeconds = 5 * 60;
// How long a pull may stay unanswered before the header stops saying a sync
// is running (APP-186). Longer than any page-by-page pull takes.
constexpr int kSyncWatchdogMs = 120 * 1000;
// How long after a Jira pull the follow-up pull runs (see the tasksFetched
// handler): long enough for Jira Cloud's search index to catch up.
constexpr int kSettlePullDelayMs = 20 * 1000;
constexpr int kBackupRetentionCount = 20;

// EN/RU string table for toast / system messages emitted from C++. QML chrome
// lives in qml/I18n.qml — this map only covers text produced by AppController
// itself. Keep keys stable: they are referenced from rewrite sites below.
struct I18nEntry {
  const char* en;
  const char* ru;
};

const QHash<QString, I18nEntry>& i18nTable() {
  static const QHash<QString, I18nEntry> table = {
      {"task.created", {"Created: %1", "Создано: %1"}},
      {"task.idTaken", {"%1 already exists — pick another id", "%1 уже занят — выберите другой id"}},
      {"task.otherProfile", {"%1 was not saved: it belongs to another profile", "%1 не сохранена: она из другого профиля"}},
      {"task.deleted", {"Deleted: %1", "Удалена: %1"}},
      {"task.restored", {"Restored: %1", "Восстановлена: %1"}},
      {"task.moved", {"%1 → %2", "%1 → %2"}},
      {"sync.summary", {"%1: %2 new · %3 updated", "%1: %2 новых · %3 обновлено"}},
      {"sync.upToDate", {"%1 is up to date", "%1 — без изменений"}},
      // APP-180: the new tickets by name, three at most, then how many more.
      {"sync.summaryNamed", {"%1: %2 new — %3 · %4 updated", "%1: %2 новых — %3 · %4 обновлено"}},
      {"sync.newMore", {"%1 more", "ещё %1"}},
      {"shortcut.log.open.label", {"Event log", "Журнал событий"}},
      {"shortcut.log.open.desc",
       {"What the notices said lately: syncs, new tickets, refusals, errors.",
        "Что говорили уведомления: синхронизации, новые тикеты, отказы, ошибки."}},
      {"ticket.noLink", {"No issue link on this task", "У задачи нет ссылки на тикет"}},
      {"ticket.notConnected", {"Connect this tracker to read its comments", "Подключите трекер, чтобы читать комментарии"}},
      {"notes.untitled", {"Untitled note", "Без названия"}},
      {"notes.inbox", {"Inbox", "Входящие"}},
      {"undo.splitSeries", {"Series change undone", "Изменение серии отменено"}},
      {"undo.deleteSeries", {"Series restored", "Серия восстановлена"}},
      {"undo.deleteFollowing", {"Following events restored", "Последующие события восстановлены"}},
      {"undo.deleteOccurrence", {"Event restored", "Событие восстановлено"}},
      {"update.available", {"Version %1 is ready. Update when it suits you.", "Версия %1 готова. Обновите, когда будет удобно."}},
      {"update.ready", {"%1 downloaded, SHA-256 checksum verified", "%1 скачано, контрольная сумма SHA-256 проверена"}},
      {"update.upToDate", {"You're up to date", "У вас последняя версия"}},
      {"update.failed", {"Update check failed", "Не удалось проверить обновления"}},
      {"task.recurs", {"next one %1", "следующая — %1"}},
      {"timer.switched", {"Timer on %1 stopped · %2", "Таймер на %1 остановлен · %2"}},
      {"dur.hm", {"%1 h %2 min", "%1 ч %2 мин"}},
      {"dur.m", {"%1 min", "%1 мин"}},
      {"task.fromTemplate", {"Created from template: %1", "Создано по шаблону: %1"}},
      {"recovery.saved", {"Recovery log saved", "Журнал восстановления сохранён"}},
      {"recovery.empty", {"No recovery log to export", "Журнал восстановления пуст"}},
      {"update.checking", {"Checking for updates…", "Проверка обновлений…"}},
      {"update.downloading", {"Downloading %1…", "Скачивание %1…"}},
      {"update.verifying", {"Checking the SHA-256 checksum…", "Проверка контрольной суммы SHA-256…"}},
      {"update.verified",
       {"%1 is downloaded. Its SHA-256 checksum matches the one published with the release, so the file is "
        "intact and exactly what was released: %2",
        "%1 скачано. Контрольная сумма SHA-256 совпала с опубликованной в релизе — файл целый и именно тот, "
        "что был выпущен: %2"}},
      {"update.mismatch",
       {"Update not installed: the file's SHA-256 checksum does not match the release's. It was deleted; try "
        "again later or download from the release page.",
        "Обновление не установлено: контрольная сумма SHA-256 файла не совпала с опубликованной в релизе. Файл "
        "удалён; попробуйте позже или скачайте со страницы релиза."}},
      {"update.noChecksum",
       {"Update not installed: the release publishes no SHA-256 checksum for this file, so it cannot be checked.",
        "Обновление не установлено: в релизе нет контрольной суммы SHA-256 для этого файла, проверить его нельзя."}},
      {"update.downloadFailed", {"Download failed: %1", "Не удалось скачать: %1"}},
      {"update.installing", {"Installing — Tabless will restart…", "Установка — Tabless перезапустится…"}},
      {"update.installFailed", {"Could not install the update: %1", "Не удалось установить обновление: %1"}},
      {"update.installed", {"Updated to %1", "Обновлено до %1"}},
      {"sync.noTracker", {"Connect a tracker in Settings → Integrations first", "Сначала подключите трекер: Настройки → Интеграции"}},
      {"sync.failed", {"%1 sync failed: %2", "%1: синхронизация не удалась — %2"}},
      {"int.connected", {"%1 connected", "%1 подключён"}},
      {"int.connectFailed", {"%1 connection failed: %2", "%1: не удалось подключиться — %2"}},
      {"contacts.upToDate", {"%1: contacts are up to date", "%1: контакты без изменений"}},
      {"int.sessionExpired", {"%1 session expired — sign in again", "%1: сессия истекла — войдите снова"}},
      {"int.noPassword",
       {"Signing in with a password is not available for this integration", "Вход по паролю для этой интеграции недоступен"}},
      {"int.needUrl", {"Enter the %1 server URL first", "Сначала укажите адрес сервера %1"}},
      {"int.signInFailed", {"%1 sign-in failed: %2", "%1: вход не удался — %2"}},
      {"int.unknown", {"Unknown integration", "Неизвестная интеграция"}},
      {"int.notConfigured", {"%1 is not fully configured", "%1 настроен не полностью"}},
      {"int.needs", {"%1 needs %2", "%1: нужно заполнить %2"}},
      {"int.noSite",
       {"%1 sign-in granted no site — for a self-hosted Jira, fill in Advanced and press Connect",
        "%1: вход не дал доступа ни к одному сайту — для своего Jira заполните «Дополнительно» и нажмите «Подключить»"}},
      {"int.browserConnectedSite", {"%1 connected via browser — %2", "%1 подключён через браузер — %2"}},
      {"int.noBrowser", {"Browser sign-in is not available for this integration", "Вход через браузер для этой интеграции недоступен"}},
      {"int.noOAuthApp",
       {"No OAuth app configured — add a client ID under Advanced first",
        "OAuth-приложение не настроено — добавьте client ID в разделе «Дополнительно»"}},
      {"int.needSecret",
       {"%1 browser sign-in needs an OAuth client secret — add one under Advanced",
        "%1: для входа через браузер нужен client secret — добавьте его в разделе «Дополнительно»"}},
      {"int.needQt",
       {"%1 browser sign-in needs Qt 6.9 or newer — use an access token",
        "%1: вход через браузер требует Qt 6.9 или новее — используйте токен доступа"}},
      {"int.deviceCode", {"%1: open %2 and enter code %3", "%1: откройте %2 и введите код %3"}},
      {"int.browserConnected", {"%1 connected via browser", "%1 подключён через браузер"}},
      {"int.signedInNeeds", {"%1 signed in — now fill in %2", "%1: вход выполнен — теперь заполните %2"}},
      {"int.browserStarting", {"Starting %1 browser sign-in…", "Запуск входа в %1 через браузер…"}},
      {"int.browserOpening", {"Opening browser for %1 — OAuth redirect: %2", "Открываю браузер для %1 — OAuth redirect: %2"}},
      {"git.noRepo",
       {"No git repository configured — add one in Settings › Git", "Git-репозиторий не настроен — добавьте его в Настройки › Git"}},
      {"git.noBranchName", {"Could not derive a branch name for %1", "Не удалось составить имя ветки для %1"}},
      {"git.branchFailed", {"Branch create failed: %1", "Не удалось создать ветку: %1"}},
      {"git.branchCreated", {"Created branch %1", "Создана ветка %1"}},
      {"contacts.updated", {"%1: %2 contact(s) updated", "%1: обновлено контактов — %2"}},
      {"event.editUndone", {"Event change undone: %1", "Изменение события отменено: %1"}},
      {"event.cannotMoveSeries",
       {"This repeat rule can't be moved by dragging — change the days in the editor",
        "Это правило повтора нельзя перенести перетаскиванием — измените дни в редакторе"}},
      {"event.badRule",
       {"Repeat rule not supported, saved as a single event: %1", "Правило повтора не поддерживается, сохранено одно событие: %1"}},
      {"task.idRequired", {"A task needs an id", "У задачи должен быть id"}},
      {"task.titleRequired", {"A task needs a title", "У задачи должен быть заголовок"}},
      {"task.idInvalid", {"“%1” can't be an id — no spaces or slashes", "«%1» не подходит для id — без пробелов и слэшей"}},
      {"task.editUndone", {"Edit undone: %1", "Правка отменена: %1"}},
      {"task.createUndone", {"Creation undone: %1", "Создание отменено: %1"}},
      // The local layer (APP-236…241, 251).
      {"local.waitsOn", {"%1 waits on %2 — still open", "%1 ждёт %2 — ещё открыто"}},
      {"local.draftCopied", {"Draft copied — paste it into the ticket", "Черновик скопирован — вставьте его в тикет"}},
      {"local.draftCleared", {"Draft cleared", "Черновик очищен"}},
      {"local.notesCopied", {"Notes copied to the note “%1”", "Заметки скопированы в заметку «%1»"}},
      {"local.notesFrom", {"From the notes of task %1", "Из заметок задачи %1"}},
      {"local.tagRenamed", {"Tag #%1 → #%2", "Метка #%1 → #%2"}},
      {"local.tagDeleted", {"Tag #%1 removed from %2 card(s)", "Метка #%1 снята с карточек: %2"}},
      {"local.itemToCard", {"%1: now a card of its own", "%1: теперь своя карточка"}},
      {"local.cardBack", {"%1 is a list item again", "%1 снова пункт списка"}},
      {"local.blockCycle", {"%1 already blocks %2: the link would make a loop", "%1 уже блокирует %2: связь замкнула бы круг"}},
      {"local.cardBackTimed", {"%1 has tracked time: it stays a card", "У %1 есть учтённое время: она остаётся карточкой"}},
      {"local.cardBackRefused",
       {"%1 holds its own text, labels or links: it stays a card", "В %1 есть свой текст, метки или связи: она остаётся карточкой"}},
      {"local.relatedNotFound", {"No task or link like “%1”", "Нет задачи или ссылки «%1»"}},
      // audit-tasks: toasts for operations that became undoable.
      {"undo.changedSince", {"Can't undo that — it has been changed since", "Нельзя отменить — это уже изменили после"}},
      {"undo.column", {"Column change undone: %1", "Изменение колонки отменено: %1"}},
      {"undo.schedule", {"Scheduling undone: %1", "Планирование отменено: %1"}},
      {"undo.deadline", {"Due date change undone: %1", "Изменение срока отменено: %1"}},
      // Drag-to-reschedule (APP-249).
      {"reschedule.scheduled", {"%1: when → %2", "%1: когда → %2"}},
      {"reschedule.due", {"%1: due → %2", "%1: срок → %2"}},
      {"reschedule.scheduledCleared", {"%1: no longer planned for a day", "%1: больше не запланировано на день"}},
      {"reschedule.dueCleared", {"%1: due date removed", "%1: срок снят"}},
      {"reschedule.localOnly", {"changed here only, not in the tracker", "изменено только здесь, не в трекере"}},
      {"reschedule.block", {"%1: %2–%3", "%1: %2–%3"}},
      {"undo.carry", {"Carry undone", "Перенос отменён"}},
      {"carry.tomorrow", {"%1 → tomorrow", "%1 → завтра"}},
      {"carry.window", {"%1 → %2", "%1 → %2"}},
      {"carry.someday", {"%1 → someday", "%1 → когда-нибудь"}},
      {"carry.clear", {"%1: no longer planned for a day", "%1: больше не запланировано на день"}},
      {"carry.many", {"%1 tasks", "Задач: %1"}},
      {"carry.windows", {"the nearest free windows", "ближайшие свободные окна"}},
      {"carry.noWindow", {"no free window in the next two weeks", "нет свободного окна в ближайшие две недели"}},
      {"carry.clearDue", {"%1: deadline removed", "%1: срок снят"}},
      {"carry.dueStays",
       {"%1 still overdue: the deadline stays (Shift S changes it)", "%1 всё ещё просрочено: срок остаётся (Shift S меняет его)"}},
      {"carry.nothingDue",
       {"Nothing to carry: these tasks have a deadline, not a plan date. Shift S changes the deadline",
        "Нечего переносить: у этих задач срок, а не дата плана. Shift S меняет срок"}},
      {"carry.nothing", {"Nothing to carry", "Нечего переносить"}},
      {"undo.timer", {"Timer change undone: %1", "Изменение таймера отменено: %1"}},
      {"undo.snooze", {"Snooze undone: %1", "Откладывание отменено: %1"}},
      {"undo.bulkEdit", {"Change undone for %1 task(s)", "Изменение отменено для задач: %1"}},
      {"task.bulkPriority", {"%1 task(s) → %2", "Задач: %1 → %2"}},
      {"task.bulkLabel", {"%1 task(s) labelled %2", "Метка %2 — задач: %1"}},
      {"task.bulkUnlabel", {"%2 removed from %1 task(s)", "Метка %2 снята — задач: %1"}},
      {"sync.conflicts",
       {"%1 changed on both sides, your edits kept: %2 — open the card to compare",
        "%1 изменены с обеих сторон, оставлены ваши правки: %2 — откройте карточку, чтобы сравнить"}},
      {"sync.gone", {"%1 no longer in the tracker", "%1 больше нет в трекере"}},
      {"sync.pushFailed", {"%1: the tracker did not take the change — %2", "%1: трекер не принял изменение — %2"}},
      {"onboarding.freshUndone", {"Demo content restored", "Демо-данные возвращены"}},
      {"notes.deleted", {"Note deleted: %1", "Заметка удалена: %1"}},
      {"notes.restored", {"Note restored: %1", "Заметка восстановлена: %1"}},
      {"calsub.readOnly", {"“%1” comes from the calendar “%2” — change it there", "«%1» из календаря «%2» — меняйте его там"}},
      {"calsub.badLink",
       {"Not a calendar link: it should start with https:// or webcal://",
        "Это не ссылка на календарь: она должна начинаться с https:// или webcal://"}},
      {"calsub.notCalendar", {"The link answered, but not with a calendar", "Ссылка ответила, но это не календарь"}},
      {"calsub.tooBig", {"The calendar is larger than 20 MB", "Календарь больше 20 МБ"}},
      {"calsub.noLink",
       {"The link is not in the keychain any more — add the calendar again",
        "Ссылки больше нет в хранилище ключей — добавьте календарь заново"}},
      {"calsub.defaultName", {"Outlook calendar", "Календарь Outlook"}},
      {"calsub.desktopName", {"Outlook on this computer", "Outlook на этом компьютере"}},
      {"calsub.noDesktop", {"No Outlook on this computer answers", "На этом компьютере Outlook не отвечает"}},
      {"calsub.desktopTwice", {"Outlook on this computer is already added", "Outlook этого компьютера уже добавлен"}},
      {"notes.merged", {"“%1” merged into “%2”", "«%1» добавлена в «%2»"}},
      {"notes.undo.merge", {"Merge undone: %1", "Объединение отменено: %1"}},
      {"docs.untitledPage", {"Untitled page", "Без названия"}},
      {"docs.undo.deletePage", {"Page deleted", "Страница удалена"}},
      {"notes.daily", {"Today's note", "Заметка на сегодня"}},
      {"notes.vault.badFolder", {"That folder could not be opened.", "Не удалось открыть папку."}},
      {"notes.vault.unreadable", {"Could not read %1", "Не удалось прочитать %1"}},
      {"notes.vault.tooBig", {"Skipped %1: %2 MB is too big for a note", "Пропущен %1: %2 МБ — слишком много для заметки"}},
      {"notes.vault.binary", {"Skipped %1: not a text file", "Пропущен %1: это не текстовый файл"}},
      {"notes.vault.conflict", {"%1 changed both here and on disk — kept both", "%1 изменён и здесь, и на диске — сохранены обе версии"}},
      {"notes.vault.conflictSuffix", {" (from disk)", " (с диска)"}},
      {"notes.vault.undo", {"Notes import undone", "Импорт заметок отменён"}},
      {"docs.pageDeleted", {"Page deleted: %1", "Страница удалена: %1"}},
      {"docs.pagesDeleted", {"Page deleted: %1 and %2 subpage(s)", "Удалена страница %1 и подстраниц: %2"}},
      {"search.notes", {"Notes", "Заметки"}},
      {"search.docs", {"Docs", "Документы"}},
      {"notes.undo.rename", {"Rename undone: %1", "Переименование отменено: %1"}},
      {"notes.undo.pin", {"Pin undone: %1", "Закрепление отменено: %1"}},
      {"notes.undo.unpin", {"Unpin undone: %1", "Открепление отменено: %1"}},
      {"notes.undo.move", {"Move undone: %1", "Перемещение отменено: %1"}},
      {"notes.undo.folder", {"Folder change undone: %1", "Изменение папки отменено: %1"}},
      {"ics.error.open", {"Could not read that file.", "Не удалось прочитать файл."}},
      {"undo.importIcs", {"Import undone", "Импорт отменён"}},
      {"event.scheduleUndone", {"Scheduling undone: %1", "Планирование отменено: %1"}},
      {"status.doingUndone", {"Column change undone: %1", "Изменение колонки отменено: %1"}},
      {"ics.error.notCalendar", {"That file is not a calendar (.ics).", "Это не файл календаря (.ics)."}},
      {"ics.reimported", {"Calendar imported again: %1 events", "Календарь импортирован снова: событий %1"}},
      {"event.seriesDeleted", {"Deleted series: %1", "Удалена серия: %1"}},
      {"event.followingDeleted", {"Deleted this and following: %1", "Удалены это и следующие: %1"}},
      {"undo.redone", {"Redone", "Повторено"}},
      {"undo.gone", {"Can't undo that any more", "Это уже не отменить"}},
      {"shortcut.cal.today.label", {"Calendar: today", "Календарь: сегодня"}},
      {"shortcut.cal.today.desc", {"Jump the calendar back to today.", "Вернуть календарь к сегодняшнему дню."}},
      {"shortcut.cal.prev.label", {"Calendar: previous", "Календарь: назад"}},
      {"shortcut.cal.prev.desc", {"Step back one week or month.", "Шаг назад на неделю или месяц."}},
      {"shortcut.cal.next.label", {"Calendar: next", "Календарь: вперёд"}},
      {"shortcut.cal.next.desc", {"Step forward one week or month.", "Шаг вперёд на неделю или месяц."}},
      {"shortcut.cal.goToDate.label", {"Calendar: go to date", "Календарь: перейти к дате"}},
      {"shortcut.cal.goToDate.desc", {"Open a date picker and jump straight there.", "Открыть выбор даты и перейти сразу к ней."}},
      {"shortcut.notes.new.label", {"Notes: new note", "Заметки: новая заметка"}},
      {"shortcut.notes.new.desc", {"Start a note and put the cursor in it.", "Создать заметку и поставить в неё курсор."}},
      {"shortcut.notes.next.label", {"Notes: next note", "Заметки: следующая"}},
      {"shortcut.notes.next.desc", {"Open the next note in the list.", "Открыть следующую заметку в списке."}},
      {"shortcut.notes.prev.label", {"Notes: previous note", "Заметки: предыдущая"}},
      {"shortcut.notes.prev.desc", {"Open the previous note in the list.", "Открыть предыдущую заметку в списке."}},
      {"shortcut.notes.rename.label", {"Notes: rename note", "Заметки: переименовать"}},
      {"shortcut.notes.rename.desc", {"Rename or re-file the open note.", "Переименовать открытую заметку или сменить её папку."}},
      {"shortcut.notes.toggleList.label", {"Notes: show or hide the list", "Заметки: показать или скрыть список"}},
      {"shortcut.notes.delete.label", {"Knowledge: delete the note or page", "Знания: удалить заметку или страницу"}},
      {"shortcut.notes.delete.desc",
       {"Delete the note or doc page the Knowledge list is on. Undo brings it back.",
        "Удалить заметку или страницу, на которой стоит список «Знаний». Отмена вернёт её."}},
      {"shortcut.editor.deleteBlock.label", {"Document: delete the block", "Документ: удалить блок"}},
      {"shortcut.editor.deleteBlock.desc",
       {"Delete the paragraph, list or code block the caret is in (the one last edited, with no block open). Undo brings it "
        "back.",
        "Удалить абзац, список или блок кода под курсором (без открытого блока — последний правленный). Отмена вернёт его."}},
      {"keymap.scheme.switched", {"Keys: %1. Your own keys stayed.", "Клавиши: %1. Ваши собственные сочетания остались."}},
      {"keymap.scheme.classic", {"Classic", "Классическая"}},
      {"keymap.scheme.vim", {"Vim", "Vim"}},
      {"shortcut.notes.toggleList.desc", {"Fold the list of notes away, or bring it back.", "Свернуть список заметок или вернуть его."}},
      {"shortcut.notes.table.rowAbove.label", {"Notes: table row above", "Заметки: строка таблицы выше"}},
      {"shortcut.notes.table.rowAbove.desc",
       {"Add an empty row above the one the caret is in.", "Добавить пустую строку над строкой с курсором."}},
      {"shortcut.notes.table.rowBelow.label", {"Notes: table row below", "Заметки: строка таблицы ниже"}},
      {"shortcut.notes.table.rowBelow.desc",
       {"Add an empty row below the one the caret is in.", "Добавить пустую строку под строкой с курсором."}},
      {"shortcut.notes.table.columnLeft.label", {"Notes: table column left", "Заметки: столбец таблицы слева"}},
      {"shortcut.notes.table.columnLeft.desc",
       {"Add an empty column left of the caret's.", "Добавить пустой столбец слева от столбца с курсором."}},
      {"shortcut.notes.table.columnRight.label", {"Notes: table column right", "Заметки: столбец таблицы справа"}},
      {"shortcut.notes.table.columnRight.desc",
       {"Add an empty column right of the caret's.", "Добавить пустой столбец справа от столбца с курсором."}},
      {"shortcut.notes.table.deleteRow.label", {"Notes: delete table row", "Заметки: удалить строку таблицы"}},
      {"shortcut.notes.table.deleteRow.desc", {"Delete the row the caret is in.", "Удалить строку таблицы, в которой курсор."}},
      {"shortcut.notes.table.deleteColumn.label", {"Notes: delete table column", "Заметки: удалить столбец таблицы"}},
      {"shortcut.notes.table.deleteColumn.desc", {"Delete the column the caret is in.", "Удалить столбец таблицы, в котором курсор."}},
      {"shortcut.cal.prevDay.label", {"Calendar: previous day", "Календарь: предыдущий день"}},
      {"shortcut.cal.prevDay.desc", {"Move the selected day back by one.", "Сдвинуть выбранный день на один назад."}},
      {"shortcut.cal.nextDay.label", {"Calendar: next day", "Календарь: следующий день"}},
      {"shortcut.cal.nextDay.desc", {"Move the selected day forward by one.", "Сдвинуть выбранный день на один вперёд."}},
      {"shortcut.cal.newEvent.label", {"New event", "Новое событие"}},
      {"shortcut.cal.newEvent.desc",
       {"Open the event editor at the next free slot of the selected day.",
        "Открыть редактор события на ближайшем свободном слоте выбранного дня."}},
      {"shortcut.cal.taskEarlier.label", {"Task: a day earlier", "Задача: на день раньше"}},
      {"shortcut.cal.taskEarlier.desc",
       {"Week, Month, Timeline: move the focused task's date back a day.",
        "Неделя, месяц, лента: сдвинуть дату задачи в фокусе на день назад."}},
      {"shortcut.cal.taskLater.label", {"Task: a day later", "Задача: на день позже"}},
      {"shortcut.cal.taskLater.desc",
       {"Week, Month, Timeline: move the focused task's date forward a day.",
        "Неделя, месяц, лента: сдвинуть дату задачи в фокусе на день вперёд."}},
      {"shortcut.cal.taskEarlierWeek.label", {"Task: a week earlier", "Задача: на неделю раньше"}},
      {"shortcut.cal.taskEarlierWeek.desc",
       {"Week, Month, Timeline: move the focused task's date back a week.",
        "Неделя, месяц, лента: сдвинуть дату задачи в фокусе на неделю назад."}},
      {"shortcut.cal.taskLaterWeek.label", {"Task: a week later", "Задача: на неделю позже"}},
      {"shortcut.cal.taskLaterWeek.desc",
       {"Week, Month, Timeline: move the focused task's date forward a week.",
        "Неделя, месяц, лента: сдвинуть дату задачи в фокусе на неделю вперёд."}},
      {"shortcut.cal.taskTimeEarlier.label", {"Task: earlier in the day", "Задача: раньше в течение дня"}},
      {"shortcut.cal.taskTimeEarlier.desc",
       {"Move a task planned at a time one grid step earlier.", "Сдвинуть задачу, запланированную на время, на шаг сетки раньше."}},
      {"shortcut.cal.taskTimeLater.label", {"Task: later in the day", "Задача: позже в течение дня"}},
      {"shortcut.cal.taskTimeLater.desc",
       {"Move a task planned at a time one grid step later.", "Сдвинуть задачу, запланированную на время, на шаг сетки позже."}},
      {"shortcut.board.cursorDown.label", {"Cursor down", "Курсор вниз"}},
      {"shortcut.board.cursorDown.desc", {"The next item: a card, a row, a meeting.", "Следующий элемент: карточка, строка, встреча."}},
      {"shortcut.board.cursorUp.label", {"Cursor up", "Курсор вверх"}},
      {"shortcut.board.cursorUp.desc", {"The previous item: a card, a row, a meeting.", "Предыдущий элемент: карточка, строка, встреча."}},
      {"shortcut.board.cursorLeft.label", {"Column or day left", "Колонка или день левее"}},
      {"shortcut.board.cursorLeft.desc",
       {"The previous column on the board, the previous day in the calendar and on Today.",
        "Предыдущая колонка на доске, предыдущий день в календаре и на «Сегодня»."}},
      {"shortcut.board.cursorRight.label", {"Column or day right", "Колонка или день правее"}},
      {"shortcut.board.cursorRight.desc",
       {"The next column on the board, the next day in the calendar and on Today.",
        "Следующая колонка на доске, следующий день в календаре и на «Сегодня»."}},
      {"shortcut.board.open.label", {"Open", "Открыть"}},
      {"shortcut.board.open.desc", {"Open the task or meeting under the cursor.", "Открыть задачу или встречу под курсором."}},
      {"shortcut.board.toggleSelect.label", {"Board: select card", "Доска: выделить карточку"}},
      {"shortcut.board.toggleSelect.desc",
       {"Add or remove the card under the cursor from the selection.", "Добавить карточку под курсором в выделение или убрать из него."}},
      {"shortcut.board.moveDown.label", {"Move down / later", "Ниже / позже"}},
      {"shortcut.board.moveDown.desc",
       {"On the board, swap the card with the one below; in the calendar, a grid step later. Also Ctrl+Down.",
        "На доске поменять карточку местами с нижней; в календаре — на шаг сетки позже. Также Ctrl+↓."}},
      {"shortcut.board.moveUp.label", {"Move up / earlier", "Выше / раньше"}},
      {"shortcut.board.moveUp.desc",
       {"On the board, swap the card with the one above; in the calendar, a grid step earlier. Also Ctrl+Up.",
        "На доске поменять карточку местами с верхней; в календаре — на шаг сетки раньше. Также Ctrl+↑."}},
      {"shortcut.board.columnLeft.label", {"Move the column left", "Сдвинуть колонку влево"}},
      {"shortcut.board.columnLeft.desc",
       {"On a column header: move the column one place left.", "На заголовке колонки: сдвинуть колонку на место левее."}},
      {"shortcut.board.columnRight.label", {"Move the column right", "Сдвинуть колонку вправо"}},
      {"shortcut.board.columnRight.desc",
       {"On a column header: move the column one place right. Elsewhere this key opens the log.",
        "На заголовке колонки: сдвинуть колонку на место правее. В остальных местах эта клавиша открывает журнал."}},
      {"shortcut.board.moveLeft.label", {"Move left / a day earlier", "Левее / на день раньше"}},
      {"shortcut.board.moveLeft.desc",
       {"Move the selection, or the item under the cursor, to the previous column; in the calendar and on Today, a day earlier. Also "
        "Ctrl+Left.",
        "Перенести выделение или элемент под курсором в предыдущую колонку; в календаре и на «Сегодня» — на день раньше. Также Ctrl+←."}},
      {"shortcut.board.moveRight.label", {"Move right / a day later", "Правее / на день позже"}},
      {"shortcut.board.moveRight.desc",
       {"Move the selection, or the item under the cursor, to the next column; in the calendar and on Today, a day later. Also Ctrl+Right.",
        "Перенести выделение или элемент под курсором в следующую колонку; в календаре и на «Сегодня» — на день позже. Также Ctrl+→."}},
      {"shortcut.board.selectDown.label", {"Board: select down", "Доска: выделить вниз"}},
      {"shortcut.board.selectDown.desc", {"Add the next card down to the selection.", "Добавить в выделение карточку ниже."}},
      {"shortcut.board.selectUp.label", {"Board: select up", "Доска: выделить вверх"}},
      {"shortcut.board.selectUp.desc", {"Add the next card up to the selection.", "Добавить в выделение карточку выше."}},
      {"shortcut.board.selectColumnLeft.label", {"Board: select column, go left", "Доска: выделить колонку, влево"}},
      {"shortcut.board.selectColumnLeft.desc",
       {"Select every card in the cursor's column, then step to the previous column.",
        "Выделить все карточки колонки с курсором и перейти в колонку левее."}},
      {"shortcut.board.selectColumnRight.label", {"Board: select column, go right", "Доска: выделить колонку, вправо"}},
      {"shortcut.board.selectColumnRight.desc",
       {"Select every card in the cursor's column, then step to the next column.",
        "Выделить все карточки колонки с курсором и перейти в колонку правее."}},
      {"shortcut.board.cardMenu.label", {"Board: card menu", "Доска: меню карточки"}},
      {"shortcut.board.cardMenu.desc",
       {"Open the menu of the card under the cursor: status, priority, archive…",
        "Открыть меню карточки под курсором: статус, приоритет, архив…"}},
      {"shortcut.board.archive.label", {"Board: archive card", "Доска: в архив"}},
      {"shortcut.board.archive.desc",
       {"Archive the selection, or the card under the cursor.", "Отправить в архив выделение или карточку под курсором."}},
      {"shortcut.board.collapseColumn.label", {"Board: fold column", "Доска: свернуть колонку"}},
      {"shortcut.board.collapseColumn.desc",
       {"Fold or unfold the column the cursor is in.", "Свернуть или развернуть колонку с курсором."}},
      {"shortcut.task.openExternal.label", {"Open in tracker", "Открыть в трекере"}},
      {"shortcut.task.openExternal.desc",
       {"Opens the selected (or hovered) mirrored issue in its tracker.",
        "Открывает выбранный (или под курсором) синхронизированный тикет в трекере."}},
      {"task.moveUndone", {"Move undone: %1", "Перемещение отменено: %1"}},
      {"task.archived", {"Archived: %1", "В архиве: %1"}},
      {"task.archived.doneKey", {" · done is %1", " · готово — %1"}},
      {"search.elsewhere", {"In other profiles: %1", "Есть в других профилях: %1"}},
      {"search.elsewhere.one", {"%1 in %2", "%1 в «%2»"}},
      {"task.unarchived", {"Unarchived: %1", "Из архива: %1"}},
      {"task.archiveUndone", {"Restored: %1", "Восстановлено: %1"}},
      {"event.deleted", {"Deleted event: %1", "Удалено событие: %1"}},
      {"event.restored", {"Restored: %1", "Восстановлено: %1"}},
      {"event.scheduled", {"%1 scheduled for %2", "%1 запланировано на %2"}},
      {"person.added", {"Added: %1", "Добавлен: %1"}},
      {"person.deleted", {"Removed: %1", "Удалён: %1"}},
      {"person.pinged", {"Wrote to %1", "Написал: %1"}},
      {"person.replied", {"%1 replied", "Ответил: %1"}},
      {"person.stateUndone", {"Back on the list: %1", "Снова в списке: %1"}},
      {"person.backToTodo", {"Message again: %1", "Снова написать: %1"}},
      {"person.restored", {"Restored: %1", "Восстановлён: %1"}},
      {"person.idTaken", {"%1 is already taken by %2 — pick another id", "%1 уже занят: %2 — выберите другой id"}},
      {"person.createUndone", {"Creation undone: %1", "Создание отменено: %1"}},
      {"person.editUndone", {"Edit undone: %1", "Правка отменена: %1"}},
      {"status.added", {"Column added: %1", "Колонка добавлена: %1"}},
      {"status.deleted", {"Column removed: %1", "Удалена колонка: %1"}},
      {"status.restored", {"Column restored: %1", "Восстановлена колонка: %1"}},
      {"profile.created", {"Profile created: %1", "Профиль создан: %1"}},
      {"profile.personal", {"Personal", "Личное"}},
      // The sheets name it "Example" in both languages (DG-007).
      {"profile.example", {"Example", "Example"}},
      {"profile.deleted", {"Profile removed: %1", "Удалён профиль: %1"}},
      {"profile.restored", {"Profile restored: %1", "Восстановлен профиль: %1"}},
      {"profile.duplicated", {"Profile duplicated: %1", "Дублирован профиль: %1"}},
      // Moving tasks to another profile (A5-14).
      {"move.profile.one", {"%1 moved to %2", "%1 перенесена в «%2»"}},
      {"move.profile.many", {"%1 tasks moved to %2", "Перенесено в «%2»: %1"}},
      {"move.profile.renamed", {"%1 is %2 there: the id was taken", "%1 там — %2: этот ID был занят"}},
      {"move.profile.trackerKept",
       {"Tracker cards stay in %1, whose integration pulled them: %2",
        "Карточки трекера остаются в «%1» — их подтянула его интеграция: %2"}},
      {"move.profile.undone", {"Back in %1: %2", "Снова в «%1»: %2"}},
      {"move.profile.redone", {"Moved to %1 again: %2", "Снова в «%1»: %2"}},
      {"move.profile.gone", {"Can't take the move back: a profile is gone", "Перенос не отменить: профиля больше нет"}},
      {"profile.exported", {"Profile exported: %1", "Профиль экспортирован: %1"}},
      {"task.keyCopied", {"Copied: %1", "Скопировано: %1"}},
      {"ci.failedTitle", {"CI failed", "CI упал"}},
      {"ci.failedTitleN", {"CI failed on %1", "CI упал: %1"}},
      {"profile.mdCopied", {"Profile Markdown copied to clipboard", "Markdown профиля скопирован в буфер"}},
      {"profile.weeklyCopied", {"Weekly report copied to clipboard", "Недельный отчёт скопирован в буфер"}},
      {"profile.imported", {"Profile imported: %1", "Импортирован профиль: %1"}},
      {"tasks.renamed", {"Tasks renamed: %1", "Переименовано задач: %1"}},
      {"tasks.renameTaken", {"Ids not renamed: %1 is already taken (%2 in all)", "Номера не переименованы: %1 уже занят (всего %2)"}},
      {"notify.meetingSoon", {"In %1 min", "Через %1 мин"}},
      {"notify.meetingNow", {"Starting now", "Начинается"}},
      {"notify.meetingSoonTitled", {"%1 in %2 min", "%1 через %2 мин"}},
      {"notify.meetingNowTitled", {"%1 is starting", "%1 начинается"}},
      {"notify.action.join", {"Join", "Подключиться"}},
      {"tray.newTask", {"New task…", "Новая задача…"}},
      {"tray.quickNote", {"Quick note…", "Быстрая заметка…"}},
      {"tray.stopTimer", {"Stop timer", "Остановить таймер"}},
      {"tray.next", {"Next: %1", "Далее: %1"}},
      {"tray.open", {"Open Tabless", "Открыть Tabless"}},
      {"tray.dnd", {"Do not disturb for 1 h", "Не беспокоить 1 ч"}},
      {"tray.dndUntil", {"Do not disturb until %1 — turn off", "Не беспокоить до %1 — выключить"}},
      {"tray.quit", {"Quit", "Выход"}},
      {"tray.tooltip", {"Tabless — %1 to capture", "Tabless — %1, чтобы записать"}},
      {"notify.dueToday", {"Due today", "Срок сегодня"}},
      {"notify.dueTomorrow", {"Due tomorrow", "Срок завтра"}},
      {"notify.waitingOn", {"waiting on %1", "ждёт ответа: %1"}},
      {"backup.restored", {"Restored from %1", "Восстановлено из %1"}},
      {"history.notFound", {"That snapshot is gone", "Этого снимка больше нет"}},
      {"history.damaged", {"That snapshot cannot be read", "Этот снимок не читается"}},
      {"history.newer", {"That snapshot was made by a newer Tabless", "Этот снимок сделан более новой версией Tabless"}},
      {"history.restored",
       {"Restored to %1. The state before it is in the time machine too.",
        "Состояние на %1 восстановлено. То, что было до него, тоже есть в машине времени."}},
      {"history.restoredName", {"%1 (restored %2)", "%1 (восстановлен %2)"}},
      {"history.profileRestored", {"Restored as profile %1", "Восстановлено как профиль «%1»"}},
      {"history.profileGone",
       {"Profile %1 no longer exists. Restore it as a copy first.", "Профиля «%1» больше нет. Сначала восстановите его копией."}},
      {"history.taskElsewhere", {"%1 lives in profile %2 now", "%1 сейчас в профиле «%2»"}},
      {"history.undo.item", {"Restore %1", "Восстановление «%1»"}},
      {"history.itemRestored", {"Restored: %1", "Восстановлено: %1"}},
      {"data.recovered",
       {"Your data file was damaged, so backup %1 was opened instead — changes made after that backup are not in it. "
        "The damaged file is kept in the data folder as %2.",
        "Файл данных был повреждён, поэтому открыт бэкап %1 — изменений, сделанных после него, здесь нет. "
        "Повреждённый файл сохранён в папке данных как %2."}},
      {"data.corruptKept",
       {"Your data file was damaged and there is no backup, so this is an empty workspace, not a new install. "
        "The damaged file is kept in the data folder as %1.",
        "Файл данных был повреждён, бэкапа нет, поэтому открыто пустое пространство — это не новая установка. "
        "Повреждённый файл сохранён в папке данных как %1."}},
      {"data.schemaTooNew",
       {"Read-only: this data file was written by a newer Tabless (schema v%1, this build reads v%2). "
        "Nothing you change now is saved — update Tabless to edit it.",
        "Только чтение: файл данных записан более новой версией Tabless (схема v%1, эта сборка знает v%2). "
        "Изменения сейчас не сохраняются — обновите Tabless, чтобы редактировать."}},
      // ── audit-plat: storage health banner (PLAT-1/4) ──
      {"storage.unreadable",
       {"Read-only: %1 could not be opened (%2). Nothing is saved over it until it opens — changes made now "
        "are not kept.",
        "Только чтение: не удаётся открыть %1 (%2). Пока файл не откроется, поверх него ничего не "
        "сохраняется — изменения сейчас не сохранятся."}},
      {"storage.showingBackup", {"Showing backup %1.", "Показан бэкап %1."}},
      {"storage.editNotKept",
       {"Not saved: this session is read-only, the change is lost when Tabless closes",
        "Не сохранено: сессия только для чтения, правка пропадёт при закрытии Tabless"}},
      {"storage.damagedLocked", {"the file is damaged and could not be set aside", "файл повреждён и его не удалось отложить в сторону"}},
      {"storage.writeFailed",
       {"Not saved: writing %1 failed (%2). Your changes are kept in memory and Tabless keeps retrying.",
        "Не сохранено: запись %1 не удалась (%2). Изменения в памяти, Tabless повторяет попытки."}},
      // The storage strip's lead (R2-037), and "Save a copy elsewhere…".
      {"storage.reason.noSpace", {"no space left on disk %1", "на диске %1 нет места"}},
      {"storage.reason.readOnly", {"the disk is read-only", "диск только для чтения"}},
      {"storage.reason.noAccess", {"no permission to write the data folder", "нет прав на запись в папку данных"}},
      {"storage.reason.locked", {"the data file is busy or unavailable", "файл данных занят или недоступен"}},
      {"storage.copySaved", {"A copy is saved: %1", "Копия сохранена: %1"}},
      {"storage.copyFailed", {"The copy was not saved (%1)", "Копия не сохранилась (%1)"}},
      {"task.conflictMixed", {"%1: your choice is applied", "%1: выбор применён"}},
      {"task.keptLocal", {"%1 kept as your own task", "%1 оставлена у вас"}},
      {"task.keptLocal.note", {"Was %1 in %2 (deleted there).", "Была %1 в %2 (там удалена)."}},
      {"keychain.name.win", {"Windows Credential Manager", "Диспетчер учётных данных Windows"}},
      {"keychain.name.mac", {"macOS Keychain", "Связка ключей macOS"}},
      {"keychain.name.linux", {"The system keyring", "Системное хранилище ключей"}},
      {"storage.dirUnwritable",
       {"Not saved: the data folder is not writable (%1). Nothing you change is saved.",
        "Не сохранено: папка данных недоступна для записи (%1). Изменения не сохраняются."}},
      {"settings.resetDone",
       {"Settings reset to defaults — connections, repos, themes and layout kept",
        "Настройки сброшены — подключения, репозитории, темы и раскладка сохранены"}},
      {"settings.resetUndone", {"Settings restored", "Настройки восстановлены"}},
      {"person.nameRequired", {"A person needs a name — kept the old one", "У человека должно быть имя — оставлено прежнее"}},
      {"storage.savedAgain", {"Saved — writing to disk works again", "Сохранено — запись на диск снова работает"}},
      {"storage.reopened", {"state.json opened — your data is loaded", "state.json открыт — данные загружены"}},
      {"storage.stillLocked", {"state.json still can't be opened", "state.json всё ещё не открывается"}},
      {"backup.snapshotFailed",
       {"Restore cancelled: could not snapshot the current state first",
        "Восстановление отменено: не удалось сохранить текущее состояние"}},
      {"backup.invalid", {"%1 is not a Tabless, lowkey or heap state file", "%1 — не файл состояния Tabless, lowkey или heap"}},
      {"hotkeys.reset", {"Hotkeys reset to defaults", "Хоткеи сброшены к дефолту"}},
      {"onboarding.startedFresh", {"Demo cleared — your workspace is empty", "Демо очищено — рабочее пространство пустое"}},
      {"branch.required", {"Set a branch — required by Settings", "Укажите ветку — этого требуют настройки"}},
      {"deadline.snoozed", {"%1: due date snoozed", "%1: срок отложен"}},
      {"notify.action.open", {"Open", "Открыть"}},
      {"notify.action.done", {"Mark done", "Готово"}},
      {"notify.snoozedUntil", {"Reminder snoozed until %1", "Напоминание отложено до %1"}},
      {"notify.reminderTitle", {"Reminder", "Напоминание"}},
      {"notify.test.title", {"Tabless test reminder", "Tabless — проверка напоминания"}},
      {"notify.test.body", {"Buttons work like on a real reminder.", "Кнопки работают как у настоящего напоминания."}},
      {"notify.test.sent",
       {"Test reminder sent. Nothing showed up? Check the system's notification settings for Tabless.",
        "Пробное напоминание отправлено. Ничего не появилось — проверьте настройки уведомлений системы для Tabless."}},
      {"notify.test.sentSystemOff",
       {"Test reminder sent. “Desktop notifications” is off, so real reminders show only here in the app.",
        "Пробное напоминание отправлено. «Уведомления в системе» выключены — настоящие напоминания будут только здесь, в приложении."}},
      {"notify.test.noNotifier",
       {"This system has no notification service for Tabless: reminders show only here in the app.",
        "В этой системе Tabless не может показывать уведомления: напоминания будут только здесь, в приложении."}},
      {"settings.system.startAtLogin.failed",
       {"Could not change the login start: the system refused", "Не удалось изменить автозапуск: система не дала"}},
      // Timeline row badge — the only date arithmetic rendered from C++.
      {"deadline.overdue", {"%1d overdue", "просрочено на %1 д"}},
      {"deadline.overdueLong", {"%1 overdue", "просрочено на %1"}},
      {"deadline.inLong", {"in %1", "через %1"}},
      {"span.months", {"%1 mo", "%1 мес."}},
      {"span.years", {"%1 yr", "%1 г."}},
      {"workday.adjusted",
       {"A working day ends after it starts — kept %1:00–%2:00", "Рабочий день должен заканчиваться позже начала — оставлено %1:00–%2:00"}},
      {"status.nameTaken", {"A column named %1 already exists", "Колонка «%1» уже есть"}},
      {"profile.nameTaken", {"A profile named %1 already exists", "Профиль «%1» уже есть"}},
      {"deadline.today", {"today", "сегодня"}},
      {"deadline.tomorrow", {"+1 day", "+1 день"}},
      // One shape for every distance: "+6 days" next to "+7d" read as two
      // different units.
      {"deadline.inDays", {"+%1d", "+%1 д"}},
      {"deadline.inDaysShort", {"+%1d", "+%1 д"}},
      {"slot.freed", {"Freed: %1", "Освобождено: %1"}},
      {"hotkeys.builtinTaken", {"%1 is built in for “%2” and can't be reassigned", "%1 встроено в «%2» и не переназначается"}},
      {"hotkeys.builtin.notesMode", {"Notes: cycle editor / split / preview", "Заметки: редактор / разделённый / просмотр"}},
      {"hotkeys.builtin.attach", {"Attach files", "Прикрепить файлы"}},
      {"import.emptyJson", {"Empty JSON", "Пустой JSON"}},
      {"import.invalidJson", {"Invalid JSON", "Невалидный JSON"}},
      {"import.missingProfile", {"JSON has no 'profile' block or expected fields", "В JSON нет блока 'profile' или ожидаемых полей"}},
      {"import.emptyPath", {"Empty path", "Пустой путь"}},
      {"import.openFail", {"Cannot open: ", "Не открывается: "}},
      {"profile.merged", {"Merged into %1: %2 added", "Слито с «%1»: добавлено %2"}},
      {"profile.mergedKept", {"Merged into %1: %2 added, kept as they were: %3", "Слито с «%1»: добавлено %2, оставлены как были: %3"}},
      {"event.newDefault", {"New event", "Новое событие"}},
      {"palette.fromTemplate", {"New from template: %1", "Новая по шаблону: %1"}},
      {"palette.templateSub", {"template", "шаблон"}},
      {"palette.profileTasks", {"%1 tasks", "задач: %1"}},
      {"palette.repeats", {"repeats", "повторяется"}},
      // ---- Shortcuts catalog (labels + descriptions) ----
      {"shortcut.palette.open.label", {"Command line", "Командная строка"}},
      {"shortcut.palette.open.desc",
       {"Search, filter and act in one line: tasks, notes, docs, people and commands.",
        "Поиск, фильтр и действия в одной строке: задачи, заметки, доки, люди и команды."}},
      {"shortcut.palette.commands.label", {"Command", "Команда"}},
      {"shortcut.palette.commands.desc",
       {"The command line on its commands, as > does inside it.", "Командная строка сразу на командах, как «>» в ней."}},
      {"shortcut.timeMachine.open.label", {"Time machine", "Машина времени"}},
      {"shortcut.timeMachine.open.desc", {"Bring back an earlier state from a snapshot.", "Вернуть прежнее состояние из снимка."}},
      {"shortcut.standup.draft.label", {"Standup draft", "Черновик стендапа"}},
      {"shortcut.standup.draft.desc",
       {"Yesterday / Today / Blockers from what Tabless saw; to edit and copy.",
        "Вчера / Сегодня / Блокеры из того, что видно в Tabless; поправить и скопировать."}},
      {"shortcut.recap.open.label", {"Weekly recap", "Сводка недели"}},
      {"shortcut.example.open.label", {"Open the example", "Открыть пример"}},
      {"shortcut.example.open.desc",
       {"A profile with sample tasks, notes and a day to try things on.", "Профиль с примером задач, заметок и дня, чтобы попробовать."}},
      {"shortcut.recap.open.desc", {"What changed column last week.", "Что сменило колонку на прошлой неделе."}},
      {"shortcut.endOfDay.open.label", {"End of day", "Конец дня"}},
      {"shortcut.endOfDay.open.desc",
       {"Today's summary: closed, carrying over, timers running.", "Итог дня: закрыто, переходит на завтра, идущие таймеры."}},
      {"shortcut.welcome.replay.label", {"Getting started", "С чего начать"}},
      {"shortcut.welcome.replay.desc", {"Open the guide.", "Открыть руководство."}},
      {"shortcut.task.new.label", {"New task", "Новая задача"}},
      {"shortcut.task.new.desc", {"Create a ticket in the active profile.", "Создать тикет в активном профиле."}},
      {"shortcut.task.done.label", {"Done", "Готово"}},
      {"shortcut.task.done.desc",
       {"The task under the cursor, or the selected ones, to Done; again puts it back.",
        "Задача под курсором или выбранные — в «Готово»; повторно — обратно."}},
      {"task.doneUndone", {"Done undone", "«Готово» отменено"}},
      {"task.reopenUndone", {"Reopen undone", "Возврат отменён"}},
      {"task.done.one", {"Done: %1", "Готово: %1"}},
      {"task.done.many", {"Done: %1 tasks", "Готово задач: %1"}},
      {"task.done.localOnly", {" · only here, the tracker is not told", " · только у вас, в трекер не ушло"}},
      {"task.done.unchecked", {" · %1 checklist items not ticked", " · не отмечено пунктов: %1"}},
      {"task.reopened.one", {"%1 is back in “%2”", "%1 снова в «%2»"}},
      {"task.reopened.many", {"%1 tasks are back where they were", "Задач возвращено: %1"}},
      {"shortcut.section.today.label", {"Go to Today", "Перейти в «Сегодня»"}},
      {"shortcut.section.today.desc", {"The day: meetings, what is due, what you are on.", "День: встречи, сроки, что в работе."}},
      {"shortcut.section.tasks.label", {"Go to Tasks", "Перейти в «Задачи»"}},
      {"shortcut.section.tasks.desc",
       {"Board, list or calendar, on the lens you left.", "Доска, список или календарь — на том виде, где вы остановились."}},
      // The sidebar row is "Notes" (A1-31, A6-27): one name for one screen.
      {"shortcut.section.knowledge.label", {"Go to Notes", "Перейти в «Заметки»"}},
      {"notes.wiki.missing", {"no such note", "нет такой"}},
      {"shortcut.section.knowledge.desc", {"Your notes and the links between them.", "Ваши заметки и ссылки между ними."}},
      // ---- The 0.8.0 keymap (keymap.md, APP-272) ----
      {"shortcut.view.calendar.label", {"Go to Calendar", "Перейти к календарю"}},
      {"shortcut.view.calendar.desc", {"Week or month, the one used last.", "Неделя или месяц — какой был последним."}},
      {"shortcut.cal.zoomDay.label", {"Calendar: day", "Календарь: день"}},
      {"shortcut.cal.zoomDay.desc", {"Zoom the calendar to one day.", "Показать в календаре один день."}},
      {"shortcut.cursor.first.label", {"To the first", "К первому"}},
      {"shortcut.cursor.first.desc", {"The cursor on the first item of the view.", "Курсор на первый элемент вида."}},
      {"shortcut.cursor.last.label", {"To the last", "К последнему"}},
      {"shortcut.cursor.last.desc", {"The cursor on the last item of the view.", "Курсор на последний элемент вида."}},
      {"shortcut.cursor.pageDown.label", {"Half a screen down", "Полэкрана вниз"}},
      {"shortcut.cursor.pageDown.desc", {"Move the cursor half a screen down.", "Сдвинуть курсор на полэкрана вниз."}},
      {"shortcut.cursor.pageUp.label", {"Half a screen up", "Полэкрана вверх"}},
      {"shortcut.cursor.pageUp.desc", {"Move the cursor half a screen up.", "Сдвинуть курсор на полэкрана вверх."}},
      {"shortcut.nav.back.label", {"Back", "Назад по переходам"}},
      {"shortcut.nav.back.desc", {"Where you were before, like Vim's jumplist.", "Туда, где вы были до этого, как jumplist в Vim."}},
      {"shortcut.nav.forward.label", {"Forward", "Вперёд по переходам"}},
      {"shortcut.nav.forward.desc", {"Forward again after going back.", "Снова вперёд после «назад»."}},
      {"shortcut.task.newBelow.label", {"New task below", "Новая задача ниже"}},
      {"shortcut.task.newBelow.desc",
       {"In the same column, group or day as the cursor.", "В той же колонке, группе или дне, что и курсор."}},
      {"shortcut.task.newAbove.label", {"New task above", "Новая задача выше"}},
      {"shortcut.task.newAbove.desc",
       {"In the same column, group or day as the cursor.", "В той же колонке, группе или дне, что и курсор."}},
      {"shortcut.task.rename.label", {"Rename", "Переименовать"}},
      {"shortcut.task.rename.desc", {"Rename the task under the cursor.", "Переименовать задачу под курсором."}},
      {"shortcut.task.schedule.label", {"Schedule", "Запланировать"}},
      {"shortcut.task.schedule.desc",
       {"Put the task in the next free slot of the selected day.", "Поставить задачу в ближайшее свободное окно выбранного дня."}},
      {"shortcut.task.due.label", {"Due date", "Срок"}},
      {"shortcut.task.due.desc", {"Set the task's due date.", "Задать срок задачи."}},
      {"shortcut.task.priority0.label", {"Priority P0", "Приоритет P0"}},
      {"shortcut.task.priority0.desc", {"Urgent: drop everything.", "Срочно: всё остальное потом."}},
      {"shortcut.task.priority1.label", {"Priority P1", "Приоритет P1"}},
      {"shortcut.task.priority1.desc", {"High.", "Высокий."}},
      {"shortcut.task.priority2.label", {"Priority P2", "Приоритет P2"}},
      {"shortcut.task.priority2.desc", {"Normal.", "Обычный."}},
      {"shortcut.task.priority3.label", {"Priority P3", "Приоритет P3"}},
      {"shortcut.task.priority3.desc", {"Low.", "Низкий."}},
      {"shortcut.task.timer.label", {"Timer", "Таймер"}},
      {"shortcut.task.timer.desc", {"Start or pause the task's timer.", "Запустить или поставить на паузу таймер задачи."}},
      {"shortcut.task.copyId.label", {"Copy ID", "Копировать ID"}},
      {"shortcut.task.copyId.desc", {"The task's ID to the clipboard.", "ID задачи — в буфер обмена."}},
      {"shortcut.task.copyBranch.label", {"Copy branch name", "Копировать имя ветки"}},
      {"shortcut.task.copyBranch.desc", {"The git branch of the task to the clipboard.", "Ветку git задачи — в буфер обмена."}},
      {"shortcut.task.copyLink.label", {"Copy tracker link", "Копировать ссылку в трекере"}},
      {"shortcut.task.copyLink.desc", {"The issue's link to the clipboard.", "Ссылку на тикет — в буфер обмена."}},
      {"shortcut.task.createBranch.label", {"Create git branch", "Создать ветку git"}},
      {"shortcut.task.createBranch.desc",
       {"A branch named after the task in the active repository.", "Ветка с именем задачи в активном репозитории."}},
      {"shortcut.task.moveProfile.label", {"Move to profile…", "Перенести в профиль…"}},
      {"shortcut.task.moveProfile.desc",
       {"The task, or the selection, to another profile: a picker, type to filter.",
        "Задачу или выбранные — в другой профиль: список, печатайте для фильтра."}},
      {"shortcut.selection.toggle.label", {"Select task", "Отметить задачу"}},
      {"shortcut.selection.toggle.desc",
       {"Add the task under the cursor to the selection, or take it out.", "Добавить задачу под курсором в выделение или убрать."}},
      {"shortcut.selection.range.label", {"Select a range", "Выделить диапазон"}},
      {"shortcut.selection.range.desc", {"Then j / k grow the selection from the cursor.", "Дальше j / k расширяют выделение от курсора."}},
      {"keymap.notice.kept", {"Tabless 0.8 has new keys; yours stay: %1.", "В Tabless 0.8 новые клавиши; ваши остались: %1."}},
      {"keymap.notice.moved", {"%1 is now %2. All keys: ?", "%1 теперь — %2; карта сочетаний — ?"}},
      {"keymap.was.view.board", {"Today; the board is g b", "«Сегодня»; доска — g b"}},
      {"keymap.was.view.timeline", {"Tasks; the list is g l", "«Задачи»; список — g l"}},
      {"keymap.was.view.week", {"Knowledge; the calendar is g c", "«Знания»; календарь — g c"}},
      {"keymap.was.view.month", {"your first view; the month is g c, then z m", "первый из «Моих видов»; месяц — g c, затем z m"}},
      {"keymap.was.view.archive",
       {"your second view; the archive is the is:archived filter", "второй из «Моих видов»; архив — фильтр is:archived"}},
      {"keymap.was.view.docs", {"your third view; docs are g d", "третий из «Моих видов»; доки — g d"}},
      {"keymap.was.view.notes", {"your fourth view; notes are g n", "четвёртый из «Моих видов»; заметки — g n"}},
      {"keymap.was.view.settings", {"your fifth view; settings are Ctrl ,", "пятый из «Моих видов»; настройки — Ctrl ,"}},
      {"keymap.was.task.openExternal", {"a new task below; open in the tracker is g x", "новая задача ниже; открыть в трекере — g x"}},
      {"keymap.was.board.collapseColumn", {"the start of z a, which folds the column", "начало z a — свернуть колонку"}},
      {"keymap.was.cal.today", {"the timer; back to today is 0", "таймер; к сегодня — 0"}},
      {"keymap.was.cal.goToDate", {"the start of g …; a date is : and the date", "начало g …; к дате — «:» и дата"}},
      {"keymap.was.savedView.1", {"free; your views are Ctrl 4… and g 1…", "свободна; «Мои виды» — Ctrl 4… и g 1…"}},
      {"keymap.was.savedView.2", {"free; your views are Ctrl 4… and g 1…", "свободна; «Мои виды» — Ctrl 4… и g 1…"}},
      {"keymap.was.savedView.3", {"free; your views are Ctrl 4… and g 1…", "свободна; «Мои виды» — Ctrl 4… и g 1…"}},
      {"hotkeys.common", {"Everywhere else %1 means “%2”. Take it for this anyway?", "Везде %1 — это «%2». Всё равно занять?"}},
      {"hotkeys.common.copy", {"copy", "копировать"}},
      {"hotkeys.common.paste", {"paste", "вставить"}},
      {"hotkeys.common.cut", {"cut", "вырезать"}},
      {"hotkeys.common.save", {"save", "сохранить"}},
      {"hotkeys.common.close", {"close", "закрыть"}},
      {"hotkeys.common.quit", {"quit", "выйти"}},
      {"hotkeys.common.selectAll", {"select all", "выделить всё"}},
      {"hotkeys.common.undo", {"undo", "отменить"}},
      {"hotkeys.common.erase", {"erase a character", "стереть символ"}},
      {"hotkeys.reserved.system", {"The system keeps this key", "Эту клавишу занимает система"}},
      {"hotkeys.prefixTaken", {"%1 cannot be bound: %2 (%3) starts the same way", "%1 нельзя назначить: так же начинается %2 («%3»)"}},
      {"hotkeys.reserved.navigation", {"Tab moves between fields in every dialog", "Tab переводит между полями во всех диалогах"}},
      {"shell.notice.keys",
       {"Tabless has a new sidebar: Ctrl+1 Today, Ctrl+2 Tasks, Ctrl+3 Knowledge, Ctrl+, Settings. Blocked and In review are in My views.",
        "В Tabless новый сайдбар: Ctrl+1 «Сегодня», Ctrl+2 «Задачи», Ctrl+3 «Знания», Ctrl+, «Настройки». «Заблокировано» и «На ревью» — в "
        "«Моих видах»."}},
      {"shell.notice.kept", {" Your own keys stay: %1.", " Ваши клавиши остались: %1."}},
      {"shortcut.view.board.label", {"Go to Board", "Перейти к доске"}},
      {"shortcut.view.board.desc", {"Kanban of the active profile.", "Канбан активного профиля."}},
      {"shortcut.view.timeline.label", {"Go to List", "Перейти к списку"}},
      {"shortcut.view.timeline.desc", {"Tasks as a list, grouped by date.", "Задачи списком, по сроку."}},
      {"shortcut.view.week.label", {"Go to Week", "Перейти к неделе"}},
      {"shortcut.view.week.desc", {"Seven-day planner.", "Семидневный планировщик."}},
      {"shortcut.view.month.label", {"Go to Month", "Перейти к месяцу"}},
      {"shortcut.view.month.desc", {"Month grid of due dates and events.", "Сетка месяца: сроки и события."}},
      {"shortcut.view.docs.label", {"Go to Docs", "Перейти в «Доки»"}},
      {"shortcut.view.docs.desc", {"Specs, links, snippets, contacts.", "Спеки, ссылки, сниппеты, контакты."}},
      {"shortcut.view.notes.label", {"Go to Notes", "Перейти к заметкам"}},
      {"shortcut.view.notes.desc", {"Markdown canvas of the active profile.", "Markdown-канвас активного профиля."}},
      {"shortcut.view.settings.label", {"Go to Settings", "Перейти к настройкам"}},
      {"shortcut.view.settings.desc",
       {"Full settings panel: profile, appearance, integrations.", "Полная панель настроек: профиль, внешний вид, интеграции."}},
      {"shortcut.zoom.in.label", {"Zoom in", "Увеличить интерфейс"}},
      {"shortcut.zoom.in.desc",
       {"Larger text and spacing, one step of Settings → Appearance → Scale.",
        "Крупнее текст и отступы — на шаг шкалы «Внешний вид → Масштаб»."}},
      {"shortcut.zoom.out.label", {"Zoom out", "Уменьшить интерфейс"}},
      {"shortcut.zoom.out.desc",
       {"Smaller text and spacing, one step of Settings → Appearance → Scale.",
        "Мельче текст и отступы — на шаг шкалы «Внешний вид → Масштаб»."}},
      {"shortcut.zoom.reset.label", {"Reset zoom", "Сбросить масштаб"}},
      {"shortcut.zoom.reset.desc", {"Back to the default size.", "Вернуть размер по умолчанию."}},
      {"shortcut.profile.next.label", {"Next profile", "Следующий профиль"}},
      {"shortcut.profile.next.desc", {"Cycle forward through profiles.", "Циклит по списку профилей вперёд."}},
      {"shortcut.profile.prev.label", {"Previous profile", "Предыдущий профиль"}},
      {"shortcut.profile.prev.desc", {"Cycle backward through profiles.", "Циклит по списку профилей назад."}},
      {"shortcut.profile.exportMd.label", {"Export profile to Markdown", "Экспорт профиля в Markdown"}},
      {"shortcut.profile.exportMd.desc",
       {"Puts a markdown summary of the active profile into the clipboard.", "Кладёт markdown-выжимку активного профиля в буфер."}},
      {"shortcut.profile.weeklyReport.label", {"Weekly shipped report", "Недельный отчёт"}},
      {"shortcut.sync.all.label", {"Sync everything", "Синхронизировать всё"}},
      {"shortcut.sync.all.desc", {"Pulls every connected tracker now.", "Забирает данные всех подключённых трекеров сейчас."}},
      {"shortcut.profile.weeklyReport.desc",
       {"Copies a Markdown report of tasks marked done in the last 7 days, with tracked time.",
        "Копирует Markdown-отчёт задач, завершённых за последние 7 дней, с учётом времени."}},
      {"shortcut.tweaks.open.label", {"Appearance settings", "Настройки внешнего вида"}},
      {"shortcut.tweaks.open.desc", {"Settings, scrolled to Appearance.", "Настройки на разделе «Внешний вид»."}},
      {"shortcut.hotkeys.open.label", {"Keyboard cheat sheet", "Шпаргалка клавиш"}},
      {"shortcut.hotkeys.open.desc", {"Every key by area, with a search.", "Все клавиши по разделам, с поиском."}},
      {"shortcut.hotkeys.edit.label", {"Change shortcuts…", "Изменить сочетания…"}},
      {"shortcut.hotkeys.edit.desc",
       {"Give any action a key of your own, two-key sequences included.",
        "Назначить любому действию свою клавишу, в том числе из двух нажатий."}},
      {"shortcut.redo.label", {"Redo", "Повторить"}},
      {"shortcut.redo.desc", {"Re-apply the operation Ctrl+Z reversed.", "Повторить отменённое действие."}},
      {"shortcut.undo.label", {"Undo", "Отменить"}},
      {"shortcut.undo.desc",
       {"Restore the last deleted task / event / profile.", "Восстановить последнюю удалённую задачу/событие/профиль."}},
      {"shortcut.search.focus.label", {"Section filter", "Фильтр раздела"}},
      {"shortcut.search.focus.desc",
       {"The filter line of the section; Esc goes back to the cursor.", "Строка фильтра раздела; Esc — обратно на курсор."}},
      {"shortcut.quick-capture.label", {"Quick-capture task", "Быстрое создание задачи"}},
      {"shortcut.quick-capture.desc",
       {"Open Quick-capture with on-the-fly date parsing.", "Открыть быстрый ввод с разбором даты на лету."}},
      {"shortcut.quick-capture-notes.label", {"Quick-capture note", "Быстрая заметка"}},
      {"shortcut.quick-capture-notes.desc",
       {"Open Quick-capture for Notes (appends to the Notes block).", "Открыть быстрый ввод заметки (дописывает в блок заметок)."}},
      {"shortcut.view.archive.label", {"Go to Archive", "Перейти к архиву"}},
      {"shortcut.view.archive.desc", {"The list with \"is:archived\", by month.", "Список с условием «в архиве», по месяцам."}},
      {"shortcut.people.open.label", {"Whom to write: everyone", "Кому написать: все"}},
      {"shortcut.people.open.desc",
       {"The people something is pending on, one of them open beside the list.",
        "Люди, от которых что-то ждёте; один открыт рядом со списком."}},
      {"shortcut.rail.toggle.label", {"Expand / collapse sidebar", "Развернуть/свернуть боковую панель"}},
      {"shortcut.rail.toggle.desc",
       {"Switch the left sidebar between labels and the icon-only rail.",
        "Переключить левую панель между подписями и узкой полосой иконок."}},
      {"shortcut.theme.toggle.label", {"Toggle light / dark", "Переключить светлую/тёмную"}},
      {"shortcut.theme.toggle.desc", {"Flip the app theme between dark and light.", "Переключить тему приложения между тёмной и светлой."}},
      {"shortcut.person.new.label", {"New contact", "Новый контакт"}},
      {"shortcut.person.new.desc", {"Add a person to the active profile.", "Добавить человека в активный профиль."}},
      {"shortcut.profile.new.label", {"New profile", "Новый профиль"}},
      {"shortcut.profile.new.desc", {"Create a new profile.", "Создать новый профиль."}},
      {"shortcut.selection.selectAll.label", {"Select all visible", "Выделить все видимые"}},
      {"shortcut.selection.selectAll.desc",
       {"Select every ticket visible in the current view.", "Выделить все тикеты, видимые в текущем виде."}},
      {"shortcut.selection.clearSel.label", {"Clear selection", "Снять выделение"}},
      {"shortcut.selection.clearSel.desc", {"Drop the current multi-selection.", "Сбросить текущее множественное выделение."}},
      {"shortcut.selection.deleteSel.label", {"Delete", "Удалить"}},
      {"shortcut.selection.deleteSel.desc",
       {"Delete the selected tasks, or the one under the cursor (undoable for 5s).",
        "Удалить выделенные задачи или задачу под курсором (отменимо 5 с)."}},
      // ---- Regions, column order, block length (APP-277 / APP-278) ----
      {"shortcut.region.next.label", {"Next region", "Следующая область"}},
      {"shortcut.region.next.desc",
       {"Sidebar, content, the side panel and the header in turn.", "По кругу: сайдбар, содержимое, боковая панель, шапка."}},
      {"shortcut.region.prev.label", {"Previous region", "Предыдущая область"}},
      {"shortcut.region.prev.desc", {"The same circle, backwards.", "Тот же круг в обратную сторону."}},
      {"shortcut.cal.longer.label", {"Block longer", "Блок длиннее"}},
      {"shortcut.cal.longer.desc",
       {"Make the task block or meeting under the cursor one grid step longer.",
        "Сделать блок задачи или встречу под курсором длиннее на шаг сетки."}},
      {"shortcut.cal.shorter.label", {"Block shorter", "Блок короче"}},
      {"shortcut.cal.shorter.desc",
       {"Make the task block or meeting under the cursor one grid step shorter.",
        "Сделать блок задачи или встречу под курсором короче на шаг сетки."}},
      // ---- Selection action bar ----
      {"selection.bar.count", {"%1 selected", "Выделено: %1"}},
      {"selection.bar.move", {"Move to…", "Переместить…"}},
      {"selection.bar.archive", {"Archive", "Архивировать"}},
      {"selection.bar.unarchive", {"Unarchive", "Из архива"}},
      {"selection.bar.delete", {"Delete", "Удалить"}},
      {"selection.bar.clear", {"Clear", "Снять"}},
      {"selection.toast.deleted", {"Tasks deleted: %1", "Удалено задач: %1"}},
      {"selection.toast.restored", {"Tasks restored: %1", "Восстановлено задач: %1"}},
      {"selection.toast.moved", {"Tasks moved: %1", "Перемещено задач: %1"}},
      {"selection.toast.archived", {"Tasks archived: %1", "Задач в архиве: %1"}},
      {"selection.toast.unarchived", {"Tasks unarchived: %1", "Задач возвращено из архива: %1"}},
      // ---- Notification copy ----
      {"notify.deadlineTitle", {"Due %1", "Срок %1"}},
      {"notify.blockNow", {"Time for “%1”", "Время для «%1»"}},
      {"notify.blockSoon", {"“%1” in %2 min", "«%1» через %2 мин"}},
      {"notify.blockBody", {"planned for %1–%2", "запланировано на %1–%2"}},
      {"notify.remindTitle", {"Reminder: %1", "Напоминание: %1"}},
      {"notify.remindBody", {"at %1", "на %1"}},
      {"notify.remindLate", {"at %1, while Tabless was closed", "на %1, пока Tabless был закрыт"}},
      {"notify.action.snooze", {"Snooze", "Отложить"}},
      {"remind.set", {"Reminder at %1", "Напомню в %1"}},
      {"remind.setLead", {"Reminder %1 min before", "Напомню за %1 мин"}},
      {"remind.cleared", {"Reminder removed", "Напоминание снято"}},
      {"remind.noTime",
       {"“%1” has no time yet: give it one, or say when to remind", "У «%1» нет времени: задайте его или скажите, когда напомнить"}},
      {"remind.unknown", {"Can’t read “%1” as a time", "Не понял «%1» как время"}},
      {"undo.remind", {"Reminder for %1", "Напоминание для %1"}},
      {"notify.action.snooze15", {"In 15 min", "Через 15 мин"}},
      {"notify.action.window", {"Next free window", "В ближайшее окно"}},
      {"notify.overdueTitle", {"Overdue %1", "Просрочено %1"}},
      {"notify.deadlineWhen.overdue", {"just now", "только что"}},
      {"notify.deadlineWhen.overdueH", {"by %1 h", "на %1 ч"}},
      {"notify.deadlineWhen.h1", {"in 1 hour", "через час"}},
      {"notify.deadlineWhen.hN", {"in %1 h", "через %1 ч"}},
      {"notify.standupTitle", {"Standup soon", "Скоро стендап"}},
      {"notify.standupBody", {"In %1 min", "Через %1 мин"}},
  };
  return table;
}

}  // namespace

namespace {
// Notes where every mouse press and touch begins, before Qt Quick delivers
// it (see AppController::lastPressGlobalPos).
class PressTracker : public QObject {
 public:
  PressTracker(QPointF* out, QObject* parent) : QObject(parent), m_out(out) {
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if(event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin) {
      auto* pointer = static_cast<QPointerEvent*>(event);
      if(pointer->pointCount() > 0) {
        *m_out = pointer->point(0).globalPosition();
      }
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  QPointF* m_out;
};
}  // namespace

AppController::AppController(QObject* parent) :
    QObject(parent),
    m_today(QDate::currentDate()),
    m_selectedDate(m_today),
    m_automationTimer(new QTimer(this)),
    m_saveTimer(new QTimer(this)),
    m_chrono(std::make_unique<heap::chrono::ChronoParser>(QLocale())) {
  if(QCoreApplication* const app = QCoreApplication::instance()) {
    app->installEventFilter(new PressTracker(&m_lastPressGlobal, this));
  }
  m_saveTimer->setSingleShot(true);
  m_saveTimer->setInterval(300);
  connect(m_saveTimer, &QTimer::timeout, this, &AppController::saveStateNow);
  m_saver = std::make_unique<heap::storage::AsyncSaver>();
  connect(m_saver.get(), &heap::storage::AsyncSaver::finished, this, &AppController::onSaveFinished);
  m_saveRetryTimer = new QTimer(this);
  m_saveRetryTimer->setSingleShot(true);
  connect(m_saveRetryTimer, &QTimer::timeout, this, &AppController::saveStateNow);
  // While state.json is locked (read-only session), look again every few
  // seconds; the first time it opens and nothing was typed meanwhile, load it.
  m_storageRetryTimer = new QTimer(this);
  m_storageRetryTimer->setInterval(5000);
  connect(m_storageRetryTimer, &QTimer::timeout, this, [this]() {
    if(m_storageState != QLatin1String("unreadable")) {
      m_storageRetryTimer->stop();
      return;
    }
    if(m_editsWhileBlocked) {
      return;  // reloading would discard them; the banner's Retry decides
    }
    if(heap::storage::readWithRetry(stateFilePath(), {}).kind != heap::storage::ReadResult::Unreadable) {
      reloadStateFromDisk();
      if(m_storageState == QLatin1String("ok")) {
        emit toast(tr_("storage.reopened"));
      }
    }
  });

  m_activePeople.setSourceModel(&m_people);
  trackAttachmentRefsInText();

  // The event log keeps what went wrong or was refused (APP-187), and what
  // can still be undone. Places that know more (which tasks, where to look)
  // log it themselves and mute this.
  connect(this, &AppController::toast, this, [this](const QString& message, const QString& kind) {
    if(m_eventLogMuted || (kind != QLatin1String("error") && kind != QLatin1String("warning"))) {
      return;
    }
    logEvent(kind, message);
  });
  connect(this, &AppController::undoableToast, this, [this](const QString& message, int) {
    logEvent(QStringLiteral("undo"), message);
  });
  // A toast with an action leaves the screen too (APP-225): what it said is
  // kept, and an entry about tasks opens them.
  connect(this, &AppController::settingsReset, this, [this](const QString& message) {
    logEvent(QStringLiteral("undo"), message);
  });
  connect(this,
          &AppController::safetyNotice,
          this,
          [this](const QString&, const QString& title, const QString& body, const QStringList& taskIds) {
            logEvent(QStringLiteral("info"), title.isEmpty() ? body : title + QStringLiteral(" · ") + body, taskIds);
          });
  // A reminder that was shown, for the log's "reminders" tab (DG-122).
  connect(this, &AppController::reminderToast, this, [this](const QString&, const QString& message) {
    logEvent(QStringLiteral("reminder"), message);
  });
  connect(this, &AppController::updateAvailable, this, [this](const QString& version, const QString&) {
    // The tag is "v0.9.0"; the sentence reads "Version 0.9.0 is ready".
    const QString bare = version.startsWith(QLatin1Char('v')) ? version.mid(1) : version;
    logEvent(QStringLiteral("info"), tr_("update.available").arg(bare), {}, QStringLiteral("settings:about"));
  });
  connect(this, &AppController::updateReadyToInstall, this, [this](const QString& version, const QString&) {
    logEvent(QStringLiteral("info"), tr_("update.ready").arg(version), {}, QStringLiteral("settings:about"));
  });
  // A refused move (APP-204) and the one-time "writes are opt-in" notice
  // (APP-243) carry actions too.
  connect(this, &AppController::trackerReadOnlyMove, this, [this](const QString& taskId, const QString& message, const QString&) {
    logEvent(QStringLiteral("warning"), message, {taskId});
  });
  connect(this, &AppController::trackerWriteNotice, this, [this](const QString& message) {
    logEvent(QStringLiteral("info"), message, {}, QStringLiteral("settings:integrations"));
  });

  m_automationTimer->setInterval(60 * 1000);
  connect(m_automationTimer, &QTimer::timeout, this, &AppController::runAutomation);

  // "Today" is not a constant: the app stays open across midnight, and a
  // laptop resumes on another day or in another zone.
  m_midnightTimer = new QTimer(this);
  m_midnightTimer->setSingleShot(true);
  connect(m_midnightTimer, &QTimer::timeout, this, [this]() {
    refreshToday();
    armMidnightTimer();
  });
  armMidnightTimer();
  // qApp would cast to QApplication, which the QML tests' app is not.
  if(auto* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    connect(gui, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
      if(state == Qt::ApplicationActive) {
        refreshToday();
        armMidnightTimer();
      }
      // Live updates: coming back to the window after a while pulls the
      // trackers, so the board is not a stale one the moment it is looked at.
      const QDateTime now = QDateTime::currentDateTime();
      if(state == Qt::ApplicationActive) {
        m_livePresence = heap::integrations::live::Presence::Active;
        if(!QStandardPaths::isTestModeEnabled() && heap::integrations::live::returnedAfterAway(m_liveLeftAt, now)) {
          liveCatchUpAt(now);
        }
        m_liveLeftAt = QDateTime();
      } else if(!m_liveLeftAt.isValid()) {
        m_liveLeftAt = now;
      }
    });
  }

  // (Legacy QSystemTrayIcon creation removed — the notification backend
  // owns its own tray icon on Windows/macOS via the tray fallback. Having
  // two would show duplicate icons in the system tray.)

  // Native notification backend — Linux uses org.freedesktop.Notifications
  // (with real action buttons); Windows/macOS fall back to the legacy tray
  // balloon path. See src/notify/NotificationCenter.h for the contract.
  // A command-line run (`heap add`, no window) has no tray to put an icon in.
  if(!s_headless) {
    m_notifier = heap::notify::NotificationCenter::create(this);
    connect(m_notifier.get(), &heap::notify::NotificationCenter::actionInvoked, this, &AppController::onNotifierAction);
    connect(m_notifier.get(), &heap::notify::NotificationCenter::activated, this, &AppController::onNotifierActivated);
    // The tray backend (Windows/macOS) doubles as the app's presence when the
    // window is hidden: clicking the icon or its "Show" entry restores the
    // window, and "Quit" exits for real. Forwarded to QML / the event loop.
    connect(m_notifier.get(), &heap::notify::NotificationCenter::showWindowRequested, this, &AppController::showWindowRequested);
    connect(m_notifier.get(), &heap::notify::NotificationCenter::quitRequested, this, []() {
      QCoreApplication::quit();
    });
    connect(m_notifier.get(), &heap::notify::NotificationCenter::trayMenuAboutToShow, this, &AppController::refreshTray);
    connect(m_notifier.get(), &heap::notify::NotificationCenter::trayItemTriggered, this, &AppController::onTrayItem);
    connect(this, &AppController::languageChanged, this, &AppController::refreshTray);
    // The tooltip names the capture key; a rebind shows at once.
    connect(this, &AppController::shortcutsChanged, this, &AppController::refreshTray);
  }

  // Route notification(...) → native toast + in-app toast bar, respecting
  // quiet hours and the user's `notifications.desktopNotif` / `soundOnPing`
  // opt-outs. Kept as a signal so existing call-sites (`emit
  // notification(...)`) keep working — the lambda just forwards to the
  // NotificationCenter without action buttons (it carries no task id).
  connect(this,
          &AppController::notification,
          this,
          [this](const QString& title, const QString& body, const QString& kind, const QString& routeId) {
            if(s_headless) {
              return;  // nobody to show it to; a held one would be saved as pending
            }
            // Focus mode holds it until the user asks (APP-160).
            if(holdForImmersion({.title = title, .body = body, .kind = kind, .taskId = QString()})) {
              return;
            }
            // Quiet hours hold a notification until they end rather than dropping
            // it. A meeting or the standup is an appointment and goes through.
            const bool appointment = kind == QStringLiteral("meeting") || kind == QStringLiteral("standup");
            if(!appointment && inQuietHours(QDateTime::currentDateTime())) {
              holdNotification({title, body, kind, QString()});
              return;
            }
            const QVariantMap notif = settingsMap().value("notifications").toMap();
            // Only raise an OS toast when the window is NOT focused. When the app is
            // active the in-app Toast bar (emitted below) already surfaces the message;
            // showing both is the "notification appears twice on Windows" bug (HEAP-47)
            // — one styled in-app toast plus one plain system balloon.
            const bool appActive = QGuiApplication::applicationState() == Qt::ApplicationActive;
            if(!routeId.isEmpty()) {
              // Remembered so a snooze brings the same words back (APP-155).
              ShownReminder& shown = m_shownReminders[routeId];
              shown.title = title;
              shown.body = body;
              shown.kind = kind;
            }
            if(notif.value("desktopNotif", true).toBool() && m_notifier && !appActive) {
              heap::notify::Notification n;
              n.id = routeId.isEmpty() ? QStringLiteral("info:") + QString::number(QDateTime::currentMSecsSinceEpoch()) : routeId;
              n.title = title;
              n.body = body;
              n.iconPath = QStringLiteral(":/brand/tabless/tabless-icon.svg");
              n.category = kind;
              if(!routeId.isEmpty() && m_notifier->supportsActions()) {
                const bool canJoin =
                    kind == QStringLiteral("meeting") && !meetingJoinUrl(heap::notify::parseRoutingId(routeId).second).isEmpty();
                n.actions = reminderActions(kind, canJoin);
              }
              m_notifier->post(n);
            }
            if(notif.value("soundOnPing", false).toBool()) {
              QApplication::beep();
            }
            // The title is what says why ("Starting now", "Deadline in 1 hour"); the
            // in-app toast used to show only the body, a bare task title.
            emit toast(title.isEmpty() ? body : title + QStringLiteral(" · ") + body);
          });

  // Invalidate the status-count cache from the model's own signals, so every
  // mutation path is covered without each one having to remember.
  const auto dropStatusCounts = [this]() {
    m_statusCountsDirty = true;
    emit statusCountsChanged();
  };
  connect(&m_tasks, &QAbstractItemModel::modelReset, this, dropStatusCounts);
  connect(&m_tasks, &QAbstractItemModel::rowsInserted, this, dropStatusCounts);
  connect(&m_tasks, &QAbstractItemModel::rowsRemoved, this, dropStatusCounts);
  connect(&m_tasks, &QAbstractItemModel::dataChanged, this, dropStatusCounts);
  const auto queueTaskTitles = [this]() {
    if(m_taskTitlesQueued) {
      return;
    }
    m_taskTitlesQueued = true;
    QMetaObject::invokeMethod(this, &AppController::refreshTaskTitles, Qt::QueuedConnection);
  };
  connect(&m_tasks, &QAbstractItemModel::modelReset, this, queueTaskTitles);
  connect(&m_tasks, &QAbstractItemModel::rowsInserted, this, queueTaskTitles);
  connect(&m_tasks, &QAbstractItemModel::rowsRemoved, this, queueTaskTitles);
  connect(&m_tasks, &QAbstractItemModel::dataChanged, this, queueTaskTitles);
  // A column's stage changes every ring of its tasks (taskStages).
  connect(this, &AppController::statusesChanged, this, queueTaskTitles);
  // Status moves for the Monday recap, noticed the same way: from the model,
  // so a drag, the editor, a bulk move, a sync and an undo are all seen.
  connect(&m_tasks, &QAbstractItemModel::modelReset, this, [this]() {
    rememberStatuses();
  });
  connect(&m_tasks, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex&, int first, int last) {
    for(int r = first; r <= last && r < m_tasks.items().size(); ++r) {
      m_knownStatus.insert(m_tasks.items().at(r).id, m_tasks.items().at(r).status);
    }
  });
  connect(&m_tasks,
          &QAbstractItemModel::dataChanged,
          this,
          [this](const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles) {
            if(roles.isEmpty() || roles.contains(TaskModel::StatusRole)) {
              noteStatusMoves(topLeft.row(), bottomRight.row());
            }
          });
  wireSavedViews();

  // A fresh install speaks the system's language (a saved one overrides it in
  // loadStateOnStart). Test runs stay on English whatever the machine says,
  // so suites that read strings do not depend on the developer's locale.
  if(!QStandardPaths::isTestModeEnabled()) {
    m_language = heap::text::uiLanguageFor(QLocale::system().uiLanguages());
  }
  // The pseudo-locale (APP-189) is stretched English: C++-made dates and
  // labels speak English with it, and the profile keeps its own language.
  if(pseudoLocale()) {
    m_languageUnderPseudo = m_language;
    m_language = QStringLiteral("en");
  }

  // The QML test suite was written against the Vim keymap and pins it for a
  // fresh test profile; a test about Classic switches to it on its own.
  if(QStandardPaths::isTestModeEnabled()) {
    const QString pinned = qEnvironmentVariable("HEAP_TEST_KEYMAP");
    if(pinned == QLatin1String("vim") || pinned == QLatin1String("classic")) {
      m_keymapScheme = pinned;
    }
  }
  seedShortcutCatalog();

  // Task history (APP-165): every single-task change the model sees, from
  // whichever path made it, minus loads. A tracker pull marks its own.
  m_tasks.setChangeObserver([this](const Task* before, const Task& after) {
    recordTaskChange(before, after);
    // A checklist item that became this card ticks with its Done (APP-236).
    followCardDone_(before, after);
  });

  // Re-localize shortcut catalog when language flips so the Settings →
  // Shortcuts list and HotkeysPanel labels update in place.
  connect(this, &AppController::languageChanged, this, [this]() {
    seedShortcutCatalog();
    emit updateStatusChanged();
    // The storage line was worded when it was raised — often at load, before
    // the saved language was known (R2-039). It follows the language now.
    recomposeStorageMessage();
    emit integrationHealthChanged();
  });

  // Unclean-exit marker (R3-018): written when a window session starts and
  // removed when it ends normally, so finding it here means the last one
  // crashed or was killed. The CLI and the test runner keep none.
  if(!s_headless && !QStandardPaths::isTestModeEnabled()) {
    m_sessionMarkerPath = heap::paths::dataDir() + QStringLiteral("/session.open");
    if(QFile::exists(m_sessionMarkerPath)) {
      m_lastExitUnclean = true;
      m_lastSaveTime = QFileInfo(stateFilePath()).lastModified();
    }
    QFile marker(m_sessionMarkerPath);
    if(marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      marker.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
    }
  }

  loadStateOnStart();
  loadSentReminders();
  loadSnoozes();
  m_automationTimer->start();

  // An unwritable data folder (a --data-dir under Program Files, a read-only
  // share) used to look like a normal first run that silently saved nothing.
  {
    QString why;
    if(m_storageState == QLatin1String("ok") && !heap::storage::probeWritableDir(heap::paths::dataDir(), &why)) {
      qWarning("data directory is not writable: %s", qUtf8Printable(why));
      setStorageStateT(QStringLiteral("writeFailed"), {{QStringLiteral("storage.dirUnwritable"), {why}}});
    }
  }

  // ---- Global hotkeys (OS-level Quick-capture) ----
  // Registered after loadStateOnStart() so any user rebind of the capture
  // sequences is already applied. On non-Windows platforms this is a no-op and
  // the app relies on the in-app QML shortcuts instead.
  // A command-line run neither grabs the capture hotkeys nor watches repos:
  // the window that owns both may start a moment later.
  if(!s_headless) {
    m_globalHotkey = heap::platform::GlobalHotkey::create(this);
    connect(m_globalHotkey.get(), &heap::platform::GlobalHotkey::activated, this, &AppController::onGlobalHotkey);
    registerGlobalHotkeys();

    // ---- Git watcher ----
    m_gitWatcher = std::make_unique<heap::git::GitWatcher>(this);
    connect(m_gitWatcher.get(), &heap::git::GitWatcher::branchChanged, this, &AppController::onGitBranchChanged);
    connect(m_gitWatcher.get(), &heap::git::GitWatcher::repoStateUpdated, this, &AppController::onGitRepoState);
    connect(m_gitWatcher.get(), &heap::git::GitWatcher::commitsUpdated, this, &AppController::onGitCommits);
    connect(m_gitWatcher.get(), &heap::git::GitWatcher::workingTreeChecked, this, &AppController::onWorkingTreeChecked);
    connect(m_gitWatcher.get(), &heap::git::GitWatcher::codeOwnersUpdated, this, &AppController::onCodeOwnersUpdated);
    // A repo on a dev server has no file watcher: coming back to the window
    // is when a checkout made there in a terminal should show.
    if(auto* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
      connect(gui, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if(state == Qt::ApplicationActive && m_gitWatcher) {
          m_gitWatcher->pollRemoteNow();
        }
      });
    }
    // "Waiting 2d" counts calendar days, so it moves on at midnight (APP-158).
    connect(this, &AppController::todayChanged, this, &AppController::waitingOnChanged);
    connect(
        m_gitWatcher.get(), &heap::git::GitWatcher::prInfoUpdated, this, [this](const QString&, const QString& br, const QVariantMap& pr) {
          const heap::git::BranchTaskMatcher m(collectPrefixes());
          const auto mr = m.extract(br);
          if(!mr.matched) {
            return;
          }
          QVariantMap entry;
          entry["prState"] = pr.value("state");
          entry["prNumber"] = pr.value("number");
          entry["prUrl"] = pr.value("url");
          entry["prMove"] = pr.value("move");
          entry["prMoveReason"] = pr.value("moveReason");
          entry["prChecks"] = pr.value("checks");
          m_tasks.setGitInfoForId(taskIdForBranchMatch(mr.taskId), entry);
        });
    applyGitSettingsFromMap(settingsMap().value("git").toMap());
    connect(this, &AppController::appSettingsJsonChanged, this, [this]() {
      applyGitSettingsFromMap(settingsMap().value("git").toMap());
    });
  }  // !s_headless

  // ---- Auto-update (HEAP-63) ----
  m_featureBuild = new heap::update::FeatureBuild(this);
  m_updater = std::make_unique<heap::update::Updater>(appVersion(), this);
  connect(m_updater.get(), &heap::update::Updater::updateAvailable, this, [this](const QString& version, const QString& url) {
    m_latestReleaseUrl = url;
    m_updateStatus = QStringLiteral("update.available");
    m_updateStatusArg = version;
    emit updateStatusChanged();
    emit updateAvailable(version, url);
  });
  connect(m_updater.get(), &heap::update::Updater::upToDate, this, [this](const QString&) {
    m_updateStatus = QStringLiteral("update.upToDate");
    m_updateStatusArg.clear();
    emit updateStatusChanged();
  });
  connect(m_updater.get(), &heap::update::Updater::checkFailed, this, [this](int kind, const QString& error) {
    qWarning() << "update check failed:" << kind << error;
    // No network, or GitHub's rate limit, is not the check failing: say it
    // could not check, and why (PLAT-27). Stored as keys so a language
    // switch repaints it.
    using heap::update::CheckFailure;
    if(kind == static_cast<int>(CheckFailure::RateLimited)) {
      m_updateStatus = QStringLiteral("update.couldNotCheck");
      m_updateStatusArg = QStringLiteral("update.rateLimited");
    } else if(kind == static_cast<int>(CheckFailure::Offline)) {
      m_updateStatus = QStringLiteral("update.couldNotCheck");
      m_updateStatusArg = QStringLiteral("update.offline");
    } else {
      m_updateStatus = QStringLiteral("update.failed");
      m_updateStatusArg.clear();
    }
    emit updateStatusChanged();
  });
  // ---- In-app update (APP-125) ----
  m_packageKind = static_cast<int>(heap::update::detectPackageKind(heap::update::currentPackageEnv()));
  m_updatesFromStore = heap::update::runningInFlatpak();
  connect(m_updater.get(), &heap::update::Updater::downloadProgress, this, [this](qint64 received, qint64 total) {
    m_updateProgress = total > 0 ? static_cast<double>(received) / static_cast<double>(total) : 0.0;
    emit updateProgressChanged();
    if(total > 0 && received >= total && m_updatePhase == QLatin1String("downloading")) {
      setUpdatePhase(QStringLiteral("verifying"), QStringLiteral("update.verifying"));
    }
  });
  connect(m_updater.get(), &heap::update::Updater::downloadVerified, this, [this](const QString& path, const QString& sha) {
    m_updatePackage = path;
    m_updateSha256 = sha;
    const QString version = m_updater->latestTag();
    // The status names the version and the checksum, so the user sees what
    // was checked, not just that something was.
    setUpdatePhase(QStringLiteral("ready"), QStringLiteral("update.verified"), version);
    emit updateReadyToInstall(version, sha);
  });
  connect(m_updater.get(), &heap::update::Updater::downloadFailed, this, [this](int kind, const QString& error) {
    qWarning() << "update download failed:" << kind << error;
    using heap::update::DownloadFailure;
    if(kind == static_cast<int>(DownloadFailure::ChecksumMismatch)) {
      setUpdatePhase(QStringLiteral("error"), QStringLiteral("update.mismatch"));
    } else if(kind == static_cast<int>(DownloadFailure::NoChecksum)) {
      setUpdatePhase(QStringLiteral("error"), QStringLiteral("update.noChecksum"));
    } else {
      setUpdatePhase(QStringLiteral("error"), QStringLiteral("update.downloadFailed"), error);
    }
    emit toast(updateStatus(), QStringLiteral("warning"));
  });
  // What the update an earlier run started came to. Left for the window to
  // report when this is a command-line run.
  if(!s_headless) {
    QTimer::singleShot(1500, this, [this]() {
      const heap::update::InstallOutcome outcome = heap::update::takeInstallOutcome();
      if(!outcome.present) {
        return;
      }
      if(outcome.ok) {
        emit toast(tr_("update.installed").arg(appVersion()));
      } else {
        emit toast(tr_("update.installFailed").arg(outcome.error), QStringLiteral("warning"));
      }
    });

    // Opt-out background check shortly after startup (never auto-downloads). The
    // delay lets settings load and the QML toast bar come up first.
    QTimer::singleShot(3000, this, [this]() {
      // A channel build is replaced by the next one, not updated (APP-309).
      // A Flatpak's updates come from its store, so it never checks (Flathub).
      if(!heap::brand::isChannelBuild() && !m_updatesFromStore &&
         settingsMap().value("updates").toMap().value("autoCheck", true).toBool()) {
        checkForUpdates();
      }
    });
    // Writes to trackers became opt-in (APP-243): say so once to whoever had
    // a tracker connected, after the toast bar is up.
    QTimer::singleShot(3500, this, [this]() {
      const QString notice = trackerWriteNoticeOnce();
      if(!notice.isEmpty()) {
        emit trackerWriteNotice(notice);
      }
    });
  }  // !s_headless

  // ---- Tracker sync (HEAP-74/75) ----
  m_secretStore = new heap::integrations::SecretStore(this);
  // The keychain refused a sign-in (R2-040): the token lives only in memory
  // until the user picks the file or a retry, so the card asks.
  connect(
      m_secretStore, &heap::integrations::SecretStore::keychainWriteFailed, this, [this](const QString& providerId, const QString& error) {
        const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
        m_keychainProblem = QVariantMap{
            {QStringLiteral("provider"), providerId},
            {QStringLiteral("name"), d ? d->displayName : providerId},
            {QStringLiteral("error"), error},
        };
        emit keychainProblemChanged();
      });
  // Connecting or disconnecting a tracker changes the sources the sync
  // indicator lists.
  connect(this, &AppController::appSettingsJsonChanged, this, &AppController::integrationHealthChanged);
  m_syncTimer = new QTimer(this);
  m_syncTimer->setSingleShot(false);
  connect(m_syncTimer, &QTimer::timeout, this, [this]() {
    autoSyncTickAt(QDateTime::currentDateTime());
  });
  loadLastTrackerSync();
  // A command-line run connects no tracker and fetches no calendar: a status
  // it changes is queued for the window's next sync (queueTrackerPush), the
  // same as a move made while disconnected.
  if(!s_headless) {
    // Move any legacy plaintext tokens out of state.json, then load the keychain.
    migrateLegacySecrets();
    QVector<QPair<QString, QString>> secretKeys;
    for(const heap::integrations::ProviderDescriptor& d : heap::integrations::providerCatalog()) {
      for(const QString& f : d.secretKeys) {
        secretKeys.append({d.id, f});
      }
      // The refresh token is written by the OAuth flow rather than by a card
      // field, so it is not in secretKeys — but it still has to come back from
      // the keychain, or the session ends at the first token expiry.
      if(d.oauth.supported) {
        secretKeys.append({d.id, QStringLiteral("refreshToken")});
      }
    }
    // Providers that need a secret build after the async keychain read completes.
    m_secretStore->load(secretKeys, [this]() {
      applyIntegrationSettings();
      emit integrationSecretsChanged();  // the Settings fields were rendered empty
    });
    applyIntegrationSettings();
    connect(this, &AppController::appSettingsJsonChanged, this, [this]() {
      applyIntegrationSettings();
      applyCalendarSubscriptions();
    });
    // Calendar subscriptions (APP-118): a minute tick fetches whichever is due.
    m_calTimer = new QTimer(this);
    m_calTimer->setInterval(60 * 1000);
    connect(m_calTimer, &QTimer::timeout, this, &AppController::applyCalendarSubscriptions);
    m_calTimer->start();
    // Outlook's own change events, debounced into one read (items 13–14 of
    // the 2026-10-19 dogfood).
    m_outlookDebounce = new QTimer(this);
    m_outlookDebounce->setSingleShot(true);
    connect(m_outlookDebounce, &QTimer::timeout, this, [this]() {
      m_outlookFirstChange = QDateTime();
      for(const QVariant& v : calendarSubscriptionSettings()) {
        const QVariantMap m = v.toMap();
        if(m.value(QStringLiteral("kind")).toString() == QStringLiteral("outlook")) {
          const QString id = m.value(QStringLiteral("id")).toString();
          if(m_outlookThread != nullptr || m_calSubState.value(id).busy) {
            m_outlookReadAgain = true;  // the read under way may predate the change
          } else {
            fetchCalendarSubscription(id);
          }
        }
      }
    });
    applyCalendarSubscriptions();

    // Live tracker updates (integrations/LiveSync.h): a short tick decides
    // whether a tracker is asked "what changed?", and its own lateness says
    // the machine slept.
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(heap::integrations::live::kTickSecs * 1000);
    connect(m_liveTimer, &QTimer::timeout, this, [this]() {
      namespace live = heap::integrations::live;
      auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
      if(app != nullptr && app->applicationState() == Qt::ApplicationActive) {
        m_livePresence = live::Presence::Active;
      } else {
        const QWindowList windows = QGuiApplication::topLevelWindows();
        const bool shown = std::any_of(windows.cbegin(), windows.cend(), [](const QWindow* w) {
          return w->isVisible() && !w->windowStates().testFlag(Qt::WindowMinimized);
        });
        m_livePresence = shown ? live::Presence::Background : live::Presence::Hidden;
      }
      liveTickAt(QDateTime::currentDateTime());
    });
    // Tests drive liveTickAt/liveCatchUpAt themselves: a tick landing late under
    // a loaded test run reads as a wake-up and pulled in the middle of an
    // unrelated test.
    if(!QStandardPaths::isTestModeEnabled()) {
      m_liveTimer->start();
    }
    // The network coming back is the moment a board is most likely stale.
    if(!QStandardPaths::isTestModeEnabled() && QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
      if(QNetworkInformation* net = QNetworkInformation::instance()) {
        connect(net, &QNetworkInformation::reachabilityChanged, this, [this](QNetworkInformation::Reachability reachability) {
          if(reachability == QNetworkInformation::Reachability::Online) {
            liveCatchUpAt(QDateTime::currentDateTime());
          }
        });
      }
    }
  }  // !s_headless
  connect(this, &AppController::activeProfileChanged, this, [this]() {
    if(m_gitWatcher) {
      m_gitWatcher->setPrefixes(collectPrefixes());
      refreshFocusedTaskId();
    }
  });

  // Fresh install or unreadable state — one empty profile of the person's
  // own (APP-271); the example is a separate profile offered from Today.
  if(m_profiles.isEmpty() && s_headless) {
    // `heap add` before heap was ever opened: the task goes into an empty
    // workspace, not into the demo, which is the window's first-run offer.
    Profile p = makeStartingProfile(tr_("profile.personal"), QString());
    p.id = QStringLiteral("default");
    m_profiles.push_back(p);
    m_activeProfileId = p.id;
    applyProfileToModels(p);
  } else if(m_profiles.isEmpty()) {
    seedStartingWorkspace();
  }

  // ── Person-id migration ──
  // Upgrade legacy "p-XXXXXXXX" ids (UUID-short, pre-slug scheme) AND the
  // sample-data ids ("p1".."p6") to slug form ("e.zaharov"). CalEvent.
  // attendees is a freeform string of names — no cross-references to fix.
  {
    bool changed = false;
    for(Profile& pr : m_profiles) {
      changed = slugLegacyPersonIds(pr) || changed;
    }
    if(changed) {
      for(const Profile& pr : m_profiles) {
        if(pr.id == m_activeProfileId) {
          applyProfileToModels(pr);
          break;
        }
      }
      scheduleSave();
    }
  }

  // Start-up's own bookkeeping saves are not user edits: a read-only session
  // opened on a locked file may still reopen it by itself.
  m_editsWhileBlocked = false;
}

AppController::~AppController() {
  // A group QML never closed records nothing: the models are going away.
  for(auto& group : m_undoGroups) {
    group->abandon();
  }
  m_undoGroups.clear();
  // Comments read in the last two seconds are not lost to the debounce.
  if(m_commentCacheSaveTimer != nullptr && m_commentCacheSaveTimer->isActive()) {
    writeCommentCache();
  }
  // The Outlook watch lets go of Outlook on its own thread first — for two
  // seconds at most: a hung Outlook does not hold the app open.
  heap::cal::OutlookWatch::retire(m_outlookWatch);
  m_outlookWatch = nullptr;
  // An Outlook read still running would call back into a dead controller.
  if(m_outlookThread != nullptr) {
    m_outlookThread->wait();
    delete m_outlookThread;
    m_outlookThread = nullptr;
  }
  flushSave();
  if(!m_sessionMarkerPath.isEmpty()) {
    QFile::remove(m_sessionMarkerPath);
  }
}

void AppController::dismissUncleanExit() {
  if(m_lastExitUnclean) {
    m_lastExitUnclean = false;
    emit lastExitUncleanChanged();
  }
}

void AppController::simulateUncleanExitForTest(const QDateTime& savedAt) {
  if(!QStandardPaths::isTestModeEnabled()) {
    return;
  }
  m_lastExitUnclean = true;
  m_lastSaveTime = savedAt;
  emit lastExitUncleanChanged();
}

void AppController::flushSave() {
  if(m_saveTimer && m_saveTimer->isActive()) {
    m_saveTimer->stop();
    saveStateNow();
  }
  // A save may be running on the worker already; flushing means on disk.
  if(m_saver) {
    m_saver->flush();
  }
}

void AppController::setSelectedDate(const QDate& d) {
  if(d == m_selectedDate) {
    return;
  }
  m_selectedDate = d;
  emit selectedDateChanged();
}

void AppController::refreshToday(const QDate& current) {
  if(!current.isValid() || current == m_today) {
    return;
  }
  const QDate was = m_today;
  m_today = current;
  // A calendar left on "today" stays on today. One the user paged elsewhere
  // stays where they put it.
  if(m_selectedDate == was) {
    m_selectedDate = current;
    emit selectedDateChanged();
  }
  emit todayChanged();
}

void AppController::armMidnightTimer() {
  if(m_midnightTimer == nullptr) {
    return;
  }
  const QDateTime now = QDateTime::currentDateTime();
  // A second past midnight, so the date read then is the new one. Capped, so
  // a clock that jumps (sleep, a manual change) is caught within the hour
  // even if the timer's own deadline was computed against the old clock.
  const QDateTime next(now.date().addDays(1), QTime(0, 0, 1));
  const qint64 ms = qBound<qint64>(1000, now.msecsTo(next), 60LL * 60 * 1000);
  m_midnightTimer->start(static_cast<int>(ms));
}

void AppController::setTheme(const QString& t) {
  if(t == m_theme) {
    return;
  }
  m_theme = t;
  emit themeChanged();
  scheduleSave();
}

void AppController::setDensity(const QString& d) {
  if(d == m_density) {
    return;
  }
  m_density = d;
  emit densityChanged();
  scheduleSave();
}

void AppController::setLanguage(const QString& v) {
  const QString norm = (v == "ru") ? QStringLiteral("ru") : QStringLiteral("en");
  if(norm == m_language) {
    return;
  }
  m_language = norm;
  emit languageChanged();
  // Re-emitting selectedDateChanged here used to refresh the few labels built
  // by shortDate()/humanDate() — and also rebuilt every calendar view from
  // scratch, which is most of what made a language switch take seconds on a
  // large profile. Those labels read I18n.lang in their bindings instead.
  scheduleSave();
}

QString AppController::tr_(const QString& key) const {
  return trIn_(key, m_language == QStringLiteral("ru"));
}

QString AppController::trIn_(const QString& key, bool ru) const {
  const auto& table = i18nTable();
  const auto it = table.constFind(key);
  if(it == table.constEnd()) {
    // The integrations keep their own strings (audit INT-7).
    // …and so do saved views.
    QString own = heap::integrations::integrationText(key, ru);
    if(own.isNull()) {
      own = heap::savedviews::text(key, ru);
    }
    if(own.isNull()) {
      own = heap::safety::text(key, ru);
    }
    return own.isNull() ? key : own;
  }
  return QString::fromUtf8(ru ? it->ru : it->en);
}

void AppController::setCurrentView(const QString& requested) {
  // An unknown name (a --view typo, a stale binding) lands on the board rather
  // than on a blank content area that would then be saved and come back.
  const QString v = heap::views::isKnown(requested) ? heap::views::canonical(requested) : QStringLiteral("today");
  if(v == m_currentView) {
    return;
  }
  m_currentView = v;
  m_sectionViews.insert(heap::views::sectionOf(v), v);
  clearSelection();
  emit currentViewChanged();
  scheduleSave();
}

QString AppController::currentSection() const {
  return heap::views::sectionOf(m_currentView);
}

QString AppController::sectionView(const QString& section) const {
  const QString last = m_sectionViews.value(section);
  const QString v = heap::views::canonical(last);
  return heap::views::isKnown(v) && heap::views::sectionOf(v) == section ? v : heap::views::defaultViewOf(section);
}

void AppController::openSection(const QString& section) {
  setCurrentView(sectionView(section));
}

void AppController::ackShellNotice() {
  if(m_shellNotice.isEmpty()) {
    return;
  }
  m_shellNotice.clear();
  emit shellNoticeChanged();
  // The new shortcuts schema is what keeps it from being said twice.
  scheduleSave();
}

void AppController::focusStatusColumn(const QString& statusId) {
  // Sidebar Blocked / Code Review entry point: make sure the Board is the
  // active view, then flag the target status column so KanbanBoard can scroll
  // to and briefly highlight it. focusedStatus is transient UI state (not
  // persisted). Emitted unconditionally so a repeat click re-triggers the
  // scroll/pulse even when the same column is already focused.
  setCurrentView(QStringLiteral("board"));
  m_focusedStatus = statusId;
  emit focusedStatusChanged();
}

void AppController::setWorkdayStart(int v) {
  v = qBound(0, v, 23);
  // A working day has to end after it starts; asking for 20 → 8 used to land
  // silently on 18–19. Keep the adjustment, but say so.
  if(v >= m_workdayEnd) {
    v = m_workdayEnd - 1;
    emit toast(tr_("workday.adjusted").arg(v).arg(m_workdayEnd));
  }
  if(v == m_workdayStart) {
    return;
  }
  m_workdayStart = v;
  emit workdayChanged();
  scheduleSave();
}

void AppController::setWorkdayEnd(int v) {
  v = qBound(1, v, 24);
  if(v <= m_workdayStart) {
    v = m_workdayStart + 1;
    emit toast(tr_("workday.adjusted").arg(m_workdayStart).arg(v));
  }
  if(v == m_workdayEnd) {
    return;
  }
  m_workdayEnd = v;
  emit workdayChanged();
  scheduleSave();
}

void AppController::setCrumbProject(const QString& v) {
  if(v == m_crumbProject) {
    return;
  }
  m_crumbProject = v;
  emit crumbProjectChanged();
  scheduleSave();
}

void AppController::setCrumbUser(const QString& v) {
  if(v == m_crumbUser) {
    return;
  }
  m_crumbUser = v;
  emit crumbUserChanged();
  scheduleSave();
}

void AppController::setDocsState(const QString& v) {
  if(v == m_docsState) {
    return;
  }
  m_docsState = v;
  emit docsStateChanged();
  scheduleSave();
}

namespace {

// The text of a note's first line when it is an H1 ("# Standup"), or empty.
QString firstH1(const QString& body) {
  qsizetype start = 0;
  while(start < body.size() && (body.at(start) == QLatin1Char('\n') || body.at(start) == QLatin1Char('\r'))) {
    ++start;
  }
  qsizetype end = body.indexOf(QLatin1Char('\n'), start);
  const QString line = body.mid(start, end < 0 ? -1 : end - start).trimmed();
  if(!line.startsWith(QStringLiteral("# "))) {
    return {};
  }
  return line.mid(2).trimmed();
}

// The title a note's text suggests: its leading H1, else its first line that
// has words in it, with the Markdown markers in front taken off ("- [ ] ",
// "> ", "## ") and cut to 60 characters. Empty when the body has no text.
QString titleFromBody(const QString& body) {
  const QString h1 = firstH1(body);
  if(!h1.isEmpty()) {
    return h1.left(60).trimmed();
  }
  // A heading marker with nothing after it yet ("# " trimmed to "#") is not
  // the note's name either: half-way through retyping a heading the title
  // used to become "#" (PERA-6).
  static const QRegularExpression kMarkers(QStringLiteral(R"(^(?:#{1,6}(?:\s+|$)|[-*+]\s+(?:\[[ xX]\]\s+)?|\d+[.)]\s+|>\s*)+)"));
  for(const QString& raw : body.split(QLatin1Char('\n'))) {
    QString line = raw.trimmed();
    line.remove(kMarkers);
    line = line.trimmed();
    if(!line.isEmpty()) {
      return line.left(60).trimmed();
    }
  }
  return {};
}

// The title a note gets when nobody named it, in either language: a profile
// written in one and opened in the other still has the other's word.
bool isPlaceholderNoteTitle(const QString& title) {
  return title.isEmpty() || title == QStringLiteral("Untitled note") || title == QStringLiteral("Без названия");
}

// `body` with its leading H1 replaced by `title`.
QString withH1(const QString& body, const QString& title) {
  qsizetype start = 0;
  while(start < body.size() && (body.at(start) == QLatin1Char('\n') || body.at(start) == QLatin1Char('\r'))) {
    ++start;
  }
  const qsizetype end = body.indexOf(QLatin1Char('\n'), start);
  return body.left(start) + QStringLiteral("# ") + title + (end < 0 ? QString() : body.mid(end));
}

// The imported contacts (source + external id) a docs blob holds.
QSet<QPair<QString, QString>> importedContactsIn(const QString& docsJson) {
  QSet<QPair<QString, QString>> out;
  const QJsonDocument d = QJsonDocument::fromJson(docsJson.toUtf8());
  for(const auto& v : d.object().value(QStringLiteral("contacts")).toArray()) {
    const QJsonObject c = v.toObject();
    const QString source = c.value(QStringLiteral("source")).toString();
    const QString id = c.value(QStringLiteral("mmId")).toString();
    if(!source.isEmpty() && !id.isEmpty()) {
      out.insert({source, id});
    }
  }
  return out;
}

}  // namespace

void AppController::syncDismissedContacts(const QString& beforeJson, const QString& afterJson) {
  const auto before = importedContactsIn(beforeJson);
  const auto after = importedContactsIn(afterJson);
  for(const auto& c : before) {
    if(!after.contains(c)) {
      dismissExternalContact(c.first, c.second);
    }
  }
  for(const auto& c : after) {
    if(!before.contains(c)) {
      restoreExternalContact(c.first, c.second);
    }
  }
}

void AppController::setDocsStateUndoable(const QString& v, const QString& label) {
  if(v == m_docsState) {
    return;
  }
  const QString before = m_docsState;
  {
    const UndoScope scope(this, label);
    setDocsState(v);
  }
  // A deleted imported contact would come back on the next sync unless the
  // importer is told; the undo tells it the opposite (applyUndoEntry).
  syncDismissedContacts(before, v);
}

void AppController::setNotesState(const QString& v) {
  if(v == m_notesState) {
    return;
  }
  m_notesState = v;
  // `notesState` IS the active note's body. Writing one without the other is
  // how an edit would survive until the next profile switch and then vanish —
  // and with no note open at all, until the next save.
  if(m_notes.indexOfId(m_activeNoteId) < 0) {
    adoptOrphanNotesState();
  } else {
    syncActiveNoteBody();
  }
  emit notesStateChanged();
  scheduleSave();
}

QString AppController::createActiveNote(const QString& title) {
  Note n;
  n.id = QStringLiteral("note-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  const QString fromText = titleFromBody(m_notesState);
  n.title = !title.trimmed().isEmpty() ? title.trimmed() : (!fromText.isEmpty() ? fromText : tr_("notes.untitled"));
  n.created = QDateTime::currentDateTime();
  n.updated = n.created;
  n.body = m_notesState;
  m_notes.upsert(n);
  m_activeNoteId = n.id;
  emit activeNoteChanged();
  scheduleSave();
  return n.id;
}

void AppController::adoptOrphanNotesState() {
  if(m_notes.indexOfId(m_activeNoteId) >= 0 || m_notesState.trimmed().isEmpty()) {
    return;
  }
  // Named after what had been typed so far; the title keeps following the
  // first line as typing goes on (syncActiveNoteBody), so the half-typed
  // word the adoption caught does not stick.
  createActiveNote(QString());
}

void AppController::syncActiveNoteBody() {
  const int row = m_notes.indexOfId(m_activeNoteId);
  if(row < 0) {
    return;
  }
  Note n = m_notes.items().at(row);
  if(n.body == m_notesState) {
    return;
  }
  // The title follows the note's text (its H1, else its first line) while
  // the two agree, and names a note nobody named: a new note opens as
  // "# Untitled note", retyping that heading left the list saying "Untitled
  // note" forever, and a note started by typing into the editor had no
  // heading to follow at all, so every one of them was "Untitled note"
  // (APP-1). Only while nothing links to the note by its title, which an
  // automatic rename would silently break.
  // A title that came from the H1 stops following once the heading is gone:
  // the user named that note, and its first line is just text.
  const QString was = titleFromBody(n.body);
  const QString now = titleFromBody(m_notesState);
  // A body with no text at all suggests nothing, so it cannot have been
  // disagreeing with the title: retyping a heading from scratch ("# Old" →
  // "# " → "# New") is still the heading naming the note. Without this the
  // title stuck at whatever was left before the heading was emptied (PERA-6).
  const bool wasFollowing = was == n.title || (was.isEmpty() && !firstH1(m_notesState).isEmpty());
  const bool followed = wasFollowing && (firstH1(n.body).isEmpty() || !firstH1(m_notesState).isEmpty());
  if(!now.isEmpty() && now != n.title && (followed || isPlaceholderNoteTitle(n.title)) &&
     heap::notes::backlinksTo(n.id, m_notes.items()).isEmpty()) {
    n.title = now;
  }
  n.body = m_notesState;
  n.updated = QDateTime::currentDateTime();
  m_notes.upsert(n);
}

void AppController::setActiveNoteId(const QString& id) {
  if(id == m_activeNoteId) {
    return;
  }
  // The editor flushes on this, while the note being left is still active, so
  // its unsaved keystrokes land there and not in the note being opened. That
  // flush may itself adopt orphaned text into a new note, which changes
  // m_activeNoteId — hence the second check.
  emit aboutToChangeActiveNote();
  if(id == m_activeNoteId) {
    return;
  }
  adoptOrphanNotesState();
  // The note being left keeps what was typed into it.
  syncActiveNoteBody();
  // A "+" nobody wrote in leaves no "Untitled note" behind (IDIOT-KNOW-14).
  // Not an undoable delete: nothing was in it.
  if(m_activeNoteId == m_freshNoteId && freshNoteUntouched()) {
    m_notes.removeById(m_freshNoteId);
  }
  m_freshNoteId.clear();
  m_activeNoteId = id;
  const int row = m_notes.indexOfId(id);
  m_notesState = row >= 0 ? m_notes.items().at(row).body : QString();
  emit activeNoteChanged();
  emit notesStateChanged();
  scheduleSave();
}

QString AppController::noteBody(const QString& id) const {
  const int row = m_notes.indexOfId(id);
  return row >= 0 ? m_notes.items().at(row).body : QString();
}

bool AppController::freshNoteUntouched() const {
  const int row = m_notes.indexOfId(m_freshNoteId);
  if(m_freshNoteId.isEmpty() || row < 0) {
    return false;
  }
  const Note& n = m_notes.items().at(row);
  const QString body = n.id == m_activeNoteId ? m_notesState : n.body;
  return !n.pinned && n.folder.isEmpty() && isPlaceholderNoteTitle(n.title) && body == QStringLiteral("# %1\n\n").arg(n.title);
}

QString AppController::newNote(const QString& title, const QString& folder) {
  // Ctrl+Alt+N pressed again over the empty note it just made: that note,
  // not another "Untitled note" (IDIOT-KNOW-14).
  if(title.trimmed().isEmpty() && folder.isEmpty() && m_activeNoteId == m_freshNoteId && freshNoteUntouched()) {
    return m_activeNoteId;
  }
  Note n;
  n.id = QStringLiteral("note-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  n.title = title.trimmed().isEmpty() ? tr_("notes.untitled") : title.trimmed();
  n.folder = folder;
  n.created = QDateTime::currentDateTime();
  n.updated = n.created;
  // The heading, so the document opens saying what it is rather than empty.
  n.body = QStringLiteral("# %1\n\n").arg(n.title);
  m_notes.upsert(n);
  setActiveNoteId(n.id);
  // A quick note in the next hour goes on in this one (APP-320).
  if(folder.isEmpty()) {
    noteChainTo(n.id, QString(), n.created);
  }
  if(title.trimmed().isEmpty() && folder.isEmpty()) {
    m_freshNoteId = n.id;
  }
  scheduleSave();
  return n.id;
}

bool AppController::noteTitleTaken(const QString& title, const QString& folder, const QString& exceptId) const {
  const QString wanted = title.trimmed();
  for(const Note& n : m_notes.items()) {
    if(n.id != exceptId && n.folder == folder && n.title.trimmed().compare(wanted, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

bool AppController::renameNote(const QString& id, const QString& title) {
  const int row = m_notes.indexOfId(id);
  const QString next = title.trimmed();
  if(row < 0 || next.isEmpty()) {
    return false;
  }
  if(m_notes.items().at(row).title == next) {
    return true;
  }
  // Two notes of one name in a folder: every [[Name]] there would be
  // retargeted onto this one, silently (IDIOT-KNOW-3).
  if(noteTitleTaken(next, m_notes.items().at(row).folder, id)) {
    return false;
  }
  // The open note's pending keystrokes belong to it before its heading moves.
  emit aboutToChangeActiveNote();
  syncActiveNoteBody();
  const QString old = m_notes.items().at(row).title;
  // Links are resolved against the titles as they stand before the rename.
  const QVector<Note> before = m_notes.items();
  {
    const UndoScope scope(this, tr_("notes.undo.rename").arg(next));
    Note n = m_notes.items().at(row);
    n.title = next;
    // The heading says what the note is called; one that said the old name
    // would drift from the list the moment the note was renamed.
    if(firstH1(n.body) == old) {
      n.body = withH1(n.body, next);
    }
    n.updated = QDateTime::currentDateTime();
    m_notes.upsert(n);
    // And every [[Old]] elsewhere that meant this note now says [[New]]: a
    // rename used to leave each link to the note pointing at nothing. Only
    // links that resolve here — a same-titled note in another folder keeps its
    // own (KNOW-7, audit 2026-09-30).
    for(const Note& other : before) {
      if(other.id == id) {
        continue;
      }
      const QString body = heap::notes::retargetLinksTo(other.body, other.id, before, id, old, next);
      if(body != other.body) {
        Note changed = other;
        changed.body = body;
        m_notes.upsert(changed);
      }
    }
    // Tasks and doc pages name notes too: their [[Old]] went dead and the
    // task fell out of the note's "tasks linking here" (KNOW-4). They are
    // written in no folder, so a link reads the way a task's does.
    const auto retarget = [&](const QString& text) {
      return text.contains(QStringLiteral("[[")) ? heap::notes::retargetLinksTo(text, QString(), before, id, old, next) : text;
    };
    const QVector<Task> tasks = m_tasks.items();
    for(const Task& t : tasks) {
      const QString desc = retarget(t.desc);
      const QString local = retarget(t.local.notes);
      if(desc != t.desc || local != t.local.notes) {
        Task changed = t;
        changed.desc = desc;
        changed.local.notes = local;
        m_tasks.upsert(changed);
      }
    }
    const QVector<DocPage> pages = m_docPages.items();
    for(const DocPage& p : pages) {
      const QString body = retarget(p.body);
      if(body != p.body) {
        DocPage changed = p;
        changed.body = body;
        m_docPages.upsert(changed);
      }
    }
  }
  reconcileActiveNote();
  scheduleSave();
  return true;
}
void AppController::deleteNote(const QString& id) {
  if(m_notes.indexOfId(id) < 0) {
    return;
  }
  // Unsaved keystrokes go into the note they were typed in before anything
  // moves; if that is the note being deleted, they go with it, which is what
  // deleting it means. Flushed before the undo scope opens, so undoing the
  // delete does not also roll those keystrokes back.
  emit aboutToChangeActiveNote();
  const int row = m_notes.indexOfId(id);
  if(row < 0) {
    return;
  }
  const QString title = m_notes.items().at(row).title;
  {
    const UndoScope scope(this, tr_("notes.restored").arg(title));
    const bool wasActive = (id == m_activeNoteId);
    m_notes.removeById(id);
    if(wasActive) {
      // Land on a neighbour rather than on nothing: an empty editor after a
      // delete reads as the rest of the notes having gone too.
      const int next = qMin(row, m_notes.rowCount() - 1);
      m_activeNoteId = next >= 0 ? m_notes.items().at(next).id : QString();
      m_notesState = next >= 0 ? m_notes.items().at(next).body : QString();
      emit activeNoteChanged();
      emit notesStateChanged();
    }
  }
  scheduleSave();
  emit undoableToast(tr_("notes.deleted").arg(title), 8);
}

bool AppController::mergeNotes(const QString& sourceId, const QString& targetId) {
  if(sourceId == targetId || m_notes.indexOfId(sourceId) < 0 || m_notes.indexOfId(targetId) < 0) {
    return false;
  }
  // Typing still in the editor belongs to its note before either text moves.
  emit aboutToChangeActiveNote();
  syncActiveNoteBody();
  const QVector<Note> before = m_notes.items();
  const Note source = before.at(m_notes.indexOfId(sourceId));
  Note target = before.at(m_notes.indexOfId(targetId));
  {
    const UndoScope scope(this, tr_("notes.undo.merge").arg(source.title));
    // The target keeps the one H1; the source's heading becomes a section
    // under it, so what was merged in can still be found and linked to.
    QString added = source.body;
    if(firstH1(added).isEmpty()) {
      added = QStringLiteral("## %1\n\n%2").arg(source.title, added.trimmed());
    } else {
      added = QStringLiteral("#") + added.trimmed();
    }
    QString body = target.body;
    while(body.endsWith(QLatin1Char('\n'))) {
      body.chop(1);
    }
    target.body = body.isEmpty() ? added + QLatin1Char('\n') : body + QStringLiteral("\n\n") + added + QLatin1Char('\n');
    target.updated = QDateTime::currentDateTime();
    m_notes.upsert(target);
    m_notes.removeById(sourceId);
    // [[Source]] anywhere (the target included) now means the target.
    const QVector<Note> merged = m_notes.items();
    for(const Note& other : merged) {
      const QString linked = heap::notes::retargetLinksTo(other.body, other.id, before, sourceId, source.title, target.title);
      if(linked != other.body) {
        Note changed = other;
        changed.body = linked;
        m_notes.upsert(changed);
      }
    }
    if(m_activeNoteId == sourceId) {
      m_activeNoteId = targetId;
      emit activeNoteChanged();
    }
  }
  reconcileActiveNote();
  scheduleSave();
  emit undoableToast(tr_("notes.merged").arg(source.title, target.title), 8);
  return true;
}

void AppController::reconcileActiveNote() {
  const int row = m_notes.indexOfId(m_activeNoteId);
  if(row < 0) {
    m_activeNoteId = m_notes.rowCount() > 0 ? m_notes.items().constFirst().id : QString();
    m_notesState = m_notes.rowCount() > 0 ? m_notes.items().constFirst().body : QString();
    emit activeNoteChanged();
    emit notesStateChanged();
    return;
  }
  const QString& body = m_notes.items().at(row).body;
  if(body != m_notesState) {
    m_notesState = body;
    emit notesStateChanged();
  }
}

void AppController::setNoteBody(const QString& id, const QString& body) {
  const int row = m_notes.indexOfId(id);
  if(row < 0) {
    return;
  }
  Note n = m_notes.items().at(row);
  if(n.body == body) {
    return;
  }
  n.body = body;
  n.updated = QDateTime::currentDateTime();
  m_notes.upsert(n);
  if(id == m_activeNoteId) {
    m_notesState = body;
    emit notesStateChanged();
  }
  scheduleSave();
}

void AppController::setNotePinned(const QString& id, bool pinned) {
  const int row = m_notes.indexOfId(id);
  if(row < 0) {
    return;
  }
  Note n = m_notes.items().at(row);
  if(n.pinned == pinned) {
    return;
  }
  const UndoScope scope(this, tr_(pinned ? "notes.undo.pin" : "notes.undo.unpin").arg(n.title));
  n.pinned = pinned;
  m_notes.upsert(n);
  scheduleSave();
}

namespace {

// Normalised the way a path is: no leading or trailing separator, so
// "/meetings/" and "meetings" are the same folder rather than two.
QString cleanFolder(const QString& folder) {
  QString clean = folder.trimmed();
  while(clean.startsWith(QLatin1Char('/'))) {
    clean = clean.mid(1);
  }
  while(clean.endsWith(QLatin1Char('/'))) {
    clean.chop(1);
  }
  return clean;
}

}  // namespace

void AppController::moveNoteToFolder(const QString& id, const QString& folder) {
  const int row = m_notes.indexOfId(id);
  if(row < 0) {
    return;
  }
  Note n = m_notes.items().at(row);
  const QString clean = cleanFolder(folder);
  if(n.folder == clean) {
    return;
  }
  const UndoScope scope(this, tr_("notes.undo.move").arg(n.title));
  n.folder = clean;
  n.updated = QDateTime::currentDateTime();
  m_notes.upsert(n);
  scheduleSave();
}

int AppController::renameNoteFolder(const QString& folder, const QString& newName) {
  const QString from = cleanFolder(folder);
  const QString to = cleanFolder(newName);
  if(from.isEmpty() || from == to) {
    return 0;
  }
  int moved = 0;
  {
    // The folder and everything under it, as one step: "meetings" → "team"
    // takes "meetings/2026" with it.
    const UndoScope scope(this, tr_("notes.undo.folder").arg(from));
    for(const Note& n : QVector<Note>(m_notes.items())) {
      QString next;
      if(n.folder == from) {
        next = to;
      } else if(n.folder.startsWith(from + QLatin1Char('/'))) {
        next = to.isEmpty() ? n.folder.mid(from.size() + 1) : to + n.folder.mid(from.size());
      } else {
        continue;
      }
      Note changed = n;
      changed.folder = next;
      m_notes.upsert(changed);
      moved++;
    }
  }
  if(moved > 0) {
    scheduleSave();
  }
  return moved;
}

int AppController::removeNoteFolder(const QString& folder) {
  // A folder is only a label on its notes. Removing it keeps every note and
  // files them one level up; deleting notes is what Delete on a note is for.
  const QString from = cleanFolder(folder);
  const qsizetype slash = from.lastIndexOf(QLatin1Char('/'));
  return renameNoteFolder(from, slash > 0 ? from.left(slash) : QString());
}
QStringList AppController::noteFolders() const {
  QStringList out;
  for(const Note& n : m_notes.items()) {
    if(!n.folder.isEmpty() && !out.contains(n.folder)) {
      out << n.folder;
    }
  }
  out.sort();
  return out;
}

QString AppController::inboxNoteId() const {
  // The Inbox is the root-level note by that name, in either language, so a
  // language switch does not start a second one.
  const QString en = QStringLiteral("Inbox");
  const QString ru = QStringLiteral("Входящие");
  const QString cur = tr_("notes.inbox");
  for(const Note& n : m_notes.items()) {
    if(n.folder.isEmpty() && (n.title.compare(cur, Qt::CaseInsensitive) == 0 || n.title.compare(en, Qt::CaseInsensitive) == 0 ||
                              n.title.compare(ru, Qt::CaseInsensitive) == 0)) {
      return n.id;
    }
  }
  return {};
}

QString AppController::quickNoteTarget(const QString& label) const {
  return quickNoteTargetAt(label, QDateTime::currentDateTime());
}

QString AppController::quickNoteTargetAt(const QString& label, const QDateTime& now) const {
  // The note the chain is on (APP-320), while it still takes this note.
  const QJsonObject chain = m_settingsExtra.value(QStringLiteral("noteChain")).toObject();
  const int row = m_notes.indexOfId(chain.value(QStringLiteral("id")).toString());
  if(row < 0 || !heap::notes::chainAppends(QDateTime::fromString(chain.value(QStringLiteral("at")).toString(), Qt::ISODate),
                                           chain.value(QStringLiteral("label")).toString(),
                                           label,
                                           now)) {
    return {};
  }
  return m_notes.items().at(row).title;
}

void AppController::noteChainTo(const QString& noteId, const QString& label, const QDateTime& at) {
  m_settingsExtra.insert(QStringLiteral("noteChain"),
                         QJsonObject{{QStringLiteral("id"), noteId},
                                     {QStringLiteral("at"), at.toString(Qt::ISODate)},
                                     {QStringLiteral("label"), heap::notes::normLabel(label)}});
}

QStringList AppController::noteLabels() const {
  QStringList out{QStringLiteral("личное"), QStringLiteral("hr"), QStringLiteral("работа")};
  if(m_language != QLatin1String("ru")) {
    out = {QStringLiteral("personal"), QStringLiteral("hr"), QStringLiteral("work")};
  }
  const auto add = [&](const QString& t) {
    const QString l = heap::notes::normLabel(t);
    if(!l.isEmpty() && !out.contains(l)) {
      out << l;
    }
  };
  for(const QVariant& v : localTagCatalog()) {
    add(v.toMap().value(QStringLiteral("id")).toString());
  }
  for(const QVariant& v : noteTagCounts()) {
    add(v.toMap().value(QStringLiteral("id")).toString());
  }
  return out;
}

QVariantList AppController::noteTagCounts() const {
  QHash<QString, int> count;
  QStringList order;
  for(const Note& n : m_notes.items()) {
    const bool open = n.id == m_activeNoteId;
    for(const QString& t : heap::notes::noteTags(open ? m_notesState : n.body)) {
      if(!count.contains(t)) {
        order << t;
      }
      ++count[t];
    }
  }
  std::stable_sort(order.begin(), order.end(), [&](const QString& a, const QString& b) {
    return count.value(a) > count.value(b);
  });
  QVariantList out;
  for(const QString& t : order) {
    out.append(QVariantMap{{QStringLiteral("id"), t}, {QStringLiteral("count"), count.value(t)}});
  }
  return out;
}

QVariantMap AppController::quickNoteDraft() const {
  const QJsonObject d = m_settingsExtra.value(QStringLiteral("quickNoteDraft")).toObject();
  if(d.value(QStringLiteral("text")).toString().isEmpty()) {
    return {};
  }
  return {{QStringLiteral("text"), d.value(QStringLiteral("text")).toString()},
          {QStringLiteral("attachId"), d.value(QStringLiteral("attachId")).toString()}};
}

void AppController::setQuickNoteDraft(const QString& text, const QString& attachId) {
  const QJsonValue was = m_settingsExtra.value(QStringLiteral("quickNoteDraft"));
  if(text.trimmed().isEmpty()) {
    if(was.isUndefined()) {
      return;
    }
    m_settingsExtra.remove(QStringLiteral("quickNoteDraft"));
  } else {
    const QJsonObject next{{QStringLiteral("text"), text}, {QStringLiteral("attachId"), attachId}};
    if(was.toObject() == next) {
      return;
    }
    m_settingsExtra.insert(QStringLiteral("quickNoteDraft"), next);
  }
  scheduleSave();
}

QString AppController::appendNoteEntry(const QString& text, const QString& label) {
  return appendNoteEntryAt(text, label, QDateTime::currentDateTime());
}

QString AppController::appendNoteEntryAt(const QString& text, const QString& label, const QDateTime& now) {
  const QString body = text.trimmed();
  if(body.isEmpty()) {
    return {};
  }
  // The editor's debounced keystrokes go into notesState first: the entry is
  // appended to notesState, and the view diffs the editor against it, so text
  // typed in the last 250 ms was erased (KNOW-17, audit 2026-09-30).
  emit aboutToChangeActiveNote();
  // Text in the editor with no note behind it becomes a note before the
  // chain is looked at, so it is neither lost nor written over.
  adoptOrphanNotesState();
  syncActiveNoteBody();
  const QString tag = heap::notes::normLabel(label);
  const QString stamp = now.toString("yyyy-MM-dd HH:mm");
  // Within an hour of the last note and under the same label: on in that
  // note; else a note of its own, named by its first words (APP-320, the
  // owner's rule: one note per sitting, not one Inbox for ever).
  const QJsonObject chain = m_settingsExtra.value(QStringLiteral("noteChain")).toObject();
  QString targetId = quickNoteTargetAt(tag, now).isEmpty() ? QString() : chain.value(QStringLiteral("id")).toString();
  if(targetId.isEmpty()) {
    Note n;
    n.id = QStringLiteral("note-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QString title = heap::notes::titleFromEntry(body);
    if(title.isEmpty()) {
      title = tr_("notes.untitled");
    }
    if(noteTitleTaken(title, QString(), QString())) {
      title += QStringLiteral(" · ") + heap::text::formatTime(now.time(), twelveHourClock());
    }
    n.title = title;
    n.created = now;
    n.updated = now;
    n.body =
        QStringLiteral("# %1\n\n%2### %3\n\n%4").arg(title, tag.isEmpty() ? QString() : QStringLiteral("#%1\n\n").arg(tag), stamp, body);
    m_notes.upsert(n);
    targetId = n.id;
    // With no note open the new one opens, so the editor shows where it went.
    if(m_notes.indexOfId(m_activeNoteId) < 0) {
      m_activeNoteId = targetId;
      m_notesState = n.body;
      emit activeNoteChanged();
    }
    noteChainTo(targetId, tag, now);
    scheduleSave();
    return targetId;
  }
  const bool open = targetId == m_activeNoteId;
  QString next = open ? m_notesState : noteBody(targetId);
  while(next.endsWith(QLatin1Char('\n')) || next.endsWith(QLatin1Char(' ')) || next.endsWith(QLatin1Char('\t'))) {
    next.chop(1);
  }
  // A rule between entries, not under the note's own heading: a note made by
  // hand a minute ago holds only "# Title".
  const QString heading = QStringLiteral("# ") + m_notes.items().at(m_notes.indexOfId(targetId)).title;
  if(next.trimmed().isEmpty()) {
    next = QStringLiteral("### %1\n\n%2").arg(stamp, body);
  } else if(next.trimmed() == heading) {
    next = QStringLiteral("%1\n\n### %2\n\n%3").arg(heading, stamp, body);
  } else {
    next += QStringLiteral("\n\n----\n### %1\n\n%2").arg(stamp, body);
  }
  noteChainTo(targetId, tag, now);
  if(open) {
    setNotesState(next);
    return targetId;
  }
  Note n = m_notes.items().at(m_notes.indexOfId(targetId));
  n.body = next;
  n.updated = now;
  m_notes.upsert(n);
  scheduleSave();
  return targetId;
}

QStringList AppController::noteHeadings(const QString& markdown) const {
  return heap::notes::collectHeadings(markdown);
}

QString AppController::taskLocalNotes(const QString& id) const {
  const int row = m_tasks.indexOfId(id);
  return row < 0 ? QString() : m_tasks.items().at(row).local.notes;
}

void AppController::setTaskLocalNotes(const QString& id, const QString& text) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0 || m_tasks.items().at(row).local.notes == text) {
    return;
  }
  const UndoScope scope(this, tr_("task.editUndone").arg(id));
  Task t = m_tasks.items().at(row);
  t.local.notes = text;
  m_tasks.upsert(t);
  scheduleSave();
}

QVariantList AppController::notesMentioningTask(const QString& id) const {
  QVariantList out;
  if(id.isEmpty()) {
    return out;
  }
  const QRegularExpression word(QStringLiteral("(?<![\\w-])") + QRegularExpression::escape(id) + QStringLiteral("(?![\\w-])"),
                                QRegularExpression::CaseInsensitiveOption);
  QVector<const Note*> hits;
  for(const Note& n : m_notes.items()) {
    if(word.match(n.body).hasMatch() || word.match(n.title).hasMatch()) {
      hits.append(&n);
    }
  }
  std::sort(hits.begin(), hits.end(), [](const Note* a, const Note* b) {
    return a->updated > b->updated;
  });
  for(const Note* n : hits) {
    out.append(QVariantMap{{"id", n->id}, {"title", n->title}, {"updated", n->updated}});
  }
  return out;
}

namespace {
// [[target]] or [[target|label]], the target trimmed; first occurrence order.
QStringList wikiTargetsOf(const QString& markdown) {
  static const QRegularExpression rx(QStringLiteral("\\[\\[([^\\]\\|\\n]+)(?:\\|[^\\]\\n]*)?\\]\\]"));
  QStringList out;
  auto it = rx.globalMatch(markdown);
  while(it.hasNext()) {
    const QString t = it.next().captured(1).trimmed();
    if(!t.isEmpty() && !out.contains(t)) {
      out << t;
    }
  }
  return out;
}

// The stage of a column as one glyph, the shape StatusRing draws.
QString stageGlyph(const QString& category) {
  if(category == QStringLiteral("done")) {
    return QStringLiteral("\u25CF");  // ●
  }
  if(category == QStringLiteral("review")) {
    return QStringLiteral("\u25D5");  // ◕
  }
  if(category == QStringLiteral("half") || category == QStringLiteral("prog")) {
    return QStringLiteral("\u25D1");  // ◑
  }
  if(category == QStringLiteral("blocked")) {
    return QStringLiteral("\u25CC");  // ◌
  }
  return QStringLiteral("\u25CB");  // ○
}
}  // namespace

QVariantList AppController::noteTaskRefs(const QString& markdown) const {
  QVariantList out;
  static const QRegularExpression key(QStringLiteral("(?<![\\w-])([A-Z][A-Z0-9]*-\\d+)(?![\\w-])"));
  QStringList seen;
  auto it = key.globalMatch(markdown);
  while(it.hasNext()) {
    const QString id = it.next().captured(1);
    if(seen.contains(id)) {
      continue;
    }
    seen << id;
    const auto statusNameOf = [this](const QString& st) {
      const int si = statusIndexOf(st);
      return si < 0 ? st : m_statuses[si].toMap().value(QStringLiteral("name")).toString();
    };
    const int row = m_tasks.indexOfId(id);
    if(row >= 0) {
      const Task& t = m_tasks.items().at(row);
      out.append(QVariantMap{{"id", t.id},
                             {"title", t.title},
                             {"status", t.status},
                             {"statusName", statusNameOf(t.status)},
                             {"category", statusCategory(t.status)},
                             {"profileName", QString()}});
      continue;
    }
    for(const Profile& p : m_profiles) {
      if(p.id == m_activeProfileId) {
        continue;
      }
      const auto hit = std::ranges::find_if(p.tasks, [&](const Task& t) {
        return t.id == id;
      });
      if(hit != p.tasks.end()) {
        QString name = hit->status, cat = heap::board::defaultCategoryFor(hit->status);
        for(const QVariant& v : p.statuses) {
          const QVariantMap m = v.toMap();
          if(m.value(QStringLiteral("id")).toString() == hit->status) {
            name = m.value(QStringLiteral("name")).toString();
            const QString c = m.value(QStringLiteral("category")).toString();
            if(heap::board::isColumnCategory(c)) {
              cat = c;
            }
          }
        }
        out.append(QVariantMap{{"id", hit->id},
                               {"title", hit->title},
                               {"status", hit->status},
                               {"statusName", name},
                               {"category", cat},
                               {"profileName", p.name}});
        break;
      }
    }
  }
  return out;
}

QVariantMap AppController::wikiTargets(const QString& markdown) const {
  QVariantMap out;
  const QStringList targets = wikiTargetsOf(markdown);
  if(targets.isEmpty()) {
    return out;
  }
  QHash<QString, QVariantMap> tasks;
  for(const QVariant& v : noteTaskRefs(targets.join(QChar(' ')))) {
    const QVariantMap m = v.toMap();
    tasks.insert(m.value(QStringLiteral("id")).toString(), m);
  }
  for(const QString& t : targets) {
    if(tasks.contains(t)) {
      const QVariantMap m = tasks.value(t);
      const QString profile = m.value(QStringLiteral("profileName")).toString();
      QString tip =
          m.value(QStringLiteral("title")).toString() + QStringLiteral(" \u00B7 ") + m.value(QStringLiteral("statusName")).toString();
      if(!profile.isEmpty()) {
        tip += QStringLiteral(" \u00B7 ") + profile;
      }
      out.insert(t,
                 QVariantMap{{"kind", QStringLiteral("task")},
                             {"label", stageGlyph(m.value(QStringLiteral("category")).toString()) + QChar(' ') + t},
                             {"tip", tip}});
      continue;
    }
    if(resolveNoteLink(t).value(QStringLiteral("kind")).toString() == QStringLiteral("missing")) {
      out.insert(t,
                 QVariantMap{{"kind", QStringLiteral("missing")}, {"label", t + QStringLiteral(" \u00B7 ") + tr_("notes.wiki.missing")}});
    }
  }
  return out;
}

QVariantList AppController::tasksLinkingToNote(const QString& noteId) const {
  QVariantList out;
  const int row = m_notes.indexOfId(noteId);
  if(row < 0) {
    return out;
  }
  const QString title = m_notes.items().at(row).title.trimmed();
  if(title.isEmpty()) {
    return out;
  }
  // Read as a link is followed: [[C\# basics]] names "C# basics", which
  // splitting on '#' missed (KNOW-12).
  const QVector<Note>& notes = m_notes.items();
  const auto links = [&](const QString& text) {
    if(!text.contains(QStringLiteral("[["))) {
      return false;
    }
    for(const QString& t : wikiTargetsOf(text)) {
      const heap::notes::LinkTarget hit = heap::notes::resolveLink(t, notes, QString());
      if(hit.kind != heap::notes::LinkTarget::Missing && hit.noteId == noteId) {
        return true;
      }
    }
    return false;
  };
  for(const Task& t : m_tasks.items()) {
    if(links(t.desc) || links(t.local.notes)) {
      out.append(QVariantMap{{"id", t.id}, {"title", t.title}});
    }
  }
  return out;
}

QVariantList AppController::noteBacklinks(const QString& markdown) const {
  return heap::notes::collectBacklinks(markdown);
}

QVariantList AppController::outgoingNoteLinks(const QString& markdown) const {
  // The note's own [[links]], each resolved the way a click would resolve it:
  // a link to another note is fine, not "broken" because no heading of this
  // note happens to carry its name.
  QVariantList out;
  for(const QVariant& v : heap::notes::collectBacklinks(markdown)) {
    QVariantMap m = v.toMap();
    const heap::notes::LinkTarget t = heap::notes::resolveLink(m.value("target").toString(), m_notes.items(), m_activeNoteId);
    m["kind"] = t.kind == heap::notes::LinkTarget::NoteRef      ? QStringLiteral("note")
                : t.kind == heap::notes::LinkTarget::HeadingRef ? QStringLiteral("heading")
                                                                : QStringLiteral("missing");
    m["noteId"] = t.noteId;
    m["heading"] = t.heading;
    m["resolved"] = t.kind != heap::notes::LinkTarget::Missing;
    // A pane, not a report: the first few places a target is used, and how
    // many there are. A note linking one target on every one of its 50 000
    // lines took seconds to draw.
    QVariantList refs = m.value("refs").toList();
    m["count"] = static_cast<int>(refs.size());
    if(refs.size() > 20) {
      refs.resize(20);
      m["refs"] = refs;
    }
    out.append(m);
  }
  return out;
}

QVariantMap AppController::noteStats(const QString& markdown) const {
  // Counted here rather than with a JavaScript regex over the whole note on
  // every keystroke, which is what the header used to do — at 3 MB that alone
  // was a good part of each keystroke. Names in any script.
  static const QRegularExpression mentionRx(QStringLiteral("(?:^|[\\s.,;:!?()\\[\\]{}])@[\\p{L}\\p{N}_.-]+"));
  static const QRegularExpression ticketRx(QStringLiteral("(?:^|[\\s.,;:!?()\\[\\]{}])#[A-Z][A-Z0-9]*-\\d+"));
  QVariantMap out;
  out["lines"] = markdown.isEmpty() ? 0 : static_cast<int>(markdown.count(QLatin1Char('\n'))) + 1;
  int mentions = 0;
  for(auto it = mentionRx.globalMatch(markdown); it.hasNext(); it.next()) {
    ++mentions;
  }
  int tickets = 0;
  for(auto it = ticketRx.globalMatch(markdown); it.hasNext(); it.next()) {
    ++tickets;
  }
  out["mentions"] = mentions;
  out["tickets"] = tickets;
  return out;
}

int AppController::noteHeadingOffset(const QString& markdown, const QString& heading) const {
  return heap::notes::headingOffset(markdown, heading);
}

void AppController::setAppSettingsJson(const QString& v) {
  if(v == m_appSettingsJson) {
    return;
  }
  m_appSettingsJson = v;
  emit appSettingsJsonChanged();
  scheduleSave();
}

void AppController::moveTask(const QString& id, const QString& newStatus) {
  moveTaskRanked(id, newStatus, std::nullopt);
}

QString AppController::doneColumn() const {
  for(const QVariant& v : m_statuses) {
    const QString id = v.toMap().value(QStringLiteral("id")).toString();
    if(statusCategory(id) == QStringLiteral("done")) {
      return id;
    }
  }
  return {};
}

namespace {

// Unticked checklist items, the card's own and "- [ ]" lines in the text.
int uncheckedItems(const Task& t) {
  int n = 0;
  for(const LocalCheckItem& c : t.local.checklist) {
    n += c.done ? 0 : 1;
  }
  static const QRegularExpression kOpen(QStringLiteral(R"((?m)^\s*[-*+]\s+\[ \])"));
  for(auto it = kOpen.globalMatch(t.desc); it.hasNext(); it.next()) {
    ++n;
  }
  return n;
}

}  // namespace

QVariantMap AppController::toggleDone(const QStringList& ids) {
  QVariantMap out{{QStringLiteral("count"), 0}};
  QStringList present;
  bool allDone = true;
  for(const QString& id : ids) {
    const int row = m_tasks.indexOfId(id);
    if(row < 0 || present.contains(id)) {
      continue;
    }
    present << id;
    allDone = allDone && statusCategory(m_tasks.items().at(row).status) == QStringLiteral("done");
  }
  if(present.isEmpty()) {
    return out;
  }
  const QString doneCol = doneColumn();
  if(!allDone && doneCol.isEmpty()) {
    out[QStringLiteral("error")] = QStringLiteral("noDoneColumn");
    emit doneColumnMissing();
    return out;
  }
  // Where a task goes back to when it has no column of its own to return
  // to: the first "to do" stage, else the first column that is not done.
  QString backTo;
  for(const QVariant& v : m_statuses) {
    const QString id = v.toMap().value(QStringLiteral("id")).toString();
    const QString cat = statusCategory(id);
    if(cat == QStringLiteral("todo")) {
      backTo = id;
      break;
    }
    if(backTo.isEmpty() && cat != QStringLiteral("done")) {
      backTo = id;
    }
  }

  int moved = 0;
  int unchecked = 0;
  bool localOnly = false;
  QString lastTarget;
  m_heldRecursNote.clear();
  {
    const UndoScope scope(this, allDone ? tr_(QStringLiteral("task.reopenUndone")) : tr_(QStringLiteral("task.doneUndone")));
    for(const QString& id : present) {
      const int row = m_tasks.indexOfId(id);
      if(row < 0) {
        continue;  // a recurrence spawn reordered nothing, but stay safe
      }
      Task t = m_tasks.items().at(row);
      const bool isDone = statusCategory(t.status) == QStringLiteral("done");
      if(allDone) {
        QString target = t.local.doneFrom;
        if(target.isEmpty() || statusIndexOf(target) < 0 || statusCategory(target) == QStringLiteral("done")) {
          target = backTo;
        }
        if(target.isEmpty()) {
          continue;
        }
        t.local.doneFrom.clear();
        m_tasks.upsert(t);
        m_moveToastHeld = true;
        moveTask(id, target);
        m_moveToastHeld = false;
        lastTarget = target;
      } else if(!isDone) {
        unchecked += uncheckedItems(t);
        if(!t.externalProvider.isEmpty() && !trackerWriteEnabled(t.externalProvider)) {
          localOnly = true;
        }
        t.local.doneFrom = t.status;
        m_tasks.upsert(t);
        m_moveToastHeld = true;
        moveTask(id, doneCol);
        m_moveToastHeld = false;
        lastTarget = doneCol;
      } else {
        continue;
      }
      const int after = m_tasks.indexOfId(id);
      if(after >= 0 && m_tasks.items().at(after).status == lastTarget) {
        ++moved;
      }
    }
  }
  out[QStringLiteral("count")] = moved;
  out[QStringLiteral("reopened")] = allDone;
  out[QStringLiteral("unchecked")] = unchecked;
  out[QStringLiteral("localOnly")] = localOnly;
  if(moved == 0) {
    m_heldRecursNote.clear();
    return out;
  }
  const auto columnName = [this](const QString& id) {
    const int i = statusIndexOf(id);
    return i < 0 ? id : m_statuses[i].toMap().value(QStringLiteral("name")).toString();
  };
  QString msg;
  if(allDone) {
    msg = moved == 1 ? tr_(QStringLiteral("task.reopened.one")).arg(displayKey(present.first()), columnName(lastTarget))
                     : tr_(QStringLiteral("task.reopened.many")).arg(moved);
  } else {
    msg = moved == 1 ? tr_(QStringLiteral("task.done.one")).arg(displayKey(present.first()))
                     : tr_(QStringLiteral("task.done.many")).arg(moved);
    if(localOnly) {
      msg += tr_(QStringLiteral("task.done.localOnly"));
    }
    if(unchecked > 0) {
      msg += tr_(QStringLiteral("task.done.unchecked")).arg(unchecked);
    }
  }
  if(!m_heldRecursNote.isEmpty()) {
    msg += QStringLiteral(" · ") + m_heldRecursNote;
  }
  m_heldRecursNote.clear();
  emit undoableToast(msg, 5);
  return out;
}

void AppController::moveTaskRanked(const QString& id, const QString& newStatus, std::optional<double> rank) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  if(t.status == newStatus) {
    return;
  }
  if(!canTransitionStatus(id, newStatus)) {
    return;
  }
  QString statusName = newStatus;
  for(const auto& v : m_statuses) {
    const QVariantMap m = v.toMap();
    if(m.value("id").toString() == newStatus) {
      statusName = m.value("name").toString();
      break;
    }
  }
  const QString taskId = t.id;
  const QString prevStatus = t.status;
  // Placed after the guards so a rejected or no-op move records nothing. It
  // covers the recurrence spawn and the focus block below too, which the old
  // hand-written undo did not.
  const UndoScope scope(this, tr_("task.moveUndone").arg(taskId));
  // Capture before any upsert can invalidate the `t` reference (HEAP-77).
  const QString recurrence = t.recurrence;
  const QDateTime recurDue = heap::local::effectiveDueAt(t);
  const QDate recurBase = recurDue.isValid() ? recurDue.date() : t.scheduledAt.date();
  m_tasks.setStatus(id, newStatus, {}, rank);
  completionSoundOnMove_(prevStatus, newStatus);

  // Mirror the change back to the linked tracker issue (e.g. moving to Done
  // closes the GitHub issue / transitions the Jira issue). Routed to whichever
  // provider owns the task. No-op for locally-created, unlinked tasks.
  // An issue pulled from an "assigned to me" endpoint belongs to some other
  // repo, but the push path is built from the configured one — the PATCH would
  // land on a different issue that happens to share the number. Its own repo is
  // known, but writing back through it is HEAP-155's problem; skip it here.
  pushStatusToTracker(taskId, newStatus);

  // Re-evaluate blocked-stuck set (the task may have left "blocked").
  if(m_blockedStuckIds.remove(id)) {
    m_tasks.setBlockedStuckIds(m_blockedStuckIds);
    emit blockedStuckChanged();
  }

  // A focus block when the card enters a "doing" column (In Progress, or any
  // column marked so), and the future ones dropped when it leaves one — for
  // Done, and equally for a card put back in To Do.
  focusBlockOnStatusChange(taskId, prevStatus, newStatus);
  // Any column of the Done kind finishes the task, not only the one whose id
  // is "done" (IDIOT-CAL-4): a "Shipped" column ended a weekly series.
  const bool finished = statusCategory(newStatus) == QStringLiteral("done");
  const bool wasFinished = statusCategory(prevStatus) == QStringLiteral("done");
  if(finished) {
    dropFutureFocusBlocks(taskId);
    // A finished task tracks no more time (IDIOT-CAL-6).
    m_tasks.stopTiming(taskId);
  }

  // Recurring task completed → spawn the next occurrence (HEAP-77).
  QString recursNote;
  if(finished && !wasFinished && !recurrence.isEmpty()) {
    const QDate today = QDate::currentDate();
    const QDate base = recurBase.isValid() ? recurBase : today;
    // The first occurrence still ahead: a weekly task finished three weeks
    // late used to spawn a copy that was already overdue.
    // A plain monthly rule keeps the day of the date it starts from, so the
    // 31st became the 28th after February for good (IDIOT-CAL-5). The copy
    // carries the day as the parser writes it, "every:month:31" (and
    // "every:2:month:31" for one every two months).
    const QString seriesRule = heap::recur::seriesRule(recurrence, recurBase);
    const QDate next = heap::recur::nextOccurrenceAfter(seriesRule, base, today);
    const int srcRow = m_tasks.indexOfId(taskId);
    // Fresh unique id (strip any prior "-rN" suffix) so upsert inserts a new
    // row rather than overwriting the just-completed one.
    QString stem = taskId;
    static const QRegularExpression kRSuffix(QStringLiteral("-r\\d+$"));
    stem.remove(kRSuffix);
    // Done → In Progress → Done again is one completion, not two: when the
    // series already holds an open copy on that date, none is spawned.
    bool alreadySpawned = false;
    const QRegularExpression series(QStringLiteral("^%1(-r\\d+)?$").arg(QRegularExpression::escape(stem)));
    for(const Task& other : m_tasks.items()) {
      if(other.id == taskId || other.archived || statusCategory(other.status) == QStringLiteral("done") ||
         (other.recurrence != recurrence && other.recurrence != seriesRule) || !series.match(other.id).hasMatch()) {
        continue;
      }
      const QDate otherDate = other.dueAt.isValid() ? other.dueAt.date() : other.scheduledAt.date();
      if(otherDate == next) {
        alreadySpawned = true;
        break;
      }
    }
    if(next.isValid() && srcRow >= 0 && !alreadySpawned) {
      Task copy = m_tasks.items().at(srcRow);  // clone title/desc/priority/branch
      QString newId;
      int n = 1;
      do {
        newId = stem + QStringLiteral("-r") + QString::number(n++);
      } while(m_tasks.indexOfId(newId) >= 0);
      copy.id = newId;
      // The next one starts at the head of the board. With the To Do column
      // deleted, a "todo" copy was drawn by no column at all.
      copy.status = statusIndexOf(QStringLiteral("todo")) >= 0 || m_statuses.isEmpty()
                        ? QStringLiteral("todo")
                        : m_statuses.constFirst().toMap().value("id").toString();
      copy.archived = false;
      // A fresh occurrence has ticked none of its checklist yet.
      static const QRegularExpression kTicked(QStringLiteral(R"(^(\s*(?:[-*+]|\d+[.)])\s+)\[[xX]\])"), QRegularExpression::MultilineOption);
      copy.desc.replace(kTicked, QStringLiteral("\\1[ ]"));
      const QVector<::Task> column = columnTasks(copy.status, QString());
      copy.rank = heap::board::between(column.isEmpty() ? 0.0 : column.last().rank, 0.0, !column.isEmpty(), false);
      // Roll each datetime onto the next occurrence, keeping the clock time the
      // user set (a 09:00 standup recurs at 09:00, not at midnight) and the gap
      // between the two dates. A field the task never carried stays invalid —
      // rebuilding it would manufacture a phantom midnight deadline/schedule and
      // fire spurious reminders.
      const int shift = static_cast<int>(base.daysTo(next));
      if(copy.dueAt.isValid()) {
        copy.dueAt = QDateTime(copy.dueAt.date().addDays(shift), copy.dueAt.time());
      }
      if(copy.scheduledAt.isValid()) {
        copy.scheduledAt = QDateTime(copy.scheduledAt.date().addDays(shift), copy.scheduledAt.time());
      }
      copy.statusChangedAt = QDateTime::currentDateTime();
      copy.trackedSeconds = 0;
      copy.local.sessions.clear();
      copy.timerStartedAt = QDateTime();
      copy.recurrence = seriesRule;  // stays recurring
      copy.externalId.clear();       // a new local occurrence, not the synced issue
      copy.externalUrl.clear();
      copy.externalProvider.clear();
      copy.assignee.clear();
      copy.externalMeta = {};
      m_tasks.upsert(copy);
      // The day as everywhere else ("tomorrow, Tue, Oct 13"), not an ISO
      // date and the copy's "-r1" id (A2-19); the copy is planned, not due.
      recursNote = tr_("task.recurs").arg(dayWords_(next));
    }
  }

  // One toast: the move toast used to replace the recurrence one at once.
  if(m_bulkMoveDepth == 0 && !m_moveToastHeld) {
    const QString moved = tr_("task.moved").arg(displayKey(taskId), statusName);
    emit undoableToast(recursNote.isEmpty() ? moved : moved + QStringLiteral(" · ") + recursNote, 5);
  } else if(!recursNote.isEmpty() && m_moveToastHeld) {
    // toggleDone says it in its own toast (A2-18): a toast of its own here
    // was replaced at once by "Done".
    m_heldRecursNote = recursNote;
  } else if(!recursNote.isEmpty()) {
    emit toast(recursNote);
  }
  scheduleSave();
}

namespace {
QString reminderPlace(const QString& location) {
  // A meeting link is the Join action's business: the toast and the OS
  // notification name only its host, not the whole URL (A6-20).
  const QString loc = location.trimmed();
  if(loc.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) || loc.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
    const QString host = QUrl(loc).host();
    if(!host.isEmpty()) {
      return host;
    }
  }
  return loc;
}
}  // namespace

QString AppController::dayWords_(const QDate& d) const {
  const bool ru = m_language == QStringLiteral("ru");
  const QLocale loc(ru ? QLocale::Russian : QLocale::English);
  QString date = loc.toString(d, ru ? QStringLiteral("ddd, d MMM") : QStringLiteral("ddd, MMM d"));
  date.remove(QLatin1Char('.'));
  const qint64 k = QDate::currentDate().daysTo(d);
  if(k == 0) {
    return (ru ? QStringLiteral("сегодня, ") : QStringLiteral("today, ")) + date;
  }
  if(k == 1) {
    return (ru ? QStringLiteral("завтра, ") : QStringLiteral("tomorrow, ")) + date;
  }
  return date;
}

QString AppController::durationWords_(int secs) const {
  const int mins = qMax(0, secs) / 60;
  return mins >= 60 ? tr_("dur.hm").arg(mins / 60).arg(mins % 60) : tr_("dur.m").arg(mins);
}

void AppController::completionSoundOnMove_(const QString& fromStatus, const QString& toStatus) {
  // Tracker pulls and undo/redo restore rows without passing through
  // moveTask(), so what reaches here is a move the user made — in this window,
  // or through `heap` on the command line (muted, or headless).
  using heap::platform::StatusChangeSource;
  const StatusChangeSource source = s_headless || m_soundMuted ? StatusChangeSource::Cli : StatusChangeSource::User;
  const bool enabled = heap::platform::soundSettingsFrom(settingsMap()).enabled;
  if(!heap::platform::shouldPlayCompletionSound(fromStatus, toStatus, source, enabled)) {
    return;
  }
  playSound_(static_cast<int>(heap::platform::SoundCue::Done));
}

void AppController::meetingChimesAt(const QDateTime& now) {
  if(s_headless) {
    return;
  }
  const QVariantMap s = settingsMap();
  const heap::platform::SoundSettings sound = heap::platform::soundSettingsFrom(s);
  if(!sound.enabled || !sound.meetingChimes) {
    return;
  }
  const QDate today = now.date();
  QVector<CalEvent> occurrences = heap::cal::expandedEvents(m_events.items(), today.addDays(-1), today.addDays(1));
  // The standup is a meeting too, at the time from the settings.
  const QVariantMap notif = s.value(QStringLiteral("notifications")).toMap();
  const QTime standup = heap::cal::clockTime(
      s.value(QStringLiteral("calendar")).toMap().value(QStringLiteral("standupTime"), QStringLiteral("10:00")).toString());
  if(notif.value(QStringLiteral("standupReminder"), true).toBool() && isWorkDay(today) && standup.isValid()) {
    CalEvent st;
    st.id = QStringLiteral("standup");
    st.type = QStringLiteral("standup");
    st.date = today;
    st.start = standup.hour() + (standup.minute() / 60.0);
    st.end = st.start + 0.25;
    occurrences.append(st);
  }
  // What the user snoozed stays quiet until the snooze brings the
  // notification back — and that brings no chime either.
  QSet<QString> snoozed;
  for(const heap::notify::SnoozedReminder& r : m_snoozed) {
    const auto [kind, ref] = heap::notify::parseRoutingId(r.id);
    if(heap::safety::isAppointment(kind)) {
      snoozed.insert(ref);
    }
  }
  const QVector<heap::cal::DueChime> due = heap::cal::dueMeetingChimes(occurrences, now, sound.chimeMinutes, sentReminderKeys(), snoozed);
  if(due.isEmpty()) {
    return;
  }
  // Two meetings at once ring once, with the more urgent melody. Every key is
  // spent either way: a chime silenced by quiet hours is not played later.
  heap::cal::ChimeStage stage = heap::cal::ChimeStage::Chords;
  for(const heap::cal::DueChime& c : due) {
    markReminderSent(c.key, now);
    stage = std::max(stage, c.stage);
  }
  using heap::platform::SoundCue;
  const SoundCue cue = stage == heap::cal::ChimeStage::Call   ? SoundCue::MeetCall
                       : stage == heap::cal::ChimeStage::Rise ? SoundCue::MeetRise
                                                              : SoundCue::MeetChords;
  playSound_(static_cast<int>(cue), now);
}

void AppController::playSound_(int cue, const QDateTime& at) {
  using heap::platform::SoundCue;
  if(s_headless || m_soundMuted) {
    return;
  }
  const heap::platform::SoundSettings sound = heap::platform::soundSettingsFrom(settingsMap());
  if(!sound.enabled) {
    return;
  }
  if(m_bulkMoveDepth > 0) {
    // One sound for the whole bulk move: a closed card outweighs a refused one.
    if(m_soundPending != static_cast<int>(SoundCue::Done)) {
      m_soundPending = cue;
    }
    return;
  }
  // Focus mode lets a meeting's chime through when it lets the meeting's
  // reminder through ("let meetings through", on by default).
  const bool chime = cue >= static_cast<int>(SoundCue::MeetChords);
  const bool passMeetings = safetySettings().value(QStringLiteral("immersionPassMeetings"), true).toBool();
  const bool focusHolds =
      heap::safety::immersionDelivery(chime ? QStringLiteral("meeting") : QStringLiteral("sound"), immersion(), passMeetings) ==
      heap::safety::Delivery::Hold;
  const QDateTime when = at.isValid() ? at : QDateTime::currentDateTime();
  if(!heap::platform::soundAllowed(sound, inQuietHours(when), focusHolds, heap::platform::systemBusy())) {
    return;
  }
  heap::platform::playCue(static_cast<SoundCue>(cue), sound.volume);
}

void AppController::previewSound(int volume) {
  heap::platform::playCue(heap::platform::SoundCue::Done, volume);
}

bool AppController::trackerCanWriteStatus(const QString& providerId) const {
  return heap::integrations::writesStatus(providerId);
}

bool AppController::trackerWriteEnabled(const QString& providerId) const {
  // Off unless the user turned it on for this tracker: an existing install
  // has no such key, and that reads as off too (APP-243).
  return heap::integrations::writesStatus(providerId) && settingsMap()
                                                             .value(QStringLiteral("integrations"))
                                                             .toMap()
                                                             .value(providerId)
                                                             .toMap()
                                                             .value(QStringLiteral("writeStatus"))
                                                             .toBool();
}

bool AppController::trackerAskBeforeWrite(const QString& providerId) const {
  const QVariant v =
      settingsMap().value(QStringLiteral("integrations")).toMap().value(providerId).toMap().value(QStringLiteral("askBeforeWrite"));
  return !v.isValid() || v.toBool();
}

void AppController::setTrackerAskBeforeWrite(const QString& providerId, bool ask) {
  if(trackerAskBeforeWrite(providerId) == ask) {
    return;
  }
  setIntegrationField(providerId, QStringLiteral("askBeforeWrite"), ask);
}

void AppController::setTrackerWriteEnabled(const QString& providerId, bool enabled) {
  if(!heap::integrations::writesStatus(providerId) || trackerWriteEnabled(providerId) == enabled) {
    return;
  }
  setIntegrationField(providerId, QStringLiteral("writeStatus"), enabled);
}

bool AppController::trackerWikiToMarkdown(const QString& providerId) const {
  // On unless the user chose to see the wiki markup as it is (APP-313).
  const QVariant v =
      settingsMap().value(QStringLiteral("integrations")).toMap().value(providerId).toMap().value(QStringLiteral("wikiToMarkdown"));
  return !v.isValid() || v.toBool();
}

void AppController::setTrackerWikiToMarkdown(const QString& providerId, bool convert) {
  if(trackerWikiToMarkdown(providerId) == convert) {
    return;
  }
  setIntegrationField(providerId, QStringLiteral("wikiToMarkdown"), convert);
}

QString AppController::trackerDescription(const QString& taskId) const {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return {};
  }
  const Task& t = m_tasks.items().at(row);
  return trackerTextAsMarkdown(t, t.desc);
}

QString AppController::trackerTextAsMarkdown(const Task& t, const QString& text) const {
  // Only Jira Server / Data Center writes wiki markup; Cloud's ADF arrives
  // as markdown already. The card's own address says which it came from
  // (only Atlassian runs *.atlassian.net) - the settings may point at
  // another Jira since; else the connected provider, else the settings'
  // address; with none of them, the text itself.
  if(t.externalProvider != QLatin1String("jira") || !trackerWikiToMarkdown(t.externalProvider)) {
    return text;
  }
  using heap::integrations::JiraDeployment;
  JiraDeployment deployment = JiraDeployment::Unknown;
  if(!t.externalUrl.trimmed().isEmpty()) {
    deployment = heap::integrations::guessJiraDeployment(t.externalUrl.trimmed());
  }
  for(const auto& provider : m_syncProviders) {
    const auto* jira = dynamic_cast<const heap::integrations::JiraProvider*>(provider.get());
    if(jira != nullptr && deployment == JiraDeployment::Unknown) {
      deployment = jira->deployment();
    }
  }
  if(deployment == JiraDeployment::Unknown) {
    const QString base = integrationConfig(t.externalProvider).value(QStringLiteral("baseUrl")).toString().trimmed();
    if(!base.isEmpty()) {
      deployment = heap::integrations::guessJiraDeployment(base);
    }
  }
  if(deployment == JiraDeployment::Cloud) {
    return withTrackerImages(t, text);
  }
  if(deployment == JiraDeployment::Server || heap::md::looksLikeJiraWiki(text)) {
    return withTrackerImages(t, heap::md::jiraWikiToMarkdown(text));
  }
  return withTrackerImages(t, text);
}

namespace {

// A picture bigger than this is not fetched: a ticket's screenshot is a few
// hundred kilobytes, and the attachments store keeps what it fetches.
constexpr qint64 kMaxTrackerImageBytes = 25LL * 1024 * 1024;

}  // namespace

void AppController::loadTrackerImages() const {
  if(m_trackerImagesLoaded) {
    return;
  }
  m_trackerImagesLoaded = true;
  QFile f(dataDir() + QStringLiteral("/tracker-images.json"));
  if(!f.open(QIODevice::ReadOnly)) {
    return;
  }
  const QJsonObject files = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("files")).toObject();
  for(auto it = files.constBegin(); it != files.constEnd(); ++it) {
    const QString id = it.value().toString();
    if(heap::attachments::isValidId(id)) {
      m_trackerImageIds.insert(it.key(), id);
    }
  }
}

QString AppController::withTrackerImages(const Task& t, const QString& markdown) const {
  using heap::integrations::TrackerAttachment;
  using heap::integrations::TrackerImageState;
  if(!markdown.contains(QLatin1String("!["))) {
    return markdown;
  }
  loadTrackerImages();
  const QString provider = t.externalProvider;
  auto* self = const_cast<AppController*>(this);
  return heap::integrations::rewriteTrackerImages(
      markdown, heap::integrations::trackerAttachments(t.externalMeta.details), [this, self, &provider](const TrackerAttachment& f) {
        TrackerImageState st;
        const QString key = provider + QChar('\n') + f.id;
        const QString local = m_trackerImageIds.value(key);
        if(!local.isEmpty() && heap::attachments::Store(attachmentsDir()).contains(local)) {
          st.localId = local;
          return st;
        }
        if(f.size > kMaxTrackerImageBytes) {
          st.error = QStringLiteral("tooBig");
          return st;
        }
        // Once per attachment, and a failure waits 5 minutes, then 10, 20…
        // (FetchBackoff): never again on every render or sync.
        const QDateTime now = fetchNow();
        if(m_trackerImageBackoff.pending(key, now)) {
          st.loading = true;
          return st;
        }
        if(!m_trackerImageBackoff.due(key, now, 0)) {
          st.error = m_trackerImageBackoff.error(key);
          if(st.error.isEmpty()) {
            st.error = QStringLiteral("failed");
          }
          return st;
        }
        st.loading = true;
        m_trackerImageBackoff.asked(key, now);
        // Not from inside a read: the answer may come back at once.
        QMetaObject::invokeMethod(
            self,
            [self, provider, f]() {
              self->fetchTrackerImage(provider, f.id, f.name, f.mime);
            },
            Qt::QueuedConnection);
        return st;
      });
}

void AppController::fetchTrackerImage(const QString& providerId, const QString& attachmentId, const QString& name, const QString& mime) {
  const QString key = providerId + QChar('\n') + attachmentId;
  const auto fail = [this, key](const QString& error) {
    m_trackerImageBackoff.failed(key, error);
    emit trackerImagesChanged();
  };
  heap::integrations::IntegrationProvider* provider = nullptr;
  for(const auto& p : m_syncProviders) {
    if(p->id() == providerId) {
      provider = p.get();
    }
  }
  if(provider == nullptr) {
    fail(QStringLiteral("notConnected"));
    return;
  }
  ensureFreshToken(
      providerId,
      [this, providerId, attachmentId, name, mime, key, fail]() {
        heap::integrations::IntegrationProvider* live = nullptr;
        for(const auto& p : m_syncProviders) {
          if(p->id() == providerId) {
            live = p.get();
          }
        }
        if(live == nullptr) {
          fail(QStringLiteral("notConnected"));
          return;
        }
        // One connection per fetch, dropped once it answers.
        auto* guard = new QObject(this);
        connect(live,
                &heap::integrations::IntegrationProvider::attachmentFetched,
                guard,
                [this, guard, attachmentId, name, mime, key, fail](const QString& id, const QByteArray& bytes, const QString& error) {
                  if(id != attachmentId) {
                    return;
                  }
                  guard->deleteLater();
                  if(!error.isEmpty() || bytes.isEmpty()) {
                    fail(providerReason(error.isEmpty() ? QStringLiteral("empty file") : error));
                    return;
                  }
                  const heap::attachments::AddResult r =
                      heap::attachments::Store(attachmentsDir()).addBytes(bytes, name, mime, kMaxTrackerImageBytes);
                  if(!r.ok()) {
                    fail(r.error == heap::attachments::AddResult::TooLarge ? QStringLiteral("tooBig") : QStringLiteral("failed"));
                    return;
                  }
                  m_trackerImageBackoff.succeeded(key, fetchNow());
                  m_trackerImageIds.insert(key, r.attachment.id);
                  QJsonObject files;
                  for(auto it = m_trackerImageIds.constBegin(); it != m_trackerImageIds.constEnd(); ++it) {
                    files.insert(it.key(), it.value());
                  }
                  QDir().mkpath(dataDir());
                  QSaveFile f(dataDir() + QStringLiteral("/tracker-images.json"));
                  if(f.open(QIODevice::WriteOnly)) {
                    f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("files"), files}})
                                .toJson(QJsonDocument::Compact));
                    static_cast<void>(f.commit());
                  }
                  emit trackerImagesChanged();
                });
        live->fetchAttachment(attachmentId, name);
      },
      [fail]() {
        fail(QStringLiteral("notConnected"));
      });
}

QString AppController::testingColumn() const {
  static const QRegularExpression rx(QStringLiteral("(^|\\b)(test|testing|qa|тест)"), QRegularExpression::CaseInsensitiveOption);
  for(const QVariant& v : m_statuses) {
    const QVariantMap m = v.toMap();
    const QString id = m.value(QStringLiteral("id")).toString();
    if(id == QLatin1String("review")) {
      continue;
    }
    if(rx.match(m.value(QStringLiteral("name")).toString()).hasMatch() || rx.match(id).hasMatch()) {
      return id;
    }
  }
  return {};
}

void AppController::learnTrackerStatusCategory(const QString& providerId, const QString& status, const QString& category) {
  if(status.trimmed().isEmpty() || category.isEmpty()) {
    return;
  }
  m_trackerStatusCategories.insert(providerId + QChar('\n') + status.trimmed().toLower(), category);
}

QString AppController::trackerColumn(const QString& providerId,
                                     const QString& status,
                                     const QHash<QString, QString>& overrides,
                                     const QString& fallback) const {
  using heap::integrations::StatusMap;
  const QString byKind = StatusMap::categoryColumn(m_trackerStatusCategories.value(providerId + QChar('\n') + status.trimmed().toLower()));
  const QString column = StatusMap::column(status, overrides, byKind.isEmpty() ? fallback : byKind);
  // A testing status nobody mapped goes to a testing column the board has.
  if(column == QLatin1String("review") && StatusMap::isTestingStatus(status)) {
    bool overridden = false;
    for(auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
      overridden = overridden || it.key().trimmed().compare(status.trimmed(), Qt::CaseInsensitive) == 0;
    }
    const QString testing = overridden ? QString() : testingColumn();
    if(!testing.isEmpty()) {
      return testing;
    }
  }
  return column;
}

QString AppController::commentCacheKey(const Task& t) {
  return t.externalProvider + QChar('\n') + (t.externalUrl.isEmpty() ? t.externalMeta.project + QChar('\n') + t.externalId : t.externalUrl);
}

QString AppController::commentCachePath() const {
  return heap::paths::dataDir() + QStringLiteral("/ticket-comments.json");
}

void AppController::loadCommentCache() const {
  if(m_commentCacheLoaded) {
    return;
  }
  m_commentCacheLoaded = true;
  QFile f(commentCachePath());
  if(f.open(QIODevice::ReadOnly)) {
    m_commentCache = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("tickets")).toObject();
  }
}

void AppController::storeTicketComments(const Task& t, const QVector<heap::integrations::ExternalComment>& comments, int total) {
  loadCommentCache();
  // A cache, not data: the newest few per ticket, a long one clipped, and
  // only the tickets read most recently.
  constexpr int kKeep = 30;
  constexpr qsizetype kMaxBody = 12000;
  QJsonArray list;
  for(const heap::integrations::ExternalComment& c : comments) {
    if(list.size() >= kKeep) {
      break;
    }
    QString url = c.url;
    if(url.isEmpty() && !t.externalUrl.isEmpty()) {
      url = t.externalUrl + c.anchor;
    }
    list.append(QJsonObject{{QStringLiteral("author"), c.author},
                            {QStringLiteral("body"), c.body.size() > kMaxBody ? c.body.left(kMaxBody) + QChar(0x2026) : c.body},
                            {QStringLiteral("createdAt"), c.createdAt.toString(Qt::ISODateWithMs)},
                            {QStringLiteral("url"), url}});
  }
  m_commentCache.insert(commentCacheKey(t),
                        QJsonObject{{QStringLiteral("comments"), list},
                                    {QStringLiteral("total"), total >= 0 ? total : static_cast<int>(comments.size())},
                                    {QStringLiteral("fetchedAt"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)}});
  heap::integrations::commentcache::trim(m_commentCache);
  // Written a moment later, once for a burst of reads (opening ticket after
  // ticket), not the whole file per read.
  if(m_commentCacheSaveTimer == nullptr) {
    m_commentCacheSaveTimer = new QTimer(this);
    m_commentCacheSaveTimer->setSingleShot(true);
    m_commentCacheSaveTimer->setInterval(2000);
    connect(m_commentCacheSaveTimer, &QTimer::timeout, this, &AppController::writeCommentCache);
  }
  m_commentCacheSaveTimer->start();
}

void AppController::writeCommentCache() {
  if(m_commentCacheSaveTimer != nullptr) {
    m_commentCacheSaveTimer->stop();
  }
  QDir().mkpath(heap::paths::dataDir());
  QSaveFile f(commentCachePath());
  if(f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("tickets"), m_commentCache}})
                .toJson(QJsonDocument::Compact));
    static_cast<void>(f.commit());
  }
}

QVariantMap AppController::ticketComments(const QString& taskId) const {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return {};
  }
  const Task& t = m_tasks.items().at(row);
  if(t.externalProvider.isEmpty()) {
    return {};
  }
  loadCommentCache();
  const QJsonObject entry = m_commentCache.value(commentCacheKey(t)).toObject();
  if(entry.isEmpty()) {
    return {};
  }
  QVariantList out;
  for(const QJsonValue& v : entry.value(QStringLiteral("comments")).toArray()) {
    const QJsonObject c = v.toObject();
    out.append(QVariantMap{
        {QStringLiteral("author"), c.value(QStringLiteral("author")).toString()},
        {QStringLiteral("body"), heap::md::markdownForTextItem(trackerTextAsMarkdown(t, c.value(QStringLiteral("body")).toString()))},
        {QStringLiteral("createdAt"), QDateTime::fromString(c.value(QStringLiteral("createdAt")).toString(), Qt::ISODate)},
        {QStringLiteral("url"), c.value(QStringLiteral("url")).toString()}});
  }
  return {{QStringLiteral("comments"), out},
          {QStringLiteral("total"), entry.value(QStringLiteral("total")).toInt()},
          {QStringLiteral("fetchedAt"), QDateTime::fromString(entry.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate)}};
}

bool AppController::ticketCommentsStale(const QString& taskId) const {
  const QVariantMap cached = ticketComments(taskId);
  const QDateTime at = cached.value(QStringLiteral("fetchedAt")).toDateTime();
  // Opening a ticket reads its comments again after a couple of minutes; a
  // ticket flipped through quickly is not asked for every time.
  return !at.isValid() || at.secsTo(QDateTime::currentDateTime()) > 120;
}

QVariantMap AppController::trackerEstimate(const QString& taskId) const {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return {};
  }
  const QJsonObject e = m_tasks.items().at(row).externalMeta.details.value(QStringLiteral("estimate")).toObject();
  QVariantMap out;
  const auto minutes = [&](const char* from, const char* to) {
    const QJsonValue v = e.value(QLatin1String(from));
    if(v.isDouble()) {
      out.insert(QLatin1String(to), static_cast<int>(std::lround(v.toDouble() / 60.0)));
    }
  };
  minutes("original", "originalMinutes");
  minutes("remaining", "remainingMinutes");
  minutes("spent", "spentMinutes");
  if(e.value(QStringLiteral("points")).isDouble()) {
    out.insert(QStringLiteral("points"), e.value(QStringLiteral("points")).toDouble());
  }
  // What a "1d" / "1w" of this tracker is (item 19): the site's own when it
  // said, Jira's defaults otherwise.
  if(!out.isEmpty()) {
    const QVariantMap tt = integrationConfig(m_tasks.items().at(row).externalProvider).value(QStringLiteral("timeTracking")).toMap();
    const double hours = tt.value(QStringLiteral("hoursPerDay")).toDouble();
    const double days = tt.value(QStringLiteral("daysPerWeek")).toDouble();
    out.insert(QStringLiteral("hoursPerDay"), hours > 0 ? hours : 8.0);
    out.insert(QStringLiteral("daysPerWeek"), days > 0 ? days : 5.0);
  }
  return out;
}

QStringList AppController::trackerWriteProviders() const {
  QStringList out;
  for(const heap::integrations::ProviderDescriptor& d : heap::integrations::providerCatalog()) {
    if(trackerWriteEnabled(d.id)) {
      out.append(d.id);
    }
  }
  return out;
}

QString AppController::trackerWriteNoticeOnce() {
  if(m_trackerWriteNoticeDone) {
    return {};
  }
  m_trackerWriteNoticeDone = true;
  scheduleSave();
  // Only a tracker that used to be written to without asking: one that is
  // connected, can write, and has not been switched on since.
  QStringList names;
  const QVariantMap integrations = settingsMap().value(QStringLiteral("integrations")).toMap();
  for(const heap::integrations::ProviderDescriptor& d : heap::integrations::providerCatalog()) {
    if(heap::integrations::writesStatus(d) && integrations.value(d.id).toMap().value(QStringLiteral("connected")).toBool() &&
       !trackerWriteEnabled(d.id)) {
      names.append(d.displayName);
    }
  }
  if(names.isEmpty()) {
    return {};
  }
  return tr_("int.writeOffNotice").arg(names.join(QStringLiteral(", ")));
}

QString AppController::columnDisplayName(const QString& statusId) const {
  for(const QVariant& v : m_statuses) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("id")).toString() == statusId) {
      return m.value(QStringLiteral("name")).toString();
    }
  }
  return statusId;
}

void AppController::markPushHeld(const QString& taskId, const QString& status, const QString& reason) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  m_tasks.setPushRuntime(taskId, false, reason);
  Task t = m_tasks.items().at(row);
  if(t.externalMeta.unsyncedStatus == status && !t.externalMeta.pushQueued) {
    return;
  }
  t.externalMeta.unsyncedStatus = status;
  t.externalMeta.pushQueued = false;
  m_tasks.upsert(t);
  scheduleSave();
}

void AppController::pushStatusToTracker(const QString& taskId, const QString& status, PushMode mode) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  if(t.externalId.isEmpty() || t.externalProvider.isEmpty()) {
    return;
  }
  // A merge / pull request is read-only in every mode (APP-242): a move of
  // its card, where allowed, stays here.
  if(isReviewItem(t)) {
    return;
  }
  // A tracker heap cannot write to has nothing to send to, and one whose
  // switch is off is only ever read: the move stays a local one (APP-243).
  // The card's own "send" is the user asking for this one write.
  if(!heap::integrations::writesStatus(t.externalProvider)) {
    return;
  }
  if(mode == PushMode::Auto && !trackerWriteEnabled(t.externalProvider)) {
    // The first such move says so on its card, once: moved here only, and
    // where writing is turned on (coach mark). Nothing is turned on.
    if(!t.archived) {
      offerCoachMark(QStringLiteral("trackerMove"),
                     {{QStringLiteral("taskId"), taskId},
                      {QStringLiteral("provider"), t.externalProvider},
                      {QStringLiteral("tracker"), providerDisplayName(t.externalProvider)},
                      {QStringLiteral("remoteStatus"), t.externalMeta.status}});
    }
    return;
  }
  // Outside the filter or gone: read-only for the tracker (APP-204). Nothing
  // goes out on heap's own initiative; the card says the status is unsent.
  // The card's "send" still asks the tracker first, and that check decides.
  if(mode == PushMode::Auto && (t.externalMeta.outOfScope || t.externalMeta.goneUpstream)) {
    markPushHeld(taskId, status, tr_("int.heldOutOfScope"));
    return;
  }
  // Writes on, but the user asked to be asked first (R3-147): held, and the
  // card waits as "not sent" until they answer send or only here.
  if(mode == PushMode::Auto && trackerAskBeforeWrite(t.externalProvider)) {
    const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(t.externalProvider);
    const QString from = t.externalMeta.column.isEmpty() ? QString() : columnDisplayName(t.externalMeta.column);
    const QString key = externalKeyOf(t);
    const QString title = t.title;
    const QString provider = t.externalProvider;
    markPushHeld(taskId, status, QString());
    emit trackerWriteAsk(taskId, key, title, d ? d->displayName : provider, provider, from, columnDisplayName(status));
    return;
  }
  // The write goes to the repo the issue came from, never simply the one in
  // the settings card: after the filter changes, a card kept from the old repo
  // would otherwise close or reopen whatever issue has its number in the new
  // one (INT-1), and under "assigned to me" it would not be sent at all
  // (INT-2). A card without a known repo was pulled from the configured one.
  const QString project = t.externalMeta.project;
  const QString providerId = t.externalProvider;
  const QString externalId = t.externalId;
  if(t.externalMeta.crossProject && project.isEmpty()) {
    // Its number means nothing in the configured repo, and its own is unknown:
    // there is nowhere the write could go. Say so rather than drop the move.
    onTaskPushed(providerId, externalId, project, false, QStringLiteral("the issue's repo is unknown — sync it again first"));
    return;
  }
  // A tracker that is disconnected has no provider to send through. The move
  // used to vanish there (INT-6): flag it and send it after the next pull.
  const bool connected = std::any_of(m_syncProviders.cbegin(), m_syncProviders.cend(), [&providerId](const auto& provider) {
    return provider->id() == providerId;
  });
  if(!connected) {
    queueTrackerPush(taskId, status);
    return;
  }
  // Remembered until the tracker answers, so a failure can name the card and
  // offer to send the same status again.
  const QString key = pushKey(providerId, project, externalId);
  // Every write is preceded by a fresh look at the issue (APP-204): still in
  // the filter, and still in the status heap last saw. Only "send anyway",
  // which the user confirmed with that very answer in front of them, skips it.
  const bool check = mode != PushMode::Confirmed;
  if(check) {
    m_pendingChecks.insert(key, PendingCheck{.taskId = taskId, .status = status, .mode = mode});
  } else {
    m_pendingPushes.insert(key, taskId);
  }
  // The card says "sending" until the tracker answers (APP-163).
  m_tasks.setPushRuntime(taskId, true, QString());
  ensureFreshToken(
      providerId,
      [this, providerId, externalId, status, project, key, taskId, check]() {
        for(const auto& provider : m_syncProviders) {
          if(provider->id() == providerId) {
            if(check) {
              provider->checkIssue(externalId, project);
            } else {
              provider->pushStatusChange(externalId, status, project);
            }
            return;
          }
        }
        // Rebuilt away while the token renewed (signed out meanwhile).
        m_pendingPushes.remove(key);
        m_pendingChecks.remove(key);
        queueTrackerPush(taskId, status);
      },
      [this, key, taskId, status]() {
        // The token could not be renewed — offline, or the session ended.
        // Either way the move has not reached the tracker yet.
        m_pendingPushes.remove(key);
        m_pendingChecks.remove(key);
        queueTrackerPush(taskId, status);
      });
}

void AppController::onIssueChecked(const QString& providerId,
                                   const QString& externalId,
                                   const QString& project,
                                   bool ok,
                                   int httpStatus,
                                   const QString& error,
                                   const QString& remoteStatus,
                                   int filterMatch) {
  const QString key = pushKey(providerId, project, externalId);
  const auto pending = m_pendingChecks.constFind(key);
  if(pending == m_pendingChecks.constEnd()) {
    return;
  }
  const PendingCheck check = *pending;
  m_pendingChecks.erase(pending);
  const int row = m_tasks.indexOfId(check.taskId);
  if(row < 0) {
    return;
  }
  // The card moved again while the check was out: that later move is the one
  // to send, and it asked for its own check.
  const QString status = m_tasks.items().at(row).status;
  if(!ok) {
    if(httpStatus == 0 && error != QStringLiteral("unsupported")) {
      // Never reached the tracker: the same as a push that could not go out.
      m_tasks.setPushRuntime(check.taskId, false, QString());
      queueTrackerPush(check.taskId, status);
      return;
    }
    // A tracker that cannot be asked is not written to, and neither is one
    // that answered the question with an error: the write would be blind.
    onTaskPushed(providerId,
                 externalId,
                 project,
                 false,
                 error == QStringLiteral("unsupported") ? QStringLiteral("the tracker cannot be checked before a write") : error);
    return;
  }
  Task t = m_tasks.items().at(row);
  using heap::integrations::IntegrationProvider;
  if(filterMatch == IntegrationProvider::FilterOut) {
    // Not mine any more (assignee changed, moved out of the JQL…): the write
    // is held and the user decides, with the tracker's status in front of
    // them. heap never sends it on its own.
    t.externalMeta.outOfScope = true;
    if(!remoteStatus.isEmpty()) {
      t.externalMeta.status = remoteStatus;
      t.externalMeta.column = trackerColumn(providerId, remoteStatus, statusOverridesFor(providerId), QStringLiteral("todo"));
    }
    t.externalMeta.unsyncedStatus = status;
    t.externalMeta.pushQueued = false;
    m_tasks.upsert(t);
    m_tasks.setPushRuntime(t.id, false, tr_("int.heldOutOfScope"));
    scheduleSave();
    emit integrationStatesChanged();
    const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
    emit trackerPushNeedsConfirm(t.id, externalKeyOf(t), t.title, d ? d->displayName : providerId, remoteStatus, columnDisplayName(status));
    return;
  }
  // The tracker moved the issue since heap last saw it. Sending now would
  // overwrite that move unseen: a status conflict instead, settled by the
  // user in the existing dialog (APP-163). Moved to where the card already
  // is, the two sides agree and there is nothing to send.
  if(!remoteStatus.isEmpty() && !t.externalMeta.status.isEmpty() && remoteStatus != t.externalMeta.status) {
    const QString mapped = trackerColumn(providerId, remoteStatus, statusOverridesFor(providerId), QStringLiteral("todo"));
    t.externalMeta.status = remoteStatus;
    t.externalMeta.column = mapped;
    t.externalMeta.pushQueued = false;
    if(mapped == status) {
      t.externalMeta.unsyncedStatus.clear();
      heap::integrations::setConflict(t.externalMeta.conflicts, QStringLiteral("status"), false);
      m_tasks.setPushRuntime(t.id, false, QString());
    } else {
      t.externalMeta.unsyncedStatus = status;
      heap::integrations::setConflict(t.externalMeta.conflicts, QStringLiteral("status"), true);
      m_tasks.setPushRuntime(t.id, false, QString());
      const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
      const QString message =
          tr_("int.checkConflict").arg(externalKeyOf(t), d ? d->displayName : providerId, remoteStatus, columnDisplayName(status));
      logEvent(QStringLiteral("warning"), message, {t.id});
      emit toast(message, QStringLiteral("warning"));
    }
    m_tasks.upsert(t);
    scheduleSave();
    return;
  }
  // Still mine and where heap left it: the write goes out.
  m_pendingPushes.insert(key, t.id);
  for(const auto& provider : m_syncProviders) {
    if(provider->id() == providerId) {
      provider->pushStatusChange(externalId, status, project);
      return;
    }
  }
  m_pendingPushes.remove(key);
  m_tasks.setPushRuntime(t.id, false, QString());
  queueTrackerPush(t.id, status);
}

void AppController::confirmTrackerPush(const QString& taskId) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  pushStatusToTracker(taskId, m_tasks.items().at(row).status, PushMode::Confirmed);
}

void AppController::discardTrackerPush(const QString& taskId) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  m_tasks.setPushRuntime(taskId, false, QString());
  if(t.externalMeta.unsyncedStatus.isEmpty() && !t.externalMeta.pushQueued) {
    return;
  }
  t.externalMeta.unsyncedStatus.clear();
  t.externalMeta.pushQueued = false;
  m_tasks.upsert(t);
  scheduleSave();
}

QString AppController::pushKey(const QString& providerId, const QString& project, const QString& externalId) {
  return providerId + QChar('\n') + project + QChar('\n') + externalId;
}

void AppController::onTaskPushed(const QString& providerId,
                                 const QString& externalId,
                                 const QString& project,
                                 bool ok,
                                 const QString& error,
                                 const QString& remoteStatus) {
  const QString taskId = m_pendingPushes.take(pushKey(providerId, project, externalId));
  int row = taskId.isEmpty() ? -1 : m_tasks.indexOfId(taskId);
  if(row < 0) {
    for(int i = 0; i < m_tasks.rowCount(); ++i) {
      const Task& t = m_tasks.items().at(i);
      if(t.externalProvider == providerId && t.externalId == externalId && t.externalMeta.project == project) {
        row = i;
        break;
      }
    }
  }
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  // The flag is what keeps the card where the user put it until the tracker
  // agrees, and what the card shows as "not synced".
  const QString wanted = ok ? QString() : t.status;
  // The issue's status in the tracker is the one just written, not the one the
  // last pull saw. Left stale, a reopen in the tracker would match it and read
  // as "nothing moved" (INT-3). A push the tracker did not describe leaves the
  // base unknown, and the next pull goes by open/closed. A pull-only answer
  // wrote nothing, so the base still holds.
  const bool wrote = ok && error != QStringLiteral("pull-only");
  const bool baseMoves = wrote && t.externalMeta.status != remoteStatus;
  // A status the tracker took settles a status conflict: the user's side is
  // now the tracker's too (APP-163).
  const bool settlesConflict = ok && t.externalMeta.conflicts.contains(QStringLiteral("status"));
  m_tasks.setPushRuntime(t.id, false, ok ? QString() : providerReason(error));
  if(t.externalMeta.unsyncedStatus != wanted || t.externalMeta.pushQueued || baseMoves || settlesConflict) {
    t.externalMeta.unsyncedStatus = wanted;
    // The tracker answered: the move is no longer waiting to be sent.
    t.externalMeta.pushQueued = false;
    if(wrote) {
      t.externalMeta.status = remoteStatus;
    }
    if(settlesConflict) {
      heap::integrations::setConflict(t.externalMeta.conflicts, QStringLiteral("status"), false);
    }
    m_tasks.upsert(t);
    scheduleSave();
  }
  if(wrote) {
    // The history says what went out, not only what came in (APP-165).
    m_history.append(
        activeProfileId(),
        t.id,
        heap::history::HistoryEvent{
            .at = QDateTime::currentDateTime(), .kind = QStringLiteral("pushed"), .from = QString(), .to = t.status, .sync = true});
    emit taskHistoryChanged(t.id);
    scheduleSave();
  }
  if(ok) {
    // The issue sits somewhere else in its workflow now. What it can move to
    // from there is what the last pull saw for an issue of the same kind in
    // that status; with no such issue it is unknown until the next pull says
    // (INT-4: dropping the guard outright let the next bad move go out).
    const QString issueKey = providerId + QChar('\n') + externalId;
    const auto seen = remoteStatus.isEmpty() ? m_workflowTransitions.constEnd()
                                             : m_workflowTransitions.constFind(workflowTransitionsKey(
                                                   providerId, t.externalMeta.project, t.externalMeta.issueType, remoteStatus));
    if(seen != m_workflowTransitions.constEnd()) {
      m_trackerTransitions.insert(issueKey, *seen);
    } else {
      m_trackerTransitions.remove(issueKey);
    }
  }
  if(!ok) {
    qWarning() << providerId << "push failed for" << externalId << ":" << error;
    const QString message = tr_("sync.pushFailed").arg(externalKeyOf(t), providerReason(error));
    logEvent(QStringLiteral("error"), message, {t.id});
    emit trackerPushFailed(t.id, message);
  }
}

bool AppController::secretsInKeychain() const {
  return m_secretStore != nullptr && m_secretStore->usingKeychain();
}

bool AppController::isSafeLink(const QString& url) const {
  return heap::md::isSafeLink(url);
}

void AppController::retryTrackerPush(const QString& taskId) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  pushStatusToTracker(taskId, m_tasks.items().at(row).status, PushMode::Explicit);
}

// Tasks of one column, in the order the board shows them. Ties on rank fall
// back to id so the order is total — two tasks that somehow share a rank must
// not swap places between launches.
QVector<::Task> AppController::columnTasks(const QString& statusId, const QString& excludeId) const {
  QVector<::Task> out;
  for(const ::Task& t : m_tasks.items()) {
    if(t.status == statusId && t.id != excludeId) {
      out.append(t);
    }
  }
  std::sort(out.begin(), out.end(), [](const ::Task& a, const ::Task& b) {
    return a.rank != b.rank ? a.rank < b.rank : a.id < b.id;
  });
  return out;
}

// Spread one column's ranks back out. Only ever called when a gap has shrunk
// past the point where a midpoint is still distinct, which takes about fifty
// consecutive drops into the same gap.
void AppController::rebalanceColumn(const QString& statusId) {
  const QVector<::Task> ordered = columnTasks(statusId, QString());
  QHash<QString, double> ranks;
  ranks.reserve(ordered.size());
  for(int i = 0; i < ordered.size(); ++i) {
    ranks.insert(ordered.at(i).id, (i + 1) * heap::state::kRankStep);
  }
  m_tasks.setRanks(ranks);
}

void AppController::moveTaskTo(const QString& id, const QString& statusId, const QString& beforeTaskId) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0 || statusId.isEmpty() || statusIndexOf(statusId) < 0) {
    return;
  }
  const QString fromStatus = m_tasks.items().at(row).status;
  if(fromStatus != statusId && !canTransitionStatus(id, statusId)) {
    return;  // the column rejected it; do not move it half-way
  }

  const UndoScope scope(this, tr_("task.moveUndone").arg(id));

  // Neighbours are taken from the destination column with the moved card
  // removed, so dropping a card one place down means what it looks like.
  const QVector<::Task> ordered = columnTasks(statusId, id);
  int at = ordered.size();  // empty target id = the end of the column
  if(!beforeTaskId.isEmpty()) {
    for(int i = 0; i < ordered.size(); ++i) {
      if(ordered.at(i).id == beforeTaskId) {
        at = i;
        break;
      }
    }
  }

  const bool hasBefore = at > 0;
  const bool hasAfter = at < ordered.size();
  const double beforeRank = hasBefore ? ordered.at(at - 1).rank : 0.0;
  const double afterRank = hasAfter ? ordered.at(at).rank : 0.0;

  double rank = 0.0;
  if(hasBefore && hasAfter && heap::board::needsRebalance(beforeRank, afterRank)) {
    rebalanceColumn(statusId);
    const QVector<::Task> spread = columnTasks(statusId, id);
    const double lo = at > 0 ? spread.at(at - 1).rank : 0.0;
    const double hi = at < spread.size() ? spread.at(at).rank : 0.0;
    rank = heap::board::between(lo, hi, at > 0, at < spread.size());
  } else {
    rank = heap::board::between(beforeRank, afterRank, hasBefore, hasAfter);
  }

  // The status change carries the recurrence spawn, the focus block and the
  // tracker push with it, so it goes through moveTask rather than being
  // duplicated here. The nested scope records nothing of its own. The rank
  // rides along: set afterwards, it re-sorted the whole destination column
  // and rebound every card in it while the dropped card was still moving
  // (APP-203).
  if(fromStatus != statusId) {
    moveTaskRanked(id, statusId, rank);
    if(m_tasks.items().at(m_tasks.indexOfId(id)).status != statusId) {
      return;  // moveTask declined after all
    }
  } else {
    ::Task t = m_tasks.items().at(m_tasks.indexOfId(id));
    t.rank = rank;
    m_tasks.upsert(t);
  }
  scheduleSave();
}

void AppController::moveSelectedTasksTo(const QString& statusId, const QString& beforeTaskId) {
  pruneSelectionToFilter_();
  if(m_selectedTaskIdsList.isEmpty()) {
    return;
  }
  const UndoScope scope(this, tr_("selection.toast.moved").arg(m_selectedTaskIdsList.size()));
  // Move them in board order and keep inserting before the same card, so the
  // block lands in the order it had rather than reversed.
  QVector<::Task> picked;
  for(const QString& taskId : m_selectedTaskIdsList) {
    const int r = m_tasks.indexOfId(taskId);
    if(r >= 0) {
      picked.append(m_tasks.items().at(r));
    }
  }
  std::sort(picked.begin(), picked.end(), [](const ::Task& a, const ::Task& b) {
    return a.rank != b.rank ? a.rank < b.rank : a.id < b.id;
  });
  // The anchor stays the same for every task: inserting A before X and then B
  // before X puts B between A and X, which keeps the block in its own order.
  for(const ::Task& t : picked) {
    moveTaskTo(t.id, statusId, beforeTaskId);
  }
}

QString AppController::taskIdPrefix() const {
  // What a #KEY-1 reference and a branch name can carry: a letter, then
  // letters and digits. "MY TEAM" or "../../X" made ids nothing could link to
  // and put slashes into branch names (SHELL-10); such a stored prefix, from
  // before the field checked, falls back to the default.
  static const QRegularExpression kPrefix(QStringLiteral("^[A-Z][A-Z0-9]{0,15}$"));
  const QString prefix = settingsMap().value("tasks").toMap().value("idPrefix", QStringLiteral("TASK")).toString().trimmed().toUpper();
  return kPrefix.match(prefix).hasMatch() ? prefix : QStringLiteral("TASK");
}

QVariantMap AppController::newTaskDraft(const QString& statusId) const {
  const QVariantMap tasksCfg = settingsMap().value("tasks").toMap();
  const QString prefix = taskIdPrefix();
  const QString priorityDefault = tasksCfg.value("defaultPriority", QStringLiteral("P2")).toString();
  const QString statusDefault = tasksCfg.value("defaultStatus", QStringLiteral("todo")).toString();

  // saveTask upserts, and upsert on a taken id replaces that row outright —
  // proposing a colliding id is proposing to destroy a task. mintTaskId walks
  // past a persisted high-water mark (a deleted id is never handed out again)
  // and past every id any profile holds, so two workspaces never share one.
  const QString stem = prefix.isEmpty() ? QStringLiteral("TASK") : prefix;
  QVariantMap m;
  m["_isNew"] = true;
  m["id"] = mintTaskId(stem);
  m["title"] = QString();
  m["desc"] = QString();
  m["priority"] = priorityDefault.isEmpty() ? QStringLiteral("P2") : priorityDefault;
  m["status"] = statusId.isEmpty() ? (statusDefault.isEmpty() ? QStringLiteral("todo") : statusDefault) : statusId;
  m["scheduledAt"] = QDateTime();
  m["dueAt"] = QDateTime();
  m["scheduledHasTime"] = false;
  m["dueHasTime"] = false;
  m["branch"] = QString();
  m["labels"] = QVariantList();
  m["estimateMinutes"] = 0;
  m["someday"] = false;
  return m;
}

namespace {
// "ENG-42" → ("ENG", 42). False for anything that is not <stem>-<number>.
bool splitTaskId(const QString& id, QString& stem, int& number) {
  const int dash = id.lastIndexOf(QChar('-'));
  if(dash <= 0 || dash == id.size() - 1) {
    return false;
  }
  bool numeric = false;
  number = id.mid(dash + 1).toInt(&numeric);
  if(!numeric || number < 0) {
    return false;
  }
  stem = id.left(dash);
  return true;
}
}  // namespace

QString AppController::mintTaskId(const QString& stem) const {
  // A workspace that has never minted under this prefix starts at 1 (UX-32);
  // one that has, continues past everything it ever handed out (TASKS-31).
  int next = qMax(1, m_taskSeq.value(stem, 1));
  QSet<QString> taken;
  const auto scan = [&](const QVector<Task>& tasks) {
    for(const Task& t : tasks) {
      taken.insert(t.id);
      QString s;
      int n = 0;
      if(splitTaskId(t.id, s, n) && s == stem && n >= next) {
        next = n + 1;
      }
    }
  };
  // Every profile, not only the active one: ids that collide across
  // workspaces made undo and event links hit the wrong task (TASKS-1).
  scan(m_tasks.items());
  for(const Profile& p : m_profiles) {
    if(p.id != m_activeProfileId) {
      scan(p.tasks);
    }
  }
  QString candidate;
  do {
    candidate = QStringLiteral("%1-%2").arg(stem).arg(next++);
  } while(taken.contains(candidate));
  return candidate;
}

void AppController::noteTaskIdUsed(const QString& id) {
  QString stem;
  int n = 0;
  if(splitTaskId(id, stem, n) && n + 1 > m_taskSeq.value(stem, 1)) {
    m_taskSeq.insert(stem, n + 1);
  }
}

QVariantMap AppController::newQuickTaskDraft(const QString& ticketKey) const {
  QVariantMap m = newTaskDraft(QStringLiteral("todo"));
  // A captured task used to get a "TODO-N" placeholder, which then showed up
  // as the task's name in the notification and on the card. It gets a real id
  // now: the ticket the text names, or the profile prefix. A taken key must not
  // be reused — saveTask upserts, and upserting a taken id replaces that task.
  const QString key = ticketKey.trimmed();
  if(!key.isEmpty() && !ticketKeyTaken(key)) {
    m["id"] = key;
  }
  return m;
}

QString AppController::taskIdForKey(const QString& key) const {
  if(key.isEmpty()) {
    return {};
  }
  if(m_tasks.indexOfId(key) >= 0) {
    return key;
  }
  // A tracker card answers to its key too ("tabless done INC-100"): a local
  // task given that id made two tasks answer to one key (A3-7).
  for(const ::Task& t : m_tasks.items()) {
    if(!t.externalId.isEmpty() && externalKeyOf(t).compare(key, Qt::CaseInsensitive) == 0) {
      return t.id;
    }
  }
  return {};
}

QString AppController::displayKey(const QString& id) const {
  // The name a card shows: a tracker card's key, else its own id (A3-10).
  // A deleted task is gone from the model; its id is all there is.
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return id;
  }
  const QString key = externalKeyOf(m_tasks.items().at(row));
  return key.isEmpty() ? id : key;
}

bool AppController::ticketKeyTaken(const QString& key) const {
  return !taskIdForKey(key).isEmpty();
}

heap::capture::ProfileSigil AppController::profileSigilIn(const QString& raw) const {
  QStringList names;
  names.reserve(m_profiles.size());
  for(const Profile& p : m_profiles) {
    names.append(p.name);
  }
  return heap::capture::findProfileSigil(raw, names);
}

QVariantMap AppController::captureParse(const QString& raw, const QDateTime& reference, const QStringList& rejected) const {
  // "+Side project" names the profile (A5-21); the rest is read without it.
  const heap::capture::ProfileSigil sigil = profileSigilIn(raw);
  const heap::text::TaskMeta meta = heap::text::extractMeta(heap::capture::withoutSigil(raw, sigil));
  QVariantMap out;
  out["profileId"] = sigil.index >= 0 ? m_profiles.at(sigil.index).id : QString();
  out["profileName"] = sigil.index >= 0 ? m_profiles.at(sigil.index).name : QString();
  heap::capture::Parsed p;
  if(m_chrono) {
    p = heap::capture::parse(meta.title, *m_chrono, reference.isValid() ? reference : QDateTime::currentDateTime(), rejected);
  } else {
    p.title = meta.title.simplified();
  }
  out["title"] = p.title;
  out["source"] = meta.title;
  out["when"] = p.when;
  out["whenHasTime"] = p.whenHasTime;
  out["whenEnd"] = p.whenEnd;
  out["whenTz"] = p.whenZone;
  out["whenPast"] = p.whenPast;
  out["due"] = p.due;
  out["dueHasTime"] = p.dueHasTime;
  out["duePast"] = p.duePast;
  out["remind"] = p.remind;
  out["priority"] = meta.priority;
  out["labels"] = meta.labels;
  out["ticketKey"] = meta.ticketKey;
  // A key another task already holds stays in the title (TASKS-22); the
  // input offers to open that task instead (APP-266).
  out["ticketTakenId"] = taskIdForKey(meta.ticketKey);
  out["ticketTaken"] = !out.value("ticketTakenId").toString().isEmpty();
  out["estimateMinutes"] = p.estimateMinutes;
  out["recurrence"] = p.recurrence;
  QVariantList spans;
  for(const heap::capture::Span& s : p.spans) {
    spans.append(QVariantMap{{"start", s.start}, {"end", s.end}, {"kind", s.kind}, {"text", s.text}});
  }
  out["spans"] = spans;
  // The same parts as offsets into `raw` itself, for colouring the field:
  // each found after the one before it, on the first line only.
  QVariantList marks;
  const int firstLineEnd = static_cast<int>(raw.indexOf(QLatin1Char('\n')) < 0 ? raw.size() : raw.indexOf(QLatin1Char('\n')));
  const QString line = raw.left(firstLineEnd);
  const auto mark = [&](const QString& words, const QString& kind) {
    if(words.isEmpty()) {
      return;
    }
    const int at = static_cast<int>(line.indexOf(words, 0, Qt::CaseInsensitive));
    if(at >= 0) {
      marks.append(QVariantMap{{"start", at}, {"end", at + static_cast<int>(words.size())}, {"kind", kind}});
    }
  };
  for(const heap::capture::Span& s : p.spans) {
    mark(s.text, s.kind);
  }
  mark(meta.priorityWord, QStringLiteral("priority"));
  for(const QString& l : meta.labels) {
    mark(QLatin1Char('#') + l, QStringLiteral("label"));
  }
  if(sigil.index >= 0) {
    marks.append(QVariantMap{{"start", sigil.start}, {"end", sigil.end}, {"kind", QStringLiteral("profile")}});
  }
  out["marks"] = marks;
  return out;
}

QVariantMap AppController::quickTaskDraft(const QString& raw, const QDateTime& reference, const QStringList& rejected) const {
  // A ticket key another task already holds cannot be the new task's id
  // (newQuickTaskDraft falls back to the prefix), so it stays in the title:
  // "APP-101 follow up with QA" used to lose its only link to the ticket
  // (TASKS-22, audit 2026-09-30).
  const heap::capture::ProfileSigil sigil = profileSigilIn(raw);
  const QString text = heap::capture::withoutSigil(raw, sigil);
  heap::text::TaskMeta meta = heap::text::extractMeta(text);
  if(ticketKeyTaken(meta.ticketKey)) {
    meta = heap::text::extractMeta(text, /*keepTicketKey=*/true);
  }
  QVariantMap draft = newQuickTaskDraft(meta.ticketKey);
  draft["_isNew"] = true;
  // "+Side project": the profile the capture saves into (A5-21).
  if(sigil.index >= 0) {
    draft["_targetProfileId"] = m_profiles.at(sigil.index).id;
  }

  // The date words and the estimate are cut out of the title. A date after
  // a deadline word ("до пятницы", "due fri") is the deadline; any other is
  // when the task is done (APP-245). One date is no longer both.
  heap::capture::Parsed parsed;
  if(m_chrono) {
    parsed = heap::capture::parse(meta.title, *m_chrono, reference.isValid() ? reference : QDateTime::currentDateTime(), rejected);
  } else {
    parsed.title = meta.title.simplified();
  }
  draft["title"] = parsed.title;
  if(!meta.priority.isEmpty()) {
    draft["priority"] = meta.priority;
  }
  if(!meta.labels.isEmpty()) {
    draft["labels"] = meta.labels;
  }
  if(!meta.desc.isEmpty()) {
    draft["desc"] = meta.desc;
  }
  // The parsed datetimes land on the task itself — including the clock time,
  // which used to survive only as a side calendar block (HEAP-115).
  if(parsed.when.isValid()) {
    draft["scheduledAt"] = parsed.when;
    draft["scheduledHasTime"] = parsed.whenHasTime;
  }
  if(parsed.due.isValid()) {
    draft["dueAt"] = parsed.due;
    draft["dueHasTime"] = parsed.dueHasTime;
  }
  if(parsed.remind.isValid()) {
    draft["remindAt"] = parsed.remind;
  }
  if(parsed.estimateMinutes > 0) {
    draft["estimateMinutes"] = parsed.estimateMinutes;
  }
  // A parsed recurrence ("every weekday…") makes completing the task
  // regenerate it (HEAP-77).
  if(!parsed.recurrence.isEmpty()) {
    draft["recurrence"] = parsed.recurrence;
  }
  return draft;
}

QVector<Profile> AppController::profilesSnapshot() const {
  QVector<Profile> out = m_profiles;
  for(Profile& p : out) {
    if(p.id == m_activeProfileId) {
      // The stored copy of the active profile lags its live models.
      p.tasks = m_tasks.items();
      p.statuses = m_statuses;
    }
  }
  return out;
}

namespace {
// Built-in task/checklist templates (HEAP-77). Checklists are markdown
// `- [ ]` lines in the description — they render as checkboxes in the notes
// preview and stay plain, editable text everywhere else.
struct TaskTemplate {
  QString name;
  QString title;
  QString desc;
  QString priority;
};

const QVector<TaskTemplate>& builtinTemplates() {
  static const QVector<TaskTemplate> kTemplates = {
      {QStringLiteral("PR review"),
       QStringLiteral("Review PR: "),
       QStringLiteral("- [ ] Pull the branch and build\n- [ ] Tests pass locally\n- [ ] Logic + edge cases\n- [ ] Naming / style\n- [ ] "
                      "No leftover debug code\n- [ ] Approve or request changes"),
       QStringLiteral("P2")},
      {QStringLiteral("Release checklist"),
       QStringLiteral("Release "),
       QStringLiteral("- [ ] Version bumped\n- [ ] Changelog updated\n- [ ] CI green on main\n- [ ] Tag pushed\n- [ ] Artifacts signed\n- "
                      "[ ] Release notes published\n- [ ] Announce"),
       QStringLiteral("P1")},
      {QStringLiteral("Bug report"),
       QStringLiteral("Bug: "),
       QStringLiteral(
           "**Steps to reproduce:**\n1. \n\n**Expected:**\n\n**Actual:**\n\n- [ ] Repro confirmed\n- [ ] Root cause\n- [ ] Fix + "
           "test"),
       QStringLiteral("P1")},
  };
  return kTemplates;
}
}  // namespace

QVariantList AppController::taskTemplates() const {
  QVariantList out;
  for(const auto& t : builtinTemplates()) {
    QVariantMap m;
    m["name"] = t.name;
    m["title"] = t.title;
    m["desc"] = t.desc;
    out.append(m);
  }
  return out;
}

void AppController::createTaskFromTemplate(const QString& name) {
  const auto& templates = builtinTemplates();
  const auto it = std::find_if(templates.cbegin(), templates.cend(), [&](const TaskTemplate& t) {
    return t.name == name;
  });
  if(it == templates.cend()) {
    return;
  }
  const QVariantMap draft = newTaskDraft(QStringLiteral("todo"));
  Task t;
  t.id = draft.value("id").toString();
  const UndoScope scope(this, tr_("task.createUndone").arg(t.id));
  t.title = it->title;
  t.desc = it->desc;
  t.priority = it->priority;
  t.status = statusIndexOf(QStringLiteral("todo")) >= 0 || m_statuses.isEmpty() ? QStringLiteral("todo")
                                                                                : m_statuses.constFirst().toMap().value("id").toString();
  t.statusChangedAt = QDateTime::currentDateTime();
  const QVector<::Task> ordered = columnTasks(t.status, t.id);
  t.rank = heap::board::beforeFirst(ordered.isEmpty() ? 0.0 : ordered.first().rank, !ordered.isEmpty());
  m_tasks.upsert(t);
  // The same bookkeeping saveTask does for a new task: without the
  // high-water mark, deleting a template task handed its id to the next
  // task (TASKS-2, audit 2026-09-30; TASKS-31 by the template path).
  noteTaskIdUsed(t.id);
  focusBlockOnStatusChange(t.id, QString(), t.status);
  scheduleSave();
  emit toast(tr_("task.fromTemplate").arg(it->name));
  emit openTaskRequested(t.id, m_activeProfileId);  // open the editor so the user fills in the blank
}

bool AppController::saveTask(const QVariantMap& draft) {
  Task t;
  const bool isNew = draft.value("_isNew").toBool();
  const QString originalId = draft.value("_originalId").toString().trimmed();
  t.id = draft.value("id").toString().trimmed();
  // An editor says which profile it was opened in. Written into any other one,
  // an edit became a same-id copy there and the task itself kept the old text
  // (PRES-1); the editor goes back to its profile before saving, so this is
  // the line nothing is allowed past.
  const QString draftProfile = draft.value("_profileId").toString();
  if(!draftProfile.isEmpty() && draftProfile != m_activeProfileId) {
    emit toast(tr_("task.otherProfile").arg(t.id.isEmpty() ? originalId : t.id), QStringLiteral("warning"));
    return false;
  }
  // A cleared id field on an existing task means "leave the id alone", not
  // "rename it to nothing" — a task with an empty id cannot be opened, moved or
  // deleted again.
  if(t.id.isEmpty() && !isNew) {
    t.id = originalId;
  }
  if(t.id.isEmpty()) {
    emit toast(tr_("task.idRequired"), QStringLiteral("warning"));
    return false;
  }
  // An id is a key: it goes into branch names, commit messages, mentions and
  // the query language, none of which survive a space or a slash in it. Only
  // a new or changed id is checked, so an old one stays openable.
  static const QRegularExpression kBadIdChar(QStringLiteral(R"([\s/\\])"));
  const bool idChanged = isNew || (!originalId.isEmpty() && originalId != t.id);
  if(idChanged && kBadIdChar.match(t.id).hasMatch()) {
    emit toast(tr_("task.idInvalid").arg(t.id));
    return false;
  }
  t.title = draft.value("title").toString();
  t.desc = draft.value("desc").toString();
  t.priority = draft.value("priority").toString();
  t.status = draft.value("status").toString();
  t.branch = draft.value("branch").toString();
  t.recurrence = draft.value("recurrence").toString();
  // Scheduling (HEAP-115). The editors hand over full datetimes and say whether
  // the clock component is real; a caller that only knows a date may still send
  // the legacy `deadline` key, which lands at midnight on both fields.
  t.scheduledAt = draft.value("scheduledAt").toDateTime();
  t.dueAt = draft.value("dueAt").toDateTime();
  // Each datetime says for itself whether its clock is real (schema v10). A
  // caller that still sets the single legacy `hasTime` (no draft this
  // controller hands out carries it) gets the migration's rule.
  if(draft.contains("hasTime")) {
    heap::state::applyLegacyHasTime(t, draft.value("hasTime").toBool());
  } else {
    t.scheduledHasTime = t.scheduledAt.isValid() && draft.value("scheduledHasTime").toBool();
    t.dueHasTime = t.dueAt.isValid() && draft.value("dueHasTime").toBool();
  }
  if(!t.scheduledAt.isValid() && !t.dueAt.isValid() && draft.contains("deadline")) {
    const QDate legacy = draft.value("deadline").toDate();
    if(legacy.isValid()) {
      t.scheduledAt = QDateTime(legacy, QTime(0, 0));
      t.dueAt = t.scheduledAt;
      t.scheduledHasTime = false;
      t.dueHasTime = false;
    }
  }
  t.estimateMinutes = draft.value("estimateMinutes").toInt();
  t.someday = draft.value("someday").toBool();
  t.labels = labelsFromVariant(draft.value("labels").toList());
  t.attachments = heap::attachments::fromVariantList(draft.value("attachments").toList());
  // Every card needs something to show; an edit that blanks the title used to
  // leave a card with nothing on it.
  if(t.title.trimmed().isEmpty()) {
    if(!isNew) {
      emit toast(tr_("task.titleRequired"), QStringLiteral("warning"));
    }
    return false;
  }

  // An id that another task already holds is a destroyed task: the save ends in
  // upsert(), and upsert on a taken id replaces that row whole — no undo is
  // armed, and on the rename path the victim also inherits the renamed task's
  // calendar events. Refuse instead, and say which task is in the way so the
  // editor stays open on the unsaved draft.
  // Ids compare without case: "app-104" and "APP-104" read as the same ticket
  // to a person, so they must not become two tasks.
  const QString heldId = (!isNew && !originalId.isEmpty()) ? originalId : (isNew ? QString() : t.id);
  for(const Task& other : m_tasks.items()) {
    if(other.id != heldId && other.id.compare(t.id, Qt::CaseInsensitive) == 0) {
      emit toast(tr_("task.idTaken").arg(other.id), QStringLiteral("warning"));
      return false;
    }
  }

  const int priorRow = isNew ? -1 : m_tasks.indexOfId(originalId.isEmpty() ? t.id : originalId);
  const Task* prior = priorRow >= 0 ? &m_tasks.items().at(priorRow) : nullptr;
  // A status no column has, or a priority outside P0..P3, hides the card: no
  // column draws it and no filter matches it. Keep what the task had, or fall
  // back to the board's first column and the default priority.
  if(statusIndexOf(t.status) < 0) {
    if(prior != nullptr && statusIndexOf(prior->status) >= 0) {
      t.status = prior->status;
    } else {
      t.status = m_statuses.isEmpty() ? QStringLiteral("todo") : m_statuses.constFirst().toMap().value("id").toString();
    }
  }
  static const QRegularExpression kPriority(QStringLiteral("^P[0-3]$"));
  if(!kPriority.match(t.priority).hasMatch()) {
    t.priority = (prior != nullptr && kPriority.match(prior->priority).hasMatch()) ? prior->priority : QStringLiteral("P2");
  }

  // A column rule that refuses the move (review needs a branch) refuses the
  // whole save, before anything is written: saving the rest and closing the
  // editor lost the status the user picked. The branch typed in this same
  // save counts.
  if(prior != nullptr && prior->status != t.status && t.status == QStringLiteral("review") &&
     settingsMap().value("tasks").toMap().value("requireBranchOnReview", false).toBool() && t.branch.trimmed().isEmpty()) {
    emit toast(tr_("branch.required"));
    playSound_(static_cast<int>(heap::platform::SoundCue::Refuse));
    return false;
  }

  // Everything from here on is one undoable edit: the rename, the re-keyed
  // calendar links and the row itself.
  const UndoScope scope(this, tr_(isNew ? "task.createUndone" : "task.editUndone").arg(t.id));
  const QString statusBefore = prior != nullptr ? prior->status : QString();

  // Preserve fields the editor doesn't expose. Without this, opening an
  // archived ticket and hitting Save silently unarchives it (struct
  // defaults to archived=false), and any edit wipes statusChangedAt,
  // breaking auto-archive-after-N-days. If the draft explicitly carries
  // the flag (round-tripped through taskById), honour it.
  if(!isNew) {
    const QString originalForLookup = draft.value("_originalId").toString().trimmed();
    const QString priorId = originalForLookup.isEmpty() ? t.id : originalForLookup;
    const int existing = m_tasks.indexOfId(priorId);
    if(existing >= 0) {
      const Task& prev = m_tasks.items().at(existing);
      t.archived = draft.contains("archived") ? draft.value("archived").toBool() : prev.archived;
      t.statusChangedAt = prev.statusChangedAt;
      // The editor exposes neither the tracker link nor the timer — carry both.
      t.trackedSeconds = prev.trackedSeconds;
      t.timerStartedAt = prev.timerStartedAt;
      // Recurrence: honour the draft when it carries the key, else preserve.
      t.recurrence = draft.contains("recurrence") ? draft.value("recurrence").toString() : prev.recurrence;
      t.externalId = prev.externalId;
      t.externalUrl = prev.externalUrl;
      t.externalProvider = prev.externalProvider;
      // The editor never shows the tracker-supplied assignee or metadata, and
      // the planning fields are honoured only when the draft carries them.
      t.assignee = prev.assignee;
      t.externalMeta = prev.externalMeta;
      if(!draft.contains("labels")) {
        t.labels = prev.labels;
      }
      if(!draft.contains("estimateMinutes")) {
        t.estimateMinutes = prev.estimateMinutes;
      }
      if(!draft.contains("someday")) {
        t.someday = prev.someday;
      }
      // The editor attaches and detaches files on the stored task directly
      // (each its own undo step), so a save that does not carry the list
      // keeps what the task has.
      if(!draft.contains("attachments")) {
        t.attachments = prev.attachments;
      }
      // The editor shows neither the card's place in its column nor its
      // dependency links. Both used to be reset by any save — the card jumped
      // to the top and its "blocks" links were gone.
      t.rank = prev.rank;
      t.links = prev.links;
      t.extra = prev.extra;  // keys a newer build wrote (PLAT-15)
      // The editor does not carry the local layer (APP-244): any save used
      // to rebuild the task without it. Keep it, then route the priority and
      // due date the editor showed (the effective ones) through it, so a
      // tracker card's tracker fields stay the tracker's.
      t.local = prev.local;
      if(!t.externalId.isEmpty()) {
        const QString chosenPriority = t.priority;
        const QDateTime chosenDue = t.dueAt;
        const bool chosenDueHasTime = t.dueHasTime;
        t.priority = prev.priority;
        t.dueAt = prev.dueAt;
        t.dueHasTime = prev.dueHasTime;
        heap::local::setMyPriority(t, chosenPriority);
        heap::local::setMyDue(t, chosenDue, chosenDueHasTime);
      }
      // A label sent as plain text keeps the colour it already had.
      for(Label& l : t.labels) {
        if(!l.color.isEmpty()) {
          continue;
        }
        for(const Label& old : prev.labels) {
          if(old.id.compare(l.id, Qt::CaseInsensitive) == 0) {
            l.color = old.color;
            break;
          }
        }
      }
    }
  } else {
    // A new task has just entered its column: without the stamp the "Updated"
    // sort lists it last, a task created in Done never auto-archives and one
    // created in Blocked never reads as stuck.
    t.statusChangedAt = QDateTime::currentDateTime();
  }

  // A reminder said in the input ("напомни в 15:00 …", APP-319).
  if(draft.contains("remindAt")) {
    t.local.remindAt = draft.value("remindAt").toDateTime();
    t.local.remindLead = -1;
  }

  // ── Rename path ──
  // If the editor captured an _originalId that differs from the new id,
  // this is an id-change on an existing row. Drop the old row, fix up
  // CalEvent.taskId backlinks, and upsert under the new id. This prevents
  // the previous "duplicate appears after edit" symptom (which used to
  // happen whenever idField text drifted from the stored id).
  if(!isNew && !originalId.isEmpty() && originalId != t.id) {
    const int row = m_tasks.indexOfId(originalId);
    if(row >= 0) {
      // Re-key dependent events first so live bindings update once.
      for(const auto& e : m_events.items()) {
        if(e.taskId == originalId) {
          m_events.setTaskId(e.id, t.id);
        }
      }
      m_tasks.removeById(originalId);
    }
  }

  // A task parked as "someday" belongs in the backlog — send it there whenever
  // the board still has a backlog column (users may rename or drop columns).
  if(t.someday && t.status != QStringLiteral("backlog")) {
    for(const auto& v : m_statuses) {
      if(v.toMap().value("id").toString() == QStringLiteral("backlog")) {
        t.status = QStringLiteral("backlog");
        break;
      }
    }
  }

  // A new card goes to the top of its column: it is the thing the user just
  // thought of, and a card appended below everything else in a long column is
  // a card they have to go looking for.
  if(isNew) {
    const QVector<::Task> ordered = columnTasks(t.status, t.id);
    t.rank = heap::board::beforeFirst(ordered.isEmpty() ? 0.0 : ordered.first().rank, !ordered.isEmpty());
  }

  // A status picked in the editor is the same move as a drag on the board: it
  // goes through moveTask, so the tracker hears about it, the review-branch
  // rule applies and a finished recurring task spawns its next one. The row is
  // saved in its old column first; the nested undo scope records nothing of
  // its own.
  const QString statusAfter = t.status;
  const bool statusMoved = !isNew && !statusBefore.isEmpty() && statusBefore != statusAfter;

  if(statusMoved) {
    t.status = statusBefore;
  }
  m_tasks.upsert(t);
  if(isNew) {
    noteTaskIdUsed(t.id);
    focusBlockOnStatusChange(t.id, QString(), t.status);
    emit toast(tr_("task.created").arg(t.id));
  } else if(statusMoved) {
    moveTask(t.id, statusAfter);
  }
  scheduleSave();
  return true;
}

void AppController::deleteTask(const QString& id) {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return;
  }
  const UndoScope scope(this, tr_("task.restored").arg(id));
  const ::Task removed = m_tasks.items().at(row);
  // A task owns its calendar presence: the meeting event a QuickCapture "sync"
  // spawned, plus any focus blocks dragged onto the day grid. Remove them with
  // the task so a sync is deleted in one action, not two (HEAP-104). The scope
  // records them, so undo puts each block back where it was.
  QStringList linkedEventIds;
  for(const CalEvent& e : m_events.items()) {
    if(e.taskId == id) {
      linkedEventIds << e.id;
    }
  }
  for(const QString& eventId : linkedEventIds) {
    m_events.removeById(eventId);
  }
  // Deleting a mirrored issue is how the user says "not mine". Without a note
  // of that, the next pull re-creates it verbatim and the deletion looks like
  // it never happened. Undo lifts the note again (applyUndoEntry).
  dismissExternalTask(removed.externalProvider, removed.externalId);
  m_tasks.removeById(id);
  const QString removedKey = externalKeyOf(removed);
  emit undoableToast(tr_("task.deleted").arg(removedKey.isEmpty() ? id : removedKey), 5);
  scheduleSave();
}

namespace {

// Where a series whose rule or start the user just set really begins: on the
// first of the rule's own days (heap::cal::firstRuleDay, TIME-5). The event
// moves whole, its end with it.
void startOnRuleDay(CalEvent& e) {
  if(e.rrule.isEmpty() || !e.masterId.isEmpty() || !e.date.isValid()) {
    return;
  }
  const QDate first = heap::cal::firstRuleDay(heap::cal::parseRRule(e.rrule), e.date, e.date);
  if(!first.isValid() || first == e.date) {
    return;
  }
  const qint64 shift = e.date.daysTo(first);
  e.date = first;
  if(e.endDate.isValid()) {
    e.endDate = e.endDate.addDays(shift);
  }
}

}  // namespace

bool AppController::migrateGitWorkingLine(QJsonObject& app) {
  QJsonObject git = app.value(QStringLiteral("git")).toObject();
  if(git.contains(QStringLiteral("workingOnLine"))) {
    return false;
  }
  git.insert(QStringLiteral("workingOnLine"), true);
  app.insert(QStringLiteral("git"), git);
  return true;
}

bool AppController::migrateExampleProfileBindings(QJsonObject& app) {
  QJsonObject integrations = app.value(QStringLiteral("integrations")).toObject();
  bool changed = false;
  for(auto it = integrations.begin(); it != integrations.end(); ++it) {
    QJsonObject cfg = it.value().toObject();
    const QString bound = cfg.value(QStringLiteral("profileId")).toString();
    for(const char* old : kLegacyExampleProfileIds) {
      if(bound == QLatin1String(old)) {
        cfg.insert(QStringLiteral("profileId"), QLatin1String(kExampleProfileId));
        it.value() = cfg;
        changed = true;
      }
    }
  }
  if(changed) {
    app.insert(QStringLiteral("integrations"), integrations);
  }
  return changed;
}

bool AppController::migrateExampleProfileId(QVector<CalEvent>& events) {
  bool renamed = false;
  const QString id = QString::fromLatin1(kExampleProfileId);
  for(const char* oldName : kLegacyExampleProfileIds) {
    const QString old = QString::fromLatin1(oldName);
    const int i = profileIndexOf(old);
    if(i < 0 || profileIndexOf(id) >= 0) {
      continue;
    }
    m_profiles[i].id = id;
    renamed = true;
    for(CalEvent& e : events) {
      if(e.profileId == old) {
        e.profileId = id;
      }
    }
    for(Profile& p : m_profiles) {
      for(Task& t : p.tasks) {
        for(LocalLink& l : t.local.related) {
          if(l.profileId == old) {
            l.profileId = id;
          }
        }
      }
    }
    if(m_activeProfileId == old) {
      m_activeProfileId = id;
    }
  }
  return renamed;
}

bool AppController::migrateLowkeyTheme(QJsonObject& app) {
  QJsonObject appearance = app.value(QStringLiteral("appearance")).toObject();
  const QJsonObject before = appearance;
  appearance.insert(QStringLiteral("darkPreset"), QStringLiteral("heap-ink"));
  appearance.insert(QStringLiteral("lightPreset"), QStringLiteral("heap-light"));
  appearance.remove(QStringLiteral("accent"));  // a pre-theme accent would tint it again
  if(appearance == before) {
    return false;
  }
  app.insert(QStringLiteral("appearance"), appearance);
  return true;
}

QString AppController::mintEventId() {
  return QString("ev-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

QVariantMap AppController::newEventDraft(double startHour, const QDate& date) const {
  QVariantMap m;
  m["id"] = mintEventId();
  m["title"] = tr_("event.newDefault");
  m["type"] = "sync";
  m["start"] = startHour;
  m["end"] = startHour + 1.0;
  m["attendees"] = QString();
  m["date"] = date.isValid() ? date : m_selectedDate;
  m["taskId"] = QString();
  m["profileId"] = m_activeProfileId;  // default attribution: active profile
  m["context"] = QString();
  m["allDay"] = false;
  m["endDate"] = QVariant();  // absent = a single day, which is the common case
  m["rrule"] = QString();     // absent = this event does not repeat
  m["location"] = QString();
  m["notes"] = QString();
  m["url"] = QString();
  m["reminderMinutes"] = CalEvent::kReminderDefault;
  m["tz"] = QString();  // floating: the event is at this hour wherever the user is
  m["_isNew"] = true;
  return m;
}

void AppController::saveEvent(const QVariantMap& draft) {
  if(refuseSubscriptionEdit(draft.value("id").toString(), draft.value("masterId").toString())) {
    return;
  }
  // Creating or editing an event is one undoable step, like a task edit.
  const UndoScope scope(this, tr_("event.editUndone").arg(draft.value("title").toString()));
  CalEvent e;
  e.id = draft.value("id").toString();
  // Every key a draft may omit keeps the stored value: the editors do not all
  // carry every field around, and an ordinary edit must not silently turn a
  // weekly meeting into a single one or wipe its notes.
  const int prevRow = m_events.indexOfId(e.id);
  const CalEvent* prev = prevRow >= 0 ? &m_events.items().at(prevRow) : nullptr;
  const auto str = [&](const char* key, const QString& old) {
    return draft.contains(QLatin1String(key)) ? draft.value(QLatin1String(key)).toString() : old;
  };
  e.title = draft.value("title").toString();
  e.type = draft.value("type").toString();
  e.start = draft.value("start").toDouble();
  e.end = draft.value("end").toDouble();
  e.attendees = draft.value("attendees").toString();
  e.date = draft.value("date").toDate();
  e.taskId = draft.value("taskId").toString();
  // EventEditor no longer exposes profileId — preserve the prior value
  // (round-tripped through showForId) when the draft omits the key, so
  // existing saved attribution survives an edit.
  e.profileId = str("profileId", prev ? prev->profileId : QString());
  e.context = draft.value("context").toString();
  e.allDay = draft.value("allDay").toBool();
  e.endDate = draft.value("endDate").toDate();
  e.rrule = str("rrule", prev ? prev->rrule : QString());
  e.masterId = str("masterId", prev ? prev->masterId : QString());
  e.originalDate = draft.contains("originalDate") ? draft.value("originalDate").toDate() : (prev ? prev->originalDate : QDate());
  e.tz = str("tz", prev ? prev->tz : QString());
  e.location = str("location", prev ? prev->location : QString());
  e.notes = str("notes", prev ? prev->notes : QString());
  e.url = str("url", prev ? prev->url : QString());
  e.reminderMinutes = draft.contains("reminderMinutes") && draft.value("reminderMinutes").isValid()
                          ? draft.value("reminderMinutes").toInt()
                          : (prev ? prev->reminderMinutes : CalEvent::kReminderDefault);
  // The deleted-occurrence list is never in a draft — nothing in the editor
  // edits it — so it is always the stored one.
  if(prev) {
    e.exdates = prev->exdates;
    e.extra = prev->extra;  // keys a newer build wrote (PLAT-15)
  }
  // A series put into another zone by its master (an editor opened by id, a
  // capture just made): its days are the old zone's, and its first meeting
  // may fall on another date over there. Moved as "all" of it, so the rule's
  // weekdays, deletions and moves go with the date, and not one meeting moves
  // in time (A6-7). Without it a Monday 20:00 Moscow series picked into Tokyo
  // turned into Sundays and lost its first meeting.
  if(prev != nullptr && !prev->rrule.isEmpty() && prev->masterId.isEmpty() && e.masterId.isEmpty() && !e.rrule.isEmpty() && !e.allDay &&
     !prev->allDay && e.tz != prev->tz && prev->date.isValid()) {
    QVariantMap d = draft;
    d["masterId"] = e.id;
    d["originalDate"] = prev->date;
    d["tz"] = e.tz;
    saveOccurrence(d, QStringLiteral("all"));
    return;
  }
  // "15:00 UTC+3" typed in Moscow is a local meeting: one moment with the
  // viewer's own offset keeps no zone, or the zone row says the same hour
  // twice. A series keeps it, since the viewer's offset may change.
  if(!e.allDay && e.rrule.isEmpty() && !e.tz.isEmpty() && !e.tz.contains(QLatin1Char('/')) && e.date.isValid()) {
    const QTimeZone fixed(e.tz.toUtf8());
    const QTimeZone local = QTimeZone::systemTimeZone();
    const QDateTime at = heap::cal::wallInstant(e.date, e.start, local);
    if(fixed.isValid() && !fixed.hasDaylightTime() && fixed.offsetFromUtc(at) == local.offsetFromUtc(at)) {
      e.tz.clear();
    }
  }
  // A draft is on the viewer's clock: the editor, a drag and the capture
  // line all speak it. An event kept in a zone stores its wall time there,
  // so a New York meeting stays 10:00 New York (A6-7). A day is a day in
  // every zone.
  if(e.allDay) {
    e.tz.clear();
  } else {
    e = heap::cal::fromViewer(e, QTimeZone::systemTimeZone());
  }
  // A series the user just made, or just gave new days or a new start, begins
  // on one of its days (TIME-5, audit 2026-09-30). One that is only renamed
  // keeps its stored start: an imported series may legally begin off its
  // rule, and that first occurrence is the organiser's.
  if(prev == nullptr || prev->rrule != e.rrule || prev->date != e.date) {
    startOnRuleDay(e);
  }
  storeEvent(e);
}

void AppController::storeEvent(CalEvent e) {
  // A rule this build cannot expand used to be stored anyway and draw as one
  // event, with nothing saying why the series never repeated.
  if(!e.rrule.isEmpty()) {
    QString why;
    if(!heap::cal::parseRRule(e.rrule, &why).isValid()) {
      emit toast(tr_("event.badRule").arg(e.rrule), QStringLiteral("warning"));
      e.rrule.clear();
    }
  }
  if(e.rrule.isEmpty() && e.masterId.isEmpty()) {
    // A single event keeps no deletions. It keeps its zone, though: a
    // meeting set for 10:00 New York used to lose it on the first edit and
    // stop following New York's clocks (A6-7).
    e.exdates.clear();
  }
  // The editor parses free-typed times and a multi-day event may legally end
  // before it starts by the clock, so the whole span is normalized in one
  // place rather than clamped edge by edge.
  const heap::cal::Span span = heap::cal::normalizeSpan(e.date, e.endDate, e.start, e.end, e.allDay, snapStepHours());
  e.date = span.date;
  e.endDate = (span.endDate == span.date) ? QDate() : span.endDate;
  e.start = span.start;
  e.end = span.end;
  const int prevRow = m_events.indexOfId(e.id);
  const std::optional<CalEvent> before = prevRow >= 0 ? std::optional<CalEvent>(m_events.items().at(prevRow)) : std::nullopt;
  m_events.upsert(e);
  if(before) {
    followFocusBlock(*before, &e);
  }
  scheduleSave();
}

void AppController::updateEvent(const QString& id, double start, double end, const QDate& date) {
  if(refuseSubscriptionEdit(id)) {
    return;
  }
  const int row = m_events.indexOfId(id);
  if(row < 0) {
    return;
  }
  CalEvent e = m_events.items().at(row);
  const CalEvent before = e;
  // A drag on the calendar is an edit like any other: Ctrl+Z puts it back.
  const UndoScope scope(this, tr_("event.editUndone").arg(e.title));
  // The grid speaks the viewer's clock. A zoned event is re-expressed in it
  // before the drag is applied, or the new hours would be read in New York,
  // and written back into its zone after, so it keeps it (A6-7).
  const QString keptTz = e.allDay ? QString() : e.tz;
  if(heap::cal::zoneOf(e).isValid() && !e.allDay) {
    e = heap::cal::localized(e, QTimeZone::systemTimeZone());
    e.tz = keptTz;
  }
  const auto upsertKept = [&](const CalEvent& shown) {
    const CalEvent stored = heap::cal::fromViewer(shown, QTimeZone::systemTimeZone());
    m_events.upsert(stored);
    return stored;
  };

  // Dragging a multi-day or all-day block moves the whole span: the grid can
  // only ever hand back one day's worth of hours, and reading them as the new
  // extent of the event would silently collapse it onto the day it was
  // dropped on.
  const bool spans = e.allDay || (e.endDate.isValid() && e.endDate > e.date);
  if(spans) {
    if(date.isValid() && e.date.isValid() && e.rrule.isEmpty()) {
      const qint64 shift = e.date.daysTo(date);
      e.date = date;
      if(e.endDate.isValid()) {
        e.endDate = e.endDate.addDays(shift);
      }
    }
    e = upsertKept(e);
    followFocusBlock(before, &e);
    scheduleSave();
    return;
  }

  const heap::cal::HourRange hours = heap::cal::clampHours(start, end, snapStepHours());
  e.start = hours.start;
  e.end = hours.end;
  // A series' date is its first occurrence. Handing it the day one later
  // occurrence was dragged on re-dated the whole series and made every
  // earlier one vanish; moving occurrences goes through moveOccurrence.
  if(date.isValid() && e.rrule.isEmpty()) {
    e.date = date;
  }
  e = upsertKept(e);
  followFocusBlock(before, &e);
  scheduleSave();
}

namespace {

// An occurrence as the QML views read it. The same keys newEventDraft() uses,
// so an editor can be opened on either without knowing which it has.
QVariantMap occurrenceToVariant(const CalEvent& e) {
  QVariantMap m;
  m["id"] = e.id;
  m["title"] = e.title;
  m["type"] = e.type;
  m["start"] = e.start;
  m["end"] = e.end;
  m["attendees"] = e.attendees;
  m["date"] = e.date;
  m["taskId"] = e.taskId;
  m["profileId"] = e.profileId;
  m["context"] = e.context;
  m["allDay"] = e.allDay;
  m["endDate"] = e.endDate.isValid() ? QVariant(e.endDate) : QVariant();
  m["rrule"] = e.rrule;
  m["masterId"] = e.masterId;
  m["originalDate"] = e.originalDate.isValid() ? QVariant(e.originalDate) : QVariant();
  m["tz"] = e.tz;
  m["location"] = e.location;
  m["notes"] = e.notes;
  m["url"] = e.url;
  m["reminderMinutes"] = e.reminderMinutes;
  return m;
}

// The rule without its end: two rules that differ only in COUNT or UNTIL put
// occurrences on the same days, so deletions and moved occurrences still
// line up with the new one.
//
// Given the series' start, a weekly rule's implicit weekday is spelled out, so
// "FREQ=WEEKLY" and "FREQ=WEEKLY;BYDAY=MO" on a Monday series read the same.
QString rulePattern(const QString& text, const QDate& anchor = QDate()) {
  heap::cal::RRule r = heap::cal::parseRRule(text);
  if(!r.isValid()) {
    return text;
  }
  if(anchor.isValid() && r.freq == heap::cal::RRule::Weekly && r.byDay.isEmpty()) {
    r.byDay = {heap::cal::WeekdayNum{0, anchor.dayOfWeek()}};
  }
  r.count = 0;
  r.until = QDate();
  r.untilAt = QDateTime();
  return heap::cal::toRRuleText(r);
}

// A weekly rule's weekdays follow its occurrence when the series is moved by
// days: dragging Monday's meeting to Tuesday for "all events" makes it a
// Tuesday meeting, not a Monday one with a Tuesday start.
QString shiftWeekdays(const QString& text, qint64 days) {
  heap::cal::RRule r = heap::cal::parseRRule(text);
  const int shift = static_cast<int>(((days % 7) + 7) % 7);
  if(!r.isValid() || shift == 0 || r.byDay.isEmpty() || (r.freq != heap::cal::RRule::Weekly && r.freq != heap::cal::RRule::Daily)) {
    return text;
  }
  for(heap::cal::WeekdayNum& w : r.byDay) {
    w.day = ((w.day - 1 + shift) % 7) + 1;
  }
  std::sort(r.byDay.begin(), r.byDay.end(), [](const heap::cal::WeekdayNum& a, const heap::cal::WeekdayNum& b) {
    return a.ord != b.ord ? a.ord < b.ord : a.day < b.day;
  });
  return heap::cal::toRRuleText(r);
}

// The rule a series has once the occurrence on `from` is dragged to `to` for
// every event. A weekly rule's weekdays shift (shiftWeekdays). A monthly or
// yearly one that names its day used to keep it: moving "all" of a series on
// the 15th to the 16th re-dated only the first occurrence and left the rest,
// the dragged one included, on the 15th (TIME-7, audit 2026-09-30). The day
// is now re-read from where the occurrence landed, as Google does: the 16th,
// or "the third Wednesday" for a second Tuesday dropped on the 15th.
// std::nullopt when the rule cannot say the new day in its own terms (several
// ordinal weekdays, BYSETPOS, a weekday-and-monthday intersection).
std::optional<QString> moveRuleDays(const QString& text, const QDate& from, const QDate& to) {
  heap::cal::RRule r = heap::cal::parseRRule(text);
  const qint64 days = (from.isValid() && to.isValid()) ? from.daysTo(to) : 0;
  if(!r.isValid() || days == 0) {
    return text;
  }
  if(r.freq == heap::cal::RRule::Weekly || r.freq == heap::cal::RRule::Daily) {
    return shiftWeekdays(text, days);
  }
  const bool namesDay = !r.byMonthDay.isEmpty() || !r.byDay.isEmpty();
  if(namesDay && !r.bySetPos.isEmpty()) {
    return std::nullopt;
  }
  if(!r.byMonthDay.isEmpty() && !r.byDay.isEmpty()) {
    return std::nullopt;
  }
  if(!r.byMonth.isEmpty() && to.month() != from.month()) {
    if(r.byMonth.size() != 1) {
      return std::nullopt;
    }
    r.byMonth = {to.month()};
  }
  if(r.byMonthDay.size() == 1) {
    r.byMonthDay = {r.byMonthDay.first() > 0 ? to.day() : to.day() - to.daysInMonth() - 1};
  } else if(!r.byMonthDay.isEmpty()) {
    for(int& md : r.byMonthDay) {
      const int moved = md + static_cast<int>(days);
      if((md > 0 && (moved < 1 || moved > 31)) || (md < 0 && (moved > -1 || moved < -31))) {
        return std::nullopt;
      }
      md = moved;
    }
    std::sort(r.byMonthDay.begin(), r.byMonthDay.end());
  }
  bool anyOrd = false;
  for(const heap::cal::WeekdayNum& w : r.byDay) {
    anyOrd = anyOrd || w.ord != 0;
  }
  if(anyOrd) {
    if(r.byDay.size() != 1) {
      return std::nullopt;
    }
    // Counted within the month, or within the year for a yearly rule with no
    // month (monthCandidates / periodDates count the same way).
    const bool inYear = r.freq == heap::cal::RRule::Yearly && r.byMonth.isEmpty();
    const int pos = inYear ? to.dayOfYear() : to.day();
    const int span = inYear ? to.daysInYear() : to.daysInMonth();
    const int ord = r.byDay.first().ord > 0 ? (pos - 1) / 7 + 1 : -((span - pos) / 7 + 1);
    r.byDay = {heap::cal::WeekdayNum{ord, to.dayOfWeek()}};
  } else if(!r.byDay.isEmpty()) {
    // "Every Monday of the month": the weekdays shift like a weekly rule's.
    const int shift = static_cast<int>(((days % 7) + 7) % 7);
    for(heap::cal::WeekdayNum& w : r.byDay) {
      w.day = ((w.day - 1 + shift) % 7) + 1;
    }
    std::sort(r.byDay.begin(), r.byDay.end(), [](const heap::cal::WeekdayNum& a, const heap::cal::WeekdayNum& b) {
      return a.ord != b.ord ? a.ord < b.ord : a.day < b.day;
    });
  }
  return heap::cal::toRRuleText(r);
}

// Where occurrence `d` of the old series is in the new one: the same place in
// the sequence. A series whose rule was re-read for a new day (moveRuleDays)
// does not move its deleted and moved occurrences by a fixed number of days —
// the second Tuesday becoming the third Wednesday is 8 days one month and 1
// the next. An invalid date when `d` is not one of the old series' days.
QDate mapOccurrence(const QString& oldText, const QDate& oldStart, const QString& newText, const QDate& newStart, const QDate& d) {
  heap::cal::RRule oldRule = heap::cal::parseRRule(oldText);
  heap::cal::RRule newRule = heap::cal::parseRRule(newText);
  if(!oldRule.isValid() || !newRule.isValid() || !d.isValid() || d < oldStart) {
    return {};
  }
  oldRule.count = 0;
  oldRule.until = QDate();
  oldRule.untilAt = QDateTime();
  const QVector<QDate> before = heap::cal::expand(oldRule, oldStart, oldStart, d);
  if(before.isEmpty() || before.last() != d) {
    return {};
  }
  newRule.count = static_cast<int>(before.size());
  newRule.until = QDate();
  newRule.untilAt = QDateTime();
  const QVector<QDate> after = heap::cal::expand(newRule, newStart, newStart, d.addYears(100));
  return after.size() == before.size() ? after.last() : QDate();
}

}  // namespace

QVariantList AppController::eventOccurrences(const QDate& from, const QDate& to) const {
  QVariantList out;
  QDate covered;
  const QVector<heap::cal::Occurrence> occurrences = heap::cal::expandEvents(m_events.items(), from, to, &covered);
  if(covered.isValid() && covered < to) {
    qWarning() << "eventOccurrences: range" << from << "…" << to << "cut short at" << covered;
  }
  for(const heap::cal::Occurrence& o : occurrences) {
    QVariantMap m = occurrenceToVariant(o.event);
    // An occurrence of a series carries no rule of its own, and must not look
    // as if it did: handed back to saveOccurrence, an empty "rrule" reads as
    // "stop repeating". The rule is the master's (eventSeriesMaster).
    if(!o.event.masterId.isEmpty()) {
      m.remove("rrule");
    }
    m["occurrenceDate"] = o.occurrenceDate;
    m["generated"] = o.generated;
    // The zone it is kept in: handed back to saveEvent / saveOccurrence with
    // these viewer-clock hours, it stays there (A6-7).
    m["tz"] = o.zone;
    // Kept in another zone (A6-4): the start on that zone's clock.
    m["srcTz"] = o.srcTz;
    m["srcStart"] = o.srcStart;
    out.append(m);
  }
  return out;
}

namespace {

QVariantMap blockToVariant(const heap::plan::Block& b) {
  return {{"kind", b.kind},
          {"id", b.id},
          {"title", b.title},
          {"eventType", b.eventType},
          {"status", b.status},
          {"category", b.category},
          {"attendees", b.attendees},
          {"occurrence", b.occurrence},
          {"profileName", b.profileName},
          {"profileId", b.profileId},
          {"start", b.start},
          {"end", b.end},
          {"past", b.past},
          {"fromPrevDay", b.fromPrevDay},
          {"toNextDay", b.toNextDay},
          {"srcTz", b.srcTz},
          {"srcStart", b.srcStart},
          {"realStart", b.realStart},
          {"realEnd", b.realEnd},
          {"totalMinutes", b.totalMinutes},
          {"firstDay", b.firstDay},
          {"lastDay", b.lastDay},
          {"dayNo", b.dayNo},
          {"dayCount", b.dayCount},
          {"overlapsWith", b.overlapsWith}};
}

// Meetings from every profile that touch `date`: the day before too, for one
// across midnight.
QVector<heap::plan::EventIn> dayEvents(const QVector<CalEvent>& all, const QDate& date) {
  QVector<heap::plan::EventIn> events;
  for(const heap::cal::Occurrence& o : heap::cal::expandEvents(all, date.addDays(-1), date)) {
    heap::plan::EventIn e;
    e.id = o.event.id;
    e.title = o.event.title;
    e.type = o.event.type;
    e.taskId = o.event.taskId;
    e.attendees = o.event.attendees;
    e.date = o.event.date;
    e.endDate = o.event.endDate;
    e.start = o.event.start;
    e.end = o.event.end;
    e.allDay = o.event.allDay;
    e.occurrence = o.occurrenceDate.toString(Qt::ISODate);
    e.srcTz = o.srcTz;
    e.srcStart = o.srcStart;
    events.append(e);
  }
  return events;
}

}  // namespace

QVariantMap AppController::nextEventAfter(const QDateTime& now) const {
  const double hour = now.time().hour() + now.time().minute() / 60.0;
  const heap::plan::EventIn* best = nullptr;
  const QVector<heap::plan::EventIn> events = dayEvents(m_events.items(), now.date());
  for(const heap::plan::EventIn& e : events) {
    if(e.allDay || e.date != now.date() || e.start < hour) {
      continue;
    }
    if(!best || e.start < best->start) {
      best = &e;
    }
  }
  if(!best) {
    return {};
  }
  const int mins = static_cast<int>(std::lround(best->start * 60.0));
  return {{QStringLiteral("id"), best->id},
          {QStringLiteral("title"), best->title},
          {QStringLiteral("time"), QDateTime(now.date(), QTime(mins / 60 % 24, mins % 60))}};
}

void AppController::doNotDisturbFor(int minutes, const QDateTime& now) {
  QVariantMap s = settingsMap();
  QVariantMap notif = s.value(QStringLiteral("notifications")).toMap();
  notif.insert(QStringLiteral("dndUntil"), now.addSecs(qint64(qMax(0, minutes)) * 60).toString(Qt::ISODate));
  s.insert(QStringLiteral("notifications"), notif);
  setAppSettingsJson(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(s)).toJson(QJsonDocument::Compact)));
}

QVariantMap AppController::todayData(const QDate& date, bool allProfiles) const {
  QVariantMap out;
  const QDateTime now = QDateTime::currentDateTime();
  const QVector<heap::plan::EventIn> events = dayEvents(m_events.items(), date);

  // The tasks: this profile's, or every profile's with its name.
  struct Src {
    const Task* task;
    QString profileName;
    bool own;
    QString profileId;
  };

  QVector<Src> all;
  for(const Task& t : m_tasks.items()) {
    all.append({&t, QString(), true, QString()});
  }
  // Another profile's row says whose it is (A1-17): its name and colour.
  QHash<QString, QString> colorOf;
  QVector<Profile> snapshot;
  if(allProfiles) {
    snapshot = profilesSnapshot();
    for(const Profile& p : std::as_const(snapshot)) {
      if(p.id == m_activeProfileId) {
        continue;
      }
      colorOf.insert(p.id, p.color);
      for(const Task& t : p.tasks) {
        all.append({&t, p.name, false, p.id});
      }
    }
  }
  const auto category = [this](const Src& s) {
    return s.own ? statusCategory(s.task->status) : heap::board::defaultCategoryFor(s.task->status);
  };

  // Every profile's block is as long as its own profile draws it: its
  // estimate, else the focus block length (F1-new-11). Another profile's
  // used to fall back to a flat hour.
  const int noEstimateMin = taskBlockMinutes(QString());
  QVector<heap::plan::TaskIn> timed;
  for(const Src& s : std::as_const(all)) {
    const Task& t = *s.task;
    if(t.archived || !t.scheduledHasTime || !t.scheduledAt.isValid()) {
      continue;
    }
    heap::plan::TaskIn in;
    in.id = t.id;
    in.title = t.title;
    in.status = t.status;
    in.category = category(s);
    in.scheduledAt = t.scheduledAt;
    in.minutes = heap::integrations::planMinutes(t) > 0 ? heap::integrations::planMinutes(t) : noEstimateMin;
    in.profileName = s.profileName;
    in.profileId = s.profileId;
    timed.append(in);
  }

  const heap::plan::Day day = heap::plan::buildDay(date, now, events, timed, m_workdayStart, m_workdayEnd, isWorkDay(date), lunchSpan());
  out["date"] = day.date;
  out["workday"] = day.workday;
  out["workStart"] = day.workStart;
  out["workEnd"] = day.workEnd;
  out["lunchStart"] = day.lunchStart;
  out["lunchEnd"] = day.lunchEnd;
  out["fromHour"] = day.fromHour;
  out["toHour"] = day.toHour;
  QVariantList allDay;
  for(const heap::plan::Block& b : day.allDay) {
    allDay.append(blockToVariant(b));
  }
  out["allDay"] = allDay;
  QVariantList blocks;
  // Who a task waits on (R3-002): the day row and the deadline line say it.
  QHash<QString, QString> waitingName;
  for(const WaitingOn& w : m_waitingOn) {
    const int row = m_people.indexOfId(w.personId);
    if(row >= 0) {
      waitingName.insert(w.taskId, m_people.items().at(row).name);
    }
  }
  int meetings = static_cast<int>(day.allDay.size());
  int planned = 0;
  for(const heap::plan::Block& b : day.blocks) {
    QVariantMap bm = blockToVariant(b);
    if(!b.profileId.isEmpty()) {
      bm["profileColor"] = colorOf.value(b.profileId);
    }
    if(b.kind == QLatin1String("meeting")) {
      ++meetings;
    } else {
      ++planned;
      bm["waiting"] = b.profileName.isEmpty() ? waitingName.value(b.id) : QString();
    }
    blocks.append(bm);
  }
  out["blocks"] = blocks;
  QVariantList free;
  for(const heap::plan::Gap& g : day.free) {
    free.append(QVariantMap{{"start", g.start}, {"end", g.end}});
  }
  out["free"] = free;
  out["load"] =
      QVariantMap{{"meetings", day.load.meetings}, {"tasks", day.load.tasks}, {"free", day.load.free}, {"overWork", day.load.overWork}};

  // In progress, deadlines, overdue, undated.
  QVariantList inProgress;
  QVariantList dayOnly;
  QVariantList deadlines;
  QVariantList overdue;
  QVector<heap::plan::DueEntry> dueEntries;
  const int horizon = deadlineHorizon();
  int dueToday = 0;
  int undated = 0;
  for(const Src& s : std::as_const(all)) {
    const Task& t = *s.task;
    const QString cat = category(s);
    if(t.archived || cat == QLatin1String("done")) {
      continue;
    }
    const QDateTime due = heap::local::effectiveDueAt(t);
    const bool dueHasTime = heap::local::effectiveDueHasTime(t);
    if(cat == QLatin1String("prog") || (s.own && isDoingStatus(t.status))) {
      QVariantMap m{{"id", t.id},
                    {"title", t.title},
                    {"status", t.status},
                    {"category", cat},
                    {"isTiming", t.timerStartedAt.isValid()},
                    {"branch", t.branch},
                    {"profileName", s.profileName},
                    {"profileId", s.profileId},
                    {"profileColor", colorOf.value(s.profileId)}};
      if(t.id == m_focusedTaskId) {
        m["repo"] = m_focusedRepoState;
      }
      inProgress.append(m);
    }
    // Like "undated": its link opens this profile's list, so it counts this
    // profile's tasks only (A1-17).
    if(s.own && due.isValid() && due.date() < date && date == m_today) {
      overdue.append(QVariantMap{{"id", t.id},
                                 {"title", t.title},
                                 {"category", cat},
                                 {"due", due},
                                 {"profileName", s.profileName},
                                 {"profileId", s.profileId},
                                 {"profileColor", colorOf.value(s.profileId)}});
    }
    if(due.isValid() && due.date() == date) {
      ++dueToday;
    }
    // Listed even when the task also has a block in the day (R3-001): the
    // facts line counts it, so the list shows it too. The day and the
    // horizon's days after it ("Ближайшие", dogfood 2026-10-19).
    if(due.isValid() && heap::plan::dueWithin(due.date(), date, horizon)) {
      heap::plan::DueEntry entry;
      entry.index = static_cast<int>(deadlines.size());
      entry.due = due;
      entry.hasTime = dueHasTime;
      entry.priority = heap::local::effectivePriority(t);
      dueEntries.append(entry);
      deadlines.append(QVariantMap{{"id", t.id},
                                   {"waiting", s.own ? waitingName.value(t.id) : QString()},
                                   {"title", t.title},
                                   {"status", t.status},
                                   {"category", cat},
                                   {"priority", heap::local::effectivePriority(t)},
                                   {"due", due},
                                   {"dueHasTime", dueHasTime},
                                   {"tomorrow", due.date() != date},
                                   {"dayOffset", static_cast<int>(date.daysTo(due.date()))},
                                   {"profileName", s.profileName},
                                   {"profileId", s.profileId},
                                   {"profileColor", colorOf.value(s.profileId)}});
    }
    // Its link opens this profile's list, so it counts this profile's.
    if(s.own && !t.someday && !due.isValid() && !t.scheduledAt.isValid()) {
      ++undated;
    }
    // Planned for the day without a time (IDIOT-CAL-10): "fix login
    // tomorrow" showed on the week's column and nowhere on Today.
    if(t.scheduledAt.isValid() && !t.scheduledHasTime && t.scheduledAt.date() == date) {
      dayOnly.append(QVariantMap{{"kind", "task"},
                                 {"id", t.id},
                                 {"title", t.title},
                                 {"status", t.status},
                                 {"category", cat},
                                 {"dayOnly", true},
                                 {"waiting", s.own ? waitingName.value(t.id) : QString()},
                                 {"profileName", s.profileName},
                                 {"profileId", s.profileId},
                                 {"profileColor", colorOf.value(s.profileId)}});
      ++planned;
    }
  }
  out["inProgress"] = inProgress;
  out["dayOnly"] = dayOnly;
  // By day, the earliest first; in a day by the hour, then the ones due by
  // its end (H2-Today "Сроки", Q-List's day groups). `deadlines` stays flat
  // in that order; `deadlineDays` says where each day starts in it.
  QVariantList sortedDeadlines;
  QVariantList deadlineDays;
  sortedDeadlines.reserve(deadlines.size());
  for(const heap::plan::DueDay& d : heap::plan::groupDueByDay(dueEntries, m_today)) {
    deadlineDays.append(QVariantMap{{"date", d.date},
                                    {"offset", d.offset},
                                    {"kind", d.kind},
                                    {"first", static_cast<int>(sortedDeadlines.size())},
                                    {"count", static_cast<int>(d.members.size())}});
    for(const int i : d.members) {
      sortedDeadlines.append(deadlines.at(i));
    }
  }
  out["deadlines"] = sortedDeadlines;
  out["deadlineDays"] = deadlineDays;
  out["deadlineHorizon"] = horizon;
  out["overdue"] = overdue;
  out["undated"] = undated;
  // The week's last working day: Today offers the weekly recap there, so it
  // is found where the week ends instead of only in the command line.
  bool lastWorkday = isWorkDay(date);
  for(QDate d = date.addDays(1); lastWorkday && d.dayOfWeek() != 1; d = d.addDays(1)) {
    lastWorkday = !isWorkDay(d);
  }
  out["recapDay"] = lastWorkday;
  out["facts"] = QVariantMap{{"meetings", meetings}, {"planned", planned}, {"dueToday", dueToday}};

  // Whom to write: the people list's open asks.
  out["people"] = openPings();
  return out;
}

QVariantList AppController::openPings() const {
  QVariantList people;
  for(const Person& p : m_people.items()) {
    if(p.state == QLatin1String("todo")) {
      people.append(QVariantMap{{"id", p.id}, {"name", p.name}, {"question", p.question}, {"color", p.color}});
    }
  }
  return people;
}

QVariantMap AppController::dayLoad(const QDate& date) const {
  const QVariantMap d = todayData(date, false);
  return d.value("load").toMap();
}

QVariantMap AppController::eventSeriesMaster(const QString& masterId) const {
  const int row = m_events.indexOfId(masterId);
  return row >= 0 ? occurrenceToVariant(m_events.items().at(row)) : QVariantMap();
}

bool AppController::isValidRRule(const QString& rule) const {
  return heap::cal::parseRRule(rule).isValid();
}

QVariantMap AppController::eventById(const QString& id) const {
  const int row = m_events.indexOfId(id);
  if(row < 0) {
    return {};
  }
  // On the viewer's clock, like every occurrence the views hand the editor,
  // with the zone it is kept in beside it (A6-7): what saveEvent reads back.
  const CalEvent& stored = m_events.items().at(row);
  const QTimeZone local = QTimeZone::systemTimeZone();
  const QTimeZone zone = heap::cal::zoneOf(stored);
  if(!zone.isValid() || stored.allDay) {
    return occurrenceToVariant(stored);
  }
  QVariantMap m = occurrenceToVariant(heap::cal::localized(stored, local));
  m["tz"] = stored.tz;
  if(zone.id() != local.id()) {
    m["srcTz"] = stored.tz;
    m["srcStart"] = stored.start;
  }
  return m;
}

QVariantList AppController::timeZoneSearch(const QString& query) const {
  QVariantList out;
  for(const heap::cal::ZoneMatch& z : heap::cal::searchZones(query, QDateTime::currentDateTimeUtc(), recentEventZones())) {
    const bool ru = m_language == QStringLiteral("ru");
    out.append(QVariantMap{{"id", z.id},
                           {"city", ru ? heap::cal::zoneCityIn(z.id, m_language) : z.city},
                           {"region", ru ? heap::cal::zoneRegionRu(z.region) : z.region},
                           {"offset", heap::cal::offsetLabel(z.offset)}});
  }
  return out;
}

QStringList AppController::recentEventZones() const {
  // The zones the user's own meetings are kept in, the latest first: what
  // "recent" means without keeping a list of its own.
  QVector<const CalEvent*> zoned;
  for(const CalEvent& e : m_events.items()) {
    if(!e.allDay && heap::cal::zoneOf(e).isValid() && e.date.isValid()) {
      zoned.append(&e);
    }
  }
  std::sort(zoned.begin(), zoned.end(), [](const CalEvent* a, const CalEvent* b) {
    return a->date > b->date;
  });
  QStringList out;
  for(const CalEvent* e : zoned) {
    if(!out.contains(e->tz)) {
      out.append(e->tz);
    }
    if(out.size() >= 5) {
      break;
    }
  }
  return out;
}

double AppController::zoneHour(const QDate& date, double hour, const QString& tz) const {
  const QTimeZone zone(tz.toUtf8());
  if(tz.isEmpty() || !zone.isValid() || !date.isValid()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  QDate d;
  double h = 0.0;
  heap::cal::toZoneWall(date, hour, QTimeZone::systemTimeZone(), zone, &d, &h);
  return h;
}

int AppController::zoneDayShift(const QDate& date, double hour, const QString& tz) const {
  const QTimeZone zone(tz.toUtf8());
  if(tz.isEmpty() || !zone.isValid() || !date.isValid()) {
    return 0;
  }
  QDate d;
  double h = 0.0;
  heap::cal::toZoneWall(date, hour, QTimeZone::systemTimeZone(), zone, &d, &h);
  return static_cast<int>(date.daysTo(d));
}

QString AppController::zoneName(const QString& tz) const {
  // In the UI's language where a name is known (F3-EYES-16).
  return heap::cal::zoneCityIn(tz, m_language);
}

QString AppController::localZoneId() const {
  return QString::fromUtf8(QTimeZone::systemTimeZone().id());
}

QString AppController::zoneOffsetLabel(const QString& tz) const {
  const QTimeZone zone = tz.isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(tz.toUtf8());
  return zone.isValid() ? heap::cal::offsetLabel(zone.offsetFromUtc(QDateTime::currentDateTimeUtc())) : QString();
}

bool AppController::isLocalZone(const QString& tz) const {
  if(tz.isEmpty()) {
    return true;
  }
  const QTimeZone zone(tz.toUtf8());
  const QTimeZone local = QTimeZone::systemTimeZone();
  if(!zone.isValid()) {
    return false;
  }
  if(zone.id() == local.id()) {
    return true;
  }
  // "UTC+3" in Moscow: the viewer's own hours, said as an offset.
  const QDateTime now = QDateTime::currentDateTimeUtc();
  return !tz.contains(QLatin1Char('/')) && !zone.hasDaylightTime() && zone.offsetFromUtc(now) == local.offsetFromUtc(now);
}

void AppController::saveOccurrence(const QVariantMap& draft, const QString& scope) {
  if(refuseSubscriptionEdit(draft.value("id").toString(), draft.value("masterId").toString())) {
    return;
  }
  const UndoScope editScope(this, tr_("event.editUndone").arg(draft.value("title").toString()));
  const QString masterId = draft.value("masterId").toString();
  const QDate original = draft.value("originalDate").toDate();
  const int masterRow = masterId.isEmpty() ? -1 : m_events.indexOfId(masterId);

  // Not part of a series (or the series is gone): an ordinary save.
  if(masterId.isEmpty() || !original.isValid() || masterRow < 0) {
    if(masterId.isEmpty() || scope == QStringLiteral("all")) {
      saveEvent(draft);
    }
    return;
  }
  const CalEvent master = m_events.items().at(masterRow);

  // "this": one occurrence, stored as an override that names the date it
  // replaces. The master is untouched, so the rest of the series does not
  // move. The draft is in the viewer's clock, so the override floats.
  if(scope != QStringLiteral("all") && scope != QStringLiteral("following")) {
    QVariantMap d = draft;
    const int existing = m_events.indexOfId(draft.value("id").toString());
    const bool isStoredOverride = existing >= 0 && !m_events.items().at(existing).masterId.isEmpty();
    if(!isStoredOverride) {
      d["id"] = mintEventId();
    }
    d["rrule"] = QString();
    d["masterId"] = masterId;
    d["originalDate"] = original;
    // The draft is on the viewer's clock; saveEvent writes it into the zone
    // the occurrence is kept in, or the one the editor picked (A6-7).
    if(!d.contains("tz")) {
      d["tz"] = master.tz;
    }
    saveEvent(d);
    return;
  }

  // The edited occurrence, written in the series' own clock. For a floating
  // series that is the draft as given; for one that kept its source zone the
  // viewer's hours are converted back into it. A zone picked in the editor
  // is the series' from now on (A6-7).
  const bool allDay = draft.value("allDay").toBool();
  const QString draftTz = draft.contains("tz") ? draft.value("tz").toString() : master.tz;
  const QString targetTz = allDay ? QString() : draftTz;
  const bool zoneChanged = targetTz != master.tz;
  const QTimeZone zone = targetTz.isEmpty() ? QTimeZone() : QTimeZone(targetTz.toUtf8());
  QDate newDate = draft.value("date").toDate();
  double newStart = draft.value("start").toDouble();
  QDate newEndDate = draft.value("endDate").toDate();
  if(!newEndDate.isValid() || newEndDate < newDate) {
    newEndDate = newDate;
  }
  double newEnd = draft.value("end").toDouble();
  if(zone.isValid() && !allDay) {
    const QTimeZone local = QTimeZone::systemTimeZone();
    heap::cal::toZoneWall(newDate, newStart, local, zone, &newDate, &newStart);
    heap::cal::toZoneWall(newEndDate, newEnd, local, zone, &newEndDate, &newEnd);
    if(newEnd <= 1e-9 && newEndDate > newDate) {
      newEndDate = newEndDate.addDays(-1);
      newEnd = 24.0;
    }
  }
  const qint64 lengthDays = newDate.isValid() ? std::max<qint64>(0, newDate.daysTo(newEndDate)) : 0;
  const qint64 dayShift = (newDate.isValid() && original.isValid()) ? original.daysTo(newDate) : 0;
  // The rule as the editor now has it. A draft that does not mention one
  // (a drag) keeps the series' own.
  const QString draftRule = draft.contains("rrule") ? draft.value("rrule").toString() : master.rrule;

  const auto applyDraftFields = [&](CalEvent& e) {
    e.tz = targetTz;
    e.title = draft.value("title").toString();
    e.type = draft.value("type").toString();
    e.attendees = draft.value("attendees").toString();
    e.context = draft.value("context").toString();
    e.allDay = allDay;
    e.start = newStart;
    e.end = newEnd;
    for(const char* key : {"location", "notes", "url"}) {
      if(draft.contains(QLatin1String(key))) {
        const QString v = draft.value(QLatin1String(key)).toString();
        if(qstrcmp(key, "location") == 0) {
          e.location = v;
        } else if(qstrcmp(key, "notes") == 0) {
          e.notes = v;
        } else {
          e.url = v;
        }
      }
    }
    if(draft.contains("reminderMinutes") && draft.value("reminderMinutes").isValid()) {
      e.reminderMinutes = draft.value("reminderMinutes").toInt();
    }
  };
  // Every override of this master whose occurrence is on or after `from`.
  const auto overridesFrom = [&](const QDate& from) {
    QStringList ids;
    for(const CalEvent& e : m_events.items()) {
      if(e.masterId == masterId && e.originalDate.isValid() && e.originalDate >= from) {
        ids << e.id;
      }
    }
    return ids;
  };

  if(scope == QStringLiteral("all")) {
    CalEvent m = master;
    applyDraftFields(m);
    if(draftRule.isEmpty()) {
      // The repeat was removed: what is left is one event, the occurrence the
      // user was looking at. The series' exceptions have nothing to except.
      m.rrule.clear();
      m.exdates.clear();
      m.date = newDate;
      m.endDate = lengthDays > 0 ? newDate.addDays(lengthDays) : QDate();
      for(const QString& id : overridesFrom(QDate(1, 1, 1))) {
        m_events.removeById(id);
      }
      storeEvent(m);
      return;
    }
    // A moved occurrence (a stored override) is measured against where it
    // sits now, not the date it replaced: renaming Monday's meeting that was
    // moved to Wednesday is not a request to make the series a Wednesday one.
    // Its time only becomes the series' when the edit changed it.
    std::optional<CalEvent> moved;
    {
      const int editedRow = m_events.indexOfId(draft.value("id").toString());
      if(editedRow >= 0 && m_events.items().at(editedRow).masterId == masterId) {
        moved = m_events.items().at(editedRow);
      }
    }
    const QDate draftDate = draft.value("date").toDate();
    const QDate draftEndDate = draft.value("endDate").toDate();
    const double draftStart = draft.value("start").toDouble();
    const double draftEnd = draft.value("end").toDouble();
    qint64 shift = dayShift;
    qint64 seriesLength = lengthDays;
    if(moved.has_value()) {
      shift = (moved->date.isValid() && draftDate.isValid()) ? moved->date.daysTo(draftDate) : 0;
      const qint64 movedSpan = (moved->endDate.isValid() && moved->endDate > moved->date) ? moved->date.daysTo(moved->endDate) : 0;
      const qint64 draftSpan =
          (draftDate.isValid() && draftEndDate.isValid() && draftEndDate > draftDate) ? draftDate.daysTo(draftEndDate) : 0;
      const bool timeKept = !zoneChanged && allDay == moved->allDay && movedSpan == draftSpan &&
                            (allDay || (std::abs(draftStart - moved->start) < 1e-9 && std::abs(draftEnd - moved->end) < 1e-9));
      if(timeKept) {
        m.allDay = master.allDay;
        m.start = master.start;
        m.end = master.end;
        seriesLength = (master.endDate.isValid() && master.endDate > master.date) ? master.date.daysTo(master.endDate) : 0;
      }
    }
    // Moving one occurrence of "all" moves the anchor by the same number of
    // days, and the end with it — the end used to stay behind, so every
    // occurrence became a multi-day event.
    // A rule that names its day takes the day the occurrence landed on
    // (TIME-7); one that cannot say it is not moved at all rather than moved
    // in part.
    const std::optional<QString> movedRule = moveRuleDays(master.rrule, original, original.addDays(shift));
    if(!movedRule.has_value() && draftRule == master.rrule) {
      emit toast(tr_("event.cannotMoveSeries"), QStringLiteral("warning"));
      return;
    }
    const QString shiftedOld = movedRule.value_or(master.rrule);
    // Re-read rather than shifted: its days are not a fixed distance from the
    // old ones, and the series starts on the first of them in the month the
    // old start moves to.
    const bool reread = shiftedOld != shiftWeekdays(master.rrule, shift);
    m.date = master.date.addDays(shift);
    if(reread) {
      const QDate first = heap::cal::firstRuleDay(
          heap::cal::parseRRule(shiftedOld), QDate(m.date.year(), m.date.month(), 1), QDate(m.date.year(), m.date.month(), 1));
      m.date = first.isValid() ? first : m.date;
    }
    m.endDate = seriesLength > 0 ? m.date.addDays(seriesLength) : QDate();
    const auto movedDay = [&](const QDate& d) {
      const QDate mapped = reread ? mapOccurrence(master.rrule, master.date, shiftedOld, m.date, d) : QDate();
      return mapped.isValid() ? mapped : d.addDays(shift);
    };
    // Compared by the days they produce: "FREQ=WEEKLY;BYDAY=MO" on a Monday
    // series (how Google and Outlook store it) is the editor's "FREQ=WEEKLY".
    const bool likeShifted = rulePattern(draftRule, m.date) == rulePattern(shiftedOld, m.date);
    const bool patternChanged = !likeShifted && rulePattern(draftRule, m.date) != rulePattern(master.rrule, master.date);
    if(patternChanged || (!likeShifted && draftRule != master.rrule)) {
      m.rrule = draftRule;
    } else {
      // The same days: the series keeps its own spelling of the rule and
      // takes only the end the editor may have changed.
      m.rrule = shiftedOld;
      heap::cal::RRule kept = heap::cal::parseRRule(shiftedOld);
      const heap::cal::RRule edited = heap::cal::parseRRule(draftRule);
      if(draftRule != master.rrule && kept.isValid() && edited.isValid() &&
         (kept.count != edited.count || kept.until != edited.until || kept.untilAt != edited.untilAt)) {
        kept.count = edited.count;
        kept.until = edited.until;
        kept.untilAt = edited.untilAt;
        m.rrule = heap::cal::toRRuleText(kept);
      }
    }
    if(patternChanged) {
      // New days: the old deletions and moves were about other dates, and the
      // series begins on the first of the new ones (TIME-5).
      startOnRuleDay(m);
      m.exdates.clear();
      for(const QString& id : overridesFrom(QDate(1, 1, 1))) {
        m_events.removeById(id);
      }
    } else {
      // The same series, maybe a few days later: its exceptions move with it,
      // or a deleted occurrence would come back and a moved one would revert.
      // A moved occurrence also takes what the edit changed — a new title is
      // the whole series' — unless it had its own value there already.
      for(QDate& d : m.exdates) {
        d = movedDay(d);
      }
      const auto follow = [](QString& own, const QString& was, const QString& now) {
        if(now != was && own == was) {
          own = now;
        }
      };
      for(const QString& id : overridesFrom(QDate(1, 1, 1))) {
        const CalEvent before = m_events.items().at(m_events.indexOfId(id));
        CalEvent ov = before;
        ov.originalDate = movedDay(ov.originalDate);
        // The override goes with its slot (IDIOT-CAL-12): only the slot
        // moved, and the meeting sat a day before it, next to a regular one.
        const qint64 slotShift = before.originalDate.daysTo(ov.originalDate);
        if(slotShift != 0 && ov.date.isValid()) {
          ov.date = ov.date.addDays(slotShift);
          if(ov.endDate.isValid()) {
            ov.endDate = ov.endDate.addDays(slotShift);
          }
        }
        follow(ov.title, master.title, m.title);
        follow(ov.type, master.type, m.type);
        follow(ov.attendees, master.attendees, m.attendees);
        follow(ov.context, master.context, m.context);
        follow(ov.location, master.location, m.location);
        follow(ov.notes, master.notes, m.notes);
        follow(ov.url, master.url, m.url);
        if(m.reminderMinutes != master.reminderMinutes && ov.reminderMinutes == master.reminderMinutes) {
          ov.reminderMinutes = m.reminderMinutes;
        }
        if(ov != before) {
          m_events.upsert(ov);
        }
      }
      // The moved occurrence that was edited is part of "all" too: it takes
      // the edit as it stands in the editor, in the viewer's clock like any
      // override, and stays where it was moved unless the edit moved it.
      const int movedRow = moved.has_value() ? m_events.indexOfId(moved->id) : -1;
      if(movedRow >= 0) {
        CalEvent ov = m_events.items().at(movedRow);
        applyDraftFields(ov);
        ov.start = draftStart;
        ov.end = draftEnd;
        ov.date = draftDate.isValid() ? draftDate : ov.date;
        ov.endDate = (draftEndDate.isValid() && draftEndDate > ov.date) ? draftEndDate : QDate();
        m_events.upsert(heap::cal::fromViewer(ov, QTimeZone::systemTimeZone()));
      }
    }
    storeEvent(m);
    return;
  }

  // "following": split the series. The old master stops the day before this
  // occurrence and a new one starts here carrying the edit. Deletions and
  // moved occurrences before the split stay with the old half; those after it
  // go to the new one, so nothing already changed comes back or reverts.
  heap::cal::RRule oldRule = heap::cal::parseRRule(master.rrule);
  CalEvent head = master;
  int usedBefore = 0;
  if(oldRule.isValid()) {
    usedBefore = static_cast<int>(heap::cal::expand(oldRule, master.date, master.date, original.addDays(-1)).size());
    oldRule.endOn(original.addDays(-1));
    head.rrule = heap::cal::toRRuleText(oldRule);
  }
  QVector<QDate> kept;
  QVector<QDate> carried;
  for(const QDate& d0 : master.exdates) {
    (d0 < original ? kept : carried).append(d0);
  }
  head.exdates = kept;

  CalEvent tail = master;
  applyDraftFields(tail);
  tail.id = mintEventId();
  tail.masterId.clear();
  tail.originalDate = QDate();
  tail.date = newDate;
  tail.endDate = lengthDays > 0 ? newDate.addDays(lengthDays) : QDate();
  tail.exdates.clear();
  // The new half repeats by the editor's rule. A COUNT carried over from the
  // old one is what is LEFT of it: a ten-time series split at the sixth is
  // five more, not ten more.
  //
  // Whether the editor kept the series' rule is a question about the days and
  // the end it describes, not its spelling: the editor hands an imported
  // "FREQ=WEEKLY;BYDAY=TU;COUNT=6" back as "FREQ=WEEKLY;COUNT=6", and the text
  // compare left the new half all six again — nine in total (TIME-3, audit
  // 2026-09-30).
  const std::optional<QString> movedRule = moveRuleDays(master.rrule, original, newDate);
  const QString shiftedMaster = movedRule.value_or(shiftWeekdays(master.rrule, dayShift));
  const bool reread = movedRule.has_value() && shiftedMaster != shiftWeekdays(master.rrule, dayShift);
  const heap::cal::RRule draftParsed = heap::cal::parseRRule(draftRule);
  const heap::cal::RRule masterParsed = heap::cal::parseRRule(master.rrule);
  const bool sameDays =
      draftRule == master.rrule || (draftParsed.isValid() && rulePattern(draftRule, newDate) == rulePattern(shiftedMaster, newDate));
  const bool sameEnd = draftParsed.isValid() && masterParsed.isValid() && draftParsed.count == masterParsed.count &&
                       draftParsed.until == masterParsed.until && draftParsed.untilAt == masterParsed.untilAt;
  const bool keepsRule = draftRule == master.rrule || (sameDays && sameEnd);
  heap::cal::RRule tailRule = heap::cal::parseRRule(keepsRule ? shiftedMaster : draftRule);
  const bool samePattern = sameDays || rulePattern(draftRule) == rulePattern(master.rrule);
  if(tailRule.isValid() && keepsRule && tailRule.count > 0) {
    tailRule.count = std::max(1, tailRule.count - usedBefore);
  }
  tail.rrule = tailRule.isValid() ? heap::cal::toRRuleText(tailRule) : QString();
  if(!keepsRule) {
    startOnRuleDay(tail);  // new days: the new half begins on one (TIME-5)
  }
  const auto carriedDay = [&](const QDate& d0) {
    const QDate mapped = reread ? mapOccurrence(master.rrule, original, tail.rrule, tail.date, d0) : QDate();
    return mapped.isValid() ? mapped : d0.addDays(dayShift);
  };
  if(!tail.rrule.isEmpty() && samePattern) {
    for(const QDate& d0 : carried) {
      tail.exdates.append(carriedDay(d0));
    }
  }

  const UndoScope undo(this, tr_("undo.splitSeries"));
  for(const QString& id : overridesFrom(original)) {
    CalEvent ov = m_events.items().at(m_events.indexOfId(id));
    if(tail.rrule.isEmpty() || !samePattern || ov.originalDate == original) {
      // The edited occurrence itself is replaced by the new half's first;
      // a new pattern has other days; a single event has no occurrences.
      m_events.removeById(id);
      continue;
    }
    ov.masterId = tail.id;
    ov.originalDate = carriedDay(ov.originalDate);
    m_events.upsert(ov);
  }
  // The split may land on the very first occurrence, and then there is
  // nothing left of the old half: it goes rather than lingering empty.
  if(oldRule.isValid() && master.date.isValid() && original <= master.date) {
    m_events.removeById(master.id);
  } else {
    m_events.upsert(head);
  }
  storeEvent(tail);
}

void AppController::moveOccurrence(const QVariantMap& occurrence, double deltaHours, const QString& scope) {
  if(refuseSubscriptionEdit(occurrence.value("id").toString(), occurrence.value("masterId").toString())) {
    return;
  }
  const QString id = occurrence.value("id").toString();
  if(id.isEmpty() || !std::isfinite(deltaHours)) {
    return;
  }
  const bool allDay = occurrence.value("allDay").toBool();
  const QDate date = occurrence.value("date").toDate();
  if(!date.isValid()) {
    return;
  }
  QDate endDate = occurrence.value("endDate").toDate();
  if(!endDate.isValid() || endDate < date) {
    endDate = date;
  }
  const double start = occurrence.value("start").toDouble();
  const double end = occurrence.value("end").toDouble();

  // The whole event moves by the drag, snapped: start and end as instants on
  // the viewer's wall clock, so a piece after midnight moves the event it is
  // part of instead of re-dating it.
  const double step = snapStepHours();
  const double delta = allDay ? std::round(deltaHours / 24.0) * 24.0 : std::round(deltaHours / step) * step;
  if(std::abs(delta) < 1e-9) {
    return;
  }
  const auto shifted = [&](const QDate& d, double h, QDate* outD, double* outH) {
    const double total = h + delta;
    const int days = static_cast<int>(std::floor(total / 24.0));
    *outD = d.addDays(days);
    *outH = total - (24.0 * days);
  };
  QDate nd;
  QDate ned;
  double ns = start;
  double ne = end;
  if(allDay) {
    const int days = static_cast<int>(std::lround(delta / 24.0));
    nd = date.addDays(days);
    ned = endDate.addDays(days);
  } else {
    shifted(date, start, &nd, &ns);
    shifted(endDate, end, &ned, &ne);
    // An end on midnight belongs to the day before.
    if(ne <= 1e-9 && ned > nd) {
      ned = ned.addDays(-1);
      ne = 24.0;
    }
  }

  QVariantMap d = occurrence;
  d["date"] = nd;
  d["endDate"] = ned > nd ? QVariant(ned) : QVariant();
  d["start"] = ns;
  d["end"] = ne;
  d.remove("rrule");  // a drag never changes the rule
  if(d.value("masterId").toString().isEmpty()) {
    // A single event: the stored row is the occurrence.
    d.remove("occurrenceDate");
    d.remove("generated");
    saveEvent(d);
    return;
  }
  d["originalDate"] = occurrence.contains("occurrenceDate") ? occurrence.value("occurrenceDate") : occurrence.value("originalDate");
  saveOccurrence(d, scope);
}

void AppController::resizeOccurrence(const QVariantMap& occurrence, double start, double end, const QString& scope) {
  if(refuseSubscriptionEdit(occurrence.value("id").toString(), occurrence.value("masterId").toString())) {
    return;
  }
  const QString id = occurrence.value("id").toString();
  if(id.isEmpty() || occurrence.value("allDay").toBool()) {
    return;
  }
  const heap::cal::HourRange hours = heap::cal::clampHours(start, end, snapStepHours());
  QVariantMap d = occurrence;
  d["start"] = hours.start;
  d["end"] = hours.end;
  d["endDate"] = QVariant();
  d.remove("rrule");
  if(d.value("masterId").toString().isEmpty()) {
    d.remove("occurrenceDate");
    d.remove("generated");
    saveEvent(d);
    return;
  }
  d["originalDate"] = occurrence.contains("occurrenceDate") ? occurrence.value("occurrenceDate") : occurrence.value("originalDate");
  saveOccurrence(d, scope);
}

void AppController::deleteOccurrence(const QString& masterId, const QDate& occurrenceDate, const QString& scope) {
  if(refuseSubscriptionEdit(QString(), masterId)) {
    return;
  }
  const int masterRow = m_events.indexOfId(masterId);
  if(masterRow < 0) {
    return;
  }
  const QString title = m_events.items().at(masterRow).title;

  if(scope == QStringLiteral("all")) {
    UndoScope undo(this, tr_("undo.deleteSeries"));
    undo.setRedoLabel(tr_("event.seriesDeleted").arg(title));
    // The overrides go with it: an override without its master is a ghost.
    QStringList doomed;
    for(const CalEvent& e : m_events.items()) {
      if(e.masterId == masterId) {
        doomed << e.id;
      }
    }
    for(const QString& id : doomed) {
      m_events.removeById(id);
    }
    m_events.removeById(masterId);
    // Like a single event's delete: said, and undoable from the toast.
    emit undoableToast(tr_("event.seriesDeleted").arg(title), 5);
    scheduleSave();
    return;
  }

  if(!occurrenceDate.isValid()) {
    return;
  }

  CalEvent master = m_events.items().at(masterRow);

  if(scope == QStringLiteral("following")) {
    heap::cal::RRule rule = heap::cal::parseRRule(master.rrule);
    UndoScope undo(this, tr_("undo.deleteFollowing"));
    undo.setRedoLabel(tr_("event.followingDeleted").arg(title));
    QStringList doomed;
    for(const CalEvent& e : m_events.items()) {
      if(e.masterId == masterId && e.originalDate.isValid() && e.originalDate >= occurrenceDate) {
        doomed << e.id;
      }
    }
    for(const QString& id : doomed) {
      m_events.removeById(id);
    }
    if(rule.isValid()) {
      rule.endOn(occurrenceDate.addDays(-1));
      master.rrule = heap::cal::toRRuleText(rule);
    }
    if(rule.isValid() && master.date.isValid() && occurrenceDate <= master.date) {
      m_events.removeById(master.id);
    } else {
      QVector<QDate> kept;
      for(const QDate& d0 : master.exdates) {
        if(d0 < occurrenceDate) {
          kept.append(d0);
        }
      }
      master.exdates = kept;
      m_events.upsert(master);
    }
    emit undoableToast(tr_("event.followingDeleted").arg(title), 5);
    scheduleSave();
    return;
  }

  // "this": remember the hole rather than rewriting the series.
  UndoScope undo(this, tr_("undo.deleteOccurrence"));
  undo.setRedoLabel(tr_("event.deleted").arg(title));
  QStringList doomed;
  for(const CalEvent& e : m_events.items()) {
    if(e.masterId == masterId && e.originalDate == occurrenceDate) {
      doomed << e.id;
    }
  }
  for(const QString& id : doomed) {
    m_events.removeById(id);
  }
  if(!master.exdates.contains(occurrenceDate)) {
    master.exdates.append(occurrenceDate);
    std::sort(master.exdates.begin(), master.exdates.end());
  }
  m_events.upsert(master);
  emit undoableToast(tr_("event.deleted").arg(title), 5);
  scheduleSave();
}

// ── Calendar subscriptions (APP-118) ─────────────────────────────────

namespace {
constexpr int kCalSubDefaultMinutes = heap::cal::kSubscriptionMinutesDefault;
constexpr qint64 kCalSubMaxBytes = 20LL * 1024 * 1024;
const QString kCalSubSecretProvider = QStringLiteral("calsub");
}  // namespace

QVariantList AppController::calendarSubscriptionSettings() const {
  const QJsonObject settings = QJsonDocument::fromJson(m_appSettingsJson.toUtf8()).object();
  return settings.value(QStringLiteral("calendars")).toObject().value(QStringLiteral("subscriptions")).toArray().toVariantList();
}

void AppController::writeCalendarSubscriptionSettings(const QVariantList& list) {
  QJsonObject settings = QJsonDocument::fromJson(m_appSettingsJson.toUtf8()).object();
  QJsonObject calendars = settings.value(QStringLiteral("calendars")).toObject();
  calendars.insert(QStringLiteral("subscriptions"), QJsonArray::fromVariantList(list));
  settings.insert(QStringLiteral("calendars"), calendars);
  setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
}

QVariantList AppController::calendarSubscriptions() const {
  QHash<QString, int> counts;
  for(const CalEvent& e : m_events.items()) {
    const QString sub = heap::cal::subscriptionOfEventId(e.id);
    if(!sub.isEmpty() && e.masterId.isEmpty()) {
      counts[sub]++;
    }
  }
  QVariantList out;
  for(const QVariant& v : calendarSubscriptionSettings()) {
    QVariantMap m = v.toMap();
    const QString id = m.value(QStringLiteral("id")).toString();
    const CalSubState st = m_calSubState.value(id);
    m.insert(QStringLiteral("minutes"), m.value(QStringLiteral("minutes"), kCalSubDefaultMinutes).toInt());
    m.insert(QStringLiteral("kind"), m.value(QStringLiteral("kind"), QStringLiteral("link")).toString());
    const bool desktop = m.value(QStringLiteral("kind")).toString() == QStringLiteral("outlook");
    m.insert(QStringLiteral("live"), desktop && m_outlookLive);
    m.insert(QStringLiteral("minMinutes"), desktop ? heap::cal::kDesktopMinutesMin : heap::cal::kLinkMinutesMin);
    m.insert(QStringLiteral("events"), counts.value(id));
    m.insert(QStringLiteral("lastSync"), st.lastSync.isValid() ? st.lastSync.toString(Qt::ISODate) : QString());
    m.insert(QStringLiteral("error"), st.error);
    m.insert(QStringLiteral("busy"), st.busy);
    out.append(m);
  }
  return out;
}

QVariantMap AppController::addCalendarSubscription(const QString& name, const QString& link, int minutes) {
  const QUrl url = heap::cal::subscriptionFetchUrl(link);
  if(url.isEmpty()) {
    return {{"ok", false}, {"error", tr_("calsub.badLink")}};
  }
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  m_secretStore->setValue(kCalSubSecretProvider, id, url.toString(QUrl::FullyEncoded));
  m_calSubSecretsLoaded.insert(id);
  QVariantList list = calendarSubscriptionSettings();
  const QString shown = name.trimmed().isEmpty() ? tr_("calsub.defaultName") : name.trimmed().left(60);
  list.append(QVariantMap{{"id", id}, {"name", shown}, {"minutes", heap::cal::subscriptionMinutes(false, minutes)}});
  writeCalendarSubscriptionSettings(list);
  fetchCalendarSubscription(id);
  return {{"ok", true}, {"id", id}};
}

bool AppController::outlookDesktopAvailable() const {
  return heap::cal::outlookDesktopAvailable();
}

QVariantMap AppController::addOutlookDesktopCalendar(int minutes) {
  if(!heap::cal::outlookDesktopAvailable()) {
    return {{"ok", false}, {"error", tr_("calsub.noDesktop")}};
  }
  QVariantList list = calendarSubscriptionSettings();
  for(const QVariant& v : std::as_const(list)) {
    if(v.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("outlook")) {
      return {{"ok", false}, {"error", tr_("calsub.desktopTwice")}};
    }
  }
  const QString id = QStringLiteral("outlook");
  list.append(QVariantMap{
      {"id", id}, {"kind", "outlook"}, {"name", tr_("calsub.desktopName")}, {"minutes", heap::cal::subscriptionMinutes(true, minutes)}});
  writeCalendarSubscriptionSettings(list);
  fetchCalendarSubscription(id);
  return {{"ok", true}, {"id", id}};
}

void AppController::removeCalendarSubscription(const QString& id) {
  QVariantList list = calendarSubscriptionSettings();
  for(qsizetype i = list.size() - 1; i >= 0; --i) {
    if(list.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
      list.removeAt(i);
    }
  }
  const QString prefix = heap::cal::subscriptionPrefix(id);
  QStringList doomed;
  for(const CalEvent& e : m_events.items()) {
    if(e.id.startsWith(prefix)) {
      doomed << e.id;
    }
  }
  for(const QString& eventId : std::as_const(doomed)) {
    m_events.removeById(eventId);
  }
  m_secretStore->remove(kCalSubSecretProvider, id);
  m_calSubSecretsLoaded.remove(id);
  m_calSubState.remove(id);
  writeCalendarSubscriptionSettings(list);
  scheduleSave();
  emit calendarSubscriptionsChanged();
}

void AppController::refreshCalendarSubscription(const QString& id) {
  fetchCalendarSubscription(id);
}

void AppController::setCalendarSubscriptionMinutes(const QString& id, int minutes) {
  QVariantList list = calendarSubscriptionSettings();
  bool changed = false;
  for(QVariant& v : list) {
    QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("id")).toString() != id) {
      continue;
    }
    const bool desktop = m.value(QStringLiteral("kind")).toString() == QStringLiteral("outlook");
    const int wanted = heap::cal::subscriptionMinutes(desktop, minutes);
    if(m.value(QStringLiteral("minutes")).toInt() != wanted) {
      m.insert(QStringLiteral("minutes"), wanted);
      v = m;
      changed = true;
    }
  }
  if(changed) {
    writeCalendarSubscriptionSettings(list);
    emit calendarSubscriptionsChanged();
  }
}

void AppController::ensureOutlookWatch(bool wanted) {
  // Nor does it watch it (see fetchCalendarSubscription).
  wanted = wanted && !QStandardPaths::isTestModeEnabled();
  if(wanted == (m_outlookWatch != nullptr)) {
    return;
  }
  if(!wanted) {
    heap::cal::OutlookWatch::retire(m_outlookWatch);
    m_outlookWatch = nullptr;
    m_outlookLive = false;
    m_outlookReadAgain = false;
    if(m_outlookDebounce != nullptr) {
      m_outlookDebounce->stop();
    }
    emit calendarSubscriptionsChanged();
    return;
  }
  m_outlookWatch = new heap::cal::OutlookWatch(this);
  connect(m_outlookWatch, &heap::cal::OutlookWatch::calendarChanged, this, &AppController::onOutlookChanged);
  connect(m_outlookWatch, &heap::cal::OutlookWatch::liveChanged, this, [this](bool live) {
    if(m_outlookLive != live) {
      m_outlookLive = live;
      emit calendarSubscriptionsChanged();
    }
  });
  m_outlookWatch->start();
}

void AppController::onOutlookChanged() {
  if(m_outlookDebounce == nullptr) {
    return;
  }
  const QDateTime now = QDateTime::currentDateTime();
  if(!m_outlookDebounce->isActive() || !m_outlookFirstChange.isValid()) {
    m_outlookFirstChange = now;
  }
  m_outlookDebounce->start(heap::cal::desktopDebounceMs(m_outlookFirstChange, now));
}

bool AppController::isSubscriptionEvent(const QString& id) const {
  return heap::cal::isSubscriptionEventId(id);
}

QString AppController::subscriptionNameOf(const QString& eventId) const {
  const QString sub = heap::cal::subscriptionOfEventId(eventId);
  if(sub.isEmpty()) {
    return {};
  }
  for(const QVariant& v : calendarSubscriptionSettings()) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("id")).toString() == sub) {
      return m.value(QStringLiteral("name")).toString();
    }
  }
  return tr_("calsub.defaultName");
}

bool AppController::refuseSubscriptionEdit(const QString& id, const QString& masterId) {
  const QString which = heap::cal::isSubscriptionEventId(id) ? id : (heap::cal::isSubscriptionEventId(masterId) ? masterId : QString());
  if(which.isEmpty()) {
    return false;
  }
  const int row = m_events.indexOfId(which);
  const QString title = row >= 0 ? m_events.items().at(row).title : QString();
  emit toast(tr_("calsub.readOnly").arg(title, subscriptionNameOf(which)));
  playSound_(static_cast<int>(heap::platform::SoundCue::Refuse));
  return true;
}

void AppController::applyCalendarSubscriptions() {
  const heap::frame::Span span("applyCalendarSubscriptions");
  const QVariantList list = calendarSubscriptionSettings();
  ensureOutlookWatch(std::any_of(list.cbegin(), list.cend(), [](const QVariant& v) {
    return v.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("outlook");
  }));
  QVector<QPair<QString, QString>> toLoad;
  for(const QVariant& v : list) {
    const QString id = v.toMap().value(QStringLiteral("id")).toString();
    if(v.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("outlook")) {
      continue;  // no link to load
    }
    if(!id.isEmpty() && !m_calSubSecretsLoaded.contains(id)) {
      toLoad.append({kCalSubSecretProvider, id});
    }
  }
  if(!toLoad.isEmpty()) {
    for(const auto& key : toLoad) {
      m_calSubSecretsLoaded.insert(key.second);
    }
    // The links come from the keychain asynchronously; fetch once they are in.
    m_secretStore->load(toLoad, [this]() {
      applyCalendarSubscriptions();
    });
    return;
  }
  const QDateTime now = QDateTime::currentDateTime();
  for(const QVariant& v : list) {
    const QVariantMap m = v.toMap();
    const QString id = m.value(QStringLiteral("id")).toString();
    // While Outlook reports its own changes the timer is only a safety net.
    const bool desktop = m.value(QStringLiteral("kind")).toString() == QStringLiteral("outlook");
    const int minutes =
        heap::cal::effectiveSubscriptionMinutes(desktop, m.value(QStringLiteral("minutes"), kCalSubDefaultMinutes).toInt(), m_outlookLive);
    const CalSubState st = m_calSubState.value(id);
    if(!st.busy && (!st.lastAttempt.isValid() || st.lastAttempt.secsTo(now) >= qint64(minutes) * 60)) {
      fetchCalendarSubscription(id);
    }
  }
}

void AppController::fetchCalendarSubscription(const QString& id) {
  CalSubState& st = m_calSubState[id];
  if(st.busy) {
    return;
  }
  st.lastAttempt = QDateTime::currentDateTime();
  bool desktop = false;
  for(const QVariant& v : calendarSubscriptionSettings()) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("id")).toString() == id && m.value(QStringLiteral("kind")).toString() == QStringLiteral("outlook")) {
      desktop = true;
    }
  }
  if(desktop) {
    // A test run never reaches into the real Outlook of the machine it runs
    // on: its meetings would land in the test profile.
    if(QStandardPaths::isTestModeEnabled()) {
      st.error = QStringLiteral("test mode");
      emit calendarSubscriptionsChanged();
      return;
    }
    if(m_outlookThread != nullptr) {
      return;  // one read at a time; the next tick tries again
    }
    // Two weeks back (what just happened still reads as context) and three
    // months ahead.
    const QDateTime from = QDate::currentDate().addDays(-14).startOfDay();
    const QDateTime to = QDate::currentDate().addDays(92).startOfDay();
    auto result = std::make_shared<heap::cal::OutlookRead>();
    QThread* worker = QThread::create([result, from, to]() {
      *result = heap::cal::readOutlookCalendar(from, to);
    });
    m_outlookThread = worker;
    st.busy = true;
    emit calendarSubscriptionsChanged();
    connect(worker, &QThread::finished, this, [this, worker, result, id]() {
      if(m_outlookThread == worker) {
        m_outlookThread = nullptr;
      }
      worker->deleteLater();
      if(!m_calSubState.contains(id)) {
        return;
      }
      CalSubState& state = m_calSubState[id];
      state.busy = false;
      if(result->ok) {
        replaceSubscriptionEvents(id, heap::cal::outlookEvents(result->items, id));
        m_calSubState[id].lastSync = QDateTime::currentDateTime();
        m_calSubState[id].error.clear();
      } else {
        state.error = result->error;
      }
      emit calendarSubscriptionsChanged();
      // Outlook changed again while this read ran: read once more.
      if(m_outlookReadAgain) {
        m_outlookReadAgain = false;
        QTimer::singleShot(0, this, [this, id]() {
          fetchCalendarSubscription(id);
        });
      }
    });
    worker->start();
    return;
  }
  const QUrl url = heap::cal::subscriptionFetchUrl(m_secretStore->value(kCalSubSecretProvider, id));
  if(url.isEmpty()) {
    st.error = tr_("calsub.noLink");
    emit calendarSubscriptionsChanged();
    return;
  }
  if(m_calNam == nullptr) {
    m_calNam = new QNetworkAccessManager(this);
  }
  QNetworkRequest req(url);
  req.setRawHeader("Accept", "text/calendar, */*;q=0.5");
  req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Tabless/%1").arg(QString::fromLatin1(HEAP_VERSION)));
  req.setTransferTimeout(30 * 1000);
  st.busy = true;
  emit calendarSubscriptionsChanged();
  QNetworkReply* reply = m_calNam->get(req);
  connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
    if(received > kCalSubMaxBytes) {
      reply->setProperty("tooBig", true);
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
    reply->deleteLater();
    if(!m_calSubState.contains(id)) {
      return;  // removed while it was being fetched
    }
    CalSubState& state = m_calSubState[id];
    state.busy = false;
    if(reply->property("tooBig").toBool()) {
      state.error = tr_("calsub.tooBig");
    } else if(reply->error() != QNetworkReply::NoError) {
      // The link is a secret; the reply's message can quote it, so only the
      // status goes on screen.
      const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      state.error = code > 0 ? QStringLiteral("HTTP %1").arg(code) : reply->errorString().section(QLatin1Char('-'), 0, 0).trimmed();
    } else {
      QString error;
      if(applyCalendarSubscriptionFeed(id, reply->readAll(), &error) >= 0) {
        m_calSubState[id].lastSync = QDateTime::currentDateTime();
        m_calSubState[id].error.clear();
      } else {
        m_calSubState[id].error = error;
      }
    }
    emit calendarSubscriptionsChanged();
  });
}

int AppController::applyCalendarSubscriptionFeed(const QString& id, const QByteArray& body, QString* error) {
  const heap::cal::IcsImport parsed = heap::cal::parseIcs(QString::fromUtf8(body), QTimeZone::systemTimeZone(), QString());
  if(!parsed.recognised) {
    if(error != nullptr) {
      *error = tr_("calsub.notCalendar");
    }
    return -1;
  }
  return replaceSubscriptionEvents(id, heap::cal::subscriptionEvents(parsed, id));
}

int AppController::replaceSubscriptionEvents(const QString& id, const QVector<CalEvent>& incoming) {
  QSet<QString> keep;
  for(const CalEvent& e : incoming) {
    keep.insert(e.id);
  }
  // The feed is the whole truth: what it no longer carries was cancelled or
  // moved out of range over there.
  const QString prefix = heap::cal::subscriptionPrefix(id);
  QStringList gone;
  for(const CalEvent& e : m_events.items()) {
    if(e.id.startsWith(prefix) && !keep.contains(e.id)) {
      gone << e.id;
    }
  }
  bool changed = !gone.isEmpty();
  for(const QString& eventId : std::as_const(gone)) {
    m_events.removeById(eventId);
  }
  for(const CalEvent& e : incoming) {
    const int row = m_events.indexOfId(e.id);
    if(row >= 0 && m_events.items().at(row) == e) {
      continue;
    }
    m_events.upsert(e);
    changed = true;
  }
  if(changed) {
    scheduleSave();
  }
  int masters = 0;
  for(const CalEvent& e : incoming) {
    masters += e.masterId.isEmpty() ? 1 : 0;
  }
  return masters;
}

QVariantMap AppController::importIcs(const QUrl& fileUrl) {
  QVariantMap out;
  out["imported"] = 0;
  out["updated"] = 0;
  out["skipped"] = 0;
  out["warnings"] = QStringList();

  const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
  QFile f(path);
  if(path.isEmpty() || !f.open(QIODevice::ReadOnly)) {
    out["error"] = tr_("ics.error.open");
    return out;
  }
  const heap::cal::IcsImport parsed = heap::cal::parseIcs(QString::fromUtf8(f.readAll()), QTimeZone::systemTimeZone(), icsUidDomain());
  f.close();
  // A file with no calendar in it is an error to name, not "0 imported".
  if(!parsed.recognised) {
    out["error"] = tr_("ics.error.notCalendar");
    return out;
  }

  int imported = 0;
  int updated = 0;
  {
    // One undo step for the whole file: an import that brought in forty events
    // is one thing the user did, and undoing it forty times is not a feature.
    UndoScope scope(this, tr_("undo.importIcs"));
    for(const CalEvent& incoming : parsed.events) {
      CalEvent e = incoming;
      // A moved occurrence is known by its series and the date it replaces,
      // not by an id: a file names it UID + RECURRENCE-ID, which the codec
      // turns into "<uid>-yyyyMMdd" while heap's own is an ev-… id. Matched
      // by id, every moved occurrence in our own export came back a second
      // time, and again on each round trip (TIME-10, audit 2026-09-30).
      // Copies an earlier import already doubled are folded into the one
      // that is kept.
      if(!e.masterId.isEmpty() && e.originalDate.isValid()) {
        QStringList same;
        for(const CalEvent& s0 : m_events.items()) {
          if(s0.masterId == e.masterId && s0.originalDate == e.originalDate) {
            same << s0.id;
          }
        }
        if(!same.isEmpty() && !same.contains(e.id)) {
          e.id = same.first();
        }
        for(const QString& dup : std::as_const(same)) {
          if(dup != e.id) {
            m_events.removeById(dup);
          }
        }
      }
      // The UID is what makes importing the same file twice an update rather
      // than a second copy of everybody's calendar.
      const int existing = m_events.indexOfId(e.id);
      if(existing >= 0) {
        const CalEvent& was = m_events.items().at(existing);
        // What heap knows and the file does not stays heap's: attribution,
        // the task link, the kind of event (a focus block must not turn into
        // a meeting that reminds), and anything the file left blank.
        e.profileId = was.profileId;
        e.taskId = was.taskId;
        if(incoming.type == QStringLiteral("sync") && !was.type.isEmpty()) {
          e.type = was.type;
        }
        if(e.attendees.isEmpty()) {
          e.attendees = was.attendees;
        }
        if(e.context.isEmpty()) {
          e.context = was.context;
        }
        if(e.notes.isEmpty()) {
          e.notes = was.notes;
        }
        if(e.url.isEmpty()) {
          e.url = was.url;
        }
        if(e.location.isEmpty()) {
          e.location = was.location;
        }
        if(e.reminderMinutes == CalEvent::kReminderDefault) {
          e.reminderMinutes = was.reminderMinutes;
        }
        updated++;
      } else {
        e.profileId = m_activeProfileId;
        imported++;
      }
      m_events.upsert(e);
    }
    // Occurrences the file cancels in a series it does not carry are deleted
    // from the stored series, as deleteOccurrence would (TIME-9).
    for(const heap::cal::IcsImport::Cancelled& c : parsed.cancelled) {
      const int row = m_events.indexOfId(c.masterId);
      if(row < 0 || !m_events.items().at(row).masterId.isEmpty()) {
        continue;
      }
      CalEvent master = m_events.items().at(row);
      QStringList doomed;
      for(const CalEvent& ov : m_events.items()) {
        if(ov.masterId == c.masterId && ov.originalDate == c.date) {
          doomed << ov.id;
        }
      }
      for(const QString& id : std::as_const(doomed)) {
        m_events.removeById(id);
      }
      if(!master.exdates.contains(c.date)) {
        master.exdates.append(c.date);
        std::sort(master.exdates.begin(), master.exdates.end());
        m_events.upsert(master);
        updated++;
      }
    }
    scope.setRedoLabel(tr_("ics.reimported").arg(imported + updated));
  }
  if(imported > 0 || updated > 0) {
    scheduleSave();
  }

  out["imported"] = imported;
  out["updated"] = updated;
  out["skipped"] = parsed.skipped;
  out["warnings"] = parsed.warnings;
  return out;
}

QString AppController::icsUidDomain() const {
  if(m_installId.isEmpty()) {
    // Saved with the next write of state.json (saveStateNow asks for it).
    m_installId = QUuid::createUuid().toString(QUuid::Id128).left(12);
  }
  return m_installId + QStringLiteral(".heap");
}

bool AppController::exportIcsToFile(const QUrl& fileUrl) const {
  const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
  if(path.isEmpty()) {
    return false;
  }
  // Stored events, so a series leaves as one VEVENT with its RRULE rather than
  // as every occurrence heap happened to have expanded.
  const QString doc = heap::cal::toIcs(m_events.items(), QTimeZone::systemTimeZone(), QDateTime::currentDateTimeUtc(), icsUidDomain());
  QSaveFile f(path);
  if(!f.open(QIODevice::WriteOnly)) {
    qWarning("todocpp: cannot open %s for writing: %s", qUtf8Printable(path), qUtf8Printable(f.errorString()));
    return false;
  }
  f.write(doc.toUtf8());
  return f.commit();
}

namespace {

// Every .md under `root`, as paths relative to it. Depth-limited: a vault that
// contains a symlink to its own parent is otherwise a walk that does not end.
constexpr int kMaxVaultDepth = 12;
constexpr int kMaxVaultFiles = 20000;

// `rootCanonical` is the chosen folder with links resolved. A directory whose
// real location is outside it is somewhere else on the disk — a junction, a
// symlink, a mount — and neither its files nor a loop back through it belong to
// this vault. `seen` stops the same real directory being walked twice.
void collectMarkdown(
    const QDir& root, const QString& prefix, int depth, const QString& rootCanonical, QSet<QString>& seen, QStringList& out) {
  if(depth > kMaxVaultDepth || out.size() >= kMaxVaultFiles) {
    return;
  }
  const QFileInfoList entries = root.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  for(const QFileInfo& info : entries) {
    const QString rel = prefix.isEmpty() ? info.fileName() : prefix + QLatin1Char('/') + info.fileName();
    if(heap::notes::isIgnoredPath(rel)) {
      continue;
    }
    // Not through a link: a vault synced from elsewhere may well contain one
    // pointing back at a parent. isSymLink() alone misses NTFS junctions.
    if(info.isSymLink() || info.isJunction()) {
      continue;
    }
    if(info.isDir()) {
      const QString real = info.canonicalFilePath();
      const bool inside = real.startsWith(rootCanonical + QLatin1Char('/'), Qt::CaseInsensitive);
      if(real.isEmpty() || !inside || seen.contains(real)) {
        continue;
      }
      seen.insert(real);
      collectMarkdown(QDir(info.absoluteFilePath()), rel, depth + 1, rootCanonical, seen, out);
    } else if(info.suffix().compare(QLatin1String("md"), Qt::CaseInsensitive) == 0 ||
              info.suffix().compare(QLatin1String("markdown"), Qt::CaseInsensitive) == 0) {
      out << rel;
    }
  }
}

QString newNoteId() {
  return QStringLiteral("note-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

}  // namespace

QVariantMap AppController::planNotesImport(const QUrl& folderUrl, QVector<heap::notes::VaultPlanItem>* plan) const {
  QVariantMap out;
  out["imported"] = 0;
  out["updated"] = 0;
  out["unchanged"] = 0;
  out["kept"] = 0;
  out["conflicts"] = 0;
  out["skipped"] = 0;
  out["files"] = 0;
  out["warnings"] = QStringList();

  const QString path = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
  const QDir root(path);
  if(path.isEmpty() || !root.exists()) {
    out["error"] = tr_("notes.vault.badFolder");
    return out;
  }
  out["folder"] = QDir::toNativeSeparators(root.absolutePath());

  QStringList relatives;
  QSet<QString> seen;
  const QString rootCanonical = QFileInfo(root.absolutePath()).canonicalFilePath();
  seen.insert(rootCanonical);
  collectMarkdown(root, QString(), 0, rootCanonical, seen, relatives);
  relatives.sort();

  QStringList warnings;
  int skipped = 0;
  QVector<heap::notes::VaultSource> sources;
  sources.reserve(relatives.size());
  int attachmentCount = 0;
  for(const QString& rel : relatives) {
    QFile f(root.filePath(rel));
    if(f.size() > heap::notes::kMaxVaultFileBytes) {
      skipped++;
      warnings << tr_("notes.vault.tooBig").arg(rel).arg(f.size() / (1024 * 1024));
      continue;
    }
    if(!f.open(QIODevice::ReadOnly)) {
      skipped++;
      warnings << tr_("notes.vault.unreadable").arg(rel);
      continue;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    bool binary = false;
    QString text = heap::notes::decodeVaultBytes(bytes, &binary);
    if(binary) {
      skipped++;
      warnings << tr_("notes.vault.binary").arg(rel);
      continue;
    }
    // Files the note links (images, PDFs) come in as attachments and the link
    // is rewritten to point at the stored copy. A preview only hashes them.
    const heap::attachments::Store attachmentStore(attachmentsDir());
    const heap::attachments::VaultRefResult refs = heap::attachments::importVaultRefs(
        text, root, rel, plan != nullptr ? &attachmentStore : nullptr, m_language == QStringLiteral("ru"));
    text = refs.text;
    attachmentCount += static_cast<int>(refs.attachments.size());
    warnings << refs.warnings;
    sources.append({rel, text});
  }

  const QVector<heap::notes::VaultPlanItem> items =
      heap::notes::planImport(m_notes.items(), sources, &newNoteId, tr_("notes.vault.conflictSuffix"));
  int imported = 0;
  int updated = 0;
  int unchanged = 0;
  int kept = 0;
  int conflicts = 0;
  int checklists = 0;
  int checklistItems = 0;
  for(const heap::notes::VaultPlanItem& it : items) {
    switch(it.action) {
      case heap::notes::VaultAction::Create: {
        imported++;
        // What "- [ ] → tasks" would make (R2-070): new notes only, so a
        // re-import never makes the same tasks twice.
        const qsizetype open = heap::notes::openChecklistItems(it.note.body).size();
        if(open > 0) {
          checklists++;
          checklistItems += static_cast<int>(open);
        }
        break;
      }
      case heap::notes::VaultAction::Update:
        updated++;
        break;
      case heap::notes::VaultAction::Unchanged:
        unchanged++;
        break;
      case heap::notes::VaultAction::KeepLocal:
        kept++;
        break;
      case heap::notes::VaultAction::Conflict:
        conflicts++;
        warnings << tr_("notes.vault.conflict").arg(it.path);
        break;
    }
  }
  if(plan != nullptr) {
    *plan = items;
  }
  out["imported"] = imported;
  out["updated"] = updated;
  out["unchanged"] = unchanged;
  out["kept"] = kept;
  out["conflicts"] = conflicts;
  out["checklists"] = checklists;
  out["checklistItems"] = checklistItems;
  out["skipped"] = skipped;
  out["files"] = static_cast<int>(relatives.size());
  out["warnings"] = warnings;
  out["attachments"] = attachmentCount;
  return out;
}

QVariantMap AppController::previewNotesFolder(const QUrl& folderUrl) {
  // What heap has typed but not yet written is heap's side of the
  // comparison, so the preview has to see it.
  emit aboutToChangeActiveNote();
  syncActiveNoteBody();
  return planNotesImport(folderUrl, nullptr);
}

QVariantMap AppController::importNotesFolder(const QUrl& folderUrl, bool checklistTasks) {
  // The editor's debounced keystrokes are part of the note: without this flush
  // an edit made a moment ago looked "untouched since the last import" and the
  // older file overwrote it.
  emit aboutToChangeActiveNote();
  adoptOrphanNotesState();
  syncActiveNoteBody();

  QVector<heap::notes::VaultPlanItem> plan;
  QVariantMap out = planNotesImport(folderUrl, &plan);
  if(out.contains("error")) {
    return out;
  }

  bool changed = false;
  {
    // One undo step for the whole folder: importing the wrong vault is taken
    // back with one Ctrl+Z, not one per file.
    const UndoScope scope(this, tr_("notes.vault.undo"));
    for(const heap::notes::VaultPlanItem& it : plan) {
      const int row = m_notes.indexOfId(it.note.id);
      if(row < 0 || !(m_notes.items().at(row) == it.note)) {
        m_notes.upsert(it.note);
        changed = true;
      }
      if(it.action == heap::notes::VaultAction::Conflict) {
        m_notes.upsert(it.copy);
        changed = true;
      }
    }
    // Asked for in the dialog: each open "- [ ]" of a new note becomes a
    // task in the first column, in the same undo step.
    int made = 0;
    if(checklistTasks) {
      for(const heap::notes::VaultPlanItem& it : plan) {
        if(it.action != heap::notes::VaultAction::Create) {
          continue;
        }
        for(const QString& title : heap::notes::openChecklistItems(it.note.body)) {
          QVariantMap draft = newTaskDraft(QString());
          draft["title"] = title;
          if(saveTask(draft)) {
            made++;
          }
        }
      }
    }
    out["tasksMade"] = made;
  }

  if(changed) {
    // The open note may have just been rewritten from disk.
    const int row = m_notes.indexOfId(m_activeNoteId);
    if(row >= 0) {
      if(m_notes.items().at(row).body != m_notesState) {
        m_notesState = m_notes.items().at(row).body;
        emit notesStateChanged();
      }
    } else if(!m_notes.items().isEmpty() && m_activeNoteId.isEmpty()) {
      m_activeNoteId = m_notes.items().first().id;
      m_notesState = m_notes.items().first().body;
      emit activeNoteChanged();
      emit notesStateChanged();
    }
    scheduleSave();
  }
  return out;
}

bool AppController::exportNoteToFile(const QString& id, const QUrl& fileUrl) {
  emit aboutToChangeActiveNote();
  adoptOrphanNotesState();
  syncActiveNoteBody();
  const int row = m_notes.indexOfId(id);
  const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
  if(row < 0 || path.isEmpty()) {
    return false;
  }
  // The same file a folder export writes for this note (title, front matter).
  const QVector<heap::notes::VaultFile> files = heap::notes::exportVault({m_notes.items().at(row)});
  if(files.isEmpty()) {
    return false;
  }
  QSaveFile f(path);
  if(!f.open(QIODevice::WriteOnly)) {
    return false;
  }
  f.write(files.first().contents.toUtf8());
  return f.commit();
}

QVariantMap AppController::exportNotesFolder(const QUrl& folderUrl, const QString& subfolder) {
  QVariantMap out;
  out["written"] = 0;
  out["skipped"] = 0;

  emit aboutToChangeActiveNote();
  adoptOrphanNotesState();
  syncActiveNoteBody();

  const QString path = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
  const QDir parent(path);
  if(path.isEmpty() || !parent.exists()) {
    out["error"] = tr_("notes.vault.badFolder");
    return out;
  }

  // Never into files that are already there. An export goes into a folder of
  // its own — the name typed into the dialog, or a dated one — and when that
  // name is taken it gets a suffix rather than a merge: the user's own
  // team/Meeting.md must not turn into heap's Meeting.md.
  QString name = subfolder.trimmed();
  if(name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
    name.chop(3);
  }
  name = name.trimmed().isEmpty()
             ? QStringLiteral("tabless-notes-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmm")))
             : heap::notes::detail::sanitiseFileName(name);
  QString target = name;
  for(int n = 2; parent.exists(target); ++n) {
    target = QStringLiteral("%1 %2").arg(name).arg(n);
  }
  if(!parent.mkpath(target)) {
    out["error"] = tr_("notes.vault.badFolder");
    return out;
  }
  const QDir root(parent.filePath(target));

  int written = 0;
  int skipped = 0;
  QHash<QString, QString> pathOf;
  for(const heap::notes::VaultFile& file : heap::notes::exportVault(m_notes.items())) {
    const QString full = root.filePath(file.path);
    QDir().mkpath(QFileInfo(full).absolutePath());
    // The folder is new, so nothing should be here. If something is — two
    // names the filesystem folds together — skip rather than overwrite.
    if(QFileInfo::exists(full)) {
      skipped++;
      continue;
    }
    // QSaveFile, like every other write in this app: a half-written note is
    // worse than one that was not exported.
    QSaveFile f(full);
    if(!f.open(QIODevice::WriteOnly)) {
      skipped++;
      continue;
    }
    // attachments/ sits at the root of the export, so a note in team/ links
    // its files as ../attachments/<id>: relative to the file, which is how
    // every other editor resolves it. Import reads it back.
    const QString up = QStringLiteral("../").repeated(static_cast<int>(file.path.count(QLatin1Char('/'))));
    f.write(heap::attachments::prefixRefLinks(file.contents, up).toUtf8());
    if(f.commit()) {
      written++;
      pathOf.insert(file.noteId, file.path);
    } else {
      skipped++;
    }
  }

  // Remember what each note looked like when it left, so importing this
  // folder back later can tell an edit made here from one made there.
  for(const Note& n : QVector<Note>(m_notes.items())) {
    const auto it = pathOf.constFind(n.id);
    if(it == pathOf.constEnd()) {
      continue;
    }
    const QString hash = heap::notes::contentHash(n);
    if(n.vaultPath != it.value() || n.vaultHash != hash) {
      Note next = n;
      next.vaultPath = it.value();
      next.vaultHash = hash;
      m_notes.upsert(next);
    }
  }
  if(!pathOf.isEmpty()) {
    scheduleSave();
  }

  // The files the exported notes link, into attachments/ at the root. The
  // links say "attachments/<id>" relative to each file (see above), so the
  // folder reads the same in heap, in another editor and when it is imported
  // back.
  int files = 0;
  {
    const heap::attachments::Store store(attachmentsDir());
    QStringList ids;
    for(const Note& n : m_notes.items()) {
      if(pathOf.contains(n.id)) {
        for(const QString& id : heap::attachments::refsIn(n.body)) {
          if(!ids.contains(id)) {
            ids << id;
          }
        }
      }
    }
    if(!ids.isEmpty() && root.mkpath(QStringLiteral("attachments"))) {
      for(const QString& id : ids) {
        const QString dest = root.filePath(QStringLiteral("attachments/") + id);
        if(store.contains(id) && QFile::copy(store.pathFor(id), dest)) {
          // The store keeps its files read-only; the copy is the user's.
          QFile::setPermissions(dest, QFile::permissions(dest) | QFile::WriteOwner);
          files++;
        }
      }
    }
  }

  out["written"] = written;
  out["skipped"] = skipped;
  out["attachments"] = files;
  out["folder"] = QDir::toNativeSeparators(root.absolutePath());
  return out;
}

QVariantMap AppController::resolveNoteLink(const QString& target) const {
  const heap::notes::LinkTarget t = heap::notes::resolveLink(target, m_notes.items(), m_activeNoteId);
  QVariantMap out;
  switch(t.kind) {
    case heap::notes::LinkTarget::NoteRef: {
      out["kind"] = QStringLiteral("note");
      out["noteId"] = t.noteId;
      const int row = m_notes.indexOfId(t.noteId);
      out["title"] = row >= 0 ? m_notes.items().at(row).title : QString();
      break;
    }
    case heap::notes::LinkTarget::HeadingRef:
      out["kind"] = QStringLiteral("heading");
      out["noteId"] = t.noteId;
      out["heading"] = t.heading;
      break;
    case heap::notes::LinkTarget::Missing:
      out["kind"] = QStringLiteral("missing");
      out["title"] = target.trimmed();
      break;
  }
  return out;
}

QVariantList AppController::backlinksToNote(const QString& noteId) const {
  QVariantList out;
  for(const heap::notes::Backlink& b : heap::notes::backlinksTo(noteId, m_notes.items())) {
    QVariantMap m;
    m["noteId"] = b.noteId;
    m["title"] = b.noteTitle;
    m["line"] = b.line;
    m["text"] = b.text;
    out.append(m);
  }
  return out;
}

QStringList AppController::unresolvedNoteLinks() const {
  return heap::notes::unresolvedLinksIn(m_notesState, m_notes.items(), m_activeNoteId);
}

QString AppController::createNoteForLink(const QString& target) {
  // "[[C\# basics]]" asks for a note called "C# basics".
  const QString title = heap::notes::detail::unescapeLink(target).trimmed();
  if(title.isEmpty()) {
    return {};
  }
  // If it exists after all, open it rather than making a second one with the
  // same name — which would leave the link ambiguous forever.
  const heap::notes::LinkTarget existing = heap::notes::resolveLink(title, m_notes.items(), m_activeNoteId);
  if(existing.kind == heap::notes::LinkTarget::NoteRef) {
    setActiveNoteId(existing.noteId);
    return existing.noteId;
  }
  // Filed beside the note that asked for it: a note created from a link
  // belongs with its neighbours, not at the root.
  QString folder;
  const int row = m_notes.indexOfId(m_activeNoteId);
  if(row >= 0) {
    folder = m_notes.items().at(row).folder;
  }
  return newNote(title, folder);
}

QString AppController::openDailyNote() {
  const QDate today = QDate::currentDate();
  // ISO, so the folder sorts chronologically in every list and in a vault on
  // disk.
  const QString title = today.toString(Qt::ISODate);
  const QString folder = QStringLiteral("daily");

  for(const Note& n : m_notes.items()) {
    if(n.title == title && n.folder == folder) {
      setActiveNoteId(n.id);
      return n.id;
    }
  }
  const QString id = newNote(title, folder);
  const int row = m_notes.indexOfId(id);
  if(row >= 0) {
    Note n = m_notes.items().at(row);
    // In the UI language: QDate::toString is always English, so a Russian
    // profile's daily notes were headed "Tuesday, 30 September 2026".
    n.body = QStringLiteral("# %1\n\n").arg(dateLabel(today, QStringLiteral("longWeekdayYear")));
    m_notes.upsert(n);
    m_notesState = n.body;
    emit notesStateChanged();
    scheduleSave();
  }
  return id;
}

namespace {

// Siblings of `parentId`, sorted by rank then title so the order is stable even
// for pages that have never been dragged.
QVector<DocPage> childrenOf(const QVector<DocPage>& all, const QString& parentId) {
  QVector<DocPage> out;
  for(const DocPage& p : all) {
    if(p.parentId == parentId) {
      out.append(p);
    }
  }
  std::sort(out.begin(), out.end(), [](const DocPage& a, const DocPage& b) {
    if(a.rank != b.rank) {
      return a.rank < b.rank;
    }
    return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
  });
  return out;
}

}  // namespace

void AppController::setActiveDocPageId(const QString& id) {
  if(id == m_activeDocPageId) {
    return;
  }
  m_activeDocPageId = id;
  emit activeDocPageChanged();
  scheduleSave();
}

QString AppController::docPageBody(const QString& id) const {
  const int row = m_docPages.indexOfId(id);
  return row >= 0 ? m_docPages.items().at(row).body : QString();
}

QString AppController::newDocPage(const QString& title, const QString& parentId) {
  DocPage p;
  p.id = QStringLiteral("doc-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  p.parentId = parentId;
  p.title = title.trimmed().isEmpty() ? tr_("docs.untitledPage") : title.trimmed();
  p.created = QDateTime::currentDateTime();
  p.updated = p.created;
  p.body = QStringLiteral("# %1\n\n").arg(p.title);
  // Last among its siblings, which is where a new thing belongs; fractional so
  // a later drag rewrites one page rather than renumbering the level.
  const QVector<DocPage> siblings = childrenOf(m_docPages.items(), parentId);
  p.rank = siblings.isEmpty() ? heap::state::kRankStep : siblings.last().rank + heap::state::kRankStep;
  m_docPages.upsert(p);
  setActiveDocPageId(p.id);
  scheduleSave();
  return p.id;
}

void AppController::renameDocPage(const QString& id, const QString& title) {
  const int row = m_docPages.indexOfId(id);
  if(row < 0 || title.trimmed().isEmpty()) {
    return;
  }
  DocPage p = m_docPages.items().at(row);
  p.title = title.trimmed();
  p.updated = QDateTime::currentDateTime();
  m_docPages.upsert(p);
  scheduleSave();
}

void AppController::setDocPageBody(const QString& id, const QString& body) {
  const int row = m_docPages.indexOfId(id);
  if(row < 0) {
    return;
  }
  DocPage p = m_docPages.items().at(row);
  if(p.body == body) {
    return;
  }
  p.body = body;
  p.updated = QDateTime::currentDateTime();
  m_docPages.upsert(p);
  scheduleSave();
}

void AppController::deleteDocPage(const QString& id) {
  if(m_docPages.indexOfId(id) < 0) {
    return;
  }
  // The open page's last words go in before the undo records it, so Undo
  // brings the page back with them (KNOW-15).
  emit flushEditorsRequested();
  if(m_docPages.indexOfId(id) < 0) {
    return;
  }
  // The subtree goes too. A page whose parent is gone would be unreachable in
  // the tree and invisible everywhere else — deleted in every sense except the
  // one that frees the space.
  QStringList doomed{id};
  for(int i = 0; i < doomed.size(); ++i) {
    for(const DocPage& p : m_docPages.items()) {
      if(p.parentId == doomed.at(i) && !doomed.contains(p.id)) {
        doomed << p.id;
      }
    }
  }
  // The open page's unsaved keystrokes go in first, so undoing the delete
  // brings back what was on screen rather than the last save.
  emit flushEditorsRequested();
  const QString title = m_docPages.items().at(m_docPages.indexOfId(id)).title;
  {
    const UndoScope scope(this, tr_("docs.undo.deletePage"));
    for(const QString& gone : doomed) {
      m_docPages.removeById(gone);
    }
    if(doomed.contains(m_activeDocPageId)) {
      m_activeDocPageId = m_docPages.rowCount() > 0 ? m_docPages.items().first().id : QString();
      emit activeDocPageChanged();
    }
  }
  scheduleSave();
  // Said out loud, with the way back, like a deleted note: a subtree went
  // with no confirm and no word, so the reader could not tell what happened.
  emit undoableToast(doomed.size() > 1 ? tr_("docs.pagesDeleted").arg(title).arg(doomed.size() - 1) : tr_("docs.pageDeleted").arg(title),
                     8);
}

void AppController::moveDocPage(const QString& id, const QString& newParentId, const QString& beforeId) {
  const int row = m_docPages.indexOfId(id);
  if(row < 0 || id == newParentId) {
    return;
  }
  // A page cannot be dropped inside its own subtree: the tree would lose the
  // branch entirely, since nothing would reach it from the root.
  QString walk = newParentId;
  while(!walk.isEmpty()) {
    if(walk == id) {
      return;
    }
    const int at = m_docPages.indexOfId(walk);
    if(at < 0) {
      break;
    }
    walk = m_docPages.items().at(at).parentId;
  }

  DocPage p = m_docPages.items().at(row);
  p.parentId = newParentId;

  QVector<DocPage> siblings = childrenOf(m_docPages.items(), newParentId);
  siblings.removeIf([&id](const DocPage& s) {
    return s.id == id;
  });

  double before = 0.0;
  double after = 0.0;
  bool hasBefore = false;
  bool hasAfter = false;
  if(beforeId.isEmpty()) {
    if(!siblings.isEmpty()) {
      before = siblings.last().rank;
      hasBefore = true;
    }
  } else {
    for(int i = 0; i < siblings.size(); ++i) {
      if(siblings.at(i).id != beforeId) {
        continue;
      }
      after = siblings.at(i).rank;
      hasAfter = true;
      if(i > 0) {
        before = siblings.at(i - 1).rank;
        hasBefore = true;
      }
      break;
    }
  }
  p.rank = heap::board::between(before, after, hasBefore, hasAfter);
  p.updated = QDateTime::currentDateTime();
  m_docPages.upsert(p);
  scheduleSave();
}

QVariantList AppController::docPageChildren(const QString& parentId) const {
  QVariantList out;
  for(const DocPage& p : childrenOf(m_docPages.items(), parentId)) {
    QVariantMap m;
    m["id"] = p.id;
    m["parentId"] = p.parentId;
    m["title"] = p.title;
    m["rank"] = p.rank;
    // So a row can draw a disclosure triangle without asking again.
    bool hasKids = false;
    for(const DocPage& c : m_docPages.items()) {
      if(c.parentId == p.id) {
        hasKids = true;
        break;
      }
    }
    m["hasChildren"] = hasKids;
    out.append(m);
  }
  return out;
}

QString AppController::scheduledLabelFor(const QString& taskId, const QDate& date) const {
  if(taskId.isEmpty() || !date.isValid()) {
    return QString();
  }
  double earliest = -1.0;
  for(const CalEvent& e : m_events.items()) {
    if(e.taskId != taskId || e.date != date) {
      continue;
    }
    if(earliest < 0.0 || e.start < earliest) {
      earliest = e.start;
    }
  }
  if(earliest < 0.0) {
    return QString();
  }
  return eventHourLabel(earliest);
}

QVariantList AppController::calendarTasks(const QDate& from, const QDate& to, bool includeArchived) const {
  QVariantList out;
  if(!from.isValid() || !to.isValid() || to < from) {
    return out;
  }
  const auto& items = m_tasks.items();
  for(int i = 0; i < items.size(); ++i) {
    const Task& t = items.at(i);
    if(t.archived && !includeArchived) {
      continue;
    }
    const QDateTime dueAt = heap::local::effectiveDueAt(t);
    const bool dueHasTime = heap::local::effectiveDueHasTime(t);
    const QDate due = dueAt.isValid() ? dueAt.date() : QDate();
    const QDate sched = t.scheduledAt.isValid() ? t.scheduledAt.date() : QDate();
    const bool dueIn = due.isValid() && due >= from && due <= to;
    const bool schedIn = sched.isValid() && sched >= from && sched <= to;
    if(!dueIn && !schedIn) {
      continue;
    }
    const QModelIndex idx = m_tasks.index(i, 0);
    QVariantMap m;
    m["id"] = t.id;
    m["title"] = t.title;
    m["desc"] = t.desc;
    m["priority"] = heap::local::effectivePriority(t);
    m["status"] = t.status;
    m["deadline"] = due.isValid() ? QVariant(due) : QVariant();
    m["dueAt"] = dueAt;
    m["scheduledAt"] = t.scheduledAt;
    m["hasTime"] = dueHasTime;
    m["dueHasTime"] = dueHasTime;
    m["scheduledHasTime"] = t.scheduledHasTime;
    m["archived"] = t.archived;
    m["estimateMinutes"] = t.estimateMinutes;
    // The block's length as Today and the day's load count it (APP-247).
    m["blockMinutes"] = taskBlockMinutes(t.id);
    m["searchText"] = m_tasks.data(idx, TaskModel::SearchTextRole);
    m["ticket"] = m_tasks.data(idx, TaskModel::TicketRole);
    m["dueDay"] = dueIn ? static_cast<int>(from.daysTo(due)) : -1;
    m["schedDay"] = schedIn ? static_cast<int>(from.daysTo(sched)) : -1;
    // A scheduled clock time, as an hour, or -1 when the schedule is a date.
    m["schedHour"] = (schedIn && t.scheduledHasTime) ? t.scheduledAt.time().hour() + (t.scheduledAt.time().minute() / 60.0) : -1.0;
    out.append(m);
  }
  return out;
}

QVariantList AppController::calendarRepeats(const QDate& from, const QDate& to) const {
  QVariantList out;
  if(!from.isValid() || !to.isValid() || to < from) {
    return out;
  }
  const QDate today = QDate::currentDate();
  const auto& items = m_tasks.items();
  for(int i = 0; i < items.size(); ++i) {
    const Task& t = items.at(i);
    // An archived or finished one brings nothing more: its open copy, when
    // there is one, carries the series on.
    if(t.recurrence.isEmpty() || t.archived || statusCategory(t.status) == QStringLiteral("done")) {
      continue;
    }
    // The date completing it rolls from, as toggleDone takes it.
    const QDateTime dueAt = heap::local::effectiveDueAt(t);
    const QDate anchor = dueAt.isValid() ? dueAt.date() : t.scheduledAt.date();
    const QVector<QDate> dates = heap::recur::occurrencesBetween(t.recurrence, anchor, today, from, to);
    if(dates.isEmpty()) {
      continue;
    }
    // The copy keeps the clock time of the date it rolls (HEAP-77).
    const bool timed = t.scheduledAt.isValid() ? t.scheduledHasTime : heap::local::effectiveDueHasTime(t);
    const QTime clock = t.scheduledAt.isValid() ? t.scheduledAt.time() : dueAt.time();
    const QModelIndex idx = m_tasks.index(i, 0);
    for(const QDate& d : dates) {
      QVariantMap m;
      m["id"] = t.id;
      m["title"] = t.title;
      m["desc"] = t.desc;
      m["priority"] = heap::local::effectivePriority(t);
      m["status"] = t.status;
      m["recurrence"] = t.recurrence;
      m["date"] = d;
      m["day"] = static_cast<int>(from.daysTo(d));
      m["hasTime"] = timed;
      m["hour"] = timed ? clock.hour() + (clock.minute() / 60.0) : -1.0;
      // What the views' search reads, as calendarTasks hands it.
      m["searchText"] = m_tasks.data(idx, TaskModel::SearchTextRole);
      m["ticket"] = m_tasks.data(idx, TaskModel::TicketRole);
      m["dueAt"] = dueAt;
      m["scheduledAt"] = t.scheduledAt;
      m["ghost"] = true;
      out.append(m);
    }
  }
  return out;
}

QString AppController::recurrenceFromText(const QString& text) const {
  const QString said = text.simplified();
  if(said.isEmpty() || !m_chrono) {
    return {};
  }
  // As typed ("every 3 weeks", "через день"), then with the repeat word the
  // row leaves out: "3 weeks", "mon, wed", "2 дня", "пн, ср".
  // A unit on its own in the plural ("weeks", "дня") says no interval: it is
  // not read as every one of them.
  static const QStringList kBareUnits = {QStringLiteral("days"),
                                         QStringLiteral("weeks"),
                                         QStringLiteral("months"),
                                         QString::fromUtf8("дня"),
                                         QString::fromUtf8("дней"),
                                         QString::fromUtf8("недели"),
                                         QString::fromUtf8("недель"),
                                         QString::fromUtf8("месяца"),
                                         QString::fromUtf8("месяцев")};
  if(kBareUnits.contains(said.toLower())) {
    return {};
  }
  const QStringList tries = {said, QStringLiteral("every ") + said, QString::fromUtf8("каждые ") + said, QString::fromUtf8("по ") + said};
  const QDateTime now = QDateTime::currentDateTime();
  for(const QString& line : tries) {
    const heap::chrono::ParseResult r = m_chrono->parse(line, now);
    if(!r.ok || r.recurrence.isEmpty() || r.startOffset < 0) {
      continue;
    }
    // The whole line, or the words are something else ("every 2 weeks on
    // the moon"): nothing is half read.
    QString rest = line.left(r.startOffset) + line.mid(r.endOffset);
    rest.remove(QLatin1Char(',')).remove(QLatin1Char('.'));
    if(rest.trimmed().isEmpty()) {
      return heap::recur::canonicalRule(r.recurrence);
    }
  }
  return {};
}

void AppController::deleteEvent(const QString& id) {
  if(refuseSubscriptionEdit(id)) {
    return;
  }
  const int row = m_events.indexOfId(id);
  if(row < 0) {
    return;
  }
  const CalEvent removedEvent = m_events.items().at(row);
  UndoScope scope(this, tr_("event.restored").arg(removedEvent.title));
  scope.setRedoLabel(tr_("event.deleted").arg(removedEvent.title));
  // A QuickCapture "sync" is one thing shown twice: the meeting event and the
  // task that mirrors it on the board. Deleting the meeting must take the mirror
  // task with it, else the user has to hunt it down separately (HEAP-104). Only
  // a "sync" owns its task — a "focus" block is just a scheduled slice of a task
  // that must outlive the block.
  const CalEvent& ev = removedEvent;
  if(ev.type == QStringLiteral("sync") && !ev.taskId.isEmpty()) {
    if(m_tasks.indexOfId(ev.taskId) >= 0) {
      // Sweep the task's other blocks (a focus slice, say) so none is left
      // pointing at a task that no longer exists.
      QStringList siblingIds;
      for(const CalEvent& sib : m_events.items()) {
        if(sib.id != ev.id && sib.taskId == ev.taskId) {
          siblingIds << sib.id;
        }
      }
      for(const QString& sibId : siblingIds) {
        m_events.removeById(sibId);
      }
      m_tasks.removeById(ev.taskId);
    }
  }
  m_events.removeById(id);
  followFocusBlock(removedEvent, nullptr);
  emit undoableToast(tr_("event.deleted").arg(ev.title), 5);
  scheduleSave();
}

int AppController::taskBlockMinutes(const QString& taskId) const {
  // Length comes from the task's own estimate when it has one: dropping a
  // 20-minute chore onto the calendar used to carve out a full hour regardless.
  // Without an estimate, the focus-block duration from settings is the better
  // guess than a hardcoded 60 minutes.
  const int row = m_tasks.indexOfId(taskId);
  const int fallbackMin = settingsMap().value("calendar").toMap().value("focusBlockDuration", 90).toInt();
  const int est = row >= 0 ? heap::integrations::planMinutes(m_tasks.items().at(row)) : 0;
  return est > 0 ? est : qMax(15, fallbackMin);
}

void AppController::scheduleTask(const QString& taskId, double startHour, const QDate& date) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);  // by value — scheduledAt is written back
  const QDate day = date.isValid() ? date : m_selectedDate;
  // The block and the task's new scheduledAt are one action (TIME-12).
  const UndoScope scope(this, tr_("undo.schedule").arg(t.id));

  const int durMin = taskBlockMinutes(taskId);
  const double step = snapStepHours();
  const heap::cal::HourRange hours = heap::cal::clampHours(startHour, startHour + durMin / 60.0, step);

  CalEvent e;
  e.id = QString("ev-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  e.title = QString("Focus · %1").arg(t.title.left(36));
  e.type = "focus";
  e.start = hours.start;
  e.end = hours.end;
  e.attendees = "deep work";
  e.date = day;
  e.taskId = t.id;
  e.profileId = m_activeProfileId;
  m_events.upsert(e);

  // The task itself has to know when it is happening. Only DayCalendar reads
  // the focus block; every other surface — Week, Month, Timeline, the card's
  // own "scheduled" label — goes through Task.scheduledAt, so without this a
  // time-blocked task stayed unscheduled everywhere but the day it was dropped.
  t.scheduledAt = QDateTime(day, heap::cal::hourToTime(hours.start));
  t.scheduledHasTime = true;  // the deadline keeps its own flag (schema v10)
  m_tasks.upsert(t);

  // Undoable from the toast, like every other drop in the calendars (APP-249).
  emit undoableToast(tr_("event.scheduled").arg(t.id, eventHourLabel(hours.start)), 5);
  scheduleSave();
}

void AppController::scheduleTaskAtNextFreeSlot(const QString& taskId, const QDate& date) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const QDate day = date.isValid() ? date : m_selectedDate;
  // The gap is looked for with the length the block will actually have, and
  // among the task blocks too (APP-253). No room in the working day is said
  // out loud, never booked past its end without a word.
  const QVariantMap w = freeWindow(taskId, day);
  if(w.value("found").toBool()) {
    placeTaskAt(taskId, day, w.value("start").toDouble());
    return;
  }
  emit freeWindowMissing(taskId,
                         m_tasks.items().at(row).title,
                         day,
                         w.value("nextDate").toDate(),
                         w.value("nextStart").toDouble(),
                         w.value("lateStart").toDouble());
}

QVector<QPair<double, double>> AppController::busySpans(const QDate& date, const QString& exceptTaskId, const QDateTime& now) const {
  QVector<heap::plan::EventIn> events = dayEvents(m_events.items(), date);
  if(!exceptTaskId.isEmpty()) {
    events.erase(std::remove_if(events.begin(),
                                events.end(),
                                [&](const heap::plan::EventIn& e) {
                                  return e.taskId == exceptTaskId;
                                }),
                 events.end());
  }
  QVector<heap::plan::TaskIn> timed;
  for(const Task& t : m_tasks.items()) {
    if(t.archived || !t.scheduledHasTime || !t.scheduledAt.isValid() || t.id == exceptTaskId) {
      continue;
    }
    if(t.scheduledAt.date() != date || statusCategory(t.status) == QLatin1String("done")) {
      continue;
    }
    heap::plan::TaskIn in;
    in.id = t.id;
    in.title = t.title;
    in.scheduledAt = t.scheduledAt;
    in.minutes = taskBlockMinutes(t.id);
    timed.append(in);
  }
  const heap::plan::Day day = heap::plan::buildDay(date, now, events, timed, m_workdayStart, m_workdayEnd, true);
  QVector<QPair<double, double>> out;
  for(const heap::plan::Block& b : day.blocks) {
    out.append({qMax(0.0, b.start), qMin(24.0, b.end)});
  }
  return out;
}

QVariantMap AppController::freeWindow(const QString& taskId, const QDate& date) const {
  return freeWindowAt(taskId, date, QDateTime::currentDateTime());
}

QVariantMap AppController::freeWindowAt(const QString& taskId, const QDate& date, const QDateTime& now) const {
  heap::plan::WindowAsk ask;
  ask.date = date;
  ask.now = now;
  ask.hours = taskBlockMinutes(taskId) / 60.0;
  ask.workStart = m_workdayStart;
  ask.workEnd = m_workdayEnd;
  ask.lunch = lunchSpan();
  ask.step = snapStepHours();
  const heap::plan::Window w = heap::plan::findWindow(
      ask,
      [&](const QDate& d) {
        return busySpans(d, taskId, now);
      },
      [this](const QDate& d) {
        return isWorkDay(d);
      });
  return {{"found", w.found},
          {"date", date},
          {"start", w.start},
          {"hours", ask.hours},
          {"nextDate", w.nextDate},
          {"nextStart", w.nextStart},
          {"lateStart", w.lateStart}};
}

bool AppController::placeTaskAt(const QString& taskId, const QDate& date, double start) {
  if(!date.isValid() || start < 0 || start >= 24) {
    return false;
  }
  return rescheduleTask(taskId, QStringLiteral("scheduled"), QDateTime(date, heap::cal::hourToTime(start)), true);
}

int AppController::carryTasks(const QStringList& ids, const QString& mode) {
  return carryTasksAt(ids, mode, QDateTime::currentDateTime());
}

int AppController::carryTasksAt(const QStringList& ids, const QString& mode, const QDateTime& now) {
  const bool tomorrow = mode == QLatin1String("tomorrow");
  const bool window = mode == QLatin1String("window");
  const bool someday = mode == QLatin1String("someday");
  const bool clear = mode == QLatin1String("clear");
  // The deadline, only when asked for by name (A5-8): carrying moves the
  // plan date and never touches a deadline the person set.
  const bool clearDue = mode == QLatin1String("clearDue");
  if(!tomorrow && !window && !someday && !clear && !clearDue) {
    return 0;
  }
  const QDate today = now.date();
  UndoScope scope(this, tr_("undo.carry"));
  int changed = 0;
  int missing = 0;
  // Carried, yet still overdue by their deadline: the toast says so, as the
  // count on Today does not go down (A5-8).
  int stillOverdue = 0;
  int withDue = 0;
  QString lastId;
  QString lastWhere;
  for(const QString& id : ids) {
    const int row = m_tasks.indexOfId(id);
    if(row < 0) {
      continue;
    }
    Task t = m_tasks.items().at(row);
    if(t.archived) {
      continue;
    }
    if(heap::local::effectiveDueAt(t).isValid()) {
      ++withDue;
    }
    if(clearDue) {
      // A tracker card's deadline is the tracker's: only a local task's goes.
      if(!t.dueAt.isValid() || !t.externalId.isEmpty()) {
        continue;
      }
      t.dueAt = QDateTime();
      t.dueHasTime = false;
      m_tasks.upsert(t);
      ++changed;
      lastId = t.id;
      continue;
    }
    const QDateTime was = t.scheduledAt;
    const bool wasTimed = t.scheduledHasTime;
    const bool wasSomeday = t.someday;
    if(tomorrow) {
      const heap::plan::Planned p = heap::plan::toTomorrow(t.scheduledAt, t.scheduledHasTime, today);
      t.scheduledAt = p.at;
      t.scheduledHasTime = p.hasTime;
      t.someday = false;
    } else if(window) {
      heap::plan::WindowAsk ask;
      ask.date = today;
      ask.now = now;
      ask.hours = taskBlockMinutes(id) / 60.0;
      ask.workStart = m_workdayStart;
      ask.workEnd = m_workdayEnd;
      ask.lunch = lunchSpan();
      ask.step = snapStepHours();
      // The tasks carried before this one are in the model already, so they
      // count as busy and the selection lines up instead of stacking.
      const auto [day, start] = heap::plan::nextWindow(
          ask,
          [&](const QDate& d) {
            return busySpans(d, id, now);
          },
          [this](const QDate& d) {
            return isWorkDay(d);
          });
      if(!day.isValid()) {
        ++missing;
        continue;
      }
      t.scheduledAt = QDateTime(day, heap::cal::hourToTime(start));
      t.scheduledHasTime = true;
      t.someday = false;
    } else {
      t.scheduledAt = QDateTime();
      t.scheduledHasTime = false;
      if(someday) {
        t.someday = true;
      }
    }
    if(t.scheduledAt == was && t.scheduledHasTime == wasTimed && t.someday == wasSomeday) {
      continue;
    }
    m_tasks.upsert(t);
    if(t.scheduledHasTime && wasTimed) {
      moveLinkedFocusBlocks(t.id, was, t.scheduledAt);
    }
    const QDateTime due = heap::local::effectiveDueAt(t);
    if(due.isValid() && due.date() < today && statusCategory(t.status) != QLatin1String("done")) {
      ++stillOverdue;
    }
    ++changed;
    lastId = t.id;
    lastWhere = t.scheduledHasTime ? dateTimeLabel(t.scheduledAt, QStringLiteral("weekdayDay")) : QString();
  }
  if(changed == 0) {
    scope.abandon();
    // Nothing moved is said too: "clear date" on tasks that only have a
    // deadline did nothing and said nothing (A5-8).
    if(missing > 0) {
      emit toast(tr_("carry.noWindow"), QStringLiteral("info"));
    } else if(withDue > 0 && !clearDue) {
      emit toast(tr_("carry.nothingDue"), QStringLiteral("info"));
    } else {
      emit toast(tr_("carry.nothing"), QStringLiteral("info"));
    }
    return 0;
  }
  const QString who = changed == 1 ? lastId : tr_("carry.many").arg(changed);
  QString msg;
  if(tomorrow) {
    msg = tr_("carry.tomorrow").arg(who);
  } else if(window) {
    msg = tr_("carry.window").arg(who, changed == 1 ? lastWhere : tr_("carry.windows"));
  } else if(someday) {
    msg = tr_("carry.someday").arg(who);
  } else if(clearDue) {
    msg = tr_("carry.clearDue").arg(who);
  } else {
    msg = tr_("carry.clear").arg(who);
  }
  if(missing > 0) {
    msg += QStringLiteral(" · ") + tr_("carry.noWindow");
  }
  if(stillOverdue > 0) {
    msg += QStringLiteral(" · ") + tr_("carry.dueStays").arg(stillOverdue);
  }
  emit undoableToast(msg, 6);
  scheduleSave();
  return changed;
}

bool AppController::rescheduleTask(const QString& taskId, const QString& field, const QDateTime& when, bool hasTime) {
  const bool due = field == QStringLiteral("due");
  if(!due && field != QStringLiteral("scheduled")) {
    return false;
  }
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return false;
  }
  Task t = m_tasks.items().at(row);
  // Minutes, not seconds: a view hands over a JS Date, and a stray second
  // would make "the same time" compare unequal.
  QDateTime next;
  bool nextHasTime = false;
  if(when.isValid() && when.date().isValid()) {
    nextHasTime = hasTime;
    next = QDateTime(when.date(), hasTime ? QTime(when.time().hour(), when.time().minute()) : QTime(0, 0));
  }
  const bool mine = due && !t.externalId.isEmpty();  // a tracker card's date is mine (APP-238)
  QDateTime& slot = due ? t.dueAt : t.scheduledAt;
  bool& slotHasTime = due ? t.dueHasTime : t.scheduledHasTime;
  const QDateTime current = mine ? heap::local::effectiveDueAt(t) : slot;
  const bool currentHasTime = mine ? heap::local::effectiveDueHasTime(t) : slotHasTime;
  if(current == next && currentHasTime == nextHasTime) {
    return false;
  }
  const UndoScope scope(this, tr_(due ? "undo.deadline" : "undo.schedule").arg(t.id));
  const QDateTime was = current;
  if(mine) {
    heap::local::setMyDue(t, next, nextHasTime);
  } else {
    slot = next;
    slotHasTime = nextHasTime;
  }
  m_tasks.upsert(t);
  if(!due && nextHasTime) {
    moveLinkedFocusBlocks(t.id, was, next);
  }

  QString what;
  if(!next.isValid()) {
    what = tr_(due ? "reschedule.dueCleared" : "reschedule.scheduledCleared").arg(t.id);
  } else {
    const QString at =
        nextHasTime ? dateTimeLabel(next, QStringLiteral("weekdayDay")) : dateLabel(next.date(), QStringLiteral("weekdayDay"));
    what = tr_(due ? "reschedule.due" : "reschedule.scheduled").arg(t.id, at);
  }
  // A tracker's deadline changed here is a local value from now on; the toast
  // says so rather than letting it look like it went upstream.
  if(due && !t.externalProvider.isEmpty()) {
    what += QStringLiteral(" · ") + tr_("reschedule.localOnly");
  }
  emit undoableToast(what, 5);
  scheduleSave();
  return true;
}

bool AppController::clearTaskDate(const QString& taskId, const QString& field) {
  return rescheduleTask(taskId, field, QDateTime(), false);
}

bool AppController::resizeTaskBlock(const QString& taskId, const QDate& date, double startHour, double endHour) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0 || !date.isValid()) {
    return false;
  }
  Task t = m_tasks.items().at(row);
  const heap::cal::HourRange hours = heap::cal::clampHours(startHour, endHour, snapStepHours());
  const QDateTime at(date, heap::cal::hourToTime(hours.start));
  const int minutes = static_cast<int>(std::lround((hours.end - hours.start) * 60.0));
  if(t.scheduledAt == at && t.scheduledHasTime && t.estimateMinutes == minutes) {
    return false;
  }
  const UndoScope scope(this, tr_("undo.schedule").arg(t.id));
  const QDateTime was = t.scheduledAt;
  t.scheduledAt = at;
  t.scheduledHasTime = true;
  t.estimateMinutes = minutes;
  m_tasks.upsert(t);
  moveLinkedFocusBlocks(t.id, was, at);
  emit undoableToast(tr_("reschedule.block").arg(t.id, dateTimeLabel(at, QStringLiteral("weekdayDay")), eventHourLabel(hours.end)), 5);
  scheduleSave();
  return true;
}

void AppController::moveLinkedFocusBlocks(const QString& taskId, const QDateTime& was, const QDateTime& now) {
  if(!was.isValid() || !now.isValid()) {
    return;
  }
  // Only the block the old schedule was read from, and only a one-off: a
  // series is moved through its own scope question, not from a task's date.
  QVector<CalEvent> moved;
  for(const CalEvent& e : m_events.items()) {
    if(e.type != QStringLiteral("focus") || e.taskId != taskId || !e.rrule.isEmpty() || e.allDay) {
      continue;
    }
    if(QDateTime(e.date, heap::cal::hourToTime(e.start)) != was) {
      continue;
    }
    CalEvent m = e;
    const double length = e.end - e.start;
    const double start = now.time().hour() + (now.time().minute() / 60.0);
    m.date = now.date();
    m.start = start;
    m.end = qMin(24.0, start + length);
    m.endDate = QDate();
    moved.append(m);
  }
  for(const CalEvent& m : moved) {
    m_events.upsert(m);
  }
}

void AppController::followFocusBlock(const CalEvent& before, const CalEvent* after) {
  if(before.type != QStringLiteral("focus") || before.taskId.isEmpty()) {
    return;
  }
  const int row = m_tasks.indexOfId(before.taskId);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  const QDateTime was(before.date, heap::cal::hourToTime(before.start));
  // Only a schedule that came from this block follows it: a task the user
  // scheduled for some other time keeps that time.
  if(t.scheduledAt.isValid() && t.scheduledAt != was) {
    return;
  }
  if(after != nullptr) {
    t.scheduledAt = QDateTime(after->date, heap::cal::hourToTime(after->start));
    t.scheduledHasTime = true;
  } else {
    // The block is gone: the task has no slot any more and goes back to the
    // "needs a slot" rail.
    t.scheduledAt = QDateTime();
    t.scheduledHasTime = false;
  }
  m_tasks.upsert(t);
}

double AppController::nextFreeSlot(const QDate& date, double durationHours) const {
  const double step = snapStepHours();
  const double dur = qMax(step, durationHours);
  // One search (APP-253): meetings and task blocks are busy, the working
  // hours of a working day first. Today starts from now; any other day from
  // the top of the working day.
  const QDateTime realNow = QDateTime::currentDateTime();
  const QDateTime now = date == realNow.date() ? realNow : QDateTime(date, QTime(0, 0));
  heap::plan::WindowAsk ask;
  ask.date = date;
  ask.now = now;
  ask.hours = dur;
  ask.workStart = m_workdayStart;
  ask.workEnd = m_workdayEnd;
  ask.lunch = lunchSpan();
  ask.step = step;
  ask.maxDays = 0;
  const heap::plan::Window w = heap::plan::findWindow(
      ask,
      [&](const QDate& d) {
        return busySpans(d, QString(), now);
      },
      [this](const QDate& d) {
        return isWorkDay(d);
      });
  if(w.found) {
    return w.start;
  }
  if(w.lateStart >= 0) {
    return w.lateStart;
  }
  // Nothing fits at all: the old answer, a block that still ends by midnight.
  double cursor = m_workdayStart;
  if(date == realNow.date()) {
    cursor = qMax(cursor, nextQuarterHour(realNow));
  }
  cursor = std::ceil(cursor / step) * step;

  // What is on the calendar that day: the occurrences, so a daily standup and
  // a meeting that started yesterday evening count. An all-day event is a
  // label on the day (a holiday, a release), not time taken.
  QVector<QPair<double, double>> busy;
  for(const CalEvent& e : heap::cal::expandedEvents(m_events.items(), date, date)) {
    if(e.allDay || !e.date.isValid()) {
      continue;
    }
    const QDate last = (e.endDate.isValid() && e.endDate > e.date) ? e.endDate : e.date;
    if(date < e.date || date > last) {
      continue;
    }
    const double from = (e.date < date) ? 0.0 : e.start;
    const double to = (last > date) ? 24.0 : e.end;
    busy.append({from, to});
  }
  std::sort(busy.begin(), busy.end());
  for(const auto& b : busy) {
    if(cursor + dur <= b.first) {
      break;  // fits in the gap before this one
    }
    if(b.second > cursor) {
      cursor = std::ceil(b.second / step) * step;
    }
  }
  // Past the end of the working day is still a legal answer — the grid runs to
  // midnight — but there has to be room for the block itself.
  return qBound(0.0, cursor, qMax(0.0, 24.0 - dur));
}

void AppController::cyclePerson(const QString& id) {
  const int row = m_people.indexOfId(id);
  const QString before = row >= 0 ? m_people.items().at(row).state : QString();
  m_people.cycleState(id);
  personStateMoved(id, before);
  scheduleSave();
}

void AppController::setPersonState(const QString& id, const QString& state) {
  const int row = m_people.indexOfId(id);
  if(row < 0) {
    return;
  }
  const Person person = m_people.items().at(row);
  const QString before = person.state;
  if(before == state) {
    return;
  }
  // "Wrote" took the person off Today with no way back (dogfood 2026-10-16):
  // the move is undoable, and the toast says so.
  const UndoScope scope(this, tr_("person.stateUndone").arg(person.name));
  m_people.setState(id, state);
  personStateMoved(id, before);
  if(state == QLatin1String("pinged") || state == QLatin1String("replied")) {
    emit undoableToast(tr_(state == QLatin1String("pinged") ? "person.pinged" : "person.replied").arg(person.name), 6);
  } else if(state == QLatin1String("todo")) {
    // Back on "Кому написать" from "Люди" (APP-321).
    emit undoableToast(tr_("person.backToTodo").arg(person.name), 6);
  }
  scheduleSave();
}

QVariantMap AppController::newPersonDraft() const {
  static const QColor palette[] = {
      QColor("#d97a6c"),
      QColor("#c87fc7"),
      QColor("#6cc4b8"),
      QColor("#7da8d9"),
      QColor("#dcc06a"),
      QColor("#7cc492"),
      QColor("#e69854"),
      QColor("#a4a4d6"),
  };
  const int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
  const QColor c = palette[m_people.rowCount() % n];
  QVariantMap m;
  m["_isNew"] = true;
  // Empty id signals "auto-derive from name on save" — see savePerson().
  m["id"] = QString();
  m["name"] = QString();
  m["role"] = QString();
  m["question"] = QString();
  m["state"] = "todo";
  m["color"] = c;
  return m;
}

QString AppController::personIdForHandle(const QString& handle) const {
  // "@Oleg_T." as written in a note: underscores for spaces, maybe trailing
  // punctuation. Compared without case, in any script.
  const auto norm = [](QString s) {
    s = s.trimmed();
    if(s.startsWith(QLatin1Char('@'))) {
      s = s.mid(1);
    }
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    while(!s.isEmpty() && QStringLiteral(".,;:!? ").contains(s.back())) {
      s.chop(1);
    }
    return s.simplified();
  };
  const QString want = norm(handle);
  if(want.isEmpty()) {
    return {};
  }
  for(const Person& p : m_people.items()) {
    if(norm(p.name).compare(want, Qt::CaseInsensitive) == 0 || p.id.compare(want, Qt::CaseInsensitive) == 0) {
      return p.id;
    }
  }
  // "@oleg" for "Oleg T.": a unique first-word match is still a match.
  QString found;
  for(const Person& p : m_people.items()) {
    const QString first = norm(p.name).section(QLatin1Char(' '), 0, 0);
    if(first.compare(want, Qt::CaseInsensitive) == 0) {
      if(!found.isEmpty()) {
        return {};
      }
      found = p.id;
    }
  }
  if(!found.isEmpty()) {
    return found;
  }
  // "@r.losev" for "Роман Лосев", hand-written: a login derived from the
  // name names them when it names nobody else. Two people it fits ("Руслан
  // Лосев" too) and it names neither.
  for(const Person& p : m_people.items()) {
    if(heap::text::personMatchRank(want, p.name, p.id) >= heap::text::PersonRank::ExactHandle) {
      if(!found.isEmpty()) {
        return {};
      }
      found = p.id;
    }
  }
  return found;
}

int AppController::personMatchRank(const QString& query, const QString& name, const QString& id) const {
  return heap::text::personMatchRank(query, name, id);
}

QVariantList AppController::matchPeople(const QString& query, int limit) const {
  struct Hit {
    int rank;
    qsizetype row;
  };

  const QVector<Person>& people = m_people.items();
  QVector<Hit> hits;
  for(qsizetype i = 0; i < people.size(); ++i) {
    const int rank = heap::text::personMatchRank(query, people[i].name, people[i].id);
    if(rank > heap::text::PersonRank::NoMatch) {
      hits.push_back({rank, i});
    }
  }
  // Best rank first; the model's own order among equals.
  std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
    return a.rank > b.rank;
  });
  QVariantList out;
  for(const Hit& h : hits) {
    if(limit > 0 && out.size() >= limit) {
      break;
    }
    const Person& p = people[h.row];
    out.append(QVariantMap{{"id", p.id}, {"name", p.name}, {"role", p.role}, {"color", p.color}, {"rank", h.rank}});
  }
  return out;
}

QVariantMap AppController::personById(const QString& id) const {
  const int row = m_people.indexOfId(id);
  if(row < 0) {
    return {};
  }
  const QModelIndex mi = m_people.index(row, 0);
  QVariantMap m;
  m["_isNew"] = false;
  m["id"] = m_people.data(mi, PersonModel::IdRole);
  m["name"] = m_people.data(mi, PersonModel::NameRole);
  m["role"] = m_people.data(mi, PersonModel::RoleRole);
  m["question"] = m_people.data(mi, PersonModel::QuestionRole);
  m["state"] = m_people.data(mi, PersonModel::StateRole);
  m["color"] = m_people.data(mi, PersonModel::ColorRole);
  return m;
}

QVariantMap AppController::personLinks(const QString& id) const {
  const int row = m_people.indexOfId(id);
  if(row < 0) {
    return {};
  }
  const Person& p = m_people.items().at(row);
  QVariantList tasks;
  for(const WaitingOn& w : m_waitingOn) {
    const int t = w.personId == p.id ? m_tasks.indexOfId(w.taskId) : -1;
    if(t < 0) {
      continue;
    }
    const Task& task = m_tasks.items().at(t);
    const QString key = externalKeyOf(task);
    tasks.append(QVariantMap{{"id", task.id}, {"key", key.isEmpty() ? task.id : key}, {"title", task.title}});
  }
  // Upcoming meetings: the next two weeks, today's only if not over yet.
  QVariantList meetings;
  const QTime nowTime = QTime::currentTime();
  const double nowHour = nowTime.hour() + nowTime.minute() / 60.0;
  for(const QVariant& v : eventOccurrences(m_today, m_today.addDays(14))) {
    const QVariantMap e = v.toMap();
    const QDate day = e.value("date").toDate();
    if(!heap::people::attendeesName(e.value("attendees").toString(), p.name, p.id)) {
      continue;
    }
    if(day == m_today && !e.value("allDay").toBool() && e.value("end").toDouble() <= nowHour) {
      continue;
    }
    meetings.append(QVariantMap{
        {"id", e.value("id")}, {"date", day}, {"start", e.value("start")}, {"allDay", e.value("allDay")}, {"title", e.value("title")}});
    if(meetings.size() >= 5) {
      break;
    }
  }
  return {{"tasks", tasks}, {"meetings", meetings}};
}

bool AppController::savePerson(const QVariantMap& draft) {
  Person p;
  p.id = draft.value("id").toString();
  p.name = draft.value("name").toString();
  // A person with no name cannot be picked, mentioned or told apart from the
  // next one — for an existing person as much as for a new one (PLAT-20).
  if(p.name.trimmed().isEmpty()) {
    if(!draft.value("_isNew").toBool()) {
      emit toast(tr_("person.nameRequired"));
    }
    return false;
  }
  p.role = draft.value("role").toString();
  p.question = draft.value("question").toString();
  p.state = draft.value("state").toString();
  p.color = draft.value("color").value<QColor>();
  if(!p.color.isValid()) {
    p.color = QColor("#7da8d9");
  }
  if(p.state.isEmpty()) {
    p.state = "todo";
  }
  // Derive a slug id ("e.zaharov") from the name when the caller did not
  // supply one. Old UUID-style ids ("p-XXXXXXXX") are upgraded too — they
  // were created before the slug scheme was in place.
  static const QRegularExpression kLegacyId(QStringLiteral("^p-[0-9a-fA-F]+$"));
  if(p.id.isEmpty() || kLegacyId.match(p.id).hasMatch()) {
    p.id = suggestPersonId(p.name, /*exceptId=*/draft.value("id").toString());
  }
  if(p.id.isEmpty()) {
    // Names with no transliterable letters at all — fall back to UUID
    // so we never end up with an empty key.
    p.id = QString("p-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  }
  // Keys a newer build wrote stay with the person (PLAT-15).
  if(const int prevRow = m_people.indexOfId(draft.value("id").toString()); prevRow >= 0) {
    const Person& prev = m_people.items().at(prevRow);
    p.extra = prev.extra;
    // The state's time moves only with the state (R3-057).
    p.stateAt = prev.state == p.state ? prev.stateAt : QDateTime::currentDateTime();
  } else {
    p.stateAt = QDateTime::currentDateTime();
  }
  const bool isNew = draft.value("_isNew").toBool();
  // upsert() on an id someone else holds replaces that person whole — name,
  // role, question — with no undo (SHELL-24). A new person, or an edit that
  // moves to another id, must pick a free one.
  const QString originalId = draft.value("_originalId").toString();
  const int holder = m_people.indexOfId(p.id);
  if(holder >= 0 && (isNew || (!originalId.isEmpty() && originalId != p.id))) {
    emit toast(tr_("person.idTaken").arg(p.id, m_people.items().at(holder).name), QStringLiteral("warning"));
    return false;
  }
  const UndoScope scope(this, tr_(isNew ? "person.createUndone" : "person.editUndone").arg(p.name));
  const int beforeRow = m_people.indexOfId(!isNew && !originalId.isEmpty() ? originalId : p.id);
  const QString stateBefore = beforeRow >= 0 ? m_people.items().at(beforeRow).state : QString();
  // An edit that changes the id renames the person: upsert() alone added a
  // second one under the new id and left the old one in the list (SHELL-25).
  // The renamed person keeps its place, and the Docs contacts linked to the
  // old id follow it.
  const int renamedRow = !isNew && !originalId.isEmpty() && originalId != p.id ? m_people.indexOfId(originalId) : -1;
  if(renamedRow >= 0) {
    p.extra = m_people.items().at(renamedRow).extra;
    m_people.removeById(originalId);
    m_people.insertAt(renamedRow, p);
    relinkDocsContacts(originalId, p.id);
  } else {
    m_people.upsert(p);
  }
  personStateMoved(p.id, stateBefore);
  // Keep Docs and the rail pointing at each other. A Person picked out of a
  // contact gets that contact's `personId` (so the next pick, and the next
  // Mattermost sync, reuse this Person instead of making a second one); a
  // Person created through "add contact" gets a contact of its own, so it is
  // searchable next time.
  if(draft.value("_createContact").toBool()) {
    appendDocsContact(p);
  } else {
    linkDocsContact(draft.value("_contactKey").toString(), p.id);
  }
  if(isNew) {
    emit undoableToast(tr_("person.added").arg(p.name), 5);
  }
  scheduleSave();
  return true;
}

QString AppController::docsContactKey(const QJsonObject& contact) {
  // Shared with the undo rebase, which must recognise an edited contact as the
  // same one.
  return heap::undo::docsContactKey(contact);
}

QVariantList AppController::pingCandidates() const {
  QVariantList out;
  // personId → the row in `out` that already represents that human, so a
  // contact and the Person it was imported into are offered once.
  QHash<QString, int> byPerson;

  const QJsonObject docs = QJsonDocument::fromJson(m_docsState.toUtf8()).object();
  const QJsonArray contacts = docs.value(QStringLiteral("contacts")).toArray();
  for(const QJsonValue& v : contacts) {
    const QJsonObject c = v.toObject();
    const QString name = c.value(QStringLiteral("name")).toString().trimmed();
    if(name.isEmpty()) {
      continue;
    }
    // A contact may point at a Person that has since been deleted; treat that
    // link as absent rather than offering a row that cannot be opened.
    QString personId = c.value(QStringLiteral("personId")).toString();
    if(!personId.isEmpty() && m_people.indexOfId(personId) < 0) {
      personId.clear();
    }
    QVariantMap m;
    m[QStringLiteral("contactKey")] = docsContactKey(c);
    m[QStringLiteral("personId")] = personId;
    m[QStringLiteral("name")] = name;
    m[QStringLiteral("role")] = c.value(QStringLiteral("role")).toString();
    m[QStringLiteral("handle")] = c.value(QStringLiteral("mattermost")).toString();
    m[QStringLiteral("channel")] = c.value(QStringLiteral("channel")).toString();
    m[QStringLiteral("color")] = c.value(QStringLiteral("color")).toString();
    m[QStringLiteral("source")] = c.value(QStringLiteral("source")).toString();
    m[QStringLiteral("state")] = QString();
    m[QStringLiteral("active")] = false;
    if(!personId.isEmpty()) {
      const QModelIndex mi = m_people.index(m_people.indexOfId(personId), 0);
      const QString state = m_people.data(mi, PersonModel::StateRole).toString();
      m[QStringLiteral("state")] = state;
      m[QStringLiteral("active")] = state != QLatin1String("idle");
      byPerson.insert(personId, static_cast<int>(out.size()));
    }
    m[QStringLiteral("key")] = personId.isEmpty() ? m.value(QStringLiteral("contactKey")).toString() : QStringLiteral("p:") + personId;
    out.append(m);
  }

  // People with no contact behind them — typed straight into the rail, or
  // created by an import whose contact the user has since deleted.
  for(const Person& p : m_people.items()) {
    if(byPerson.contains(p.id)) {
      continue;
    }
    QVariantMap m;
    m[QStringLiteral("key")] = QStringLiteral("p:") + p.id;
    m[QStringLiteral("contactKey")] = QString();
    m[QStringLiteral("personId")] = p.id;
    m[QStringLiteral("name")] = p.name;
    m[QStringLiteral("role")] = p.role;
    m[QStringLiteral("handle")] = QLatin1Char('@') + p.id;
    m[QStringLiteral("channel")] = QString();
    m[QStringLiteral("color")] = p.color.name();
    m[QStringLiteral("source")] = QString();
    m[QStringLiteral("state")] = p.state;
    m[QStringLiteral("active")] = p.state != QLatin1String("idle");
    out.append(m);
  }
  return out;
}

QVariantMap AppController::pingDraftFor(const QVariantMap& candidate) const {
  const QString personId = candidate.value(QStringLiteral("personId")).toString();
  if(!personId.isEmpty() && m_people.indexOfId(personId) >= 0) {
    QVariantMap draft = personById(personId);
    // Picking someone off the list is the act of putting them on it. Someone
    // already on it keeps the state they are in — reopening a "pinged" row
    // from the picker must not quietly walk it back to "to write".
    if(draft.value(QStringLiteral("state")).toString() == QLatin1String("idle")) {
      draft[QStringLiteral("state")] = QStringLiteral("todo");
    }
    return draft;
  }

  QVariantMap draft = newPersonDraft();
  const QString name = candidate.value(QStringLiteral("name")).toString().trimmed();
  draft[QStringLiteral("name")] = name;
  draft[QStringLiteral("role")] = candidate.value(QStringLiteral("role")).toString();
  // The handle is the id people already @-mention this person by; a slug of
  // the display name would not match it. Same reasoning as
  // upsertImportedPerson().
  QString handle = candidate.value(QStringLiteral("handle")).toString().trimmed();
  while(handle.startsWith(QLatin1Char('@'))) {
    handle.remove(0, 1);
  }
  static const QRegularExpression kUnsafe(QStringLiteral("[^a-z0-9._-]"));
  handle = handle.toLower().replace(kUnsafe, QString());
  // uniquePersonId, not suggestPersonId: slugifyPersonName drops the dots and
  // dashes a handle is made of, so "olga.t" would become "olgat" and stop
  // matching the @-mention people actually type. Same reasoning as
  // upsertImportedPerson().
  draft[QStringLiteral("id")] = handle.isEmpty() ? suggestPersonId(name) : uniquePersonId(handle);
  const QColor c(candidate.value(QStringLiteral("color")).toString());
  if(c.isValid()) {
    draft[QStringLiteral("color")] = c;
  }
  // savePerson() links the contact to whatever id the editor ends up saving.
  draft[QStringLiteral("_contactKey")] = candidate.value(QStringLiteral("contactKey"));
  return draft;
}

QVariantMap AppController::newContactDraft(const QString& name) const {
  QVariantMap draft = newPersonDraft();
  draft[QStringLiteral("name")] = name.trimmed();
  draft[QStringLiteral("id")] = suggestPersonId(name);
  draft[QStringLiteral("_createContact")] = true;
  return draft;
}

void AppController::linkDocsContact(const QString& contactKey, const QString& personId) {
  if(contactKey.isEmpty() || personId.isEmpty()) {
    return;
  }
  QJsonObject docs = QJsonDocument::fromJson(m_docsState.toUtf8()).object();
  QJsonArray list = docs.value(QStringLiteral("contacts")).toArray();
  for(int i = 0; i < list.size(); ++i) {
    QJsonObject c = list.at(i).toObject();
    if(docsContactKey(c) != contactKey) {
      continue;
    }
    if(c.value(QStringLiteral("personId")).toString() == personId) {
      return;
    }
    c.insert(QStringLiteral("personId"), personId);
    list.replace(i, c);
    docs.insert(QStringLiteral("contacts"), list);
    setDocsState(QString::fromUtf8(QJsonDocument(docs).toJson(QJsonDocument::Compact)));
    scheduleSave();
    return;
  }
}

void AppController::relinkDocsContacts(const QString& fromPersonId, const QString& toPersonId) {
  QJsonObject docs = QJsonDocument::fromJson(m_docsState.toUtf8()).object();
  QJsonArray list = docs.value(QStringLiteral("contacts")).toArray();
  bool changed = false;
  for(int i = 0; i < list.size(); ++i) {
    QJsonObject c = list.at(i).toObject();
    if(c.value(QStringLiteral("personId")).toString() != fromPersonId) {
      continue;
    }
    c.insert(QStringLiteral("personId"), toPersonId);
    list.replace(i, c);
    changed = true;
  }
  if(changed) {
    docs.insert(QStringLiteral("contacts"), list);
    setDocsState(QString::fromUtf8(QJsonDocument(docs).toJson(QJsonDocument::Compact)));
  }
}

void AppController::appendDocsContact(const Person& p) {
  QJsonObject docs = QJsonDocument::fromJson(m_docsState.toUtf8()).object();
  QJsonArray list = docs.value(QStringLiteral("contacts")).toArray();
  for(const QJsonValue& v : list) {
    if(v.toObject().value(QStringLiteral("personId")).toString() == p.id) {
      return;
    }
  }
  QJsonObject c;
  c.insert(QStringLiteral("name"), p.name);
  c.insert(QStringLiteral("role"), p.role);
  c.insert(QStringLiteral("mattermost"), QLatin1Char('@') + p.id);
  c.insert(QStringLiteral("channel"), QString());
  c.insert(QStringLiteral("color"), p.color.name());
  c.insert(QStringLiteral("personId"), p.id);
  // Append only, for the reason mergeExternalContacts() gives: DocsView
  // renders this array by index.
  list.append(c);
  docs.insert(QStringLiteral("contacts"), list);
  setDocsState(QString::fromUtf8(QJsonDocument(docs).toJson(QJsonDocument::Compact)));
  scheduleSave();
}

QString AppController::uniquePersonId(const QString& base, const QString& exceptId) const {
  if(base.isEmpty()) {
    return QString();
  }
  // Collision check against every Person across every profile so that ids
  // remain globally unique (even though models are per-profile). The live
  // model is checked too: m_profiles holds the snapshot taken at the last
  // profile switch, so a Person added since is not in it.
  auto inUse = [this, &exceptId](const QString& candidate) {
    if(candidate == exceptId) {
      return false;
    }
    if(m_people.indexOfId(candidate) >= 0) {
      return true;
    }
    for(const Profile& pr : m_profiles) {
      for(const Person& pe : pr.people) {
        if(pe.id == candidate) {
          return true;
        }
      }
    }
    return false;
  };
  if(!inUse(base)) {
    return base;
  }
  for(int i = 2; i < 1000; ++i) {
    const QString c = base + QChar('-') + QString::number(i);
    if(!inUse(c)) {
      return c;
    }
  }
  return base;  // last-resort, caller already guarded against empty
}

QString AppController::suggestPersonId(const QString& name, const QString& exceptId) const {
  return uniquePersonId(heap::text::slugifyPersonName(name), exceptId);
}

void AppController::deletePerson(const QString& id) {
  const int row = m_people.indexOfId(id);
  if(row < 0) {
    return;
  }
  const Person removedPerson = m_people.items().at(row);
  const UndoScope scope(this, tr_("person.restored").arg(removedPerson.name));
  m_people.removeById(id);
  personStateMoved(id, removedPerson.state);
  emit undoableToast(tr_("person.deleted").arg(removedPerson.name), 5);
  scheduleSave();
}

// Every task in the column, archived ones too: this is what a column delete
// re-homes, so it is what the confirmation has to count.
int AppController::countByStatus(const QString& statusId) const {
  int n = 0;
  for(const Task& t : m_tasks.items()) {
    n += t.status == statusId ? 1 : 0;
  }
  return n;
}

QVariantMap AppController::statusCounts() const {
  // One pass, cached until the task model changes. The rail and the top bar
  // between them asked for six separate counts on every single task edit, each
  // a full scan of the model; now they read one map that is built once.
  if(!m_statusCountsDirty) {
    return m_statusCounts;
  }
  // Archived tasks are off the board, so they are off its counters too: the
  // sidebar's Blocked badge counted every blocked card ever archived.
  // "_total" (no column id can start with "_") is the live task count, left
  // out when there is none so an empty board still counts as an empty map.
  m_statusCounts.clear();
  int total = 0;
  for(const Task& t : m_tasks.items()) {
    if(t.archived) {
      continue;
    }
    ++total;
    m_statusCounts[t.status] = m_statusCounts.value(t.status).toInt() + 1;
  }
  if(total > 0) {
    m_statusCounts[QStringLiteral("_total")] = total;  // absent reads as 0
  }
  m_statusCountsDirty = false;
  return m_statusCounts;
}

void AppController::refreshTaskTitles() {
  m_taskTitlesQueued = false;
  QVariantMap next;
  QVariantMap keys;
  QVariantMap stages;
  for(const Task& t : m_tasks.items()) {
    next.insert(t.id, t.title);
    const QString key = externalKeyOf(t);
    keys.insert(t.id, key.isEmpty() ? t.id : key);
    stages.insert(t.id, statusCategory(t.status));
  }
  if(keys != m_taskKeys) {
    m_taskKeys = keys;
    emit taskKeysChanged();
  }
  if(stages != m_taskStages) {
    m_taskStages = stages;
    emit taskStagesChanged();
  }
  if(next == m_taskTitles) {
    return;
  }
  m_taskTitles = next;
  emit taskTitlesChanged();
}

int AppController::statusIndexOf(const QString& id) const {
  for(int i = 0; i < m_statuses.size(); ++i) {
    if(m_statuses[i].toMap().value("id").toString() == id) {
      return i;
    }
  }
  return -1;
}

// As long as a saved view's name: a pasted 3000-character name was kept
// whole and carried into filters and the status mapping (IDIOT-TASKS-16).
static constexpr int kMaxColumnName = 60;

void AppController::addStatus(const QString& typedName, const QString& color) {
  const QString name = typedName.left(kMaxColumnName);
  if(name.trimmed().isEmpty()) {
    return;
  }
  if(statusNameTaken(name, QString())) {
    emit toast(tr_("status.nameTaken").arg(name.trimmed()), QStringLiteral("warning"));
    return;
  }
  const QString base = name.toLower();
  QString slug;
  for(const QChar c : base) {
    slug.append(c.isLetterOrNumber() ? c : QChar('-'));
  }
  while(slug.contains("--")) {
    slug.replace("--", "-");
  }
  if(slug.startsWith('-')) {
    slug = slug.mid(1);
  }
  while(slug.endsWith('-')) {
    slug.chop(1);
  }
  if(slug.isEmpty()) {
    slug = "status";
  }
  QString id = slug;
  int n = 2;
  while(statusIndexOf(id) >= 0) {
    id = slug + "-" + QString::number(n++);
  }
  QVariantMap m;
  m["id"] = id;
  m["name"] = name;
  m["color"] = QColor(color.isEmpty() ? QStringLiteral("#5cc2dd") : color);
  // A new column is "to do" until the user picks its stage (APP-259).
  m["category"] = QStringLiteral("todo");
  m["categoryGuessed"] = false;
  const UndoScope scope(this, tr_("undo.column").arg(name));
  m_statuses.append(m);
  emit statusesChanged();
  emit toast(tr_("status.added").arg(name));
  scheduleSave();
}

bool AppController::statusNameTaken(const QString& name, const QString& exceptId) const {
  for(const QVariant& v : m_statuses) {
    const QVariantMap m = v.toMap();
    if(m.value("id").toString() != exceptId && m.value("name").toString().trimmed().compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

void AppController::renameStatus(const QString& id, const QString& typedName) {
  const QString name = typedName.left(kMaxColumnName);
  const int i = statusIndexOf(id);
  if(i < 0 || name.trimmed().isEmpty()) {
    return;
  }
  // Two columns with one name cannot be told apart on the board, in a filter
  // or in the status mapping.
  if(statusNameTaken(name, id)) {
    emit toast(tr_("status.nameTaken").arg(name.trimmed()), QStringLiteral("warning"));
    emit statusesChanged();  // the editor falls back to the stored name
    return;
  }
  QVariantMap m = m_statuses[i].toMap();
  if(m.value("name").toString() == name) {
    return;
  }
  const UndoScope scope(this, tr_("undo.column").arg(name));
  m["name"] = name;
  m_statuses[i] = m;
  emit statusesChanged();
  scheduleSave();
}

// A WIP limit is advisory: the column says it is over, and nothing is blocked.
// A hard cap would mean a drag that silently does nothing, which reads as a
// bug — the point of the limit is to be noticed, not to police.
void AppController::setStatusWipLimit(const QString& id, int limit) {
  const int i = statusIndexOf(id);
  if(i < 0) {
    return;
  }
  QVariantMap m = m_statuses[i].toMap();
  const int clamped = qBound(0, limit, 999);  // 0 = no limit
  if(m.value("wip").toInt() == clamped) {
    return;
  }
  const UndoScope scope(this, tr_("undo.column").arg(m.value("name").toString()));
  m["wip"] = clamped;
  m_statuses[i] = m;
  emit statusesChanged();
  scheduleSave();
}

namespace {

// Each column's auto-archive days for a set of statuses (APP-122). Done
// takes `doneDays` (Settings → Tasks); another column its own archiveDays,
// absent meaning never.
QHash<QString, int> archiveDaysByStatus(const QVariantList& statuses, int doneDays) {
  QHash<QString, int> out;
  out.insert(QStringLiteral("done"), doneDays);
  for(const QVariant& v : statuses) {
    const QVariantMap m = v.toMap();
    const QString id = m.value("id").toString();
    if(id != QStringLiteral("done") && m.contains(QStringLiteral("archiveDays"))) {
      out.insert(id, qMax(0, m.value("archiveDays").toInt()));
    }
  }
  return out;
}

}  // namespace

int AppController::statusArchiveDays(const QString& id) const {
  const int doneDays = qMax(0, settingsMap().value("tasks").toMap().value("archiveDoneAfterDays", 7).toInt());
  return archiveDaysByStatus(m_statuses, doneDays).value(id, 0);
}

void AppController::setStatusArchiveDays(const QString& id, int days) {
  const int clamped = qBound(0, days, 3650);
  if(id == QStringLiteral("done")) {
    QJsonObject settings = QJsonDocument::fromJson(m_appSettingsJson.toUtf8()).object();
    QJsonObject tasks = settings.value(QStringLiteral("tasks")).toObject();
    tasks.insert(QStringLiteral("archiveDoneAfterDays"), clamped);
    settings.insert(QStringLiteral("tasks"), tasks);
    setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
    emit statusesChanged();  // the column menu reads the number through statuses
    return;
  }
  const int i = statusIndexOf(id);
  if(i < 0) {
    return;
  }
  QVariantMap m = m_statuses[i].toMap();
  if(m.contains(QStringLiteral("archiveDays")) && m.value("archiveDays").toInt() == clamped) {
    return;
  }
  const UndoScope scope(this, tr_("undo.column").arg(m.value("name").toString()));
  m["archiveDays"] = clamped;
  m_statuses[i] = m;
  emit statusesChanged();
  scheduleSave();
}

void AppController::setStatusColor(const QString& id, const QString& color) {
  const int i = statusIndexOf(id);
  if(i < 0) {
    return;
  }
  const QColor c(color);
  if(!c.isValid()) {
    return;
  }
  QVariantMap m = m_statuses[i].toMap();
  if(m.value("color").value<QColor>() == c) {
    return;
  }
  const UndoScope scope(this, tr_("undo.column").arg(m.value("name").toString()));
  m["color"] = c;
  m_statuses[i] = m;
  emit statusesChanged();
  scheduleSave();
}

QString AppController::statusCategory(const QString& id) const {
  const int i = statusIndexOf(id);
  if(i < 0) {
    return QStringLiteral("todo");
  }
  const QString c = m_statuses[i].toMap().value(QStringLiteral("category")).toString();
  return heap::board::isColumnCategory(c) ? c : heap::board::defaultCategoryFor(id);
}

void AppController::setStatusCategory(const QString& id, const QString& category) {
  const int i = statusIndexOf(id);
  if(i < 0 || !heap::board::isColumnCategory(category)) {
    return;
  }
  QVariantMap m = m_statuses[i].toMap();
  if(m.value(QStringLiteral("category")).toString() == category && !m.value(QStringLiteral("categoryGuessed")).toBool()) {
    return;
  }
  const UndoScope scope(this, tr_("undo.column").arg(m.value("name").toString()));
  m[QStringLiteral("category")] = category;
  m[QStringLiteral("categoryGuessed")] = false;  // picked, so no longer to check
  m_statuses[i] = m;
  emit statusesChanged();
  scheduleSave();
}

QStringList AppController::columnsWithGuessedCategory() const {
  QStringList out;
  for(const QVariant& v : m_statuses) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("categoryGuessed")).toBool()) {
      out.append(m.value(QStringLiteral("name")).toString());
    }
  }
  return out;
}

void AppController::moveStatus(const QString& id, int newIndex) {
  const int from = statusIndexOf(id);
  if(from < 0) {
    return;
  }
  newIndex = qBound(0, newIndex, m_statuses.size() - 1);
  if(from == newIndex) {
    return;
  }
  const UndoScope scope(this, tr_("undo.column").arg(m_statuses[from].toMap().value("name").toString()));
  const QVariant v = m_statuses.takeAt(from);
  m_statuses.insert(newIndex, v);
  emit statusesChanged();
  scheduleSave();
}

void AppController::deleteStatus(const QString& id, const QString& into) {
  const int i = statusIndexOf(id);
  if(i < 0 || m_statuses.size() <= 1) {
    return;  // never let the board run out of columns
  }
  const QString statusName = m_statuses[i].toMap().value("name").toString();
  const UndoScope scope(this, tr_("status.restored").arg(statusName));

  // re-home any tasks with this status to the column picked, else to the
  // first remaining one
  QString fallback = (into != id && statusIndexOf(into) >= 0) ? into : QString();
  for(int k = 0; fallback.isEmpty() && k < m_statuses.size(); ++k) {
    if(k == i) {
      continue;
    }
    fallback = m_statuses[k].toMap().value("id").toString();
    break;
  }
  QStringList reHomed;
  for(const Task& t : m_tasks.items()) {
    if(t.status == id) {
      reHomed << t.id;
    }
  }
  // statusChangedAt is stamped by setStatus and restored by the recorded diff,
  // so undoing a column delete does not reset how long a task had been sitting
  // in it — that timestamp drives the "stuck" badge.
  for(const QString& taskId : reHomed) {
    m_tasks.setStatus(taskId, fallback);
  }

  m_statuses.removeAt(i);
  emit statusesChanged();
  emit undoableToast(tr_("status.deleted").arg(statusName), 5);
  scheduleSave();
}

QVariantMap AppController::taskById(const QString& id) const {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return {};
  }
  const Task& t = m_tasks.items().at(row);
  QVariantMap m;
  m["id"] = t.id;
  m["title"] = t.title;
  m["desc"] = t.desc;
  m["priority"] = heap::local::effectivePriority(t);
  m["status"] = t.status;
  m["scheduledAt"] = t.scheduledAt;
  m["dueAt"] = heap::local::effectiveDueAt(t);
  m["scheduledHasTime"] = t.scheduledHasTime;
  m["dueHasTime"] = heap::local::effectiveDueHasTime(t);
  m["branch"] = t.branch;
  m["archived"] = t.archived;
  m["trackedSeconds"] = t.trackedSeconds;
  m["isTiming"] = t.timerStartedAt.isValid();
  m["recurrence"] = t.recurrence;
  m["labels"] = labelsToVariant(t.labels);
  m["estimateMinutes"] = t.estimateMinutes;
  m["someday"] = t.someday;
  m["assignee"] = t.assignee;
  m["externalKey"] = externalKeyOf(t);
  m["externalUrl"] = t.externalUrl;
  m["externalProvider"] = t.externalProvider;
  // Everything the editor's read-only ticket strip shows (HEAP-117).
  m["ticket"] = ticketToVariant(t);
  // My own tags (APP-239), apart from the tracker's labels.
  QVariantList tags;
  for(const LocalTag& tag : t.local.tags) {
    tags.append(QVariantMap{{"id", tag.id}, {"color", tag.color}});
  }
  m["localTags"] = tags;
  // "Remind me" (APP-319): as set, and the moment it comes to.
  m["remindAt"] = t.local.remindAt;
  m["remindLead"] = t.local.remindLead;
  m["remindFireAt"] = heap::cal::taskReminderFireAt(t.local.remindAt,
                                                    t.local.remindLead,
                                                    t.scheduledAt,
                                                    t.scheduledHasTime,
                                                    heap::local::effectiveDueAt(t),
                                                    heap::local::effectiveDueHasTime(t));
  // The document's meta column (DG-061): history and the PR, if one is known.
  m["statusChangedAt"] = t.statusChangedAt;
  m.insert(m_tasks.gitFactsFor(t.id));
  return m;
}

QVariantMap AppController::compileSearch(const QString& text) const {
  const heap::query::TaskQuery q = compileTaskQuery_(text);
  QVariantMap out;
  out["isQuery"] = q.isQuery();
  out["freeText"] = q.freeText();
  out["unknown"] = q.unknownClauses();
  // The id list is what the JS views filter on. Built only for a real query:
  // with no clauses it would be every task, which is both useless and the
  // largest thing this call could return.
  QStringList ids;
  if(q.isQuery()) {
    const QVector<Task>& items = m_tasks.items();
    for(int row = 0; row < items.size(); ++row) {
      if(q.matches(items.at(row), m_tasks.searchTextAt(row))) {
        ids << items.at(row).id;
      }
    }
  }
  out["ids"] = ids;
  return out;
}

QString AppController::matchesElsewhere(const QString& text) const {
  // A filter that finds nothing here says where it would (A3-13): a
  // freelancer does not remember which client a task belongs to.
  if(text.trimmed().isEmpty()) {
    return {};
  }
  QStringList parts;
  for(const Profile& p : m_profiles) {
    if(p.id == m_activeProfileId) {
      continue;
    }
    const heap::query::TaskQuery q = heap::query::TaskQuery::compile(text, m_today, p.statuses);
    const QString free = q.freeText();
    int n = 0;
    for(const Task& t : p.tasks) {
      if(t.archived) {
        continue;
      }
      const QString hay = TaskModel::searchTextOf(t);
      if(heap::query::textMatches(hay, free) && (!q.isQuery() || q.matches(t, hay))) {
        ++n;
      }
    }
    if(n > 0) {
      parts << tr_("search.elsewhere.one").arg(n).arg(p.name);
    }
  }
  return parts.isEmpty() ? QString() : tr_("search.elsewhere").arg(parts.join(QStringLiteral(", ")));
}

bool AppController::searchIsQuery(const QString& text) const {
  return heap::query::TaskQuery::compile(text, m_today, m_statuses).isQuery();
}

QStringList AppController::searchProblems(const QString& text) const {
  return heap::query::TaskQuery::compile(text, m_today, m_statuses).unknownClauses();
}

QVariantMap AppController::commandLine(const QString& text, int limit) const {
  const bool scheduled = heap::query::queryFields().contains(QStringLiteral("scheduled"));
  const heap::query::CommandQuery cq =
      heap::query::parseCommandLine(text, m_statuses, m_chrono.get(), QDateTime::currentDateTime(), scheduled);
  QVariantMap out;
  out["commandsOnly"] = cq.commandsOnly;
  out["text"] = cq.text;
  out["query"] = cq.commandsOnly ? QString() : cq.query();
  QVariantList tokens;
  for(const heap::query::CommandToken& t : cq.tokens) {
    tokens.append(QVariantMap{{"words", t.words}, {"kind", t.kind}, {"clause", t.clause}, {"value", t.value}});
  }
  out["tokens"] = tokens;
  QVariantList tasks;
  int total = 0;
  if(!cq.commandsOnly && (!cq.tokens.isEmpty() || !cq.text.isEmpty())) {
    const heap::query::TaskQuery q = heap::query::TaskQuery::compile(cq.query(), m_today, m_statuses, m_syncNewIds);
    const QString free = q.freeText();
    QString typedId = cq.text.trimmed();
    // "SD-105" alone is a ticket chip, not text: it is still the key asked for.
    if(typedId.isEmpty()) {
      int tickets = 0;
      for(const heap::query::CommandToken& tok : cq.tokens) {
        if(tok.kind == QLatin1String("ticket")) {
          typedId = tok.value;
          ++tickets;
        }
      }
      if(tickets != 1) {
        typedId.clear();
      }
    }
    QVector<int> exact;
    QVector<int> open;
    QVector<int> archived;
    const QVector<Task>& items = m_tasks.items();
    for(int row = 0; row < items.size(); ++row) {
      const Task& t = items.at(row);
      const QString& hay = m_tasks.searchTextAt(row);
      if(!heap::query::textMatches(hay, free)) {
        continue;
      }
      if(q.isQuery() && !q.matches(t, hay)) {
        continue;
      }
      ++total;
      // The key typed whole comes first, by id or by tracker key (A3-23).
      if(!typedId.isEmpty() && (t.id.compare(typedId, Qt::CaseInsensitive) == 0 ||
                                (!t.externalId.isEmpty() && externalKeyOf(t).compare(typedId, Qt::CaseInsensitive) == 0))) {
        exact << row;
      } else if(t.archived) {
        archived << row;
      } else {
        open << row;
      }
    }
    for(const auto* list : {&exact, &open, &archived}) {
      for(int row : *list) {
        if(tasks.size() >= limit) {
          break;
        }
        const Task& t = items.at(row);
        QString statusName = t.status;
        for(const QVariant& v : m_statuses) {
          if(v.toMap().value(QStringLiteral("id")).toString() == t.status) {
            statusName = v.toMap().value(QStringLiteral("name")).toString();
          }
        }
        const QString key = externalKeyOf(t);
        tasks.append(QVariantMap{{"id", t.id},
                                 // The name the card shows: the tracker key where
                                 // there is one (A3-10).
                                 {"key", key.isEmpty() ? t.id : key},
                                 {"title", t.title},
                                 {"status", t.status},
                                 {"statusName", statusName},
                                 {"category", statusCategory(t.status)},
                                 {"priority", heap::local::effectivePriority(t)},
                                 {"archived", t.archived}});
      }
    }
  }
  out["tasks"] = tasks;
  out["total"] = total;
  return out;
}

QStringList AppController::searchFields() const {
  return heap::query::queryFields();
}

QString AppController::eventHourLabel(double hour) const {
  // 24:00, an end at midnight, is 12:00am — not 12:00pm, which reads (and
  // parses back) as noon (TIME-22).
  return heap::text::formatHour(hour, twelveHourClock());
}

bool AppController::twelveHourClock() const {
  const QString f = settingsMap().value("calendar").toMap().value("timeFormat").toString();
  // "system" (Settings → Language, DG-100): the clock the system's locale uses.
  if(f == QLatin1String("system")) {
    return QLocale::system().timeFormat(QLocale::ShortFormat).contains(QLatin1Char('a'), Qt::CaseInsensitive);
  }
  return f == QLatin1String("12h");
}

QString AppController::datePattern(const QString& style, const QString& lang) const {
  return heap::text::datePattern(style, lang.isEmpty() ? m_language : lang);
}

QString AppController::dateLabel(const QDate& d, const QString& style) const {
  return heap::text::formatDate(d, style, m_language);
}

QString AppController::dateTimeLabel(const QDateTime& dt, const QString& style) const {
  return heap::text::formatDateTime(dt, style, m_language, twelveHourClock());
}

QVariantList AppController::sprintMarkers(const QDate& from, const QDate& to) const {
  QVariantList out;
  for(const heap::integrations::SprintMarker& m : heap::integrations::sprintMarkers(m_tasks.items(), from, to)) {
    out.append(QVariantMap{{QStringLiteral("name"), m.name},
                           {QStringLiteral("state"), m.state},
                           {QStringLiteral("start"), m.start},
                           {QStringLiteral("end"), m.end},
                           {QStringLiteral("endDay"), static_cast<int>(from.daysTo(m.end))},
                           {QStringLiteral("tasks"), m.tasks}});
  }
  return out;
}

QVariantMap AppController::currentSprint() const {
  const heap::integrations::SprintMarker m = heap::integrations::currentSprint(m_tasks.items(), m_today);
  if(m.name.isEmpty()) {
    return {};
  }
  return {{QStringLiteral("name"), m.name},
          {QStringLiteral("start"), m.start},
          {QStringLiteral("end"), m.end},
          {QStringLiteral("daysLeft"), m.end.isValid() ? static_cast<int>(m_today.daysTo(m.end)) : -1}};
}

QString AppController::sprintLabel() const {
  // A sprint the cards are in says it better than any week number (APP-255).
  const heap::integrations::SprintMarker sprint = heap::integrations::currentSprint(m_tasks.items(), m_today);
  if(!sprint.name.isEmpty()) {
    return sprint.name;
  }
  // The ISO week, in the UI language. It used to be "sprint-<month × 2.1>":
  // a number nobody's sprint was on, and English in a Russian UI.
  const int week = m_today.weekNumber();
  return (m_language == QStringLiteral("ru") ? QStringLiteral("нед. %1") : QStringLiteral("wk %1")).arg(week);
}

QString AppController::humanDate(const QDate& date) const {
  // "Friday, May 15" / "пятница, 15 мая".
  return heap::text::formatDate(date, QStringLiteral("longWeekday"), m_language);
}

QString AppController::deadlineBucket(const QDate& deadline) const {
  if(!deadline.isValid()) {
    return "nodl";
  }
  const int d = m_today.daysTo(deadline);
  if(d < 0) {
    return "overdue";
  }
  if(d == 0) {
    return "today";
  }
  if(d == 1) {
    return "tomorrow";
  }
  // Calendar weeks, Monday to Sunday: a rolling seven days put next Monday
  // under "This week" (TASKS-30).
  const QDate sunday = m_today.addDays(7 - m_today.dayOfWeek());
  if(deadline <= sunday) {
    return "thisweek";
  }
  if(deadline <= sunday.addDays(7)) {
    return "nextweek";
  }
  return "later";
}

QString AppController::deadlineDiffLabel(const QDate& deadline) const {
  if(!deadline.isValid()) {
    return QStringLiteral("—");
  }
  const int d = m_today.daysTo(deadline);
  // Past two months a day count stops meaning anything ("2460d overdue").
  const auto longSpan = [this](int days) {
    return days >= 365 ? tr_("span.years").arg(days / 365) : tr_("span.months").arg(days / 30);
  };
  if(d <= -60) {
    return tr_("deadline.overdueLong").arg(longSpan(-d));
  }
  if(d >= 60) {
    return tr_("deadline.inLong").arg(longSpan(d));
  }
  if(d < 0) {
    return tr_("deadline.overdue").arg(-d);
  }
  if(d == 0) {
    return tr_("deadline.today");
  }
  if(d == 1) {
    return tr_("deadline.tomorrow");
  }
  if(d < 7) {
    return tr_("deadline.inDays").arg(d);
  }
  return tr_("deadline.inDaysShort").arg(d);
}

QString AppController::shortDate(const QDate& d) const {
  // "Fri, May 15" / "пт, 15 мая".
  return heap::text::formatDate(d, QStringLiteral("weekdayDay"), m_language);
}

int AppController::isoWeekNumber(const QDate& d) const {
  if(!d.isValid()) {
    return 0;
  }
  return d.weekNumber();
}

QVariantMap AppController::parseDateTime(const QString& input, const QDateTime& reference) const {
  QVariantMap m;
  if(!m_chrono) {
    m.insert("ok", false);
    return m;
  }
  const auto r = m_chrono->parse(input, reference);
  m.insert("ok", r.ok);
  m.insert("start", r.start);
  m.insert("end", r.end);
  m.insert("hasTime", r.hasTime);
  m.insert("recurrence", r.recurrence);
  m.insert("tz", r.zone);
  m.insert("consumed", r.consumed);
  m.insert("startOffset", r.startOffset);
  m.insert("endOffset", r.endOffset);
  return m;
}

void AppController::perfMarkShown(const QString& name, QObject* item) const {
  if(!heap::perf::enabled()) {
    return;
  }
  heap::perf::beginIfIdle(name);
  const auto* quickItem = qobject_cast<QQuickItem*>(item);
  const QQuickWindow* window = quickItem != nullptr ? quickItem->window() : nullptr;
  if(window == nullptr) {
    heap::perf::end(name);
    return;
  }
  // frameSwapped comes from the render thread under the threaded loop; the
  // perf log is thread-safe, so the end is stamped right there.
  QObject::connect(
      window,
      &QQuickWindow::frameSwapped,
      window,
      [name]() {
        heap::perf::end(name);
      },
      static_cast<Qt::ConnectionType>(Qt::DirectConnection | Qt::SingleShotConnection));
}

QVariantList AppController::parseAllDateTimes(const QString& input, const QDateTime& reference) const {
  QVariantList out;
  if(!m_chrono) {
    return out;
  }
  const auto all = m_chrono->parseAll(input, reference);
  for(const auto& r : all) {
    QVariantMap m;
    m.insert("ok", r.ok);
    m.insert("start", r.start);
    m.insert("end", r.end);
    m.insert("hasTime", r.hasTime);
    m.insert("recurrence", r.recurrence);
    m.insert("consumed", r.consumed);
    m.insert("startOffset", r.startOffset);
    m.insert("endOffset", r.endOffset);
    out.append(m);
  }
  return out;
}

void AppController::copyToClipboard(const QString& text) {
  if(auto* cb = QGuiApplication::clipboard()) {
    cb->setText(text);
  }
}

void AppController::copyTaskKey(const QString& key) {
  const QString k = key.trimmed();
  if(k.isEmpty()) {
    return;
  }
  copyToClipboard(k);
  emit toast(tr_("task.keyCopied").arg(k));
}

void AppController::copyActiveProfileMarkdownToClipboard() {
  emit flushEditorsRequested();
  snapshotActiveProfile();  // flush live models into the active Profile first
  const int pi = profileIndexOf(m_activeProfileId);
  if(pi < 0) {
    return;
  }
  const Profile& p = m_profiles[pi];

  const auto renderTask = [](const Task& t) {
    QString line = QStringLiteral("- ");
    if(!heap::local::effectivePriority(t).isEmpty()) {
      line += QStringLiteral("**[") + heap::local::effectivePriority(t) + QStringLiteral("]** ");
    }
    if(!t.id.isEmpty()) {
      line += QStringLiteral("`") + t.id + QStringLiteral("` ");
    }
    line += t.title;
    QStringList meta;
    if(const QDateTime dueAt = heap::local::effectiveDueAt(t); dueAt.isValid()) {
      meta << QStringLiteral("deadline: ") +
                  (heap::local::effectiveDueHasTime(t) ? dueAt.toString(Qt::ISODate) : dueAt.date().toString(Qt::ISODate));
    }
    if(!t.branch.isEmpty()) {
      meta << QStringLiteral("branch: ") + t.branch;
    }
    if(!meta.isEmpty()) {
      line += QStringLiteral("  — ") + meta.join(QStringLiteral(" · "));
    }
    return line;
  };

  QString md;
  md += QStringLiteral("# ") + p.name + QStringLiteral("\n\n");
  md += QStringLiteral("_Exported ") + QDate::currentDate().toString(Qt::ISODate) + QStringLiteral("_\n");

  // Tasks grouped by column, in the profile's status order; archived hidden.
  md += QStringLiteral("\n## Tasks\n");
  QSet<QString> knownStatus;
  for(const QVariant& sv : p.statuses) {
    const QVariantMap sm = sv.toMap();
    const QString sid = sm.value(QStringLiteral("id")).toString();
    knownStatus.insert(sid);
    QStringList lines;
    for(const Task& t : p.tasks) {
      if(!t.archived && t.status == sid) {
        lines << renderTask(t);
      }
    }
    if(lines.isEmpty()) {
      continue;
    }
    md += QStringLiteral("\n### ") + sm.value(QStringLiteral("name")).toString() + QStringLiteral(" (") + QString::number(lines.size()) +
          QStringLiteral(")\n");
    md += lines.join(QStringLiteral("\n")) + QStringLiteral("\n");
  }
  // Tasks whose status is not one of the profile's columns.
  QStringList orphan;
  for(const Task& t : p.tasks) {
    if(!t.archived && !knownStatus.contains(t.status)) {
      orphan << renderTask(t);
    }
  }
  if(!orphan.isEmpty()) {
    md += QStringLiteral("\n### Other (") + QString::number(orphan.size()) + QStringLiteral(")\n");
    md += orphan.join(QStringLiteral("\n")) + QStringLiteral("\n");
  }

  // People.
  if(!p.people.isEmpty()) {
    md += QStringLiteral("\n## People\n");
    for(const Person& pe : p.people) {
      QString line = QStringLiteral("- **") + pe.name + QStringLiteral("**");
      QStringList meta;
      if(!pe.role.isEmpty()) {
        meta << pe.role;
      }
      if(!pe.question.isEmpty()) {
        meta << QStringLiteral("Q: ") + pe.question;
      }
      if(!meta.isEmpty()) {
        line += QStringLiteral(" — ") + meta.join(QStringLiteral(" · "));
      }
      md += line + QStringLiteral("\n");
    }
  }

  // Every note and every doc page, not just the note that happened to be
  // open. Each under its own heading; a leading H1 that only repeats the
  // title is dropped so the outline stays one document.
  const auto withoutTitleH1 = [](const QString& body, const QString& title) {
    const QString h1 = firstH1(body);
    if(h1.isEmpty() || h1 != title) {
      return body.trimmed();
    }
    const qsizetype nl = body.indexOf(QLatin1Char('\n'), body.indexOf(QStringLiteral("# ")));
    return nl < 0 ? QString() : body.mid(nl + 1).trimmed();
  };
  QStringList noteBlocks;
  for(const Note& n : p.notes) {
    const QString body = withoutTitleH1(n.body, n.title);
    QString block = QStringLiteral("### ") + n.title + QStringLiteral("\n");
    if(!n.folder.isEmpty()) {
      block += QStringLiteral("_") + n.folder + QStringLiteral("_\n");
    }
    if(!body.isEmpty()) {
      block += QStringLiteral("\n") + body + QStringLiteral("\n");
    }
    noteBlocks << block;
  }
  if(!noteBlocks.isEmpty()) {
    md += QStringLiteral("\n## Notes\n\n") + noteBlocks.join(QStringLiteral("\n"));
  }
  QStringList pageBlocks;
  std::function<void(const QString&, int)> walk = [&](const QString& parent, int depth) {
    for(const DocPage& d : childrenOf(p.docPages, parent)) {
      const QString body = withoutTitleH1(d.body, d.title);
      QString block = QStringLiteral("### ") + QStringLiteral("› ").repeated(depth) + d.title + QStringLiteral("\n");
      if(!body.isEmpty()) {
        block += QStringLiteral("\n") + body + QStringLiteral("\n");
      }
      pageBlocks << block;
      walk(d.id, depth + 1);
    }
  };
  walk(QString(), 0);
  if(!pageBlocks.isEmpty()) {
    md += QStringLiteral("\n## Docs\n\n") + pageBlocks.join(QStringLiteral("\n"));
  }

  copyToClipboard(md);
  emit toast(tr_("profile.mdCopied"));
}

namespace {
QString formatTrackedDuration(int secs) {
  const int h = secs / 3600;
  const int m = (secs % 3600) / 60;
  return h > 0 ? QStringLiteral("%1h %2m").arg(h).arg(m) : QStringLiteral("%1m").arg(m);
}
}  // namespace

void AppController::rememberStatuses() {
  m_knownStatus.clear();
  for(const Task& t : m_tasks.items()) {
    m_knownStatus.insert(t.id, t.status);
  }
}

void AppController::noteStatusMoves(int first, int last) {
  bool moved = false;
  for(int r = qMax(0, first); r <= last && r < m_tasks.items().size(); ++r) {
    const Task& t = m_tasks.items().at(r);
    const auto it = m_knownStatus.find(t.id);
    if(it == m_knownStatus.end()) {
      m_knownStatus.insert(t.id, t.status);
      continue;
    }
    if(*it == t.status) {
      continue;
    }
    m_statusLog.append({.taskId = t.id,
                        .from = *it,
                        .to = t.status,
                        .at = t.statusChangedAt.isValid() ? t.statusChangedAt : QDateTime::currentDateTime()});
    *it = t.status;
    moved = true;
  }
  if(moved) {
    // A recap looks back one week; a quarter of a year is plenty.
    const QDateTime horizon = QDateTime::currentDateTime().addDays(-92);
    if(m_statusLog.size() > 2000 || m_statusLog.constFirst().at < horizon) {
      m_statusLog = heap::recap::pruned(m_statusLog, horizon);
    }
    scheduleSave();
  }
}

QVariantMap AppController::weeklyRecap() const {
  return weeklyRecapFor(QDate::currentDate());
}

QVariantMap AppController::weeklyRecapFor(const QDate& today) const {
  const QDate thisWeek = heap::recap::weekStart(today);
  const QDate lastWeek = thisWeek.addDays(-7);
  const QVector<heap::recap::Move> moves = heap::recap::netMoves(m_statusLog, lastWeek.startOfDay(), thisWeek.startOfDay());

  QHash<QString, int> column;
  QHash<QString, QVariantMap> statusById;
  for(int i = 0; i < m_statuses.size(); ++i) {
    const QVariantMap st = m_statuses.at(i).toMap();
    const QString id = st.value(QStringLiteral("id")).toString();
    column.insert(id, i);
    statusById.insert(id, st);
  }
  const auto colOf = [&](const QString& id) {
    return column.value(id, static_cast<int>(m_statuses.size()));
  };

  // (from, to) -> its tasks, in the order the tasks first moved.
  QList<QPair<QString, QString>> keys;
  QHash<QPair<QString, QString>, QVariantList> tasks;
  for(const heap::recap::Move& m : moves) {
    const int row = m_tasks.indexOfId(m.taskId);
    if(row < 0) {
      continue;  // deleted since: nothing to open
    }
    const Task& t = m_tasks.items().at(row);
    const QPair<QString, QString> key{m.from, m.to};
    if(!tasks.contains(key)) {
      keys.append(key);
    }
    tasks[key].append(QVariantMap{{"id", t.id}, {"title", t.title}, {"priority", heap::local::effectivePriority(t)}});
  }
  std::stable_sort(keys.begin(), keys.end(), [&](const auto& a, const auto& b) {
    return std::pair(colOf(a.first), colOf(a.second)) < std::pair(colOf(b.first), colOf(b.second));
  });

  QVariantList groups;
  for(const auto& key : keys) {
    const QVariantMap from = statusById.value(key.first);
    const QVariantMap to = statusById.value(key.second);
    groups.append(QVariantMap{{"from", key.first},
                              {"fromName", from.value(QStringLiteral("name"), key.first)},
                              {"fromColor", from.value(QStringLiteral("color"))},
                              {"to", key.second},
                              {"toName", to.value(QStringLiteral("name"), key.second)},
                              {"toColor", to.value(QStringLiteral("color"))},
                              {"tasks", tasks.value(key)}});
  }
  return QVariantMap{{"weekStart", lastWeek.toString(Qt::ISODate)}, {"weekEnd", thisWeek.toString(Qt::ISODate)}, {"groups", groups}};
}

QVariantMap AppController::weekFacts(const QDate& day) const {
  const QDate start = heap::recap::weekStart(day.isValid() ? day : QDate::currentDate());
  const QDate end = start.addDays(7);
  QVector<const Task*> closed;
  for(const Task& t : m_tasks.items()) {
    if(statusCategory(t.status) == QLatin1String("done") && t.statusChangedAt.isValid() && t.statusChangedAt.date() >= start &&
       t.statusChangedAt.date() < end) {
      closed.push_back(&t);
    }
  }
  std::stable_sort(closed.begin(), closed.end(), [](const Task* a, const Task* b) {
    return a->statusChangedAt < b->statusChangedAt;
  });
  QVariantList rows;
  QVariantList perDay{0, 0, 0, 0, 0, 0, 0};
  for(const Task* t : closed) {
    rows.append(QVariantMap{{"id", t->id}, {"title", t->title}, {"closedAt", t->statusChangedAt}});
    const auto i = static_cast<qsizetype>(start.daysTo(t->statusChangedAt.date()));
    perDay[i] = perDay.at(i).toInt() + 1;
  }
  // What came in and what was put off in the week, from the task history
  // (APP-165): the summary counted only what closed, and a week of new work
  // and carried-over tasks read as an empty one (dogfood 2026-10-16). Moved =
  // its date or deadline pushed later; once per task, at its last move.
  QVariantList created;
  QVariantList moved;
  const QString pid = activeProfileId();
  const auto inWeek = [&](const QDateTime& at) {
    return at.isValid() && at.toLocalTime().date() >= start && at.toLocalTime().date() < end;
  };
  for(const Task& t : m_tasks.items()) {
    QDateTime createdAt;
    QDateTime movedAt;
    QString movedTo;
    for(const heap::history::HistoryEvent& e : m_history.events(pid, t.id)) {
      if(!inWeek(e.at)) {
        continue;
      }
      if(e.kind == QLatin1String("created")) {
        createdAt = e.at;
      } else if((e.kind == QLatin1String("scheduled") || e.kind == QLatin1String("due")) && !e.from.isEmpty() && !e.to.isEmpty() &&
                e.to.left(10) > e.from.left(10)) {
        movedAt = e.at;
        movedTo = e.to.left(10);
      }
    }
    // A ticket came in when it was made in the tracker, not when a pull
    // first brought it here (dogfood 2026-10-19: a months-old ticket pulled
    // today was "new this week").
    if(!t.externalProvider.isEmpty() && t.externalMeta.createdAt.isValid()) {
      createdAt = inWeek(t.externalMeta.createdAt) ? t.externalMeta.createdAt : QDateTime();
    }
    if(createdAt.isValid()) {
      created.append(QVariantMap{{"id", t.id}, {"title", t.title}, {"at", createdAt}});
    }
    if(movedAt.isValid()) {
      moved.append(QVariantMap{{"id", t.id}, {"title", t.title}, {"at", movedAt}, {"to", QDate::fromString(movedTo, Qt::ISODate)}});
    }
  }
  const auto byAt = [](const QVariant& a, const QVariant& b) {
    return a.toMap().value("at").toDateTime() < b.toMap().value("at").toDateTime();
  };
  std::stable_sort(created.begin(), created.end(), byAt);
  std::stable_sort(moved.begin(), moved.end(), byAt);
  return QVariantMap{
      {"weekStart", start}, {"weekEnd", end.addDays(-1)}, {"closed", rows}, {"perDay", perDay}, {"created", created}, {"moved", moved}};
}

void AppController::copyWeeklyReportToClipboard() {
  snapshotActiveProfile();
  const int pi = profileIndexOf(m_activeProfileId);
  if(pi < 0) {
    return;
  }
  const Profile& p = m_profiles[pi];
  const QDate since = QDate::currentDate().addDays(-7);

  QVector<const Task*> shipped;
  int totalSecs = 0;
  for(const Task& t : p.tasks) {
    if(statusCategory(t.status) == QLatin1String("done") && t.statusChangedAt.isValid() && t.statusChangedAt.date() >= since) {
      shipped.push_back(&t);
      totalSecs += t.trackedSeconds;
    }
  }

  QString md;
  md += QStringLiteral("# What I shipped — ") + p.name + QStringLiteral("\n\n");
  md += QStringLiteral("_") + since.toString(Qt::ISODate) + QStringLiteral(" → ") + QDate::currentDate().toString(Qt::ISODate) +
        QStringLiteral("_\n\n");
  if(shipped.isEmpty()) {
    md += QStringLiteral("_Nothing marked done in the last 7 days._\n");
  } else {
    md += QStringLiteral("**") + QString::number(shipped.size()) + QStringLiteral(" task(s) shipped");
    if(totalSecs > 0) {
      md += QStringLiteral(" · ") + formatTrackedDuration(totalSecs) + QStringLiteral(" tracked");
    }
    md += QStringLiteral("**\n\n");
    for(const Task* t : shipped) {
      QString line = QStringLiteral("- ");
      if(!t->id.isEmpty()) {
        line += QStringLiteral("`") + t->id + QStringLiteral("` ");
      }
      line += t->title;
      QStringList meta;
      meta << QStringLiteral("done ") + t->statusChangedAt.date().toString(Qt::ISODate);
      if(t->trackedSeconds > 0) {
        meta << formatTrackedDuration(t->trackedSeconds);
      }
      line += QStringLiteral("  — ") + meta.join(QStringLiteral(" · "));
      md += line + QStringLiteral("\n");
    }
  }

  copyToClipboard(md);
  emit toast(tr_("profile.weeklyCopied"));
}

// Stops the timers running in the profiles that are not active, each one
// recorded as a session the way TaskModel::stopTiming does. Returns whether
// any was running. `onlyId` limits it to one task.
bool AppController::stopTimersInOtherProfiles_(const QString& onlyId) {
  bool any = false;
  for(Profile& p : m_profiles) {
    if(p.id == m_activeProfileId) {
      continue;
    }
    for(Task& t : p.tasks) {
      if(!t.timerStartedAt.isValid() || (!onlyId.isEmpty() && t.id != onlyId)) {
        continue;
      }
      heap::local::sessions::adoptTotal(t.local.sessions, t.trackedSeconds);
      heap::local::sessions::record(t.local.sessions, t.timerStartedAt, QDateTime::currentDateTime());
      t.trackedSeconds = heap::local::sessions::total(t.local.sessions);
      t.timerStartedAt = QDateTime();
      any = true;
    }
  }
  return any;
}

void AppController::startTaskTimer(const QString& id) {
  if(m_tasks.indexOfId(id) < 0) {
    return;
  }
  // The timer this one replaces, said out loud (A1-14): a 6-hour timer
  // ended without a word.
  QString stoppedKey;
  int stoppedSecs = 0;
  const QDateTime now = QDateTime::currentDateTime();
  for(const Profile& p : std::as_const(m_profiles)) {
    const QVector<Task>& tasks = p.id == m_activeProfileId ? m_tasks.items() : p.tasks;
    for(const Task& t : tasks) {
      if(t.id != id && t.timerStartedAt.isValid()) {
        const QString key = externalKeyOf(t);
        stoppedKey = key.isEmpty() ? t.id : key;
        stoppedSecs = static_cast<int>(t.timerStartedAt.secsTo(now));
      }
    }
  }
  {
    const UndoScope scope(this, tr_("undo.timer").arg(id));
    // Only one task tracks at a time, across profiles too (IDIOT-CAL-6): a timer
    // left running in another profile double-counted the hours.
    stopTimersInOtherProfiles_();
    m_tasks.startTiming(id);
  }
  if(!stoppedKey.isEmpty()) {
    emit undoableToast(tr_("timer.switched").arg(stoppedKey, durationWords_(stoppedSecs)), 6);
  }
  scheduleSave();
  refreshTray();
}

void AppController::stopTaskTimer(const QString& id) {
  if(m_tasks.indexOfId(id) < 0) {
    // The timer the sidebar shows may run in another profile (IDIOT-CAL-6).
    if(stopTimersInOtherProfiles_(id)) {
      scheduleSave();
      refreshTray();
    }
    return;
  }
  const UndoScope scope(this, tr_("undo.timer").arg(id));
  m_tasks.stopTiming(id);
  scheduleSave();
  refreshTray();
}

int AppController::elapsedSecondsFor(const QString& id) const {
  const int row = m_tasks.indexOfId(id);
  if(row < 0) {
    return 0;
  }
  const Task& t = m_tasks.items().at(row);
  int secs = t.trackedSeconds;
  if(t.timerStartedAt.isValid()) {
    secs += static_cast<int>(t.timerStartedAt.secsTo(QDateTime::currentDateTime()));
  }
  return secs;
}

void AppController::markWelcomeSeen() {
  if(m_welcomeSeen) {
    return;
  }
  m_welcomeSeen = true;
  emit onboardingChanged();
  scheduleSave();
}

void AppController::dismissDemo() {
  if(!m_demoActive) {
    return;
  }
  m_demoActive = false;
  emit onboardingChanged();
  scheduleSave();
}

void AppController::replayWelcome() {
  // UI-only request: ask Main.qml to re-open the welcome guide. Deliberately
  // leaves m_welcomeSeen / m_demoActive untouched so replaying the tour never
  // resurrects the demo banner or changes what persists.
  emit welcomeReplayRequested();
}

void AppController::seedStartingWorkspace() {
  // A fresh install opens an empty Today in a profile of the person's own
  // (APP-271); the sample data is the example, a separate profile offered from
  // there, never mixed into this one.

  // A new user starts on the lowkey themes with normal contrast — or high
  // contrast and no motion when the system asks for them (design audit
  // DES-23). Written into the settings rather than made the built-in
  // fallback, so someone who has been on heap. dark without ever opening
  // Appearance keeps it, and the Appearance switches show what is in effect.
  if(m_appSettingsJson.isEmpty()) {
    m_appSettingsJson = heap::platform::firstRunAppearanceJson(heap::platform::systemAccessibilityPrefs());
    emit appSettingsJsonChanged();
  }
  // New data starts on the new defaults (the git line off, R3-095).
  m_gitLineDefaultDone = true;
  m_themeLowkeyDone = true;
  Profile p = makeStartingProfile(tr_("profile.personal"), QString());
  p.id = QStringLiteral("default");
  // No starter views (R2-058): "Мои виды" fills as filters are saved.
  // The reference catalogue is sample content: explicitly empty, not
  // absent, so it is not seeded into this profile on the first visit.
  p.docsState = QStringLiteral(R"({"sections":[],"snippets":[],"contacts":[]})");
  m_profiles.push_back(p);
  m_activeProfileId = p.id;
  m_events.reset({});
  applyProfileToModels(p);
  emit profilesChanged();
  emit activeProfileChanged();

  // welcomeSeen stays false until the first task: until then Today is the
  // first-run screen, again on the next start if the app is closed first.
  m_demoActive = false;
  emit onboardingChanged();
  scheduleSave();
}

Profile AppController::buildExampleProfile(QVector<CalEvent>* events) const {
  Profile p;
  p.id = QString::fromLatin1(kExampleProfileId);
  p.name = tr_("profile.example");
  p.color = "#5cc2dd";
  p.createdAt = QDateTime::currentDateTime();
  const SampleData::Lang seedLang = (m_language == "ru") ? SampleData::Lang::Ru : SampleData::Lang::En;
  p.tasks = SampleData::tasks(seedLang);
  p.people = SampleData::people(seedLang);
  // The ids every later launch would give them anyway: an example saved with
  // "p1" was renamed on each start, and the file never matched what it held.
  slugLegacyPersonIds(p);
  QVariantList st;
  for(const auto& m : SampleData::statuses(seedLang)) {
    st.push_back(m);
  }
  p.statuses = st;
  // Empty: the starter reference catalogue is seeded on the first visit.
  p.docsState.clear();
  p.notes = SampleData::notes(seedLang);
  p.activeNoteId = p.notes.isEmpty() ? QString() : p.notes.constFirst().id;
  p.notesState = p.notes.isEmpty() ? QString() : p.notes.constFirst().body;
  p.savedViews = heap::savedviews::starterViews(seedLang == SampleData::Lang::Ru);
  p.waitingOn = SampleData::waitingOn();
  if(events) {
    *events = SampleData::events(m_today, seedLang);
    for(CalEvent& e : *events) {
      e.profileId = p.id;
    }
  }
  return p;
}

bool AppController::hasExample() const {
  return profileIndexOf(QString::fromLatin1(kExampleProfileId)) >= 0;
}

QString AppController::openExample() {
  const QString id = QString::fromLatin1(kExampleProfileId);
  if(!hasExample()) {
    snapshotActiveProfile();
    parkWorkspaceUndo(m_activeProfileId);
    clearPendingUndo();  // undo is scoped to the active workspace
    QVector<CalEvent> events;
    Profile p = buildExampleProfile(&events);
    // Task ids are app-wide; a sample id the person already uses gets a
    // fresh one, and the meetings follow it.
    const QHash<QString, QString> renamed = reissueSharedTaskIds(p, &events);
    for(WaitingOn& w : p.waitingOn) {
      w.taskId = renamed.value(w.taskId, w.taskId);
    }
    // Meeting ids too: a second example after a removal must not collide.
    for(CalEvent& e : events) {
      if(m_events.indexOfId(e.id) >= 0) {
        e.id = e.id + QStringLiteral("-") + QString::number(QDateTime::currentMSecsSinceEpoch());
      }
      m_events.upsert(e);
    }
    m_profiles.push_back(p);
    m_activeProfileId = p.id;
    applyProfileToModels(p);
    emit profilesChanged();
    emit activeProfileChanged();
    scheduleSave();
    return id;
  }
  if(m_activeProfileId != id) {
    setActiveProfileId(id);
  }
  return id;
}

int AppController::exampleChanges() const {
  const int i = profileIndexOf(QString::fromLatin1(kExampleProfileId));
  if(i < 0) {
    return 0;
  }
  const Profile& p = m_profiles[i];
  const QVector<Task>& tasks = p.id == m_activeProfileId ? m_tasks.items() : p.tasks;
  // A task that is no sample task as it was made (in either language) —
  // its title, text, column or priority changed, or a task added — is the
  // person's work. Tasks carry no edit time, so the samples are the measure.
  // An example made by 0.8.0 - 0.8.5 holds the samples of then: they are
  // samples too, so removing an old example does not count them as work.
  // A #KEY-1 reference renamed by openExample (a sample id the person already
  // used) is still the sample's text: references compare without their keys.
  static const QRegularExpression kTicketRef(QStringLiteral("(?<![A-Za-z0-9_])#[A-Z][A-Z0-9]*-\\d+"));
  const auto plain = [](QString text) {
    return text.replace(kTicketRef, QStringLiteral("#"));
  };
  QSet<QString> samples;
  const auto key = [&plain](const Task& t) {
    return t.title + QChar(0x1f) + plain(t.desc) + QChar(0x1f) + t.status + QChar(0x1f) + t.priority;
  };
  for(const SampleData::Lang lang : {SampleData::Lang::En, SampleData::Lang::Ru}) {
    for(const Task& t : SampleData::tasks(lang) + SampleData::legacy::tasks(lang)) {
      samples.insert(key(t));
    }
  }
  int n = 0;
  for(const Task& t : tasks) {
    if(!samples.contains(key(t))) {
      ++n;
    }
  }
  // Meetings the person added to the example are theirs too (DATA-1).
  for(const CalEvent& e : m_events.items()) {
    if(e.profileId == p.id && !isSampleEvent(e)) {
      ++n;
    }
  }
  // And notes: a note written in the example went without the question.
  QSet<QString> sampleNotes;
  for(const SampleData::Lang lang : {SampleData::Lang::En, SampleData::Lang::Ru}) {
    for(const Note& s : SampleData::notes(lang) + SampleData::legacy::notes(lang)) {
      sampleNotes.insert(s.title + QChar(0x1f) + plain(s.body.trimmed()));
    }
  }
  const QVector<Note>& notes = p.id == m_activeProfileId ? m_notes.items() : p.notes;
  for(const Note& note : notes) {
    if(!sampleNotes.contains(note.title + QChar(0x1f) + plain(note.body.trimmed()))) {
      ++n;
    }
  }
  return n;
}

// A meeting the example came with, in either language, now or as 0.8.0 -
// 0.8.5 made it; matched by title, since the sample's dates follow the day
// it was opened on.
bool AppController::isSampleEvent(const CalEvent& e) {
  static const QSet<QString> titles = [] {
    QSet<QString> out;
    for(const QString& t : SampleData::legacy::eventTitles()) {
      out.insert(t);
    }
    for(const SampleData::Lang lang : {SampleData::Lang::En, SampleData::Lang::Ru}) {
      for(const CalEvent& s : SampleData::events(QDate::currentDate(), lang)) {
        out.insert(s.title);
      }
    }
    return out;
  }();
  return titles.contains(e.title);
}

void AppController::removeExample() {
  const QString id = QString::fromLatin1(kExampleProfileId);
  if(!hasExample()) {
    return;
  }
  // The example is never the last profile: without one of the person's own
  // it gives way to an empty one.
  if(m_profiles.size() <= 1) {
    snapshotActiveProfile();
    Profile own = makeStartingProfile(tr_("profile.personal"), QString());
    own.id = makeProfileId(own.name);
    own.docsState = QStringLiteral(R"({"sections":[],"snippets":[],"contacts":[]})");
    m_profiles.push_back(own);
  }
  // Its sample meetings go with it rather than staying behind unassigned.
  // The person's own stay: deleteProfile detaches them into its undo entry,
  // so Undo gives them back with the profile (DATA-1: they were deleted for
  // good, without a question).
  for(int r = m_events.rowCount() - 1; r >= 0; --r) {
    const CalEvent& e = m_events.items().at(r);
    if(e.profileId == id && isSampleEvent(e)) {
      m_events.removeById(e.id);
    }
  }
  deleteProfile(id);
}

void AppController::resetToFirstRun() {
  // Destructive: erase every trace of user data — on disk and in memory — and
  // re-seed the app exactly as a fresh install (Example profile + onboarding).
  // m_loading gates the debounced writer so nothing persists a half-torn state
  // while we tear it down.
  m_loading = true;

  // 1. Remove persisted state so a crash mid-reset can't half-recover and the
  //    next launch sees a genuine first run. Backups + quarantined corrupt
  //    snapshots must go too, or loadStateOnStart would resurrect old data.
  // A pending debounced write lands first, so it cannot recreate anything
  // after the wipe.
  if(m_saver) {
    m_saver->flush();
  }
  QFile::remove(stateFilePath());
  QDir(backupDirPath()).removeRecursively();
  // The time machine's snapshots are the same data, compressed, and the
  // attachments are its files: the dialog promises every copy goes (A5-6).
  QDir(historyDirPath()).removeRecursively();
  QDir(attachmentsDir()).removeRecursively();
  {
    const QDir dir(dataDir());
    const QStringList corrupt = dir.entryList(QStringList{"state.corrupt-*.json"}, QDir::Files);
    for(const QString& f : corrupt) {
      QFile::remove(dir.filePath(f));
    }
  }

  // 2. Drop transient UI state that points at rows we're about to delete.
  m_undo.clear();
  m_parkedUndo.clear();
  emit pendingUndoChanged();
  clearSelection();
  m_focusedStatus.clear();
  emit focusedStatusChanged();

  // 3. Reset preferences to their defaults. Language is preserved so the
  //    reseeded demo + UI stay in the user's tongue.
  m_theme = "dark";
  m_density = "comfy";
  m_currentView = "today";
  m_sectionViews.clear();
  m_workdayStart = 9;
  m_workdayEnd = 19;
  m_crumbProject.clear();
  m_crumbUser = "You";
  m_selectedDate = m_today;
  m_appSettingsJson.clear();
  resetAllShortcuts();
  emit themeChanged();
  emit densityChanged();
  emit currentViewChanged();
  emit workdayChanged();
  emit crumbProjectChanged();
  emit crumbUserChanged();
  emit selectedDateChanged();
  emit appSettingsJsonChanged();

  // 4. Clear every profile + model, then re-seed like a fresh install.
  m_profiles.clear();
  m_activeProfileId.clear();
  m_rootExtra = {};
  m_history.clear();
  m_settingsExtra = {};
  m_taskSeq.clear();
  m_events.reset({});
  m_tasks.reset({});
  m_people.reset({});
  m_notesState.clear();
  emit notesStateChanged();
  m_docsState.clear();
  emit docsStateChanged();

  m_welcomeSeen = false;
  m_demoActive = false;
  seedStartingWorkspace();

  // 5. Persist the fresh state immediately and let the UI re-onboard.
  m_loading = false;
  saveStateNow();
  m_saver->flush();  // on disk before the UI re-onboards
  emit firstRunReset();
}

void AppController::resetSettingsToDefaults() {
  const QJsonObject current = QJsonDocument::fromJson(m_appSettingsJson.toUtf8()).object();
  // The groups the Settings page owns and resets. Everything else in the
  // blob — window geometry, panel sizes, close-to-tray, notes view mode, the
  // profile card, tracker connections — is state or identity, not a
  // preference, and a reset that wiped it read as data loss.
  static const QStringList kReset = {QStringLiteral("appearance"),
                                     QStringLiteral("notifications"),
                                     QStringLiteral("calendar"),
                                     QStringLiteral("tasks"),
                                     QStringLiteral("cpp"),
                                     QStringLiteral("data"),
                                     QStringLiteral("updates"),
                                     QStringLiteral("git"),
                                     QStringLiteral("developer"),
                                     QStringLiteral("shortcuts")};
  QJsonObject next;
  for(auto it = current.constBegin(); it != current.constEnd(); ++it) {
    if(!kReset.contains(it.key())) {
      next.insert(it.key(), it.value());
    }
  }
  // A new install's look, not the built-in fallback (heap. dark, normal).
  QJsonObject appearance{{"darkPreset", "heap-ink"}, {"lightPreset", "heap-light"}, {"contrast", "normal"}};
  const QJsonObject oldAppearance = current.value("appearance").toObject();
  if(oldAppearance.contains("customThemes")) {
    appearance["customThemes"] = oldAppearance.value("customThemes");  // the user's own work
  }
  next["appearance"] = appearance;
  const QJsonArray repos = current.value("git").toObject().value("watchedRepos").toArray();
  if(!repos.isEmpty()) {
    next["git"] = QJsonObject{{"watchedRepos", repos}};
  }
  m_settingsBeforeReset = m_appSettingsJson;
  setAppSettingsJson(QString::fromUtf8(QJsonDocument(next).toJson(QJsonDocument::Compact)));
  emit settingsReset(tr_("settings.resetDone"));
}

void AppController::undoSettingsReset() {
  if(m_settingsBeforeReset.isNull()) {
    return;
  }
  const QString previous = m_settingsBeforeReset;
  m_settingsBeforeReset = QString();
  setAppSettingsJson(previous);
  emit toast(tr_("settings.resetUndone"));
}

void AppController::startFresh() {
  // Wipe the active profile's seeded demo content, leaving an empty but usable
  // workspace: keep the profile and its kanban columns, drop tasks / people /
  // this profile's events / notes / docs. The banner offers it before the user
  // has made anything of their own, but nothing stops them adding work first,
  // so the whole wipe is one undoable step.
  {
    const UndoScope scope(this, tr_("onboarding.freshUndone"));
    m_tasks.reset({});
    m_people.reset({});

    QVector<CalEvent> kept;
    for(const CalEvent& e : m_events.items()) {
      if(e.profileId != m_activeProfileId) {
        kept.append(e);
      }
    }
    m_events.reset(kept);

    m_notes.reset({});
    m_activeNoteId.clear();
    emit activeNoteChanged();
    m_notesState.clear();
    emit notesStateChanged();
    m_docPages.reset({});
    m_activeDocPageId.clear();
    emit activeDocPageChanged();
    // Explicitly empty, not absent: an empty blob is what DocsView reads as
    // "never opened" and seeds the demo catalogue into (UX-6).
    m_docsState = QStringLiteral(R"({"sections":[],"snippets":[],"contacts":[]})");
    emit docsStateChanged();
  }

  m_demoActive = false;
  emit onboardingChanged();

  snapshotActiveProfile();
  scheduleSave();
  emit undoableToast(tr_("onboarding.startedFresh"), 10);
}

QString AppController::classifyTaskKind(const QString& text) const {
  using heap::text::TaskKind;
  switch(heap::text::classifyKind(text)) {
    case TaskKind::Focus:
      return QStringLiteral("focus");
    case TaskKind::Sync:
      return QStringLiteral("sync");
    case TaskKind::Ticket:
      return QStringLiteral("ticket");
    case TaskKind::Contact:
      return QStringLiteral("contact");
    case TaskKind::None:
      break;
  }
  return QStringLiteral("none");
}

QString AppController::meetingType(const QString& text) const {
  return heap::text::meetingType(text);
}

QVariantMap AppController::extractTaskMeta(const QString& text, bool keepTicketKey) const {
  const auto m = heap::text::extractMeta(text, keepTicketKey);
  QVariantMap out;
  out["title"] = m.title;
  out["desc"] = m.desc;
  out["handles"] = QVariant::fromValue(m.handles);
  out["ticketKey"] = m.ticketKey;
  out["priority"] = m.priority;
  out["labels"] = m.labels;
  out["head"] = m.head;
  return out;
}

// ───────────────────────────────────────────────────────── Undo ──

AppController::UndoScope::UndoScope(AppController* owner, QString label) :
    m_owner(owner),
    m_label(std::move(label)),
    m_outermost(owner->m_undoScopeDepth == 0),
    // Implicitly shared: these are refcount bumps, not copies. The buffers
    // only diverge if the operation actually writes.
    m_tasks(m_outermost ? owner->m_tasks.items() : QVector<::Task>{}),
    m_events(m_outermost ? owner->m_events.items() : QVector<::CalEvent>{}),
    m_people(m_outermost ? owner->m_people.items() : QVector<::Person>{}),
    m_docPages(m_outermost ? owner->m_docPages.items() : QVector<::DocPage>{}),
    m_notes(m_outermost ? owner->m_notes.items() : QVector<::Note>{}),
    m_statuses(m_outermost ? owner->m_statuses : QVariantList{}),
    m_docsState(m_outermost ? owner->m_docsState : QString{}),
    m_savedViews(m_outermost ? owner->m_savedViews : QVector<heap::savedviews::SavedView>{}) {
  ++owner->m_undoScopeDepth;
  if(m_outermost) {
    m_serial = ++owner->m_undoSerialCounter;
    owner->m_openUndoSerial = m_serial;
  }
}

AppController::UndoScope::~UndoScope() {
  --m_owner->m_undoScopeDepth;
  if(m_outermost) {
    m_owner->m_openUndoSerial = 0;
  }
  // An inner scope is part of a bigger operation; the outer one is recording
  // the whole thing.
  if(!m_armed || !m_outermost) {
    return;
  }
  heap::undo::Entry entry;
  entry.label = m_label;
  entry.serial = m_serial;
  entry.redoLabel = m_redoLabel;
  entry.tasks = heap::undo::diff(m_tasks, m_owner->m_tasks.items(), [](const ::Task& t) {
    return t.id;
  });
  entry.events = heap::undo::diff(m_events, m_owner->m_events.items(), [](const ::CalEvent& e) {
    return e.id;
  });
  entry.people = heap::undo::diff(m_people, m_owner->m_people.items(), [](const ::Person& p) {
    return p.id;
  });
  entry.docPages = heap::undo::diff(m_docPages, m_owner->m_docPages.items(), [](const ::DocPage& p) {
    return p.id;
  });
  entry.notes = heap::undo::diff(m_notes, m_owner->m_notes.items(), [](const ::Note& n) {
    return n.id;
  });
  if(m_statuses != m_owner->m_statuses) {
    entry.statusesTouched = true;
    entry.statusesBefore = m_statuses;
    entry.statusesAfter = m_owner->m_statuses;
  }
  if(m_docsState != m_owner->m_docsState) {
    entry.docsStateTouched = true;
    entry.docsStateBefore = m_docsState;
    entry.docsStateAfter = m_owner->m_docsState;
  }
  if(m_savedViews != m_owner->m_savedViews) {
    entry.savedViewsTouched = true;
    entry.savedViewsBefore = m_savedViews;
    entry.savedViewsAfter = m_owner->m_savedViews;
  }
  const bool wasEmpty = entry.isEmpty();
  m_owner->m_undo.push(std::move(entry));
  if(!wasEmpty) {
    emit m_owner->pendingUndoChanged();
  }
}

void AppController::applyUndoEntry(const heap::undo::Entry& entry, bool backward) {
  if(entry.profileRemoved) {
    // A profile is the whole workspace, so this is a swap rather than a diff.
    // Only the undo direction is meaningful: redoing a profile deletion would
    // throw the restored work away again, which is not what Ctrl+Shift+Z is
    // for, so the stack is cleared instead (see undo()).
    const int idx = qBound(0, entry.profileRow, static_cast<int>(m_profiles.size()));
    snapshotActiveProfile();
    Profile restored = entry.profile;
    // The id may have been handed out again since (delete "Work", create
    // "Work", undo): two profiles under one id cannot both be addressed, so
    // the restored one takes a fresh id, and a fresh name if that is taken too.
    if(profileIndexOf(restored.id) >= 0) {
      restored.id = makeProfileId(restored.name);
    }
    restored.name = uniqueProfileName(restored.name);
    // Give back the events the deletion detached — only those still detached;
    // one the user has since assigned elsewhere stays where they put it.
    for(const QString& eventId : entry.profileEventIds) {
      const int row = m_events.indexOfId(eventId);
      if(row < 0) {
        continue;
      }
      CalEvent e = m_events.items().at(row);
      if(e.profileId.isEmpty()) {
        e.profileId = restored.id;
        m_events.upsert(e);
      }
    }
    m_profiles.insert(idx, restored);
    m_activeProfileId = restored.id;
    applyProfileToModels(restored);
    emit profilesChanged();
    emit activeProfileChanged();
    return;
  }

  if(backward) {
    heap::undo::applyBackward(m_tasks, entry.tasks);
    heap::undo::applyBackward(m_events, entry.events);
  } else {
    heap::undo::applyForward(m_tasks, entry.tasks);
    heap::undo::applyForward(m_events, entry.events);
  }
  // Notes and doc pages are typed into without undo steps: the entry's own
  // changes are merged onto them rather than the recorded copies put back
  // whole, which would take that typing with them (KNOW-1). People likewise:
  // a chip click or a Mattermost import since an edit stays.
  heap::undo::applyMerged(m_people, entry.people, backward);
  heap::undo::applyMerged(m_docPages, entry.docPages, backward);
  heap::undo::applyMerged(m_notes, entry.notes, backward);
  if(!entry.notes.isEmpty()) {
    reconcileActiveNote();
  }

  // Undoing a move is a move too: the tracker was told about the first one, so
  // it has to hear the status the card is going back to, or the next pull
  // re-applies the move the user just took back.
  for(const heap::undo::Edit<::Task>& e : entry.tasks) {
    if(e.existedBefore && e.existsAfter && e.before.status != e.after.status) {
      pushStatusToTracker(e.id, backward ? e.before.status : e.after.status);
    }
  }

  // A deleted task told its tracker "not mine"; bringing it back has to
  // withdraw that, and removing it again has to re-record it.
  for(const heap::undo::Edit<::Task>& e : entry.tasks) {
    if(e.existedBefore && !e.existsAfter) {
      if(backward) {
        restoreExternalTask(e.before.externalProvider, e.before.externalId);
      } else {
        dismissExternalTask(e.before.externalProvider, e.before.externalId);
      }
    }
  }

  if(entry.statusesTouched) {
    m_statuses = backward ? entry.statusesBefore : entry.statusesAfter;
    emit statusesChanged();
  }
  if(entry.docsStateTouched) {
    const QString was = m_docsState;
    // Only what the entry changed, by id, on top of the blob as it is now:
    // an entry edited after a deletion keeps its edit (KNOW-2).
    m_docsState = heap::undo::rebaseDocsState(m_docsState,
                                              backward ? entry.docsStateAfter : entry.docsStateBefore,
                                              backward ? entry.docsStateBefore : entry.docsStateAfter,
                                              nullptr);
    emit docsStateChanged();
    // Same reasoning as the tasks above: a contact coming back must stop
    // being "dismissed", and one going again must be dismissed again.
    syncDismissedContacts(was, m_docsState);
  }
  if(entry.savedViewsTouched) {
    setSavedViews(backward ? entry.savedViewsBefore : entry.savedViewsAfter);
  }
  // The status-count cache drops itself from the task model's own signals, so
  // nothing here has to remember to invalidate it.
}

double AppController::undoSerialForToast() const {
  if(m_openUndoSerial != 0) {
    return static_cast<double>(m_openUndoSerial);
  }
  const heap::undo::Entry* top = m_undo.peekUndo();
  return top != nullptr ? static_cast<double>(top->serial) : 0.0;
}

bool AppController::undoEntry(double serialValue) {
  const auto serial = static_cast<quint64>(serialValue);
  const heap::undo::Entry* top = m_undo.peekUndo();
  if(top == nullptr) {
    // The stack went with a profile switch: say so rather than nothing
    // (IDIOT-SHELL-4).
    emit toast(tr_("undo.gone"), QStringLiteral("warning"));
    return false;
  }
  // The common case — nothing happened since the toast — is a plain undo.
  if(serial == 0 || top->serial == serial) {
    undo();
    return true;
  }
  const heap::undo::Entry* found = m_undo.findUndoable(serial);
  if(found == nullptr) {
    // Already undone (Ctrl+Z got there first) or evicted.
    emit toast(tr_("undo.gone"), QStringLiteral("warning"));
    return false;
  }
  const heap::undo::Entry copy = *found;
  if(copy.profileMove) {
    heap::undo::Entry e = copy;
    const QString other = profileMoveOtherSide_(e);
    const bool ok = applyProfileMove_(e, /*backward=*/true);
    m_undo.removeUndoable(serial);
    if(m_parkedUndo.contains(other)) {
      m_parkedUndo[other].removeUndoable(serial);
    }
    emit pendingUndoChanged();
    if(ok) {
      emit toast(copy.label);
    }
    scheduleSave();
    return ok;
  }
  if(copy.profileRemoved) {
    // A profile swap only makes sense off the top of the stack.
    emit toast(tr_("undo.changedSince"), QStringLiteral("warning"));
    return false;
  }
  // Reversing one operation out of order is only safe while nothing it
  // touched has changed again since; otherwise the later edit would be
  // silently thrown away.
  flushNotesForUndo();
  bool statusesClean = true;
  QVariantList statuses = m_statuses;
  if(copy.statusesTouched) {
    statuses = heap::undo::revertStatusesOnly(m_statuses, copy.statusesBefore, copy.statusesAfter, &statusesClean);
  }
  bool docsClean = true;
  if(copy.docsStateTouched) {
    heap::undo::rebaseDocsState(m_docsState, copy.docsStateAfter, copy.docsStateBefore, &docsClean);
  }
  const bool clean = statusesClean && heap::undo::untouchedSince(m_tasks, copy.tasks) &&
                     heap::undo::untouchedSince(m_events, copy.events) && heap::undo::mergeableSince(m_people, copy.people) &&
                     heap::undo::mergeableSince(m_docPages, copy.docPages) && heap::undo::mergeableSince(m_notes, copy.notes) &&
                     docsClean && (!copy.savedViewsTouched || m_savedViews == copy.savedViewsAfter);
  if(!clean) {
    emit toast(tr_("undo.changedSince"));
    return false;
  }
  heap::undo::Entry partial = copy;
  partial.statusesTouched = false;  // columns are merged below, not swapped whole
  applyUndoEntry(partial, /*backward=*/true);
  if(copy.statusesTouched && statuses != m_statuses) {
    m_statuses = statuses;
    emit statusesChanged();
  }
  m_undo.removeUndoable(serial);
  emit pendingUndoChanged();
  emit toast(copy.label);
  scheduleSave();
  return true;
}

void AppController::beginUndoGroup(const QString& label) {
  m_undoGroups.push_back(std::make_unique<UndoScope>(this, label));
}

void AppController::endUndoGroup() {
  if(!m_undoGroups.empty()) {
    m_undoGroups.pop_back();  // the scope's destructor records the group
  }
}

void AppController::clearWorkspaceUndo() {
  if(m_undo.clearKeepingProfileRemoval()) {
    emit pendingUndoChanged();
  }
}

void AppController::parkWorkspaceUndo(const QString& profileId) {
  if(profileId.isEmpty()) {
    return;
  }
  const heap::undo::UndoStack part = m_undo.workspacePart();
  if(part.isEmpty()) {
    m_parkedUndo.remove(profileId);
  } else {
    m_parkedUndo.insert(profileId, part);
  }
}

void AppController::restoreWorkspaceUndo(const QString& profileId) {
  const auto it = m_parkedUndo.constFind(profileId);
  if(it == m_parkedUndo.constEnd()) {
    return;
  }
  const heap::undo::UndoStack parked = *it;
  m_parkedUndo.remove(profileId);
  // Only onto the workspace the stack left: a CLI write or a restore while it
  // was away makes its diffs describe something else, and then it goes.
  const heap::undo::Entry* top = parked.peekUndo();
  if(top != nullptr && !(heap::undo::untouchedSince(m_tasks, top->tasks) && heap::undo::untouchedSince(m_events, top->events))) {
    return;
  }
  m_undo.adopt(parked);
  emit pendingUndoChanged();
}

void AppController::clearPendingUndo() {
  if(!m_undo.canUndo() && !m_undo.canRedo()) {
    return;
  }
  m_undo.clear();
  emit pendingUndoChanged();
}

void AppController::flushNotesForUndo() {
  // The open note's pending keystrokes go into it first, so the merge sees
  // them and the reload after it does not drop them. The same for an open doc
  // page and the Docs catalogue: their debounced write would otherwise land
  // after the undo, over what it merged.
  emit aboutToChangeActiveNote();
  emit flushEditorsRequested();
  adoptOrphanNotesState();
  syncActiveNoteBody();
}

bool AppController::undoWouldRemoveTask(const QString& id) const {
  const heap::undo::Entry* top = m_undo.peekUndo();
  if(top == nullptr || id.isEmpty()) {
    return false;
  }
  return std::any_of(top->tasks.cbegin(), top->tasks.cend(), [&id](const heap::undo::Edit<::Task>& e) {
    return e.id == id && !e.existedBefore && e.existsAfter;
  });
}

bool AppController::undoIsKnowledgeOnly() const {
  const heap::undo::Entry* top = m_undo.peekUndo();
  if(top == nullptr || top->profileRemoved || top->profileMove || top->statusesTouched || top->savedViewsTouched) {
    return false;
  }
  return top->tasks.isEmpty() && top->events.isEmpty() && top->people.isEmpty();
}

void AppController::undo() {
  if(!m_undo.canUndo()) {
    return;
  }
  flushNotesForUndo();
  heap::undo::Entry* entry = m_undo.takeUndo();
  if(entry == nullptr) {
    return;
  }
  if(entry->profileMove) {
    // A move between profiles (A5-14): back from the other profile, and the
    // other profile's copy of the step goes with it.
    const quint64 serial = entry->serial;
    const QString other = profileMoveOtherSide_(*entry);
    const bool ok = applyProfileMove_(*entry, /*backward=*/true);
    const QString label = entry->label;
    if(!ok) {
      // The step is spent: it would fail forward too, and nothing recorded
      // on top of it can be redone without it. What is below stays.
      m_undo.discardRedo();
    }
    if(m_parkedUndo.contains(other)) {
      m_parkedUndo[other].removeUndoable(serial);
    }
    emit pendingUndoChanged();
    if(ok) {
      emit toast(label);
      playSound_(static_cast<int>(heap::platform::SoundCue::Undo));
    }
    scheduleSave();
    return;
  }
  heap::undo::refreshLeaving(m_notes, entry->notes, /*backward=*/true);
  heap::undo::refreshLeaving(m_docPages, entry->docPages, /*backward=*/true);
  heap::undo::refreshLeaving(m_people, entry->people, /*backward=*/true);
  // Copy: restoring a profile re-enters the stack's owner and the pointer
  // would not survive it.
  const heap::undo::Entry copy = *entry;
  const bool wasProfile = copy.profileRemoved;
  applyUndoEntry(copy, /*backward=*/true);
  if(wasProfile) {
    m_undo.clear();
  }
  emit pendingUndoChanged();
  emit toast(copy.label);
  playSound_(static_cast<int>(heap::platform::SoundCue::Undo));
  scheduleSave();
}

void AppController::redo() {
  if(!m_undo.canRedo()) {
    return;
  }
  flushNotesForUndo();
  heap::undo::Entry* entry = m_undo.takeRedo();
  if(entry == nullptr) {
    return;
  }
  if(entry->profileMove) {
    const bool ok = applyProfileMove_(*entry, /*backward=*/false);
    const heap::undo::Entry copy = *entry;
    if(ok) {
      m_parkedUndo[profileMoveOtherSide_(copy)].push(copy);
      emit toast(copy.redoLabel.isEmpty() ? tr_("undo.redone") : copy.redoLabel);
    } else {
      // Only this step goes (with the redo tail above it); older undo stays.
      m_undo.removeUndoable(copy.serial);
    }
    emit pendingUndoChanged();
    scheduleSave();
    return;
  }
  heap::undo::refreshLeaving(m_notes, entry->notes, /*backward=*/false);
  heap::undo::refreshLeaving(m_docPages, entry->docPages, /*backward=*/false);
  heap::undo::refreshLeaving(m_people, entry->people, /*backward=*/false);
  const heap::undo::Entry copy = *entry;
  applyUndoEntry(copy, /*backward=*/false);
  emit pendingUndoChanged();
  emit toast(copy.redoLabel.isEmpty() ? tr_("undo.redone") : copy.redoLabel);
  scheduleSave();
}

// ─────────────────────────────────────────────────── Persistence ──

QString AppController::stateFilePath() const {
  const QString dir = heap::paths::dataDir();
  QDir().mkpath(dir);
  return dir + "/state.json";
}

int AppController::backupRetention() const {
  return kBackupRetentionCount;
}

bool AppController::gitLinksBranches() const {
  return settingsMap().value("git").toMap().value("linkBranches", true).toBool();
}

QString AppController::backupDirPath() const {
  const QString dir = heap::paths::dataDir() + "/backups";
  QDir().mkpath(dir);
  return dir;
}

QString AppController::dataDir() const {
  return heap::paths::dataDir();
}

QString AppController::qtVersion() const {
  return QString::fromLatin1(qVersion());
}

QString AppController::appVersion() const {
  return QString::fromLatin1(HEAP_VERSION);
}

void AppController::openLogsFolder() const {
  QDesktopServices::openUrl(QUrl::fromLocalFile(heap::logging::logDirPath()));
}

QString AppController::issueReportBody() const {
  const auto scrubbed = [](const QString& text) {
    return heap::diag::scrubPersonalPaths(text, QDir::homePath(), heap::diag::currentUserName());
  };
  QString body = QStringLiteral(
                     "<!-- Describe the problem above this line. The diagnostics below are "
                     "filled in automatically — please keep them. -->\n\n"
                     "---\n"
                     "**Diagnostics**\n"
                     "- Tabless version: %1\n"
                     "- OS: %2 (%3)\n"
                     "- Qt: %4\n"
                     "- The full log is on your clipboard — paste it below if it helps.\n\n"
                     "<details><summary>Recent log tail</summary>\n\n"
                     "```\n%5\n```\n</details>\n")
                     .arg(QCoreApplication::applicationVersion(),
                          QSysInfo::prettyProductName(),
                          QSysInfo::currentCpuArchitecture(),
                          QString::fromLatin1(qVersion()),
                          // A URL is not private: a short tail, and no home
                          // folder or user name in it (PLAT-28).
                          heap::diag::tailLines(scrubbed(heap::logging::logTail()), 25, 1500));

  // Corruption recoveries are the failures nobody reports because nobody sees
  // them. Attach them to the report the user is already writing (HEAP-156).
  const QString recovery = heap::diag::tailLines(scrubbed(heap::recovery::tail()), 8, 600);
  if(!recovery.isEmpty()) {
    body += QStringLiteral("\n<details><summary>Recovery log</summary>\n\n```\n%1\n```\n</details>\n").arg(recovery);
  }
  return body;
}

QString AppController::issueReportPreview() const {
  const auto scrubbed = [](const QString& text) {
    return heap::diag::scrubPersonalPaths(text, QDir::homePath(), heap::diag::currentUserName());
  };
  // The form's preview (X/N-Err-Storage): the facts on one line, then the
  // short log tail, scrubbed as for the URL (PLAT-28).
  QString out = QStringLiteral("Tabless %1 · %2 · Qt %3").arg(appVersion(), QSysInfo::prettyProductName(), QString::fromLatin1(qVersion()));
  const QString tail = heap::diag::tailLines(scrubbed(heap::logging::logTail()), 25, 1500);
  if(!tail.trimmed().isEmpty()) {
    out += QChar('\n') + tail;
  }
  const QString recovery = heap::diag::tailLines(scrubbed(heap::recovery::tail()), 8, 600);
  if(!recovery.trimmed().isEmpty()) {
    out += QChar('\n') + recovery;
  }
  return out;
}

QString AppController::issueDiagnostics() const {
  const auto scrubbed = [](const QString& text) {
    return heap::diag::scrubPersonalPaths(text, QDir::homePath(), heap::diag::currentUserName());
  };
  QString out = QStringLiteral("Tabless %1 · %2 (%3) · Qt %4\n\n```\n%5\n```\n")
                    .arg(QCoreApplication::applicationVersion(),
                         QSysInfo::prettyProductName(),
                         QSysInfo::currentCpuArchitecture(),
                         QString::fromLatin1(qVersion()),
                         scrubbed(heap::logging::logTail(12000)));
  const QString recovery = scrubbed(heap::recovery::tail());
  if(!recovery.isEmpty()) {
    out += QStringLiteral("\nRecovery log:\n```\n%1\n```\n").arg(recovery);
  }
  return out;
}

QVariantList AppController::recoveryLog() const {
  return heap::recovery::entries();
}

bool AppController::exportRecoveryLog(const QUrl& fileUrl) {
  const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
  const bool ok = heap::recovery::exportTo(path);
  emit toast(ok ? tr_("recovery.saved") : tr_("recovery.empty"));
  return ok;
}

void AppController::reportAnIssue() {
  const QString body = issueReportBody();
  // The long version goes where only the user can see it until they paste it.
  if(QClipboard* clipboard = QGuiApplication::clipboard()) {
    clipboard->setText(issueDiagnostics());
    emit toast(tr_("issue.logCopied"));
  }

  QUrl url(QStringLiteral("https://github.com/sectapunterx/tabless/issues/new"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("title"), QStringLiteral("[bug] "));
  query.addQueryItem(QStringLiteral("body"), body);
  url.setQuery(query);
  QDesktopServices::openUrl(url);
}

void AppController::openIssueReport(const QString& body) const {
  QUrl url(QStringLiteral("https://github.com/sectapunterx/tabless/issues/new"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("title"), QStringLiteral("[bug] "));
  // What the person saw in the preview, under a line for their own words.
  const QString text = body.trimmed().isEmpty()
                           ? QString()
                           : QStringLiteral("<!-- Describe the problem above this line. -->\n\n```\n") + body + QStringLiteral("\n```\n");
  query.addQueryItem(QStringLiteral("body"), text);
  url.setQuery(query);
  QDesktopServices::openUrl(url);
}

void AppController::checkForUpdates() {
  if(!m_updater || m_updater->isChecking() || m_updatesFromStore) {
    return;
  }
  m_updateStatus = QStringLiteral("update.checking");
  m_updateStatusArg.clear();
  emit updateStatusChanged();
  m_updater->checkForUpdates();
}

void AppController::openLatestRelease() const {
  if(!m_latestReleaseUrl.isEmpty()) {
    QDesktopServices::openUrl(QUrl(m_latestReleaseUrl));
  }
}

bool AppController::updateCanInstall() const {
  const auto kind = static_cast<heap::update::PackageKind>(m_packageKind);
  return m_updater && kind != heap::update::PackageKind::None &&
         m_updater->latestAssets().contains(heap::update::assetNameFor(kind, m_updater->latestTag()));
}

void AppController::setUpdatePhase(const QString& phase, const QString& statusKey, const QString& arg) {
  m_updatePhase = phase;
  m_updateStatus = statusKey;
  m_updateStatusArg = arg;
  emit updateStatusChanged();
}

void AppController::downloadUpdate() {
  if(m_updatesFromStore) {
    return;
  }
  if(!updateCanInstall()) {
    openLatestRelease();
    return;
  }
  if(m_updater->isDownloading() || m_updatePhase == QLatin1String("installing")) {
    return;
  }
  if(m_updatePhase == QLatin1String("ready")) {
    emit updateReadyToInstall(m_updater->latestTag(), m_updateSha256);
    return;
  }
  const QString asset = heap::update::assetNameFor(static_cast<heap::update::PackageKind>(m_packageKind), m_updater->latestTag());
  m_updateProgress = 0.0;
  m_updateSha256.clear();
  emit updateProgressChanged();
  setUpdatePhase(QStringLiteral("downloading"), QStringLiteral("update.downloading"), m_updater->latestTag());
  m_updater->downloadAsset(asset, heap::update::updateWorkDir());
}

void AppController::cancelUpdateDownload() {
  if(!m_updater || !m_updater->isDownloading()) {
    return;
  }
  m_updater->cancelDownload();
  setUpdatePhase(QString(), QStringLiteral("update.available"), m_updater->latestTag());
}

void AppController::installUpdate() {
  if(m_updatePhase != QLatin1String("ready") || m_updatePackage.isEmpty()) {
    return;
  }
  snapshotBeforeChange(QStringLiteral("update"));
  // The package was checked when it arrived; check it again now, so a file
  // touched on disk since then is not what gets installed.
  if(heap::update::sha256OfFile(m_updatePackage) != m_updateSha256) {
    QFile::remove(m_updatePackage);
    m_updatePackage.clear();
    setUpdatePhase(QStringLiteral("error"), QStringLiteral("update.mismatch"));
    emit toast(updateStatus(), QStringLiteral("warning"));
    return;
  }
  flushSave();
  QString error;
  if(!heap::update::startInstall(static_cast<heap::update::PackageKind>(m_packageKind), m_updatePackage, error)) {
    qWarning() << "update install failed:" << error;
    setUpdatePhase(QStringLiteral("error"), QStringLiteral("update.installFailed"), error);
    emit toast(updateStatus(), QStringLiteral("warning"));
    return;
  }
  setUpdatePhase(QStringLiteral("installing"), QStringLiteral("update.installing"));
  // Quit from the event loop, after this call has returned to QML.
  QTimer::singleShot(0, this, []() {
    QCoreApplication::quit();
  });
}

void AppController::syncNow(bool here) {
  QStringList directories;
  for(auto it = m_directoryClients.constBegin(); it != m_directoryClients.constEnd(); ++it) {
    directories.append(it.key());
  }
  if(m_syncProviders.empty() && directories.isEmpty()) {
    emit toast(tr_("sync.noTracker"));
    return;
  }
  for(const QString& id : directories) {
    fetchDirectory(id, /*rebindProfile=*/here);
  }
  if(m_syncProviders.empty()) {
    return;
  }
  // No "Syncing…" toast: the header says so if it takes a while (APP-186),
  // and the result is the one message a sync gets (APP-180).
  ++m_syncRunSerial;
  // A pull by hand counts too: the next periodic one is a full period away.
  m_lastTrackerSync = QDateTime::currentDateTime();
  saveLastTrackerSync();
  // Collect the ids first: refreshing a token rebuilds m_syncProviders.
  QStringList ids;
  ids.reserve(static_cast<qsizetype>(m_syncProviders.size()));
  for(const auto& provider : m_syncProviders) {
    ids.append(provider->id());
  }
  for(const QString& id : ids) {
    // A tracker bound to another profile had its pull dropped unmerged: Sync
    // everything left a GitLab MR beside its Jira issue, where a card's own
    // Sync now (which binds) folded it (0.9.4).
    if(here && integrationConfig(id).value(QStringLiteral("profileId")).toString() != activeProfileId()) {
      setIntegrationField(id, QStringLiteral("profileId"), activeProfileId());
    }
    syncProviderNow(id);
  }
}

void AppController::syncProviderNow(const QString& providerId) {
  setSyncInFlight(providerId, true);
  ensureFreshToken(
      providerId,
      [this, providerId]() {
        for(const auto& provider : m_syncProviders) {
          if(provider->id() == providerId) {
            // The project statuses follow the pull by themselves, once a
            // day (JiraProvider); Settings asks when it shows them.
            provider->pullTasks();
            return;
          }
        }
        setSyncInFlight(providerId, false);
        emit integrationActionFinished(providerId, QStringLiteral("sync"), false, tr_("sync.noTracker"));
      },
      [this, providerId]() {
        setSyncInFlight(providerId, false);
        // A quiet pull that never went out must not make the next one quiet.
        m_settlePulls.remove(providerId);
        // The refresh already said why (offline, or signed out).
        emit integrationActionFinished(
            providerId,
            QStringLiteral("sync"),
            false,
            tr_("sync.failed").arg(providerDisplayName(providerId), providerReason(QStringLiteral("token refresh failed"))));
      });
}

QString AppController::uniqueTaskId(const QString& base) const {
  if(m_tasks.indexOfId(base) < 0) {
    return base;
  }
  // Two issues can legitimately want the same id — "#5" from two repos in an
  // assigned-to-me pull. Without this, the second upsert would overwrite the
  // first task outright. Same "-N" shape the recurrence clone uses.
  int n = 2;
  QString candidate;
  do {
    candidate = base + QChar('-') + QString::number(n++);
  } while(m_tasks.indexOfId(candidate) >= 0);
  return candidate;
}

// The Jira issues a review can fold into, by key ("PROJ-123"). An archived
// one counts too: a merged MR names a ticket already done and archived here,
// and kept a card of its own beside it. A live one wins over an archived one.
QHash<QString, QString> AppController::jiraIssuesByKey() const {
  QHash<QString, QString> byKey;
  for(const Task& cur : m_tasks.items()) {
    if(cur.externalProvider != QLatin1String("jira") || cur.externalId.isEmpty()) {
      continue;
    }
    const QString key = cur.externalId.toUpper();
    if(!cur.archived || !byKey.contains(key)) {
      byKey.insert(key, cur.id);
    }
  }
  return byKey;
}

// What the issue's card shows of a folded review: "PR #n state", CI.
QVariantMap AppController::reviewGitFacts(const QJsonObject& details, const QString& externalId, const QString& url) {
  const QString pipeline = details.value(QStringLiteral("pipeline")).toString();
  QString checks;
  if(pipeline == QLatin1String("success")) {
    checks = QStringLiteral("passing");
  } else if(pipeline == QLatin1String("failed")) {
    checks = QStringLiteral("failing");
  } else if(!pipeline.isEmpty() && pipeline != QLatin1String("canceled") && pipeline != QLatin1String("skipped")) {
    checks = QStringLiteral("pending");
  }
  QVariantMap entry;
  entry["prState"] = details.value(QStringLiteral("state")).toString();
  entry["prNumber"] = QString(externalId).remove(QChar('!')).toInt();
  entry["prUrl"] = url;
  entry["prChecks"] = checks;
  return entry;
}

// "Sync everything" pulls every tracker at once, and GitLab can answer
// before Jira: a review pulled ahead of its issue got a card of its own
// (APP-293 folds only into issues already here). Once the issues are in,
// those cards fold the way a later pull would have folded them.
// The "PR #n" an issue shows lives in memory only: a review folded before
// (its card archived) gives it back too, so a restart does not leave the
// issue without its MR until GitLab's next pull. `archive` false only does
// that, for a profile being loaded.
int AppController::foldReviewCardsIntoIssues(bool archive) {
  const QHash<QString, QString> issueByKey = jiraIssuesByKey();
  if(issueByKey.isEmpty()) {
    return 0;
  }
  const QSet<QString> issueKeys(issueByKey.keyBegin(), issueByKey.keyEnd());
  QStringList folded;
  for(const Task& cur : m_tasks.items()) {
    if(cur.externalProvider.isEmpty() || cur.externalProvider == QLatin1String("jira") || !reviewFoldOn(cur.externalProvider)) {
      continue;
    }
    const QJsonObject& details = cur.externalMeta.details;
    const QString kind = details.value(QStringLiteral("kind")).toString();
    if(kind != QLatin1String("pr") && kind != QLatin1String("mr")) {
      continue;
    }
    const QString key = heap::integrations::linkedIssueKey(details.value(QStringLiteral("sourceBranch")).toString(),
                                                           cur.externalMeta.title.isEmpty() ? cur.title : cur.externalMeta.title,
                                                           cur.externalMeta.body,
                                                           issueKeys);
    if(key.isEmpty()) {
      continue;
    }
    const QString issueId = issueByKey.value(key);
    // An open review wins over a merged or closed one already shown.
    const QVariantMap shown = m_tasks.gitFactsFor(issueId);
    const QString stage = details.value(QStringLiteral("state")).toString();
    if(shown.value(QStringLiteral("prNumber")).toInt() == 0 ||
       (shown.value(QStringLiteral("prState")).toString() != QLatin1String("open") && stage == QLatin1String("open"))) {
      m_tasks.setGitInfoForId(issueId, reviewGitFacts(details, cur.externalId, cur.externalUrl));
    }
    if(archive && !cur.archived) {
      folded.append(cur.id);
      folded.append(issueId);
    }
    offerReviewFoldHint(issueId, cur);
  }
  // Archived, not deleted: the card may hold notes.
  for(int i = 0; i + 1 < folded.size(); i += 2) {
    markReviewFolded(folded.at(i), folded.at(i + 1));
  }
  return static_cast<int>(folded.size() / 2);
}

void AppController::markReviewFolded(const QString& cardId, const QString& issueId) {
  const int at = m_tasks.indexOfId(cardId);
  if(at < 0) {
    return;
  }
  // The one key of `details` written here: the next pull of a folded card
  // skips it, so the mark stays until folding is turned off.
  Task t = m_tasks.items().at(at);
  t.externalMeta.details.insert(QStringLiteral("foldedInto"), issueId);
  m_tasks.upsert(t);
  m_tasks.setArchived(cardId, true);
}

bool AppController::reviewFoldOn(const QString& providerId) const {
  return integrationConfig(providerId).value(QStringLiteral("reviewFold"), true).toBool();
}

void AppController::setReviewFold(const QString& providerId, bool on) {
  if(reviewFoldOn(providerId) == on) {
    return;
  }
  setIntegrationField(providerId, QStringLiteral("reviewFold"), on);
  // Seen from the settings it points to: the coach mark has done its job.
  dismissCoachMark(QStringLiteral("reviewFold"));
  if(on) {
    if(foldReviewCardsIntoIssues() > 0) {
      scheduleSave();
    }
    return;
  }
  // Off: the cards folding archived come back, and the issues they were
  // folded into stop showing their "PR #n".
  const QScopedValueRollback<bool> fromTracker(m_historySync, true);
  QStringList back;
  QSet<QString> issues;
  for(const Task& cur : m_tasks.items()) {
    if(cur.externalProvider != providerId) {
      continue;
    }
    const QString into = cur.externalMeta.details.value(QStringLiteral("foldedInto")).toString();
    if(!into.isEmpty()) {
      back.append(cur.id);
      issues.insert(into);
    }
  }
  for(const QString& id : back) {
    const int at = m_tasks.indexOfId(id);
    if(at < 0) {
      continue;
    }
    Task t = m_tasks.items().at(at);
    t.externalMeta.details.remove(QStringLiteral("foldedInto"));
    m_tasks.upsert(t);
    m_tasks.setArchived(id, false);
  }
  const QVariantMap none{{QStringLiteral("prState"), QString()},
                         {QStringLiteral("prNumber"), 0},
                         {QStringLiteral("prUrl"), QString()},
                         {QStringLiteral("prChecks"), QString()}};
  for(const QString& issueId : issues) {
    m_tasks.setGitInfoForId(issueId, none);
  }
  if(!back.isEmpty()) {
    scheduleSave();
  }
}

void AppController::offerReviewFoldHint(const QString& issueId, const Task& review) {
  const int at = m_tasks.indexOfId(issueId);
  // An archived issue is on no board: the coach mark waits for a live one.
  if(at < 0 || m_tasks.items().at(at).archived) {
    return;
  }
  offerCoachMark(QStringLiteral("reviewFold"),
                 {{QStringLiteral("taskId"), issueId},
                  {QStringLiteral("issueKey"), m_tasks.items().at(at).externalId},
                  {QStringLiteral("provider"), review.externalProvider},
                  {QStringLiteral("providerName"), providerDisplayName(review.externalProvider)},
                  {QStringLiteral("mr"), review.externalId}});
}

AppController::MergeStats AppController::mergeExternalTasks(const QString& providerId,
                                                            const QString& idPrefix,
                                                            const QVector<heap::integrations::ExternalTask>& issues,
                                                            bool complete,
                                                            QStringList* goneCandidates) {
  using heap::integrations::StatusMap;
  const heap::frame::Span span("mergeExternalTasks");
  MergeStats stats;
  // Merge / pull requests whose CI went red in this pull (APP-318).
  QStringList ciTurnedRed;
  // Whatever this pull changes on a card is the tracker's doing (APP-165).
  const QScopedValueRollback<bool> fromTracker(m_historySync, true);

  // Resolved once for the batch: the map is the same for every issue, and
  // re-reading the settings blob per issue is how settingsMap() used to show
  // up in a sync profile.
  const QHash<QString, QString> statusOverrides = statusOverridesFor(providerId);
  QStringList seenStatuses;
  // While heap does not write this tracker, a column the user picked here is
  // theirs and no pull moves it (APP-243).
  const bool localOwnsColumn = !trackerWriteEnabled(providerId);

  // Auto-sync fires wherever the user happens to be, and the integration
  // config is global — so without this, a timer tick while another profile is
  // open imports every ticket into that profile. Same bind-on-first-use rule
  // the contact merge has: claim the profile the card was connected from, and
  // stay out of any other. A manual sync rebinds it.
  const QString boundProfile = integrationConfig(providerId).value(QStringLiteral("profileId")).toString();
  if(boundProfile.isEmpty()) {
    setIntegrationField(providerId, QStringLiteral("profileId"), activeProfileId());
  } else if(boundProfile != activeProfileId()) {
    return stats;
  }

  // Issues the user deleted here. Deleting one is how they say "not mine";
  // re-adding it on the next pull would make the deletion meaningless.
  const QStringList dismissed = dismissedTasks(providerId);
  // Which filter this batch answers. A card last pulled under another one is
  // not evidence of anything when it is missing (INT-1).
  const QString scopeNow = scopeFingerprintFor(providerId);
  // Rows this pull accounted for, and the ones it closed.
  QSet<QString> seenIds;
  QStringList closedIds;

  // An issue's URL is unique across a whole provider; its number is not, once a
  // pull spans projects. Claim by URL first so "#5 of repo A" cannot be matched
  // to the task that "#5 of repo B" is about to claim. Collected up front
  // because the decision for one issue depends on the whole batch.
  QSet<QString> claimedByUrl;
  const bool hadCardsBefore = std::any_of(m_tasks.items().cbegin(), m_tasks.items().cend(), [&providerId](const Task& cur) {
    return cur.externalProvider == providerId;
  });
  for(const heap::integrations::ExternalTask& ext : issues) {
    if(ext.url.isEmpty()) {
      continue;
    }
    for(const Task& cur : m_tasks.items()) {
      if(cur.externalProvider == providerId && cur.externalUrl == ext.url) {
        claimedByUrl.insert(cur.id);
        break;
      }
    }
  }

  // A merge / pull request about a Jira issue is that issue's work, not a
  // card of its own (APP-293): it shows on the issue's card as "PR #n state"
  // and its own card is not made. Keyed by the issue's key, "PROJ-123".
  const QHash<QString, QString> issueByKey =
      providerId != QLatin1String("jira") && reviewFoldOn(providerId) ? jiraIssuesByKey() : QHash<QString, QString>{};
  const QSet<QString> issueKeys(issueByKey.keyBegin(), issueByKey.keyEnd());
  // The issue a review was folded into this pull: an open one wins over a
  // merged or closed one for the same issue.
  QHash<QString, QString> foldedState;

  for(const heap::integrations::ExternalTask& ext : issues) {
    // An issue the user deleted here stays deleted.
    if(ext.externalId.isEmpty() || dismissed.contains(ext.externalId)) {
      continue;
    }
    // Identity is provider + project + issue id, with the URL bridging rows
    // stored before the project was ever recorded.
    QString existingId;
    if(!ext.url.isEmpty()) {
      for(const Task& cur : m_tasks.items()) {
        if(cur.externalProvider == providerId && cur.externalUrl == ext.url) {
          existingId = cur.id;
          break;
        }
      }
    }
    if(existingId.isEmpty()) {
      for(const Task& cur : m_tasks.items()) {
        if(cur.externalProvider != providerId || cur.externalId != ext.externalId) {
          continue;
        }
        // Another issue in this batch owns that row by URL; leave it alone.
        if(claimedByUrl.contains(cur.id)) {
          continue;
        }
        // Projects have to be compatible: either side may not know its own yet.
        const QString& mine = cur.externalMeta.project;
        if(!mine.isEmpty() && !ext.project.isEmpty() && mine.compare(ext.project, Qt::CaseInsensitive) != 0) {
          continue;
        }
        existingId = cur.id;
        break;
      }
    }

    if(!issueKeys.isEmpty()) {
      const QString reviewKind = ext.details.value(QStringLiteral("kind")).toString();
      if(reviewKind == QLatin1String("pr") || reviewKind == QLatin1String("mr")) {
        const QString key = heap::integrations::linkedIssueKey(
            ext.details.value(QStringLiteral("sourceBranch")).toString(), ext.title, ext.body, issueKeys);
        if(!key.isEmpty()) {
          const QString issueId = issueByKey.value(key);
          const QString stage = ext.details.value(QStringLiteral("state")).toString();
          const QString prev = foldedState.value(issueId);
          if(prev.isEmpty() || (prev != QLatin1String("open") && stage == QLatin1String("open"))) {
            foldedState.insert(issueId, stage);
            m_tasks.setGitInfoForId(issueId, reviewGitFacts(ext.details, ext.externalId, ext.url));
          }
          // A card this review got before it was folded goes to the archive
          // (not deleted: it may hold notes), and stays accounted for.
          if(!existingId.isEmpty()) {
            seenIds.insert(existingId);
            const int at = m_tasks.indexOfId(existingId);
            if(at >= 0 && !m_tasks.items().at(at).archived) {
              markReviewFolded(existingId, issueId);
              ++stats.updated;
            }
          }
          {
            Task review;
            review.externalProvider = providerId;
            review.externalId = ext.externalId;
            offerReviewFoldHint(issueId, review);
          }
          continue;
        }
      }
    }

    const int row = existingId.isEmpty() ? -1 : m_tasks.indexOfId(existingId);
    Task t;
    if(row >= 0) {
      t = m_tasks.items().at(row);
    } else {
      // A cross-project number needs its repo in the id to stay distinguishable.
      QString base = idPrefix;
      if(ext.crossProject && !ext.project.isEmpty()) {
        base += ext.project.section(QChar('/'), -1) + QChar('-');
      }
      // "!17" is a merge request (APP-242); an id cannot carry the "!".
      const QString number = ext.externalId.startsWith(QChar('!')) ? QStringLiteral("MR") + ext.externalId.mid(1) : ext.externalId;
      t.id = uniqueTaskId(base + number);
      // When the issue entered its status, as far as the pull can tell; the
      // tracker's history refines it right after (statusTimesWanted). The
      // moment of the pull is only the last resort: a ticket closed a month
      // ago and pulled today was "closed today" in the week recap.
      t.statusChangedAt = heap::integrations::statustime::provisional(
          ext.statusCategoryChangedAt,
          ext.updatedAt,
          ext.createdAt,
          QDateTime(),
          QDateTime::currentDateTime(),
          ext.statusCategory == QLatin1String("done") ||
              StatusMap::column(ext.status, statusOverrides, QString()) == QLatin1String("done"));
    }
    const bool needsRank = row < 0;
    const Task before = t;
    const bool isNewRow = row < 0;
    seenIds.insert(t.id);

    // Title, description and priority are a three-way merge against what the
    // tracker sent last time. Nothing is ever pushed back for them, so a local
    // edit the tracker did not also touch has to survive the pull; when both
    // sides changed, the local value wins, the card is flagged and the user is
    // told which one — the editor then offers the tracker's version (INT-4).
    const bool noBase = !isNewRow && before.externalMeta.title.isEmpty() && before.externalMeta.status.isEmpty();
    bool conflicted = false;
    const auto mergeScalar =
        [&](const QString& field, QString& local, QString& base, const QString& remote, bool hasBase, bool legacyKeepsLocal) {
          using heap::integrations::FieldMerge;
          const FieldMerge m =
              isNewRow ? FieldMerge::TakeRemote : heap::integrations::mergeField(local, base, remote, hasBase, legacyKeepsLocal);
          if(m == FieldMerge::TakeRemote) {
            local = remote;
          }
          if(m == FieldMerge::Conflict) {
            conflicted = true;
            heap::integrations::setConflict(t.externalMeta.conflicts, field, true);
          } else if(m != FieldMerge::KeepLocal) {
            // Taken or converged: nothing left to choose between.
            heap::integrations::setConflict(t.externalMeta.conflicts, field, false);
          }
          base = remote;
        };
    // No base means a card from before bases were kept; that build overwrote
    // every pull, so the tracker's text is what it expects.
    mergeScalar(QStringLiteral("title"), t.title, t.externalMeta.title, ext.title, !noBase, false);
    mergeScalar(QStringLiteral("body"), t.desc, t.externalMeta.body, ext.body, !noBase, false);
    // Priority used to be overwritten on every pull (INT-2). A card without a
    // priority base was, too — so a value that differs from the tracker's now
    // is an edit made since the last pull, and it stays.
    if(!ext.priority.isEmpty()) {
      const QString remotePriority = StatusMap::priority(ext.priority);
      mergeScalar(QStringLiteral("priority"),
                  t.priority,
                  t.externalMeta.priority,
                  remotePriority,
                  !t.externalMeta.priority.isEmpty(),
                  /*legacyKeepsLocal=*/!t.priority.isEmpty());
    } else {
      t.externalMeta.priority.clear();
      heap::integrations::setConflict(t.externalMeta.conflicts, QStringLiteral("priority"), false);
      if(t.priority.isEmpty()) {
        t.priority = QStringLiteral("P2");
      }
    }
    if(!ext.status.isEmpty() && !seenStatuses.contains(ext.status)) {
      seenStatuses.append(ext.status);
    }
    // The column is the tracker's only while the tracker is the one moving it.
    // A status that has not changed since the last pull says nothing new, so
    // the column the user picked here stands; that is what lets a card sit in
    // In Progress or Blocked against a tracker that only knows open/closed.
    // The tracker moving the issue while a local move is still unsent (refused
    // or queued) is a conflict: the local column stays, nothing is pushed, and
    // the user picks a side (APP-163) — it used to drop the local move.
    // A status name nobody mapped follows its kind in the tracker: a Russian
    // or team-named workflow put every card, the done ones too, in To Do.
    learnTrackerStatusCategory(providerId, ext.status, ext.statusCategory);
    for(auto it = ext.transitionCategories.constBegin(); it != ext.transitionCategories.constEnd(); ++it) {
      learnTrackerStatusCategory(providerId, it.key(), it.value());
    }
    const QString mapped = trackerColumn(providerId, ext.status, statusOverrides, QStringLiteral("todo"));
    const auto isDone = [](const QString& column) {
      return column == QStringLiteral("done");
    };
    // A merge / pull request (APP-242) is never written back. Its column is
    // its stage — unless the user lets such cards move, and then a column
    // picked here is theirs, whatever the tracker's write switch says.
    const QString kind = ext.details.value(QStringLiteral("kind")).toString();
    const bool review = kind == QLatin1String("mr") || kind == QLatin1String("pr");
    const bool reviewFollowsStage = review && !reviewCardsMovable(providerId);
    using heap::integrations::StatusPull;
    const StatusPull statusPull = isNewRow || reviewFollowsStage ? StatusPull::TakeRemote
                                                                 : heap::integrations::mergeStatusOnPull(t.status,
                                                                                                         t.externalMeta.unsyncedStatus,
                                                                                                         t.externalMeta.status,
                                                                                                         ext.status,
                                                                                                         mapped,
                                                                                                         t.externalMeta.column,
                                                                                                         localOwnsColumn || review);
    if(statusPull == StatusPull::TakeRemote) {
      t.status = mapped;
      if(!t.externalMeta.unsyncedStatus.isEmpty() && t.externalMeta.status != ext.status) {
        // The tracker moved it (to where the card already is, or with
        // nothing of ours waiting): there is nothing left to send.
        t.externalMeta.unsyncedStatus.clear();
        t.externalMeta.pushQueued = false;
      }
      heap::integrations::setConflict(t.externalMeta.conflicts, QStringLiteral("status"), false);
    } else if(statusPull == StatusPull::Conflict) {
      // Held, not sent: a queued move would now undo theirs unasked.
      t.externalMeta.pushQueued = false;
      if(!t.externalMeta.conflicts.contains(QStringLiteral("status"))) {
        conflicted = true;
      }
      heap::integrations::setConflict(t.externalMeta.conflicts, QStringLiteral("status"), true);
    }
    if(conflicted) {
      ++stats.conflicts;
      // The first one ever is explained on its card, once (coach mark).
      if(!t.archived && !t.externalMeta.conflicts.isEmpty()) {
        offerCoachMark(QStringLiteral("conflict"),
                       {{QStringLiteral("taskId"), t.id},
                        {QStringLiteral("field"), t.externalMeta.conflicts.first()},
                        {QStringLiteral("tracker"), providerDisplayName(providerId)}});
      }
    }
    t.externalMeta.column = mapped;
    t.externalMeta.status = ext.status;
    t.externalMeta.goneUpstream = false;
    t.externalMeta.outOfScope = false;
    t.externalMeta.scope = scopeNow;
    const QString transitionsKey = providerId + QChar('\n') + ext.externalId;
    if(ext.transitionsKnown) {
      m_trackerTransitions.insert(transitionsKey, ext.transitions);
      m_workflowTransitions.insert(workflowTransitionsKey(providerId, ext.project, ext.issueType, ext.status), ext.transitions);
    } else {
      m_trackerTransitions.remove(transitionsKey);
    }
    if(t.status != before.status && !isNewRow) {
      // When the tracker moved it, not when this pull saw it: pulls run by
      // hand by default, days after the move (dogfood 2026-10-19).
      namespace st = heap::integrations::statustime;
      const QDateTime known = st::since(before.externalMeta.details, ext.status);
      const QDateTime now = QDateTime::currentDateTime();
      t.statusChangedAt = known.isValid() && known <= now ? known
                                                          : st::provisional(ext.statusCategoryChangedAt,
                                                                            ext.updatedAt,
                                                                            ext.createdAt,
                                                                            before.statusChangedAt,
                                                                            now,
                                                                            ext.statusCategory == QLatin1String("done") || isDone(mapped));
      if(isDone(t.status)) {
        closedIds.append(t.id);
      }
    }
    t.externalId = ext.externalId;
    t.externalUrl = ext.url;
    t.externalProvider = providerId;
    t.assignee = ext.assignee;

    // The tracker owns this deadline only while the user has not touched it.
    // Comparing against the value the tracker last sent is what tells the two
    // apart — a local edit, or a snooze, makes them differ and then wins.
    const bool syncOwnedDue = !t.dueAt.isValid() || t.dueAt == t.externalMeta.dueAt;
    if(syncOwnedDue) {
      t.dueAt = ext.dueAt;  // an invalid value clears a deadline dropped upstream
      t.dueHasTime = ext.dueAt.isValid() && ext.dueHasTime;
    }

    // Pulled labels used to be parsed and thrown away (HEAP-124), then only
    // ever added (INT-3). Three-way against the labels the tracker sent last
    // time: a label added locally survives, one removed upstream goes, one the
    // user took off here stays off. A chip that has no colour yet takes the
    // tracker's (the editor drops colours when it rewrites labels from text).
    t.labels = heap::integrations::mergeLabels(t.labels, t.externalMeta.labels, ext.labels, ext.labelColors);
    t.externalMeta.labels = ext.labels;

    t.externalMeta.author = ext.author;
    t.externalMeta.issueType = ext.issueType;
    t.externalMeta.project = ext.project;
    t.externalMeta.milestone = ext.milestone;
    // A tracker that does not count comments in its search (Jira) leaves the
    // count the comments read on opening the ticket gave.
    if(ext.commentCount >= 0 || isNewRow) {
      t.externalMeta.commentCount = ext.commentCount;
    }
    t.externalMeta.createdAt = ext.createdAt;
    t.externalMeta.updatedAt = ext.updatedAt;
    t.externalMeta.dueAt = ext.dueAt;
    t.externalMeta.crossProject = ext.crossProject;
    // My merge / pull request's CI went red since the last sync (APP-318): a
    // notification, on by default (owner, 2026-10-16), off in Settings.
    {
      const QString wasCi = t.externalMeta.details.value(QStringLiteral("pipeline")).toString();
      const QString nowCi = ext.details.value(QStringLiteral("pipeline")).toString();
      if(row >= 0 && heap::ci::turnedRed(wasCi, nowCi) &&
         settingsMap().value(QStringLiteral("notifications")).toMap().value(QStringLiteral("ciFailed"), true).toBool()) {
        ciTurnedRed.append(QStringLiteral("%1 %2").arg(ext.externalId, ext.title));
      }
    }
    {
      QJsonObject details = ext.details;
      heap::integrations::statustime::carrySince(before.externalMeta.details, details, ext.status);
      t.externalMeta.details = details;
    }
    // Not yet known when the issue entered its status: asked of the
    // tracker's history after the merge (a pull alone cannot say exactly).
    // A merge / pull request's stage is its own and is not asked about.
    // One the history could not answer a few times running is left on the
    // provisional time.
    if(!review && !ext.status.isEmpty() && !heap::integrations::statustime::since(t.externalMeta.details, ext.status).isValid() &&
       heap::integrations::statustime::tries(t.externalMeta.details, ext.status) < heap::integrations::statustime::kMaxTries) {
      // The ones that moved first: a batch is capped, the rest wait a sync.
      if(t.status != before.status || isNewRow) {
        stats.statusTimesWanted.prepend(ext.externalId);
      } else {
        stats.statusTimesWanted.append(ext.externalId);
      }
    }

    // An unchanged issue must not mark the state dirty: auto-sync runs on a
    // timer, and rewriting the whole state.json every cycle for nothing is what
    // the contact merge already avoids.
    if(row >= 0 && t == before) {
      continue;
    }
    if(conflicted) {
      stats.conflictKeys.append(externalKeyOf(t));
    }
    if(needsRank) {
      // A pulled issue lands at the end of its column with a rank of its own;
      // rank 0 tied it with every other synced card (TASKS-3).
      const QVector<::Task> col = columnTasks(t.status, t.id);
      t.rank = heap::board::between(col.isEmpty() ? 0.0 : col.last().rank, 0.0, !col.isEmpty(), false);
    }
    m_tasks.upsert(t);
    (row >= 0 ? stats.updated : stats.added)++;
    if(row < 0) {
      stats.addedIds.append(t.id);
    }
  }
  // A complete pull that no longer carries an issue, under the same filter the
  // card was last pulled under, means the issue was deleted or moved out of
  // reach. The card stays — it may hold local notes — but says so, instead of
  // looking like live work forever. Under a different filter (the user
  // switched repo, "my issues", or edited the JQL) the absence only says the
  // filter changed: the card is out of scope, not gone (INT-1).
  if(complete) {
    for(int i = 0; i < m_tasks.rowCount(); ++i) {
      Task t = m_tasks.items().at(i);
      if(t.externalProvider != providerId || t.externalId.isEmpty() || seenIds.contains(t.id) || t.externalMeta.goneUpstream ||
         t.externalMeta.outOfScope) {
        continue;
      }
      // An empty scope is a card from before scopes were kept: nothing says
      // which filter it came from, so it gets the benefit of the doubt.
      if(t.externalMeta.scope == scopeNow) {
        if(goneCandidates != nullptr) {
          // Not yet: a filter on status drops a closed issue too (INT-6).
          goneCandidates->append(t.externalId);
          continue;
        }
        t.externalMeta.goneUpstream = true;
        ++stats.gone;
      } else {
        t.externalMeta.outOfScope = true;
        ++stats.outOfScope;
      }
      m_tasks.upsert(t);
    }
  }
  for(const QString& id : closedIds) {
    dropFutureFocusBlocks(id);
  }
  // Learn this provider's vocabulary from what it actually sent, so the
  // mapping UI can offer real statuses instead of asking the user to type
  // them. Written even when nothing else changed — a status appearing for the
  // first time is news whether or not the issue carrying it was new.
  const bool learned = rememberSeenStatuses(providerId, seenStatuses);
  // The issues are in: a review another tracker brought before them folds
  // now rather than on that tracker's next pull.
  if(providerId == QLatin1String("jira")) {
    stats.updated += foldReviewCardsIntoIssues();
  }
  if(stats.added > 0 || stats.updated > 0 || stats.gone > 0 || stats.outOfScope > 0 || learned) {
    scheduleSave();
  }
  if(stats.outOfScope > 0 || stats.added > 0 || stats.updated > 0) {
    emit integrationStatesChanged();
  }
  // The first pull of a tracker brings in everything it has: that is the
  // board being filled, not news, so its cards carry no dot.
  noteSyncAdded(stats.addedIds, /*markUnseen=*/hadCardsBefore);
  if(!ciTurnedRed.isEmpty()) {
    emit notification(ciTurnedRed.size() == 1 ? tr_("ci.failedTitle") : tr_("ci.failedTitleN").arg(ciTurnedRed.size()),
                      ciTurnedRed.join(QStringLiteral(" · ")),
                      QStringLiteral("ci"));
  }
  return stats;
}

AppController::MergeStats AppController::settleMissingIssues(const QString& providerId,
                                                             const QString& idPrefix,
                                                             const QVector<heap::integrations::ExternalTask>& found,
                                                             const QStringList& missing) {
  // Still in the tracker: merged as a pull would merge it, so a card closed
  // there under "statusCategory != Done" moves to Done instead of reading as
  // deleted. Then marked outside the filter, which it is — and which keeps the
  // next sync from asking about it again.
  MergeStats stats = mergeExternalTasks(providerId, idPrefix, found, /*complete=*/false);
  QSet<QString> foundIds;
  for(const auto& e : found) {
    foundIds.insert(e.externalId);
  }
  bool marked = false;
  for(int i = 0; i < m_tasks.rowCount(); ++i) {
    Task t = m_tasks.items().at(i);
    if(t.externalProvider != providerId || t.externalId.isEmpty()) {
      continue;
    }
    if(foundIds.contains(t.externalId) && !t.externalMeta.outOfScope) {
      t.externalMeta.outOfScope = true;
      // A closed issue that moved to Done is the news; an open one that left
      // the filter is "outside filter", as for a changed filter (INT-1).
      if(statusCategory(t.status) != QStringLiteral("done")) {
        ++stats.outOfScope;
      }
      m_tasks.upsert(t);
      marked = true;
    } else if(missing.contains(t.externalId) && !t.externalMeta.goneUpstream) {
      t.externalMeta.goneUpstream = true;
      ++stats.gone;
      m_tasks.upsert(t);
      marked = true;
    }
  }
  if(marked) {
    scheduleSave();
    emit integrationStatesChanged();
  }
  return stats;
}

void AppController::reportSync(const QString& label, const MergeStats& stats, bool settlePull) {
  const bool changed = stats.added > 0 || stats.updated > 0;
  const bool news = changed || stats.gone > 0 || stats.outOfScope > 0 || stats.conflicts > 0;
  if(settlePull && !news) {
    return;
  }
  // "Synced 12 issues" every quarter of an hour says nothing about
  // whether anything happened. Report what actually changed — and
  // never "up to date" next to something that did (INT-7).
  if(!news) {
    emit toast(tr_("sync.upToDate").arg(label));
    return;
  }
  QStringList parts;
  // The new cards by name (APP-180): key and title, three at most, then how
  // many more. Only cards still on the board count.
  QStringList newIds;
  QStringList names;
  for(const QString& id : stats.addedIds) {
    const int row = m_tasks.indexOfId(id);
    if(row < 0) {
      continue;
    }
    newIds.append(id);
    if(names.size() < 3) {
      const Task& t = m_tasks.items().at(row);
      QString title = t.title.simplified();
      if(title.size() > 40) {
        title = title.left(39).trimmed() + QChar(0x2026);
      }
      const QString key = externalKeyOf(t);
      names.append(title.isEmpty() ? key : key + QChar(' ') + title);
    }
  }
  if(!names.isEmpty()) {
    if(newIds.size() > names.size()) {
      names.append(tr_("sync.newMore").arg(newIds.size() - names.size()));
    }
    parts.append(tr_("sync.summaryNamed").arg(label).arg(stats.added).arg(names.join(QStringLiteral(", "))).arg(stats.updated));
  } else if(changed) {
    parts.append(tr_("sync.summary").arg(label).arg(stats.added).arg(stats.updated));
  }
  if(stats.conflicts > 0) {
    // Name the cards, so the user can go and pick a side (INT-4).
    QStringList keys = stats.conflictKeys.mid(0, 3);
    if(stats.conflictKeys.size() > 3) {
      keys.append(QStringLiteral("…"));
    }
    parts.append(tr_("sync.conflicts").arg(stats.conflicts).arg(keys.join(QStringLiteral(", "))));
  }
  if(stats.gone > 0) {
    parts.append(tr_("sync.gone").arg(stats.gone));
  }
  if(stats.outOfScope > 0) {
    parts.append(tr_("sync.outOfScope").arg(stats.outOfScope));
  }
  QString message = parts.join(QStringLiteral(" · "));
  if(!changed) {
    message = tr_("sync.headline").arg(label, message);
  }
  // One message per sync: with new cards it carries "Show" (syncNews),
  // otherwise it is a plain toast. Either way the log keeps it.
  logEvent(QStringLiteral("sync"), message, newIds);
  if(!newIds.isEmpty()) {
    emit syncNews(message, newIds);
    return;
  }
  emit toast(message);
}

void AppController::noteSyncAdded(const QStringList& ids, bool markUnseen) {
  if(ids.isEmpty()) {
    return;
  }
  if(m_syncNewSerial != m_syncRunSerial) {
    m_syncNewIds = ids;
    m_syncNewSerial = m_syncRunSerial;
  } else {
    for(const QString& id : ids) {
      if(!m_syncNewIds.contains(id)) {
        m_syncNewIds.append(id);
      }
    }
  }
  emit syncNewTaskIdsChanged();
  if(markUnseen) {
    markTasksUnseen(ids);
  }
}

void AppController::markTasksUnseen(const QStringList& taskIds) {
  bool changed = false;
  for(const QString& id : taskIds) {
    if(!id.isEmpty() && !m_unseenTaskIds.contains(id)) {
      m_unseenTaskIds.insert(id);
      changed = true;
    }
  }
  if(changed) {
    ++m_unseenRevision;
    emit unseenTasksChanged();
  }
}

void AppController::markTaskSeen(const QString& taskId) {
  if(m_unseenTaskIds.remove(taskId)) {
    ++m_unseenRevision;
    emit unseenTasksChanged();
  }
}

void AppController::logEvent(const QString& kind, const QString& message, const QStringList& taskIds, const QString& route) {
  if(message.trimmed().isEmpty()) {
    return;
  }
  m_eventLog.add(QDateTime::currentDateTime(), kind.isEmpty() ? QStringLiteral("info") : kind, message, taskIds, route);
  emit eventLogChanged();
}

void AppController::toastAndLog(const QString& message, const QString& kind, const QStringList& taskIds, const QString& route) {
  logEvent(kind, message, taskIds, route);
  const QScopedValueRollback<bool> muted(m_eventLogMuted, true);
  emit toast(message, kind);
}

void AppController::setSyncInFlight(const QString& providerId, bool inFlight) {
  const bool was = !m_syncInFlight.isEmpty();
  if(inFlight) {
    const int serial = ++m_syncStartSerial;
    m_syncInFlight.insert(providerId, serial);
    // A pull that never answers (a provider rebuilt under it, a server that
    // holds the socket open) must not keep the header dot on for good.
    QTimer::singleShot(kSyncWatchdogMs, this, [this, providerId, serial]() {
      if(m_syncInFlight.value(providerId) == serial) {
        setSyncInFlight(providerId, false);
      }
    });
  } else {
    m_syncInFlight.remove(providerId);
  }
  if(was != !m_syncInFlight.isEmpty()) {
    emit syncingChanged();
  }
  // Per source: the indicator's popover and the first-load panel.
  emit integrationHealthChanged();
}

QHash<QString, QString> AppController::statusOverridesFor(const QString& providerId) const {
  QHash<QString, QString> out;
  const QVariantMap raw =
      settingsMap().value(QStringLiteral("integrations")).toMap().value(providerId).toMap().value(QStringLiteral("statusMap")).toMap();
  for(auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
    const QString column = it.value().toString().trimmed();
    if(!column.isEmpty()) {
      out.insert(it.key(), column);
    }
  }
  return out;
}

bool AppController::rememberSeenStatuses(const QString& providerId, const QStringList& statuses) {
  if(statuses.isEmpty()) {
    return false;
  }
  QVariantMap settings = settingsMap();
  QVariantMap integrations = settings.value(QStringLiteral("integrations")).toMap();
  QVariantMap cfg = integrations.value(providerId).toMap();
  QStringList seen = cfg.value(QStringLiteral("seenStatuses")).toStringList();
  bool changed = false;
  for(const QString& s : statuses) {
    if(!seen.contains(s)) {
      seen.append(s);
      changed = true;
    }
  }
  if(!changed) {
    return false;
  }
  // A tracker with a sprawling workflow should not grow the settings blob
  // without bound. The oldest go first: sorting and cutting at 64 dropped
  // every status past "D" of a company Jira's instance-wide list, the user's
  // own "In Progress" and "Selected for Development" among them (dogfood
  // 2026-10-20). The list is sorted where it is shown.
  constexpr qsizetype kMaxSeen = 200;
  if(seen.size() > kMaxSeen) {
    seen = seen.mid(seen.size() - kMaxSeen);
  }
  cfg.insert(QStringLiteral("seenStatuses"), seen);
  integrations.insert(providerId, cfg);
  settings.insert(QStringLiteral("integrations"), integrations);
  m_appSettingsJson = QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson(QJsonDocument::Compact);
  emit appSettingsJsonChanged();
  return true;
}

QVariantList AppController::statusMappingFor(const QString& providerId) const {
  using heap::integrations::StatusMap;
  const QVariantMap cfg = settingsMap().value(QStringLiteral("integrations")).toMap().value(providerId).toMap();
  const QHash<QString, QString> overrides = statusOverridesFor(providerId);

  // The statuses of the projects synced (Jira, dogfood 2026-10-20), by
  // project; then everything an issue of this provider arrived in, then
  // anything the user has already mapped — a status can disappear upstream
  // without the choice becoming uninteresting, and dropping the row would
  // silently drop the mapping. One row per name: the mapping is by name.
  struct Entry {
    QString status;
    QString group;
    QString category;
  };

  QVector<Entry> entries;
  QSet<QString> have;
  const auto add = [&](const QString& status, const QString& group, const QString& category) {
    const QString name = status.trimmed();
    if(name.isEmpty() || have.contains(name.toLower())) {
      return;
    }
    have.insert(name.toLower());
    entries.append({name, group, category});
  };
  const QVariantMap projects = cfg.value(QStringLiteral("projectStatuses")).toMap();
  QStringList keys = projects.keys();
  keys.sort(Qt::CaseInsensitive);
  for(const QString& project : std::as_const(keys)) {
    for(const QVariant& v : projects.value(project).toList()) {
      const QVariantMap s = v.toMap();
      add(s.value(QStringLiteral("name")).toString(), project, s.value(QStringLiteral("category")).toString());
    }
  }
  for(const QString& s : cfg.value(QStringLiteral("seenStatuses")).toStringList()) {
    add(s, QString(), m_trackerStatusCategories.value(providerId + QChar('\n') + s.trimmed().toLower()));
  }
  for(auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
    add(it.key(), QString(), m_trackerStatusCategories.value(providerId + QChar('\n') + it.key().trimmed().toLower()));
  }
  std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if(a.group.isEmpty() != b.group.isEmpty()) {
      return b.group.isEmpty();
    }
    const int g = a.group.compare(b.group, Qt::CaseInsensitive);
    return g != 0 ? g < 0 : a.status.compare(b.status, Qt::CaseInsensitive) < 0;
  });

  QVariantList out;
  out.reserve(entries.size());
  for(const Entry& e : std::as_const(entries)) {
    const QString byKind = StatusMap::categoryColumn(e.category);
    QString column = trackerColumn(providerId, e.status, overrides, byKind.isEmpty() ? QStringLiteral("todo") : byKind);
    QVariantMap row;
    row.insert(QStringLiteral("status"), e.status);
    row.insert(QStringLiteral("column"), column);
    // The UI shows a guess differently from a decision: a guess is what the
    // built-in table came up with and may be wrong, a decision is the user's.
    row.insert(QStringLiteral("overridden"), overrides.contains(e.status));
    // Neither in the built-in table nor of a kind the tracker said: the
    // column is only the fallback (Settings says "not recognised — the
    // default picked", DG-093). A kind is a recognition too.
    row.insert(QStringLiteral("known"), !StatusMap::defaultColumn(e.status).isEmpty() || !byKind.isEmpty());
    row.insert(QStringLiteral("category"), e.category);
    row.insert(QStringLiteral("group"), e.group);
    out.append(row);
  }
  return out;
}

void AppController::rememberProjectStatuses(const QString& providerId, const QVector<heap::integrations::ExternalStatus>& statuses) {
  if(providerId.isEmpty() || statuses.isEmpty()) {
    return;
  }
  QVariantMap settings = settingsMap();
  QVariantMap integrations = settings.value(QStringLiteral("integrations")).toMap();
  QVariantMap cfg = integrations.value(providerId).toMap();
  QVariantMap projects = cfg.value(QStringLiteral("projectStatuses")).toMap();
  QHash<QString, QVariantList> fresh;
  for(const heap::integrations::ExternalStatus& s : statuses) {
    if(s.project.isEmpty() || s.name.trimmed().isEmpty()) {
      continue;
    }
    fresh[s.project].append(QVariantMap{{QStringLiteral("name"), s.name.trimmed()}, {QStringLiteral("category"), s.category}});
    learnTrackerStatusCategory(providerId, s.name, s.category);
  }
  QVariantMap at = cfg.value(QStringLiteral("projectStatusesAt")).toMap();
  const QString now = fetchNow().toString(Qt::ISODate);
  for(auto it = fresh.constBegin(); it != fresh.constEnd(); ++it) {
    projects.insert(it.key(), it.value());
    at.insert(it.key(), now);
  }
  cfg.insert(QStringLiteral("projectStatuses"), projects);
  cfg.insert(QStringLiteral("projectStatusesAt"), at);
  // The seen list held the whole instance's statuses before (GET /status):
  // only those an issue of this provider is in now stay.
  QSet<QString> current;
  for(const Task& t : m_tasks.items()) {
    if(t.externalProvider == providerId && !t.externalMeta.status.isEmpty()) {
      current.insert(t.externalMeta.status.trimmed().toLower());
    }
  }
  QStringList kept;
  for(const QString& s : cfg.value(QStringLiteral("seenStatuses")).toStringList()) {
    if(current.contains(s.trimmed().toLower())) {
      kept.append(s);
    }
  }
  cfg.insert(QStringLiteral("seenStatuses"), kept);
  integrations.insert(providerId, cfg);
  settings.insert(QStringLiteral("integrations"), integrations);
  const QString json = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson(QJsonDocument::Compact));
  if(json == m_appSettingsJson) {
    return;
  }
  m_appSettingsJson = json;
  emit appSettingsJsonChanged();
  scheduleSave();
}

void AppController::refreshTrackerStatuses(const QString& providerId) {
  for(const auto& provider : m_syncProviders) {
    if(provider->id() == providerId) {
      // Throttled inside: a list read in the last 5 minutes, or one whose
      // last ask failed and still waits its turn, is not asked.
      provider->fetchStatuses();
      return;
    }
  }
}

void AppController::rememberTrackerTimeTracking(const QString& providerId, const QJsonObject& config) {
  if(providerId.isEmpty() || config.isEmpty()) {
    return;
  }
  QVariantMap settings = settingsMap();
  QVariantMap integrations = settings.value(QStringLiteral("integrations")).toMap();
  QVariantMap cfg = integrations.value(providerId).toMap();
  const QVariantMap value = config.toVariantMap();
  if(cfg.value(QStringLiteral("timeTracking")).toMap() == value) {
    return;
  }
  cfg.insert(QStringLiteral("timeTracking"), value);
  integrations.insert(providerId, cfg);
  settings.insert(QStringLiteral("integrations"), integrations);
  m_appSettingsJson = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson(QJsonDocument::Compact));
  emit appSettingsJsonChanged();
  scheduleSave();
}

QDateTime AppController::fetchNow() const {
  return m_fetchClock ? m_fetchClock() : QDateTime::currentDateTime();
}

void AppController::setStatusMapping(const QString& providerId, const QString& status, const QString& column) {
  if(providerId.isEmpty() || status.isEmpty()) {
    return;
  }
  // Only a real column: an id no board has would hide every ticket carrying
  // that status, with nothing on screen to explain where they went.
  QString target = column.trimmed();
  if(!target.isEmpty()) {
    bool known = false;
    for(const QVariant& v : m_statuses) {
      if(v.toMap().value(QStringLiteral("id")).toString() == target) {
        known = true;
        break;
      }
    }
    // The six ids StatusMap itself produces count as known even when this
    // profile has renamed or dropped a column, so a mapping made against the
    // stock board is not silently discarded by a board that differs.
    if(!known) {
      for(const auto* canonical : {"backlog", "todo", "prog", "review", "blocked", "done"}) {
        if(target == QLatin1String(canonical)) {
          known = true;
          break;
        }
      }
    }
    if(!known) {
      target.clear();
    }
  }

  QVariantMap settings = settingsMap();
  QVariantMap integrations = settings.value(QStringLiteral("integrations")).toMap();
  QVariantMap cfg = integrations.value(providerId).toMap();
  QVariantMap map = cfg.value(QStringLiteral("statusMap")).toMap();
  if(target.isEmpty()) {
    // Clearing is how the user goes back to the built-in guess, so an empty
    // value must remove the key rather than store "".
    if(!map.remove(status)) {
      return;
    }
  } else {
    if(map.value(status).toString() == target) {
      return;
    }
    map.insert(status, target);
  }
  cfg.insert(QStringLiteral("statusMap"), map);
  integrations.insert(providerId, cfg);
  settings.insert(QStringLiteral("integrations"), integrations);
  m_appSettingsJson = QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson(QJsonDocument::Compact);
  emit appSettingsJsonChanged();
  scheduleSave();
  for(const auto& provider : m_syncProviders) {
    if(provider->id() == providerId) {
      provider->setStatusOverrides(statusOverridesFor(providerId));
    }
  }
  // Existing cards are not rewritten: moving a mirrored ticket is a real move
  // that heap pushes back, so re-columning them here would push a change the
  // user never made. The next sync applies the new mapping.
}

void AppController::applyIntegrationSettings() {
  // The rebuild drops every tracker's pull in flight with its provider: none
  // of them will answer now.
  for(const auto& provider : m_syncProviders) {
    if(m_syncInFlight.contains(provider->id())) {
      setSyncInFlight(provider->id(), false);
    }
    // Nor will a live probe.
    m_liveProbe[provider->id()].inFlight = false;
  }
  // Retired, not destroyed: this runs on every settings write, and a write
  // can come from inside a provider's own signal — the tasksFetched handler
  // merges, the merge writes settings, and a provider deleted there was used
  // again by the handler and by the network callback that emitted it
  // (heap_live_updates_tests segfault, 2026-10-19). Cut off from this
  // controller at once, freed once control is back in the event loop.
  for(auto& provider : m_syncProviders) {
    provider->disconnect(this);
    provider.release()->deleteLater();
  }
  m_syncProviders.clear();
  const QVariantMap integrations = settingsMap().value("integrations").toMap();

  // Wire one provider's signals into the shared merge/push/toast handlers.
  const auto wire = [this](heap::integrations::IntegrationProvider* provider, const QString& idPrefix, const QString& label) {
    const QString providerId = provider->id();
    connect(provider,
            &heap::integrations::IntegrationProvider::tasksFetched,
            this,
            [this, provider, providerId, idPrefix, label](const QVector<heap::integrations::ExternalTask>& issues) {
              m_retriedAfter401.remove(providerId);
              setSyncInFlight(providerId, false);
              // What the board now accounts for, for the live probe.
              QDateTime newest;
              for(const heap::integrations::ExternalTask& issue : issues) {
                if(issue.updatedAt.isValid() && (!newest.isValid() || issue.updatedAt > newest)) {
                  newest = issue.updatedAt;
                }
              }
              if(heap::integrations::live::pullAnswered(m_liveProbe[providerId], newest, QDateTime::currentDateTime())) {
                // A probe saw a change while this pull ran: one more takes it in.
                QTimer::singleShot(0, this, [this, providerId]() {
                  livePull(providerId, QDateTime::currentDateTime());
                });
              }
              // The tracker answered, so it is reachable again.
              setProviderOffline(providerId, false);
              recordSyncHealth(providerId, true, static_cast<int>(issues.size()), 0, QString());
              emit integrationActionFinished(providerId, QStringLiteral("sync"), true, QString());
              const bool settlePull = m_settlePulls.remove(providerId);
              // Issues missing from the pull are looked up before anything
              // calls them gone, where the tracker can (INT-6).
              QStringList goneCandidates;
              // Read before the merge: its settings writes rebuild the
              // provider list (the provider itself stays alive until the
              // event loop, see applyIntegrationSettings).
              const bool canFetchStatusChanges = provider->canFetchStatusChanges();
              const MergeStats stats = mergeExternalTasks(
                  providerId, idPrefix, issues, provider->lastPullComplete(), provider->canLookUpIssues() ? &goneCandidates : nullptr);
              // Moves made while the tracker was out of reach go now that it
              // answers (INT-6). Deferred: sending builds on the provider list,
              // which this handler's own settings writes may rebuild.
              QTimer::singleShot(0, this, [this, providerId]() {
                flushQueuedPushes(providerId);
              });
              // When the moves happened, from the tracker's own history, for
              // the cards that do not know yet (dogfood 2026-10-19). Deferred
              // like the pushes.
              if(canFetchStatusChanges && !stats.statusTimesWanted.isEmpty()) {
                // One batch a sync, the moved ones first (see the merge).
                QTimer::singleShot(0, this, [this, providerId, wanted = stats.statusTimesWanted.mid(0, kStatusTimeBatch)]() {
                  for(const auto& p : m_syncProviders) {
                    if(p->id() == providerId) {
                      m_statusTimesAsked.insert(providerId, wanted);
                      p->fetchStatusChanges(wanted);
                      return;
                    }
                  }
                });
              }
              // Jira Cloud's search is eventually consistent: an issue created
              // a moment ago is often missing from the first answer and turned
              // up only on the next sync. One quiet follow-up pull a little
              // later picks it up; it only speaks if it found something.
              if(!settlePull && providerId == QStringLiteral("jira")) {
                QTimer::singleShot(kSettlePullDelayMs, this, [this, providerId]() {
                  m_settlePulls.insert(providerId);
                  syncProviderNow(providerId);
                });
              }
              if(goneCandidates.isEmpty()) {
                reportSync(label, stats, settlePull);
                return;
              }
              // The toast waits for the lookup, so it says once what happened.
              m_pendingLookups.insert(providerId, {stats, settlePull});
              // Deferred for the same reason as the pushes: the provider this
              // handler was called from may be rebuilt by now.
              QTimer::singleShot(0, this, [this, providerId, idPrefix, label, goneCandidates]() {
                for(const auto& p : m_syncProviders) {
                  if(p->id() == providerId) {
                    p->lookUpIssues(goneCandidates);
                    return;
                  }
                }
                // Disconnected meanwhile: nothing to ask, so the old answer.
                const PendingSyncReport pending = m_pendingLookups.take(providerId);
                MergeStats total = pending.stats;
                total.gone += settleMissingIssues(providerId, idPrefix, {}, goneCandidates).gone;
                reportSync(label, total, pending.settlePull);
              });
            });
    connect(provider,
            &heap::integrations::IntegrationProvider::statusChangesFetched,
            this,
            [this, providerId](const QVector<heap::integrations::ExternalStatusChange>& changes) {
              applyTrackerStatusChanges(providerId, changes);
              countStatusTimeTries(providerId, m_statusTimesAsked.take(providerId), changes);
            });
    connect(provider,
            &heap::integrations::IntegrationProvider::changesProbed,
            this,
            [this, providerId](bool ok, int httpStatus, int retryAfter, const QDateTime& newest, const QString& /*error*/) {
              onChangesProbed(providerId, ok, httpStatus, retryAfter, newest);
            });
    connect(provider,
            &heap::integrations::IntegrationProvider::issuesLookedUp,
            this,
            [this, providerId, idPrefix, label](const QVector<heap::integrations::ExternalTask>& found, const QStringList& missing) {
              const PendingSyncReport pending = m_pendingLookups.take(providerId);
              const MergeStats settled = settleMissingIssues(providerId, idPrefix, found, missing);
              MergeStats total = pending.stats;
              total.updated += settled.updated;
              total.gone += settled.gone;
              total.outOfScope += settled.outOfScope;
              total.conflicts += settled.conflicts;
              total.conflictKeys += settled.conflictKeys;
              reportSync(label, total, pending.settlePull);
            });
    // A failed pull used to arrive as an empty task list, so a bad token read
    // as "Synced 0 issue(s)" — say what the tracker actually answered.
    connect(
        provider,
        &heap::integrations::IntegrationProvider::pullFailed,
        this,
        [this, provider, providerId, label](int status, const QString& error) {
          setSyncInFlight(providerId, false);
          // The live updates wait out a failure, a 429's Retry-After first,
          // instead of pulling again at the next minute.
          heap::integrations::live::pullFailed(
              m_liveProbe[providerId], status, provider->lastRetryAfterSecs(), QDateTime::currentDateTime());
          // A failed follow-up pull is not news: the pull before it answered.
          if(m_settlePulls.remove(providerId)) {
            return;
          }
          // A session connected before expiry tracking existed has no
          // tokenExpiresAt, so its first warning is the 401 itself: refresh once
          // and retry rather than making the user sign in again. Only worth it
          // when there is actually a refresh token to spend — otherwise the
          // failure has to reach the user.
          const QVariantMap cfg = integrationConfig(providerId);
          if(status == 401 && !m_retriedAfter401.contains(providerId) &&
             cfg.value(QStringLiteral("authMode")).toString() == QStringLiteral("oauth") &&
             !cfg.value(QStringLiteral("refreshToken")).toString().isEmpty()) {
            m_retriedAfter401.insert(providerId);
            refreshOAuthToken(providerId, [this, providerId, label, error, status](bool ok) {
              if(ok) {
                syncProviderNow(providerId);
              } else {
                recordSyncHealth(providerId, false, -1, status, error);
                emit integrationActionFinished(
                    providerId, QStringLiteral("sync"), false, tr_("sync.failed").arg(label, providerReason(error)));
              }
            });
            return;
          }
          recordSyncHealth(providerId, false, -1, status, error);
          const QString message = tr_("sync.failed").arg(label, providerReason(error));
          toastAndLog(message, QStringLiteral("error"), {}, QStringLiteral("settings:integrations"));
          emit integrationActionFinished(providerId, QStringLiteral("sync"), false, message);
        });
    connect(
        provider,
        &heap::integrations::IntegrationProvider::taskPushed,
        this,
        [this, providerId](const QString& externalId, const QString& project, bool ok, const QString& error, const QString& remoteStatus) {
          onTaskPushed(providerId, externalId, project, ok, error, remoteStatus);
        });
    connect(provider,
            &heap::integrations::IntegrationProvider::issueChecked,
            this,
            [this, providerId](const QString& externalId,
                               const QString& project,
                               bool ok,
                               int httpStatus,
                               const QString& error,
                               const QString& remoteStatus,
                               int filterMatch) {
              onIssueChecked(providerId, externalId, project, ok, httpStatus, error, remoteStatus, filterMatch);
            });
    connect(provider,
            &heap::integrations::IntegrationProvider::connectionTested,
            this,
            [this, providerId, label](bool ok, const QString& error) {
              const QString message = ok ? tr_("int.connected").arg(label) : tr_("int.connectFailed").arg(label, providerReason(error));
              emit toast(message, ok ? QStringLiteral("success") : QStringLiteral("error"));
              emit integrationActionFinished(providerId, QStringLiteral("test"), ok, ok ? QString() : message);
            });
    // The mapping UI used to list only statuses an issue had already arrived
    // in; the tracker's own list fills in the rest.
    connect(provider, &heap::integrations::IntegrationProvider::statusesFetched, this, [this, providerId](const QStringList& statuses) {
      if(rememberSeenStatuses(providerId, statuses)) {
        scheduleSave();
      }
    });
    connect(provider,
            &heap::integrations::IntegrationProvider::projectStatusesFetched,
            this,
            [this, providerId](const QVector<heap::integrations::ExternalStatus>& statuses) {
              rememberProjectStatuses(providerId, statuses);
            });
    connect(provider, &heap::integrations::IntegrationProvider::timeTrackingFetched, this, [this, providerId](const QJsonObject& config) {
      rememberTrackerTimeTracking(providerId, config);
    });
    provider->setStatusOverrides(statusOverridesFor(providerId));
    // What was read before and when: a rebuilt provider (every settings
    // write) or a restart does not ask again inside a day.
    QHash<QString, QDateTime> statusesAt;
    const QVariantMap at = integrationConfig(providerId).value(QStringLiteral("projectStatusesAt")).toMap();
    for(auto it = at.constBegin(); it != at.constEnd(); ++it) {
      statusesAt.insert(it.key(), QDateTime::fromString(it.value().toString(), Qt::ISODate));
    }
    provider->seedProjectStatuses(statusesAt);
  };

  // Build every connected + configured provider from the registry. Generic
  // trackers run on RestIssueProvider; the awkward few (Jira, Trello) are
  // bespoke. Adding a provider is a registry entry — no change here.
  for(const heap::integrations::ProviderDescriptor& d : heap::integrations::providerCatalog()) {
    const QVariantMap cfg = integrationConfig(d.id);
    // A directory has none of the IntegrationProvider verbs, so it is built
    // separately — and unlike a tracker, only when its config actually changed.
    if(d.kind == heap::integrations::ProviderKind::Directory) {
      directoryClient(d.id);
      continue;
    }
    if(!cfg.value(QStringLiteral("connected"), false).toBool()) {
      continue;
    }
    std::unique_ptr<heap::integrations::IntegrationProvider> provider;
    if(d.bespoke) {
      provider = heap::integrations::makeBespokeProvider(d.id, cfg, this);
    } else {
      auto rest = std::make_unique<heap::integrations::RestIssueProvider>(d, this);
      rest->setConfig(cfg);
      if(rest->isConfigured()) {
        provider = std::move(rest);
      }
    }
    if(provider) {
      wire(provider.get(), d.id + QStringLiteral("-"), d.displayName);
      m_syncProviders.push_back(std::move(provider));
    }
  }

  // Optional periodic auto-sync (integrations.autoSyncMinutes: 0 = off, up
  // to a month). The timer only checks; autoSyncTickAt decides.
  const int mins = qBound(0, integrations.value(QStringLiteral("autoSyncMinutes")).toInt(), heap::integrations::kMaxAutoSyncMinutes);
  if(m_syncTimer) {
    if(mins > 0 && !m_syncProviders.empty()) {
      const int check = heap::integrations::autoSyncCheckMinutes(mins) * 60 * 1000;
      if(!m_syncTimer->isActive() || m_syncTimer->interval() != check) {
        m_syncTimer->start(check);
      }
    } else {
      m_syncTimer->stop();
    }
  }
}

heap::integrations::MattermostClient* AppController::directoryClient(const QString& providerId) {
  const QVariantMap cfg = integrationConfig(providerId);
  const QString host = cfg.value(QStringLiteral("host")).toString().trimmed();
  const QString token = cfg.value(QStringLiteral("token")).toString();
  const QString channels = cfg.value(QStringLiteral("channels")).toString();
  const bool connected = cfg.value(QStringLiteral("connected"), false).toBool();

  if(!connected || host.isEmpty() || token.isEmpty()) {
    retireDirectoryClient(providerId);
    return nullptr;
  }
  // applyIntegrationSettings() runs on every settings write, and a rebuild
  // would drop a fetch already in flight. Only what the client actually reads
  // is worth rebuilding for.
  // A digest, not the values: this is kept for the lifetime of the client and
  // there is no reason for a second plaintext copy of the token to live in it.
  const QString hash = QString::fromLatin1(
      QCryptographicHash::hash((host + QLatin1Char('\n') + token + QLatin1Char('\n') + channels).toUtf8(), QCryptographicHash::Sha256)
          .toHex());
  if(m_directoryConfigHash.value(providerId) == hash) {
    return m_directoryClients.value(providerId);
  }

  retireDirectoryClient(providerId);
  auto* client = new heap::integrations::MattermostClient(this);
  QStringList extra;
  const QStringList raw = channels.split(QLatin1Char(','), Qt::SkipEmptyParts);
  for(const QString& name : raw) {
    // People type "#backend" as often as "backend".
    QString trimmed = name.trimmed();
    while(trimmed.startsWith(QLatin1Char('#'))) {
      trimmed.remove(0, 1);
    }
    if(!trimmed.isEmpty()) {
      extra.append(trimmed);
    }
  }
  client->setConfig(host, token, extra);

  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
  const QString label = d ? d->displayName : providerId;
  connect(client, &heap::integrations::MattermostClient::connectionTested, this, [this, providerId, label](bool ok, const QString& error) {
    const QString message = ok ? tr_("int.connected").arg(label) : tr_("int.connectFailed").arg(label, providerReason(error));
    emit toast(message, ok ? QStringLiteral("success") : QStringLiteral("error"));
    emit integrationActionFinished(providerId, QStringLiteral("test"), ok, ok ? QString() : message);
  });
  connect(client,
          &heap::integrations::MattermostClient::contactsFetched,
          this,
          [this, providerId, label](const QVector<heap::integrations::ExternalContact>& contacts) {
            setSyncInFlight(providerId, false);
            const int changed = mergeExternalContacts(providerId, contacts);
            recordSyncHealth(providerId, true, static_cast<int>(contacts.size()), 0, QString());
            emit integrationActionFinished(providerId, QStringLiteral("sync"), true, QString());
            if(changed == 0) {
              emit toast(tr_("contacts.upToDate").arg(label));
              return;
            }
            emit toast(tr_("contacts.updated").arg(label).arg(changed));
          });
  connect(client, &heap::integrations::MattermostClient::failed, this, [this, providerId, label](int status, const QString& error) {
    // A session token dies after ~30 days, and a revoked one is a 401 too.
    // Saying "expired" beats repeating the same failure on every auto-sync.
    setSyncInFlight(providerId, false);
    recordSyncHealth(providerId, false, -1, status, error);
    if(status == 401) {
      disconnectIntegration(providerId);
      emit toast(tr_("int.sessionExpired").arg(label), QStringLiteral("warning"));
      emit integrationActionFinished(providerId, QStringLiteral("sync"), false, tr_("int.sessionExpired").arg(label));
      return;
    }
    const QString message = tr_("sync.failed").arg(label, providerReason(error));
    toastAndLog(message, QStringLiteral("error"), {}, QStringLiteral("settings:integrations"));
    emit integrationActionFinished(providerId, QStringLiteral("sync"), false, message);
  });

  m_directoryClients.insert(providerId, client);
  m_directoryConfigHash.insert(providerId, hash);
  return client;
}

QStringList AppController::dismissedContacts(const QString& providerId) const {
  return integrationConfig(providerId).value(QStringLiteral("dismissed")).toMap().value(activeProfileId()).toStringList();
}

void AppController::dismissExternalContact(const QString& providerId, const QString& externalId) {
  if(externalId.isEmpty()) {
    return;
  }
  QVariantMap byProfile = integrationConfig(providerId).value(QStringLiteral("dismissed")).toMap();
  QStringList ids = byProfile.value(activeProfileId()).toStringList();
  if(ids.contains(externalId)) {
    return;
  }
  ids.append(externalId);
  byProfile.insert(activeProfileId(), ids);
  setIntegrationField(providerId, QStringLiteral("dismissed"), byProfile);
}

void AppController::restoreExternalContact(const QString& providerId, const QString& externalId) {
  QVariantMap byProfile = integrationConfig(providerId).value(QStringLiteral("dismissed")).toMap();
  QStringList ids = byProfile.value(activeProfileId()).toStringList();
  if(ids.removeAll(externalId) == 0) {
    return;
  }
  byProfile.insert(activeProfileId(), ids);
  setIntegrationField(providerId, QStringLiteral("dismissed"), byProfile);
}

QStringList AppController::dismissedTasks(const QString& providerId) const {
  return integrationConfig(providerId).value(QStringLiteral("dismissedTasks")).toMap().value(activeProfileId()).toStringList();
}

void AppController::dismissExternalTask(const QString& providerId, const QString& externalId) {
  if(providerId.isEmpty() || externalId.isEmpty()) {
    return;
  }
  QVariantMap byProfile = integrationConfig(providerId).value(QStringLiteral("dismissedTasks")).toMap();
  QStringList ids = byProfile.value(activeProfileId()).toStringList();
  if(ids.contains(externalId)) {
    return;
  }
  ids.append(externalId);
  byProfile.insert(activeProfileId(), ids);
  setIntegrationField(providerId, QStringLiteral("dismissedTasks"), byProfile);
}

void AppController::restoreExternalTask(const QString& providerId, const QString& externalId) {
  if(providerId.isEmpty() || externalId.isEmpty()) {
    return;
  }
  QVariantMap byProfile = integrationConfig(providerId).value(QStringLiteral("dismissedTasks")).toMap();
  QStringList ids = byProfile.value(activeProfileId()).toStringList();
  if(ids.removeAll(externalId) == 0) {
    return;
  }
  byProfile.insert(activeProfileId(), ids);
  setIntegrationField(providerId, QStringLiteral("dismissedTasks"), byProfile);
}

int AppController::mergeExternalContacts(const QString& providerId, const QVector<heap::integrations::ExternalContact>& contacts) {
  // Auto-sync fires wherever the user happens to be, so a background import
  // must only ever touch the profile the card was connected from. A manual
  // sync rebinds it — that is what "sync this here" means.
  const QString boundProfile = integrationConfig(providerId).value(QStringLiteral("profileId")).toString();
  if(boundProfile.isEmpty()) {
    setIntegrationField(providerId, QStringLiteral("profileId"), activeProfileId());
  } else if(boundProfile != activeProfileId()) {
    return 0;
  }

  QJsonObject docs = QJsonDocument::fromJson(m_docsState.toUtf8()).object();
  QJsonArray list = docs.value(QStringLiteral("contacts")).toArray();
  const QStringList dismissed = dismissedContacts(providerId);

  // Index what is already there. Match on the external id first; fall back to
  // the Mattermost handle so contacts typed by hand before the integration
  // existed get adopted instead of duplicated.
  QHash<QString, int> byExternalId;
  QHash<QString, int> byHandle;
  for(int i = 0; i < list.size(); ++i) {
    const QJsonObject c = list.at(i).toObject();
    const QString ext = c.value(QStringLiteral("mmId")).toString();
    if(!ext.isEmpty()) {
      // Already claimed by an external id; another user's handle must not be
      // able to take this row over.
      byExternalId.insert(ext, i);
      continue;
    }
    QString handle = c.value(QStringLiteral("mattermost")).toString().trimmed();
    while(handle.startsWith(QLatin1Char('@'))) {
      handle.remove(0, 1);
    }
    if(!handle.isEmpty()) {
      byHandle.insert(handle.toLower(), i);
    }
  }

  static const QStringList kPalette = {QStringLiteral("#d97a6c"),
                                       QStringLiteral("#dcb86b"),
                                       QStringLiteral("#dcc06a"),
                                       QStringLiteral("#7cc492"),
                                       QStringLiteral("#6cc4b8"),
                                       QStringLiteral("#5cc2dd"),
                                       QStringLiteral("#7da8d9"),
                                       QStringLiteral("#a4a4d6"),
                                       QStringLiteral("#c87fc7")};

  int changed = 0;
  for(const heap::integrations::ExternalContact& ext : contacts) {
    if(ext.externalId.isEmpty() || dismissed.contains(ext.externalId)) {
      continue;
    }
    // The handle index is a one-shot bridge for rows typed before the
    // integration existed: take() consumes the match, so a second server user
    // with the same handle is treated as the different person they are instead
    // of overwriting the first one's row.
    const QString handleKey = ext.username.toLower();
    int existing = -1;
    if(byExternalId.contains(ext.externalId)) {
      existing = byExternalId.value(ext.externalId);
    } else if(byHandle.contains(handleKey)) {
      // take(), not value(): QHash::take on a missing key would hand back 0 and
      // silently claim the first row.
      existing = byHandle.take(handleKey);
    }
    QJsonObject c = existing >= 0 ? list.at(existing).toObject() : QJsonObject{};
    const QJsonObject before = c;

    // The shadow is what the provider last said. A visible field is only
    // overwritten while it still matches that — so an edit of your own
    // survives, and a title change upstream still reaches you.
    QJsonObject shadow = c.value(QStringLiteral("mm")).toObject();
    const auto follow = [&c, &shadow](const QString& key, const QString& value) {
      if(value.isEmpty()) {
        return;
      }
      const QString current = c.value(key).toString();
      if(current.isEmpty() || current == shadow.value(key).toString()) {
        c.insert(key, value);
      }
      shadow.insert(key, value);
    };

    const QString handle = QLatin1Char('@') + ext.username;
    follow(QStringLiteral("name"), ext.displayName);
    follow(QStringLiteral("role"), ext.role);
    follow(QStringLiteral("mattermost"), handle);
    follow(QStringLiteral("channel"), ext.channelLabel.startsWith(QLatin1Char('#')) ? ext.channelLabel : QString());

    c.insert(QStringLiteral("mm"), shadow);
    c.insert(QStringLiteral("mmId"), ext.externalId);
    c.insert(QStringLiteral("source"), providerId);
    if(!c.contains(QStringLiteral("color"))) {
      // Stable across runs: qHash(QString) is seeded per process, so it would
      // give the same person a different colour in another profile.
      const QByteArray digest = QCryptographicHash::hash(ext.externalId.toUtf8(), QCryptographicHash::Sha1);
      c.insert(QStringLiteral("color"), kPalette.at(static_cast<uchar>(digest.at(0)) % kPalette.size()));
    }

    // People the user actually talks to are worth having in the rail and in
    // @-autocomplete; a channel roster is not.
    if(ext.channelLabel == QLatin1String("direct message")) {
      const QString personId = c.value(QStringLiteral("personId")).toString();
      if(personId.isEmpty() || m_people.indexOfId(personId) < 0) {
        const QString linked = upsertImportedPerson(ext, personId);
        if(!linked.isEmpty()) {
          c.insert(QStringLiteral("personId"), linked);
        }
      }
    }

    if(c == before) {
      continue;
    }
    ++changed;
    if(existing >= 0) {
      list.replace(existing, c);
    } else {
      // Append only: DocsView renders this array by index, and reordering it
      // under an open editor would retarget the row being edited.
      // Both indexes, or a second server user with the same handle would match
      // this new row by handle and overwrite it, losing the first one.
      byExternalId.insert(ext.externalId, static_cast<int>(list.size()));
      list.append(c);
    }
  }

  if(changed == 0) {
    return 0;
  }
  docs.insert(QStringLiteral("contacts"), list);
  setDocsState(QString::fromUtf8(QJsonDocument(docs).toJson(QJsonDocument::Compact)));
  scheduleSave();
  return changed;
}

QString AppController::upsertImportedPerson(const heap::integrations::ExternalContact& ext, const QString& linkedPersonId) {
  // The id is the Mattermost username, not a slug of the display name:
  // slugifyPersonName drops dots and dashes, so "olga.t" would become "olgat"
  // and @-mentions in heap would stop matching the handle people actually use.
  QString id = ext.username.toLower();
  static const QRegularExpression kUnsafe(QStringLiteral("[^a-z0-9._-]"));
  id.replace(kUnsafe, QString());
  if(id.isEmpty()) {
    return {};
  }
  // An id already in use is NOT automatically the same human: usernames are
  // sanitised down to [a-z0-9._-], so "ALEX", "al ex" and "alex!" all land on
  // "alex" — and a Person the user typed by hand may already own it. Only a
  // contact that already points at this id may reuse it; anyone else gets a
  // fresh one, or nothing at all rather than being welded onto a stranger.
  const bool idIsTaken = [this, &id]() {
    if(m_people.indexOfId(id) >= 0) {
      return true;  // present in the active profile right now, snapshot or not
    }
    for(const Profile& pr : m_profiles) {
      for(const Person& pe : pr.people) {
        if(pe.id == id) {
          return true;
        }
      }
    }
    return false;
  }();
  if(idIsTaken) {
    if(linkedPersonId == id) {
      return id;  // this contact's own Person — leave it exactly as it is
    }
    id = suggestPersonId(ext.username);
    if(id.isEmpty() || m_people.indexOfId(id) >= 0) {
      return {};
    }
  }

  Person p;
  p.id = id;
  p.name = ext.displayName.isEmpty() ? ext.username : ext.displayName;
  p.role = ext.role;
  // "idle", not "todo": an import is not a list of people you owe an answer to,
  // and the rail's pending badge would otherwise jump by however many
  // colleagues you have ever DM'd.
  p.state = QStringLiteral("idle");
  p.color = QColor(QStringLiteral("#7da8d9"));
  m_people.upsert(p);
  return id;
}

void AppController::retireDirectoryClient(const QString& providerId) {
  m_directoryConfigHash.remove(providerId);
  heap::integrations::MattermostClient* client = m_directoryClients.take(providerId);
  if(client == nullptr) {
    return;
  }
  // deleteLater, never delete: this runs from applyIntegrationSettings(), which
  // a 401 reaches synchronously through failed() → disconnectIntegration() →
  // appSettingsJsonChanged(). The client owns the QNetworkAccessManager that
  // owns the very QNetworkReply whose finished() is still on the stack, and Qt
  // forbids destroying it from there. It also gives a logout() queued a moment
  // earlier the chance to actually reach the socket.
  client->disconnect(this);
  client->deleteLater();
}

void AppController::fetchDirectory(const QString& providerId, bool rebindProfile) {
  if(rebindProfile) {
    // "Sync now" means "sync this, here". Auto-sync gets no say: it fires
    // wherever the user happens to be.
    setIntegrationField(providerId, QStringLiteral("profileId"), activeProfileId());
  }
  if(heap::integrations::MattermostClient* client = directoryClient(providerId)) {
    setSyncInFlight(providerId, true);
    client->fetchContacts();
  }
}

void AppController::connectWithCredentials(const QString& providerId, const QVariantMap& credentials) {
  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
  if(d == nullptr || d->loginFields.isEmpty()) {
    emit toast(tr_("int.noPassword"));
    emit integrationActionFinished(providerId, QStringLiteral("signin"), false, tr_("int.noPassword"));
    return;
  }
  const QString host = integrationConfig(providerId).value(QStringLiteral("host")).toString().trimmed();
  if(host.isEmpty()) {
    emit toast(tr_("int.needUrl").arg(d->displayName));
    emit integrationLoginFinished(providerId, false);
    emit integrationActionFinished(providerId, QStringLiteral("signin"), false, tr_("int.needUrl").arg(d->displayName));
    return;
  }

  // A throwaway client: it has no token yet, so it cannot be the cached one.
  auto* client = new heap::integrations::MattermostClient(this);
  client->setConfig(host, QString(), {});
  const QString label = d->displayName;
  connect(client,
          &heap::integrations::MattermostClient::loggedIn,
          this,
          [this, providerId, label, client](bool ok, const QString& token, const QString& error) {
            client->deleteLater();
            emit integrationLoginFinished(providerId, ok);
            if(!ok) {
              const QString message = tr_("int.signInFailed").arg(label, providerReason(error));
              emit toast(message, QStringLiteral("error"));
              emit integrationActionFinished(providerId, QStringLiteral("signin"), false, message);
              return;
            }
            if(m_secretStore) {
              m_secretStore->setValue(providerId, QStringLiteral("token"), token);
            }
            // authMode=session is what makes Disconnect end the server-side
            // session and drop the token, the way it does for a browser one.
            setIntegrationFields(providerId,
                                 {
                                     {QStringLiteral("authMode"), QStringLiteral("session")},
                                     {QStringLiteral("connected"), true},
                                 });
            emit integrationSecretsChanged();
            emit toast(tr_("int.connected").arg(label));
            emit integrationActionFinished(providerId, QStringLiteral("signin"), true, QString());
          });
  client->login(credentials.value(QStringLiteral("loginId")).toString(),
                credentials.value(QStringLiteral("password")).toString(),
                credentials.value(QStringLiteral("mfaToken")).toString());
}

QStringList AppController::missingRequiredFields(const QString& providerId) const {
  // A browser sign-in fills some required fields from the app's own identity
  // rather than from anything the user can type. Trello is the case in point:
  // its "API key" *is* the baked client ID (makeBespokeProvider substitutes it),
  // so asking for it after a successful sign-in demands something the user has
  // no way to obtain and makes a working card look broken.
  // The flow cannot have run without a client ID, so if authMode says it did,
  // that field is answered however the ID was obtained.
  return missingRequiredFields(providerId,
                               integrationConfig(providerId).value(QStringLiteral("authMode")).toString() == QStringLiteral("oauth"));
}

QStringList AppController::missingRequiredFields(const QString& providerId, bool signedInViaBrowser) const {
  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
  if(d == nullptr) {
    return {};
  }
  const QVariantMap cfg = integrationConfig(providerId);
  const QString browserSuppliedField = signedInViaBrowser ? d->oauth.clientIdParam : QString();

  // Fields only a hand-filled card has to provide (Jira's base URL) join the
  // list exactly when there is no browser sign-in to inherit them from.
  QStringList keys = d->requiredKeys;
  if(!signedInViaBrowser) {
    for(const QString& key : d->manualRequiredKeys) {
      if(!keys.contains(key)) {
        keys.append(key);
      }
    }
  }

  QStringList missing;
  for(const QString& key : keys) {
    if(!cfg.value(key).toString().trimmed().isEmpty()) {
      continue;
    }
    if(!browserSuppliedField.isEmpty() && key == browserSuppliedField) {
      continue;
    }
    // Report the card's own label ("Workspace GID"), not the config key.
    QString label = key;
    for(const heap::integrations::FieldSpec& f : d->uiFields) {
      if(f.key == key) {
        label = f.label;
        break;
      }
    }
    missing.append(label);
  }
  return missing;
}

QVariantMap AppController::integrationConfig(const QString& providerId) const {
  QVariantMap cfg = settingsMap().value("integrations").toMap().value(providerId).toMap();
  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
  if(d && m_secretStore) {
    for(const QString& field : d->secretKeys) {
      cfg.insert(field, m_secretStore->value(providerId, field));
    }
    // The refresh token is a secret no card ever shows, so it is not one of the
    // descriptor's secretKeys — and without this it never reached the config,
    // leaving ensureFreshToken / refreshOAuthToken / the 401 retry with nothing
    // to spend: an OAuth session went quiet as soon as its access token expired.
    if(d->oauth.supported) {
      cfg.insert(QStringLiteral("refreshToken"), m_secretStore->value(providerId, QStringLiteral("refreshToken")));
    }
  }
  return cfg;
}

void AppController::migrateLegacySecrets() {
  if(!m_secretStore) {
    return;
  }
  QVariantMap settings = settingsMap();
  QVariantMap integrations = settings.value("integrations").toMap();
  bool changed = false;
  for(const heap::integrations::ProviderDescriptor& d : heap::integrations::providerCatalog()) {
    if(!integrations.contains(d.id)) {
      continue;
    }
    QVariantMap cfg = integrations.value(d.id).toMap();
    for(const QString& field : d.secretKeys) {
      const QString existing = cfg.value(field).toString();
      if(!existing.isEmpty()) {
        m_secretStore->setValue(d.id, field, existing);
        cfg.remove(field);
        changed = true;
      }
    }
    integrations.insert(d.id, cfg);
  }
  if(changed) {
    settings.insert(QStringLiteral("integrations"), integrations);
    m_appSettingsJson = QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson(QJsonDocument::Compact);
    emit appSettingsJsonChanged();
    scheduleSave();
  }
}

void AppController::syncProvider(const QString& providerId) {
  // One card's "Sync now" is a run of its own: its new cards are what
  // `is:new` shows next (APP-180).
  ++m_syncRunSerial;
  if(m_directoryClients.contains(providerId)) {
    fetchDirectory(providerId, /*rebindProfile=*/true);
    return;
  }
  for(const auto& provider : m_syncProviders) {
    if(provider->id() == providerId) {
      // "Sync now" means "sync this, here" — the same rebind the directory
      // path does, so the tracker follows a deliberate click to this profile.
      setIntegrationField(providerId, QStringLiteral("profileId"), activeProfileId());
      syncProviderNow(providerId);
      return;
    }
  }
  emit toast(tr_("sync.noTracker"));
  emit integrationActionFinished(providerId, QStringLiteral("sync"), false, tr_("sync.noTracker"));
}

void AppController::testIntegration(const QString& providerId) {
  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
  if(!d) {
    emit toast(tr_("int.unknown"));
    emit integrationActionFinished(providerId, QStringLiteral("test"), false, tr_("int.unknown"));
    return;
  }
  if(d->kind == heap::integrations::ProviderKind::Directory) {
    heap::integrations::MattermostClient* client = directoryClient(providerId);
    if(client == nullptr) {
      emit toast(tr_("int.notConfigured").arg(d->displayName));
      emit integrationActionFinished(providerId, QStringLiteral("test"), false, tr_("int.notConfigured").arg(d->displayName));
      return;
    }
    client->testConnection();
    return;
  }
  const QVariantMap cfg = integrationConfig(providerId);
  heap::integrations::IntegrationProvider* provider = nullptr;
  if(d->bespoke) {
    provider = heap::integrations::makeBespokeProvider(providerId, cfg, this).release();
  } else {
    auto* rest = new heap::integrations::RestIssueProvider(*d, this);
    rest->setConfig(cfg);
    provider = rest;
  }
  if(!provider) {
    emit toast(tr_("int.notConfigured").arg(d->displayName));
    emit integrationActionFinished(providerId, QStringLiteral("test"), false, tr_("int.notConfigured").arg(d->displayName));
    return;
  }
  const QString label = d->displayName;
  connect(provider,
          &heap::integrations::IntegrationProvider::connectionTested,
          this,
          [this, provider, providerId, label](bool ok, const QString& error) {
            const QString message = ok ? tr_("int.connected").arg(label) : tr_("int.connectFailed").arg(label, providerReason(error));
            emit toast(message, ok ? QStringLiteral("success") : QStringLiteral("error"));
            emit integrationActionFinished(providerId, QStringLiteral("test"), ok, ok ? QString() : message);
            provider->deleteLater();
          });
  provider->testConnection();
}

QString AppController::providerDisplayName(const QString& providerId) const {
  for(const heap::integrations::ProviderDescriptor& d : heap::integrations::providerCatalog()) {
    if(d.id == providerId) {
      return d.displayName;
    }
  }
  return providerId;
}

QVariantMap AppController::providerBadges() const {
  QVariantMap out;
  for(const heap::integrations::ProviderDescriptor& d : heap::integrations::providerCatalog()) {
    out.insert(d.id,
               QVariantMap{{QStringLiteral("name"), d.displayName}, {QStringLiteral("icon"), d.icon}, {QStringLiteral("color"), d.color}});
  }
  return out;
}

QUrl AppController::externalUrlFor(const QString& taskId) const {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return {};
  }
  const QUrl url(m_tasks.items().at(row).externalUrl);
  // The URL came from a tracker, and a Jira session that never learned its site
  // yields a bare "/browse/KEY". Only ever hand the browser a web address.
  if(!url.isValid() || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
    return {};
  }
  return url;
}

bool AppController::openTaskExternal(const QString& taskId) {
  const QUrl url = externalUrlFor(taskId);
  if(url.isEmpty()) {
    emit toast(tr_("ticket.noLink"));
    return false;
  }
  QDesktopServices::openUrl(url);
  return true;
}

void AppController::fetchTicketComments(const QString& taskId) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    emit ticketCommentsLoaded(taskId, {}, QStringLiteral("unknown task"));
    return;
  }
  const Task& t = m_tasks.items().at(row);
  if(t.externalProvider.isEmpty() || t.externalId.isEmpty()) {
    emit ticketCommentsLoaded(taskId, {}, QStringLiteral("not a ticket"));
    return;
  }
  const QString providerId = t.externalProvider;
  const QString externalId = t.externalId;
  // The issue's own repo, which after a cross-project pull is not the one in
  // the settings card.
  const QString project = t.externalMeta.project;
  const QString issueUrl = t.externalUrl;

  ensureFreshToken(providerId, [this, taskId, providerId, externalId, project, issueUrl]() {
    for(const auto& provider : m_syncProviders) {
      if(provider->id() != providerId) {
        continue;
      }
      // One connection per fetch, dropped as soon as it answers: the reply
      // belongs to this request and nothing keeps the comments afterwards.
      auto* guard = new QObject(this);
      connect(provider.get(),
              &heap::integrations::IntegrationProvider::commentsFetched,
              guard,
              [this, guard, provider = provider.get(), taskId, externalId, issueUrl](
                  const QString& id, const QVector<heap::integrations::ExternalComment>& comments, const QString& error) {
                if(id != externalId) {
                  return;  // another ticket's reply on the same provider
                }
                guard->deleteLater();
                const int at = m_tasks.indexOfId(taskId);
                if(error.isEmpty() && at >= 0) {
                  // Kept for reading offline, and counted on the card.
                  const int total = provider->lastCommentTotal();
                  Task t = m_tasks.items().at(at);
                  storeTicketComments(t, comments, total);
                  const int count = total >= 0 ? total : static_cast<int>(comments.size());
                  if(t.externalMeta.commentCount != count) {
                    t.externalMeta.commentCount = count;
                    const QScopedValueRollback<bool> fromTracker(m_historySync, true);
                    m_tasks.upsert(t);
                    scheduleSave();
                  }
                }
                QVariantList out;
                out.reserve(comments.size());
                for(const heap::integrations::ExternalComment& c : comments) {
                  // Every comment links somewhere: its own URL, an anchor on
                  // the issue page, or at worst the issue itself.
                  QString url = c.url;
                  if(url.isEmpty() && !issueUrl.isEmpty()) {
                    url = issueUrl + c.anchor;
                  }
                  out.append(QVariantMap{{QStringLiteral("author"), c.author},
                                         {QStringLiteral("body"), at >= 0 ? trackerTextAsMarkdown(m_tasks.items().at(at), c.body) : c.body},
                                         {QStringLiteral("createdAt"), c.createdAt},
                                         {QStringLiteral("url"), url}});
                }
                emit ticketCommentsLoaded(taskId, out, providerReason(error));
              });
      provider->fetchComments(externalId, project);
      return;
    }
    emit ticketCommentsLoaded(taskId, {}, tr_("ticket.notConnected"));
  });
}

namespace {

// The card whose CI a task shows (APP-318): a merge / pull request itself, or
// for a Jira card the open MR folded into it (else the last one).
const Task* ciSource(const QVector<Task>& tasks, const Task& t) {
  const auto kind = [](const Task& x) {
    return x.externalMeta.details.value(QStringLiteral("kind")).toString();
  };
  if(kind(t) == QLatin1String("mr") || kind(t) == QLatin1String("pr")) {
    return &t;
  }
  const Task* found = nullptr;
  for(const Task& x : tasks) {
    if(x.externalMeta.details.value(QStringLiteral("foldedInto")).toString() != t.id) {
      continue;
    }
    const QString state = x.externalMeta.details.value(QStringLiteral("state")).toString();
    if(state == QLatin1String("open") || state == QLatin1String("opened")) {
      return &x;
    }
    found = &x;
  }
  return found;
}

}  // namespace

QVariantMap AppController::ciOf(const QString& taskId) const {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return {};
  }
  const Task* src = ciSource(m_tasks.items(), m_tasks.items().at(row));
  if(src == nullptr) {
    return {};
  }
  const QJsonObject d = src->externalMeta.details;
  return {{QStringLiteral("state"), d.value(QStringLiteral("pipeline")).toString()},
          {QStringLiteral("url"), d.value(QStringLiteral("pipelineUrl")).toString()},
          {QStringLiteral("at"), d.value(QStringLiteral("pipelineAt")).toString()},
          {QStringLiteral("hasRef"), !d.value(QStringLiteral("ciRef")).toString().isEmpty()},
          {QStringLiteral("source"), src->id}};
}

bool AppController::isCiErrorLine(const QString& line) const {
  return heap::ci::isErrorLine(line);
}

void AppController::fetchCiJobs(const QString& taskId) {
  const int row = m_tasks.indexOfId(taskId);
  const Task* src = row >= 0 ? ciSource(m_tasks.items(), m_tasks.items().at(row)) : nullptr;
  if(src == nullptr) {
    emit ciJobsLoaded(taskId, {}, QStringLiteral("no pipeline"));
    return;
  }
  const QString providerId = src->externalProvider;
  const QString externalId = src->externalId;
  const QString project = src->externalMeta.project;
  const QString ref = src->externalMeta.details.value(QStringLiteral("ciRef")).toString();
  ensureFreshToken(providerId, [this, taskId, providerId, externalId, project, ref]() {
    for(const auto& provider : m_syncProviders) {
      if(provider->id() != providerId) {
        continue;
      }
      auto* guard = new QObject(this);
      connect(provider.get(),
              &heap::integrations::IntegrationProvider::ciJobsFetched,
              guard,
              [this, guard, taskId, externalId](const QString& id, const QVariantList& jobs, const QString& error) {
                if(id != externalId) {
                  return;
                }
                guard->deleteLater();
                emit ciJobsLoaded(taskId, jobs, providerReason(error));
              });
      provider->fetchCiJobs(externalId, project, ref);
      return;
    }
    emit ciJobsLoaded(taskId, {}, tr_("ticket.notConnected"));
  });
}

void AppController::fetchCiLog(const QString& taskId, const QString& jobId) {
  const int row = m_tasks.indexOfId(taskId);
  const Task* src = row >= 0 ? ciSource(m_tasks.items(), m_tasks.items().at(row)) : nullptr;
  if(src == nullptr) {
    emit ciLogLoaded(taskId, jobId, QString(), QStringLiteral("no pipeline"));
    return;
  }
  const QString providerId = src->externalProvider;
  const QString externalId = src->externalId;
  const QString project = src->externalMeta.project;
  ensureFreshToken(providerId, [this, taskId, jobId, providerId, externalId, project]() {
    for(const auto& provider : m_syncProviders) {
      if(provider->id() != providerId) {
        continue;
      }
      auto* guard = new QObject(this);
      connect(provider.get(),
              &heap::integrations::IntegrationProvider::ciLogFetched,
              guard,
              [this, guard, taskId, externalId, jobId](const QString& id, const QString& job, const QString& text, const QString& error) {
                if(id != externalId || job != jobId) {
                  return;
                }
                guard->deleteLater();
                emit ciLogLoaded(taskId, jobId, text, providerReason(error));
              });
      provider->fetchCiLog(externalId, project, jobId);
      return;
    }
    emit ciLogLoaded(taskId, jobId, QString(), tr_("ticket.notConnected"));
  });
}

QVariantList AppController::integrationCatalog() const {
  QVariantList out;
  for(const heap::integrations::ProviderDescriptor& d : heap::integrations::providerCatalog()) {
    QVariantMap m;
    m.insert(QStringLiteral("id"), d.id);
    m.insert(QStringLiteral("name"), d.displayName);
    m.insert(QStringLiteral("color"), d.color);
    m.insert(QStringLiteral("icon"), d.icon);
    m.insert(QStringLiteral("descKey"), d.descKey);
    m.insert(QStringLiteral("oauth"), d.oauth.supported);
    // oauthReady = this build can run the flow with no help from the user, so
    // "Connect with browser" is truly one-click. That needs a baked-in client
    // ID, a baked-in secret when the provider refuses public clients (a build
    // made without the CI credentials has neither), and — for the device grant
    // — a Qt new enough to run it.
    const bool haveCredentials =
        !d.oauth.clientId.isEmpty() && (!d.oauth.needsSecret || !d.oauth.clientSecret.isEmpty()) &&
        (d.oauth.effectiveFlow() != heap::integrations::OAuthFlow::Device || heap::integrations::OAuthManager::deviceFlowAvailable());
    m.insert(QStringLiteral("oauthReady"), d.oauth.supported && haveCredentials);
    m.insert(QStringLiteral("oauthNeedsSecret"), d.oauth.needsSecret);
    QVariantList fields;
    for(const heap::integrations::FieldSpec& f : d.uiFields) {
      QVariantMap fm;
      fm.insert(QStringLiteral("key"), f.key);
      fm.insert(QStringLiteral("label"), f.label);
      fm.insert(QStringLiteral("placeholder"), f.placeholder);
      fm.insert(QStringLiteral("mono"), f.mono);
      fm.insert(QStringLiteral("secret"), f.secret);
      fields.append(fm);
    }
    m.insert(QStringLiteral("fields"), fields);
    // Password sign-in inputs are a separate list on purpose: the card must
    // never route them through the same commit path as uiFields, which writes
    // secrets to the keychain.
    QVariantList loginFields;
    for(const heap::integrations::FieldSpec& f : d.loginFields) {
      QVariantMap fm;
      fm.insert(QStringLiteral("key"), f.key);
      fm.insert(QStringLiteral("label"), f.label);
      fm.insert(QStringLiteral("placeholder"), f.placeholder);
      fm.insert(QStringLiteral("mono"), f.mono);
      fm.insert(QStringLiteral("secret"), f.secret);
      loginFields.append(fm);
    }
    m.insert(QStringLiteral("loginFields"), loginFields);
    m.insert(QStringLiteral("directory"), d.kind == heap::integrations::ProviderKind::Directory);
    // Whether the card offers "change the status in …" (APP-243).
    m.insert(QStringLiteral("writesStatus"), heap::integrations::writesStatus(d));
    // Merge / pull requests (APP-242): the config keys the card's switches
    // write, absent for a provider that has none.
    if(!d.reviewLists.isEmpty() && d.reviewParser != nullptr) {
      m.insert(QStringLiteral("reviewEnabledKey"), d.reviewEnabledKey);
      m.insert(QStringLiteral("reviewRolesKey"), d.reviewRolesKey);
      m.insert(QStringLiteral("reviewRolesDefault"), d.reviewRolesDefault);
    }
    out.append(m);
  }
  return out;
}

QString AppController::integrationSecret(const QString& providerId, const QString& field) const {
  return m_secretStore ? m_secretStore->value(providerId, field) : QString();
}

bool AppController::hasIntegrationSecret(const QString& providerId, const QString& field) const {
  return m_secretStore && m_secretStore->has(providerId, field);
}

void AppController::setIntegrationSecret(const QString& providerId, const QString& field, const QString& value) {
  if(m_secretStore) {
    m_secretStore->setValue(providerId, field, value);
  }
  // Pasting a personal access token over a browser session ends that session.
  // Leaving authMode=oauth behind would send the new token as a Bearer, which
  // GitLab (PRIVATE-TOKEN) and ClickUp (raw Authorization) both reject.
  if(field == QStringLiteral("token") &&
     integrationConfig(providerId).value(QStringLiteral("authMode")).toString() == QStringLiteral("oauth")) {
    if(m_secretStore) {
      m_secretStore->remove(providerId, QStringLiteral("refreshToken"));
    }
    setIntegrationFields(providerId, {{QStringLiteral("authMode"), QString()}, {QStringLiteral("tokenExpiresAt"), QString()}});
  }
  applyIntegrationSettings();
  emit integrationSecretsChanged();
}

void AppController::connectIntegrationManually(const QString& providerId) {
  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
  if(d == nullptr) {
    emit toast(tr_("int.unknown"));
    return;
  }
  // Validated as a token card, never as an OAuth one: this connect is what
  // turns the card into a token card, so a field the browser flow would have
  // supplied (Trello's API key) is the user's to provide here.
  const QStringList missing = missingRequiredFields(providerId, /*signedInViaBrowser=*/false);
  if(!missing.isEmpty()) {
    // Silence was the old failure mode — "connected" went true, no provider
    // could be built from the half-filled config, and nothing ever synced.
    emit toast(tr_("int.needs").arg(d->displayName, missing.join(QStringLiteral(", "))));
    emit integrationNeedsFields(providerId, missing);
    return;
  }

  QVariantMap fields{{QStringLiteral("connected"), true}};
  // Typed credentials are not a browser session. Leaving authMode=oauth behind
  // would keep sending the old access token as a Bearer and — on Jira — keep
  // routing every call through the api.atlassian.com gateway, so a self-hosted
  // base URL typed right here would never be used.
  if(integrationConfig(providerId).value(QStringLiteral("authMode")).toString() == QStringLiteral("oauth")) {
    if(m_secretStore) {
      m_secretStore->remove(providerId, QStringLiteral("refreshToken"));
    }
    fields.insert(QStringLiteral("authMode"), QString());
    fields.insert(QStringLiteral("tokenExpiresAt"), QString());
  }
  setIntegrationFields(providerId, fields);
  emit integrationSecretsChanged();
  emit toast(tr_("int.connected").arg(d->displayName));
}

void AppController::disconnectIntegration(const QString& providerId) {
  const QVariantMap cfg = integrationConfig(providerId);
  const QString authMode = cfg.value(QStringLiteral("authMode")).toString();
  // End the server-side session before dropping the token, or it stays alive
  // for its full lifetime (30 days on a default Mattermost).
  if(authMode == QStringLiteral("session")) {
    if(heap::integrations::MattermostClient* client = m_directoryClients.value(providerId)) {
      client->logout();
    }
  }
  QVariantMap fields{{QStringLiteral("connected"), false}};
  // A PAT is the user's own credential and is left where it is, so "disconnect,
  // reconnect" doesn't mean "paste the token again". Tokens this app obtained
  // are ours to drop.
  if(!authMode.isEmpty()) {
    if(m_secretStore) {
      m_secretStore->remove(providerId, QStringLiteral("token"));
      m_secretStore->remove(providerId, QStringLiteral("refreshToken"));
    }
    fields.insert(QStringLiteral("authMode"), QString());
    fields.insert(QStringLiteral("tokenExpiresAt"), QString());
  }
  setIntegrationFields(providerId, fields);
  emit integrationSecretsChanged();
}

void AppController::setIntegrationField(const QString& providerId, const QString& field, const QVariant& value) {
  setIntegrationFields(providerId, {{field, value}});
}

void AppController::setIntegrationFields(const QString& providerId, const QVariantMap& fields) {
  QVariantMap settings = settingsMap();
  QVariantMap integrations = settings.value(QStringLiteral("integrations")).toMap();
  QVariantMap cfg = integrations.value(providerId).toMap();
  for(auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
    // An empty string means "drop this key", so a cleared authMode doesn't stay
    // in state.json as "".
    if(it.value().typeId() == QMetaType::QString && it.value().toString().isEmpty()) {
      cfg.remove(it.key());
    } else {
      cfg.insert(it.key(), it.value());
    }
  }
  integrations.insert(providerId, cfg);
  settings.insert(QStringLiteral("integrations"), integrations);
  m_appSettingsJson = QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson(QJsonDocument::Compact);
  emit appSettingsJsonChanged();  // rebuilds providers + refreshes the QML settings copy
  scheduleSave();
}

void AppController::ensureFreshToken(const QString& providerId, std::function<void()> then, std::function<void()> onFail) {
  const QVariantMap cfg = integrationConfig(providerId);
  const bool isOAuth = cfg.value(QStringLiteral("authMode")).toString() == QStringLiteral("oauth");
  const QString refreshToken = cfg.value(QStringLiteral("refreshToken")).toString();
  const QDateTime expiresAt = heap::integrations::expiryFromString(cfg.value(QStringLiteral("tokenExpiresAt")).toString());
  // PAT providers, non-expiring tokens and sessions with nothing to refresh
  // with all go straight through.
  if(!isOAuth || refreshToken.isEmpty() || !heap::integrations::tokenNeedsRefresh(expiresAt)) {
    then();
    return;
  }
  refreshOAuthToken(providerId, [then = std::move(then), onFail = std::move(onFail)](bool ok) {
    if(ok) {
      then();
    } else if(onFail) {
      onFail();
    }
  });
}

QString AppController::providerReason(const QString& reason) const {
  return heap::integrations::translateProviderReason(reason, m_language == QStringLiteral("ru"));
}

QVariantMap AppController::integrationStates() const {
  QVariantMap out;
  QHash<QString, int> outOfScope;
  for(const Task& t : m_tasks.items()) {
    if(t.externalMeta.outOfScope && !t.archived && !t.externalProvider.isEmpty()) {
      ++outOfScope[t.externalProvider];
    }
  }
  QSet<QString> ids = m_offlineProviders;
  for(auto it = outOfScope.constBegin(); it != outOfScope.constEnd(); ++it) {
    ids.insert(it.key());
  }
  for(const QString& id : ids) {
    out.insert(id,
               QVariantMap{
                   {QStringLiteral("offline"), m_offlineProviders.contains(id)},
                   {QStringLiteral("outOfScope"), outOfScope.value(id)},
               });
  }
  return out;
}

void AppController::recordTaskChange(const Task* before, const Task& after) {
  if(m_loading || after.id.isEmpty()) {
    return;
  }
  const QString profileId = activeProfileId();
  // Undo putting a deleted task back is not its creation.
  if(before == nullptr && m_history.count(profileId, after.id) > 0) {
    return;
  }
  // What a pull brings happened in the tracker no later than the issue's
  // last update, which may be days before the pull (dogfood 2026-10-19):
  // dated so, never before the task's previous event, never in the future.
  QDateTime at = QDateTime::currentDateTime();
  if(m_historySync && before != nullptr && after.externalMeta.updatedAt.isValid() && after.externalMeta.updatedAt < at) {
    const QVector<heap::history::HistoryEvent> past = m_history.events(profileId, after.id);
    const QDateTime floor = past.isEmpty() ? QDateTime() : past.constLast().at;
    at = floor.isValid() ? qMax(floor, after.externalMeta.updatedAt) : after.externalMeta.updatedAt;
  }
  const QVector<heap::history::HistoryEvent> events = heap::history::diffTask(before, after, at, m_historySync);
  if(events.isEmpty()) {
    return;
  }
  for(const heap::history::HistoryEvent& e : events) {
    m_history.append(profileId, after.id, e);
  }
  emit taskHistoryChanged(after.id);
}

int AppController::applyTrackerStatusChanges(const QString& providerId, const QVector<heap::integrations::ExternalStatusChange>& changes) {
  namespace st = heap::integrations::statustime;
  const QString pid = activeProfileId();
  const QDateTime now = QDateTime::currentDateTime();
  int changed = 0;
  bool logMoved = false;
  QStringList retimed;
  for(const heap::integrations::ExternalStatusChange& c : changes) {
    if(c.externalId.isEmpty() || c.to.isEmpty() || !c.at.isValid()) {
      continue;
    }
    // A tracker clock a little ahead is still not the future.
    const QDateTime at = qMin(c.at, now);
    for(int row = 0; row < m_tasks.items().size(); ++row) {
      const Task& cur = m_tasks.items().at(row);
      if(cur.externalProvider != providerId || cur.externalId != c.externalId) {
        continue;
      }
      // Moved again since the pull that asked: the next one asks again.
      if(cur.externalMeta.status.compare(c.to, Qt::CaseInsensitive) != 0) {
        continue;
      }
      Task t = cur;
      st::setSince(t.externalMeta.details, t.externalMeta.status, at);
      // Only a card in the column the tracker put it in entered it when the
      // tracker moved it; one the user placed here keeps their time. And
      // only ever earlier: the wrong dates were the late ones.
      if(t.status == t.externalMeta.column && (!t.statusChangedAt.isValid() || at < t.statusChangedAt)) {
        const QDateTime wrong = t.statusChangedAt;
        t.statusChangedAt = at;
        logMoved = heap::recap::retime(m_statusLog, t.id, wrong, at) || logMoved;
        if(m_history.retimeLastSync(pid, t.id, QStringLiteral("status"), t.status, at)) {
          retimed.append(t.id);
        }
      }
      if(!(t == cur)) {
        m_tasks.upsert(t);
        ++changed;
      }
    }
  }
  for(const QString& id : std::as_const(retimed)) {
    emit taskHistoryChanged(id);
  }
  if(changed > 0 || logMoved) {
    scheduleSave();
  }
  return changed;
}

void AppController::countStatusTimeTries(const QString& providerId,
                                         const QStringList& asked,
                                         const QVector<heap::integrations::ExternalStatusChange>& answered) {
  namespace st = heap::integrations::statustime;
  // An empty answer to a batch of keys that all exist is a failed request,
  // not a verdict on each issue: nothing is counted.
  if(asked.isEmpty() || answered.isEmpty()) {
    return;
  }
  QSet<QString> got;
  for(const heap::integrations::ExternalStatusChange& c : answered) {
    got.insert(c.externalId);
  }
  bool changed = false;
  for(int row = 0; row < m_tasks.items().size(); ++row) {
    const Task& cur = m_tasks.items().at(row);
    if(cur.externalProvider != providerId || !asked.contains(cur.externalId) || got.contains(cur.externalId)) {
      continue;
    }
    const QString status = cur.externalMeta.status;
    if(status.isEmpty() || st::since(cur.externalMeta.details, status).isValid()) {
      continue;
    }
    Task t = cur;
    st::setTries(t.externalMeta.details, status, st::tries(cur.externalMeta.details, status) + 1);
    m_tasks.upsert(t);
    changed = true;
  }
  if(changed) {
    scheduleSave();
  }
}

QVariantList AppController::taskHistory(const QString& taskId) const {
  return heap::history::TaskHistory::toVariant(m_history.events(activeProfileId(), taskId));
}

void AppController::recordSyncHealth(const QString& providerId, bool ok, int items, int httpStatus, const QString& error) {
  heap::integrations::ProviderHealth& h = m_syncHealth[providerId];
  const QDateTime now = QDateTime::currentDateTime();
  if(ok) {
    h.recordOk(now, items);
  } else {
    h.recordFailure(now, httpStatus, error);
  }
  emit integrationHealthChanged();
}

QVariantList AppController::integrationHealth() const {
  return integrationHealthAt(QDateTime::currentDateTime());
}

QVariantList AppController::integrationHealthAt(const QDateTime& now) const {
  using namespace heap::integrations;
  const bool ru = m_language == QStringLiteral("ru");
  const QVariantMap integrations = settingsMap().value(QStringLiteral("integrations")).toMap();
  // Status moves waiting for the tracker, per provider: the offline line
  // says how many changes wait (X-Err-Tracker).
  QHash<QString, int> waiting;
  for(const Task& t : m_tasks.items()) {
    if(!t.externalProvider.isEmpty() && t.externalMeta.pushQueued && !t.externalMeta.unsyncedStatus.isEmpty()) {
      ++waiting[t.externalProvider];
    }
  }
  QVariantList out;
  for(const ProviderDescriptor& d : providerCatalog()) {
    const QVariantMap cfg = integrations.value(d.id).toMap();
    if(!cfg.value(QStringLiteral("connected"), false).toBool()) {
      continue;
    }
    const ProviderHealth h = m_syncHealth.value(d.id);
    const bool failing = h.failing();
    static const char* const kKinds[] = {"", "auth", "forbidden", "notFound", "rateLimited", "server", "network", "other"};
    out.append(QVariantMap{
        {QStringLiteral("id"), d.id},
        {QStringLiteral("name"), d.displayName},
        {QStringLiteral("lastOk"), relativeAge(h.lastOk, now, ru)},
        {QStringLiteral("items"), h.lastItems},
        {QStringLiteral("failing"), failing},
        {QStringLiteral("error"), failing ? failureText(h.lastFailure, ru) : QString()},
        {QStringLiteral("errorDetail"), failing ? providerReason(h.lastError) : QString()},
        {QStringLiteral("errorAge"), failing ? relativeAge(h.lastFailureAt, now, ru) : QString()},
        {QStringLiteral("expiry"), expiryText(expiryFromString(cfg.value(QStringLiteral("tokenExpiresAt")).toString()), now, ru)},
        {QStringLiteral("offline"), m_offlineProviders.contains(d.id)},
        // For the problem strip and the sync indicator (R2-035/036/048):
        // what kind of failure, when (clock time), whether a pull is out now,
        // whether this tracker ever answered, and the moves waiting for it.
        {QStringLiteral("kind"), failing ? QString::fromLatin1(kKinds[static_cast<int>(h.lastFailure)]) : QString()},
        {QStringLiteral("failedAt"),
         failing && h.lastFailureAt.isValid() ? heap::text::formatTime(h.lastFailureAt.time(), twelveHourClock()) : QString()},
        {QStringLiteral("failedAtMs"), failing && h.lastFailureAt.isValid() ? double(h.lastFailureAt.toMSecsSinceEpoch()) : 0.0},
        {QStringLiteral("inFlight"), m_syncInFlight.contains(d.id)},
        {QStringLiteral("everOk"), h.lastOk.isValid()},
        {QStringLiteral("waiting"), waiting.value(d.id)},
    });
  }
  return out;
}

void AppController::setProviderOffline(const QString& providerId, bool offline) {
  if(offline == m_offlineProviders.contains(providerId)) {
    return;
  }
  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
  const QString label = d ? d->displayName : providerId;
  if(offline) {
    m_offlineProviders.insert(providerId);
    emit toast(tr_("int.offline").arg(label));
  } else {
    m_offlineProviders.remove(providerId);
    m_refreshRetryMs.remove(providerId);
    emit toast(tr_("int.backOnline").arg(label));
  }
  emit integrationStatesChanged();
  emit integrationHealthChanged();
}

void AppController::scheduleRefreshRetry(const QString& providerId) {
  // 15 s, 30 s, 1 min … capped at 15 min: soon enough that a laptop waking
  // onto Wi-Fi is back within a sync, slow enough not to hammer a dead
  // endpoint all night.
  constexpr int kFirstMs = 15 * 1000;
  constexpr int kMaxMs = 15 * 60 * 1000;
  const int delay = m_refreshRetryMs.value(providerId, kFirstMs);
  m_refreshRetryMs.insert(providerId, qMin(delay * 2, kMaxMs));
  QTimer::singleShot(delay, this, [this, providerId]() {
    if(!m_offlineProviders.contains(providerId)) {
      return;  // back already, or signed out meanwhile
    }
    if(m_refreshing.contains(providerId)) {
      scheduleRefreshRetry(providerId);
      return;
    }
    refreshOAuthToken(providerId, [this, providerId](bool ok) {
      if(ok) {
        syncProviderNow(providerId);
      }
    });
  });
}

bool AppController::reviewCardsMovable(const QString& providerId) const {
  return integrationConfig(providerId).value(QStringLiteral("reviewMovable")).toBool();
}

QString AppController::scopeFingerprintFor(const QString& providerId) const {
  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
  if(d == nullptr) {
    return {};
  }
  return heap::integrations::scopeFingerprint(*d, integrationConfig(providerId));
}

void AppController::queueTrackerPush(const QString& taskId, const QString& status) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  m_tasks.setPushRuntime(taskId, false, QString());
  Task t = m_tasks.items().at(row);
  if(t.externalMeta.pushQueued && t.externalMeta.unsyncedStatus == status) {
    return;
  }
  t.externalMeta.unsyncedStatus = status;
  t.externalMeta.pushQueued = true;
  m_tasks.upsert(t);
  scheduleSave();
  if(m_bulkMoveDepth == 0) {
    const bool connected = integrationConfig(t.externalProvider).value(QStringLiteral("connected"), false).toBool();
    const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(t.externalProvider);
    const QString label = d ? d->displayName : t.externalProvider;
    emit toast(connected ? tr_("sync.queued").arg(externalKeyOf(t)) : tr_("sync.queuedDisconnected").arg(externalKeyOf(t), label));
  }
}

void AppController::flushQueuedPushes(const QString& providerId) {
  // With the tracker's switch off a queued move stays on the card as "not
  // sent", for the user to send or drop (APP-243). Never on its own.
  if(!trackerWriteEnabled(providerId)) {
    return;
  }
  QStringList ids;
  for(const Task& t : m_tasks.items()) {
    // A move held by a status conflict waits for the user, not for a sync.
    if(t.externalProvider == providerId && t.externalMeta.pushQueued && !t.externalMeta.conflicts.contains(QStringLiteral("status"))) {
      ids.append(t.id);
    }
  }
  for(const QString& id : ids) {
    const int row = m_tasks.indexOfId(id);
    if(row < 0) {
      continue;
    }
    Task t = m_tasks.items().at(row);
    // Sent now; the answer clears or re-flags it. The column is whatever the
    // card says today — a later local move supersedes the queued one.
    t.externalMeta.pushQueued = false;
    m_tasks.upsert(t);
    pushStatusToTracker(id, t.status);
  }
  if(!ids.isEmpty()) {
    scheduleSave();
  }
}

void AppController::resolveTrackerConflict(const QString& taskId, bool useTracker) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const QStringList fields = m_tasks.items().at(row).externalMeta.conflicts;
  resolveTrackerConflictFields(taskId, fields, useTracker);
}

void AppController::resolveTrackerConflictField(const QString& taskId, const QString& field, bool useTracker) {
  resolveTrackerConflictFields(taskId, {field}, useTracker);
}

namespace {

// One side of one conflicting field (APP-163). Returns true when the choice
// means the card's status has to go to the tracker now: keeping my status is
// keeping it *and sending it*, since the tracker still says otherwise.
// Taking the tracker's title or description no longer throws mine away: it
// goes to the card's notepad (APP-244, owner's rule — sync never drops a
// local edit). `ru` picks the header's language.
bool applyConflictChoice(Task& t, const QString& field, bool useTracker, bool ru) {
  if(useTracker) {
    if(field == QStringLiteral("title") && !t.externalMeta.title.isEmpty()) {
      if(t.title != t.externalMeta.title) {
        heap::local::appendNote(t.local, (ru ? QStringLiteral("Мой заголовок: ") : QStringLiteral("My title: ")) + t.title);
      }
      t.title = t.externalMeta.title;
    } else if(field == QStringLiteral("body")) {
      if(t.desc != t.externalMeta.body && !t.desc.trimmed().isEmpty()) {
        heap::local::appendNote(
            t.local, (ru ? QStringLiteral("## Моя версия описания\n\n") : QStringLiteral("## My version of the description\n\n")) + t.desc);
      }
      t.desc = t.externalMeta.body;
    } else if(field == QStringLiteral("priority") && !t.externalMeta.priority.isEmpty()) {
      t.priority = t.externalMeta.priority;
    } else if(field == QStringLiteral("status")) {
      if(!t.externalMeta.column.isEmpty() && t.status != t.externalMeta.column) {
        t.status = t.externalMeta.column;
        t.statusChangedAt = QDateTime::currentDateTime();
      }
      // The local move is dropped by the user's own choice: nothing to send.
      t.externalMeta.unsyncedStatus.clear();
      t.externalMeta.pushQueued = false;
    }
  }
  // Keeping mine for a text field only stops the flag: the base stays what
  // the tracker sent, so the next upstream change to it is flagged again.
  heap::integrations::setConflict(t.externalMeta.conflicts, field, false);
  return !useTracker && field == QStringLiteral("status");
}

}  // namespace

void AppController::resolveTrackerConflictFields(const QString& taskId, const QStringList& fields, bool useTracker) {
  if(useTracker) {
    resolveTrackerConflictChoices(taskId, {}, fields);
  } else {
    resolveTrackerConflictChoices(taskId, fields, {});
  }
}

void AppController::resolveTrackerConflictChoices(const QString& taskId, const QStringList& mine, const QStringList& theirs) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  QStringList keep;
  QStringList take;
  for(const QString& f : mine) {
    if(t.externalMeta.conflicts.contains(f) && !keep.contains(f)) {
      keep.append(f);
    }
  }
  for(const QString& f : theirs) {
    if(t.externalMeta.conflicts.contains(f) && !keep.contains(f) && !take.contains(f)) {
      take.append(f);
    }
  }
  if(keep.isEmpty() && take.isEmpty()) {
    return;
  }
  const bool useTracker = keep.isEmpty();
  bool send = false;
  {
    const UndoScope scope(this, tr_("task.editUndone").arg(taskId));
    const bool ru = m_language == QStringLiteral("ru");
    for(const QString& f : take) {
      applyConflictChoice(t, f, true, ru);
    }
    for(const QString& f : keep) {
      send = applyConflictChoice(t, f, false, ru) || send;
    }
    if(send && !trackerWriteEnabled(t.externalProvider)) {
      // Nothing is written to this tracker: keeping mine keeps it here only,
      // and there is no unsent move left to show (APP-243).
      t.externalMeta.unsyncedStatus.clear();
      t.externalMeta.pushQueued = false;
      send = false;
    }
    m_tasks.upsert(t);
    scheduleSave();
  }
  if(send) {
    pushStatusToTracker(taskId, t.status);
  }
  emit trackerConflictResolved(taskId);
  const char* key = useTracker ? "task.conflictTookTracker" : (take.isEmpty() ? "task.conflictKeptMine" : "task.conflictMixed");
  emit toast(tr_(key).arg(externalKeyOf(t)));
}

QString AppController::lastLocalEditAt(const QString& taskId) const {
  QDateTime best;
  for(const heap::history::HistoryEvent& e : m_history.events(activeProfileId(), taskId)) {
    if(!e.sync && (!best.isValid() || e.at > best)) {
      best = e.at;
    }
  }
  return best.isValid() ? best.toString(Qt::ISODate) : QString();
}

void AppController::keepGoneTicketLocally(const QString& taskId) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  if(t.externalProvider.isEmpty() || !t.externalMeta.goneUpstream) {
    return;
  }
  const QString key = externalKeyOf(t);
  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(t.externalProvider);
  const QString tracker = d ? d->displayName : t.externalProvider;
  {
    const UndoScope scope(this, tr_("task.keptLocal").arg(key));
    // Where it came from stays readable in the task's own notes.
    QString line = tr_("task.keptLocal.note").arg(key, tracker);
    if(!t.externalUrl.isEmpty()) {
      line += QStringLiteral(" ") + t.externalUrl;
    }
    heap::local::appendNote(t.local, line);
    t.externalProvider.clear();
    t.externalId.clear();
    t.externalUrl.clear();
    t.externalMeta = ExternalMeta{};
    m_tasks.upsert(t);
    scheduleSave();
  }
  emit integrationStatesChanged();
  emit undoableToast(tr_("task.keptLocal").arg(key), 5);
}

QVariantMap AppController::runningTimer() const {
  for(const Task& t : m_tasks.items()) {
    if(t.timerStartedAt.isValid()) {
      int secs = t.trackedSeconds + static_cast<int>(t.timerStartedAt.secsTo(QDateTime::currentDateTime()));
      return QVariantMap{
          {QStringLiteral("id"), t.id},
          {QStringLiteral("title"), t.title},
          {QStringLiteral("key"), t.externalProvider.isEmpty() ? t.id : externalKeyOf(t)},
          {QStringLiteral("seconds"), qMax(0, secs)},
      };
    }
  }
  // A timer started before a profile switch keeps running there; show it with
  // the profile's name instead of nothing (IDIOT-CAL-6).
  for(const Profile& p : m_profiles) {
    if(p.id == m_activeProfileId) {
      continue;
    }
    for(const Task& t : p.tasks) {
      if(t.timerStartedAt.isValid()) {
        const int secs = t.trackedSeconds + static_cast<int>(t.timerStartedAt.secsTo(QDateTime::currentDateTime()));
        return QVariantMap{
            {QStringLiteral("id"), t.id},
            {QStringLiteral("title"), t.title},
            {QStringLiteral("key"), t.externalProvider.isEmpty() ? t.id : externalKeyOf(t)},
            {QStringLiteral("seconds"), qMax(0, secs)},
            {QStringLiteral("profile"), p.name},
        };
      }
    }
  }
  return {};
}

void AppController::keepSecretsInFile() {
  const QString providerId = m_keychainProblem.value(QStringLiteral("provider")).toString();
  if(m_secretStore && !providerId.isEmpty()) {
    m_secretStore->keepInFile(providerId);
  }
  dismissKeychainProblem();
}

void AppController::retryKeychain() {
  const QString providerId = m_keychainProblem.value(QStringLiteral("provider")).toString();
  dismissKeychainProblem();
  if(m_secretStore && !providerId.isEmpty()) {
    m_secretStore->retryKeychain(providerId);
  }
}

void AppController::dismissKeychainProblem() {
  if(m_keychainProblem.isEmpty()) {
    return;
  }
  m_keychainProblem.clear();
  emit keychainProblemChanged();
}

QString AppController::keychainName() const {
#if defined(Q_OS_WIN)
  return tr_("keychain.name.win");
#elif defined(Q_OS_MACOS)
  return tr_("keychain.name.mac");
#else
  return tr_("keychain.name.linux");
#endif
}

void AppController::archiveOutOfScope(const QString& providerId) {
  QStringList ids;
  for(const Task& t : m_tasks.items()) {
    if(t.externalProvider == providerId && t.externalMeta.outOfScope && !t.archived) {
      ids.append(t.id);
    }
  }
  if(ids.isEmpty()) {
    return;
  }
  const UndoScope scope(this, tr_("int.outOfScopeArchived").arg(ids.size()));
  for(const QString& id : ids) {
    m_tasks.setArchived(id, true);
  }
  scheduleSave();
  emit integrationStatesChanged();
  emit undoableToast(tr_("int.outOfScopeArchived").arg(ids.size()), 5);
}

void AppController::refreshOAuthToken(const QString& providerId, std::function<void(bool)> done) {
  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
  const QVariantMap cfg = integrationConfig(providerId);
  const QString refreshToken = cfg.value(QStringLiteral("refreshToken")).toString();
  if(d == nullptr || !d->oauth.supported || refreshToken.isEmpty()) {
    done(false);
    return;
  }
  // The refresh token is single-use and rotated, so two callers must not spend
  // the same one; the loser simply gives up and the next sync picks it up.
  if(m_refreshing.contains(providerId)) {
    done(false);
    return;
  }
  m_refreshing.insert(providerId);

  QString host = cfg.value(QStringLiteral("host")).toString().trimmed();
  if(host.isEmpty()) {
    host = d->baseUrlFallback;
  }
  while(host.endsWith(QLatin1Char('/'))) {
    host.chop(1);
  }
  heap::integrations::RefreshParams p;
  p.tokenUrl = QString(d->oauth.tokenUrl).replace(QStringLiteral("{host}"), host);
  p.clientId = cfg.value(QStringLiteral("clientId")).toString().trimmed();
  if(p.clientId.isEmpty()) {
    p.clientId = d->oauth.clientId;
  }
  p.clientSecret = cfg.value(QStringLiteral("clientSecret")).toString();
  if(p.clientSecret.isEmpty()) {
    p.clientSecret = d->oauth.clientSecret;
  }
  p.refreshToken = refreshToken;
  p.style = d->oauth.tokenStyle;
  // The device flow has no redirect; the loopback flow must repeat the one it
  // was granted with (GitLab checks).
  if(d->oauth.effectiveFlow() != heap::integrations::OAuthFlow::Device) {
    p.redirectUri = heap::integrations::OAuthManager::redirectUri();
  }

  if(m_oauthNam == nullptr) {
    m_oauthNam = new QNetworkAccessManager(this);
  }
  const QString label = d->displayName;
  heap::integrations::refreshAccessToken(
      m_oauthNam, p, [this, providerId, label, done = std::move(done)](const heap::integrations::OAuthResult& r) {
        m_refreshing.remove(providerId);
        if(!r.ok) {
          qWarning() << providerId << "token refresh failed:" << r.httpStatus << r.error;
          if(!r.grantRejected) {
            // No answer, a timeout, a 5xx, a captive portal: the network's
            // problem, not the session's (INT-5). Stay signed in, say
            // "offline" and try again later.
            setProviderOffline(providerId, true);
            scheduleRefreshRetry(providerId);
            done(false);
            return;
          }
          // The grant is gone for good (revoked, or a rotated refresh token was
          // reused). Drop the card back to disconnected so the Integrations
          // panel offers the sign-in button again instead of failing forever.
          m_offlineProviders.remove(providerId);
          m_refreshRetryMs.remove(providerId);
          emit integrationStatesChanged();
          setIntegrationField(providerId, QStringLiteral("connected"), false);
          emit toast(tr_("int.sessionExpired").arg(label), QStringLiteral("warning"));
          done(false);
          return;
        }
        setProviderOffline(providerId, false);
        if(m_secretStore) {
          // Refresh token first: providers rotate it, so the old one is already
          // dead. Dying between the two writes must not leave a live access
          // token paired with a refresh token that can never be spent.
          if(!r.refreshToken.isEmpty()) {
            m_secretStore->setValue(providerId, QStringLiteral("refreshToken"), r.refreshToken);
          }
          m_secretStore->setValue(providerId, QStringLiteral("token"), r.accessToken);
        }
        setIntegrationField(providerId, QStringLiteral("tokenExpiresAt"), heap::integrations::expiryToString(r.expiresAt));
        done(true);
      });
}

void AppController::resolveJiraSite(const QString& accessToken, const QString& label) {
  // An Atlassian 3LO token is not bound to a site. Every API call goes to
  // api.atlassian.com/ex/jira/{cloudId}, and the only way to learn the cloudId
  // is to ask which sites this token was granted.
  static const QString kProviderId = QStringLiteral("jira");
  if(m_oauthNam == nullptr) {
    m_oauthNam = new QNetworkAccessManager(this);
  }
  QNetworkRequest req{QUrl(QStringLiteral("https://api.atlassian.com/oauth/token/accessible-resources"))};
  // Qt would follow a redirect to another host and carry the credentials
  // with it; keep every authenticated call on the origin it was aimed at.
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
  req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + accessToken.toUtf8());
  req.setRawHeader("Accept", "application/json");
  req.setRawHeader("User-Agent", "tabless-sync");

  QNetworkReply* reply = m_oauthNam->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply, label]() {
    reply->deleteLater();
    const QByteArray body = reply->readAll();
    if(reply->error() != QNetworkReply::NoError) {
      emit toast(
          tr_("int.signInFailed")
              .arg(label, heap::integrations::describeHttpError(heap::integrations::replyHttpStatus(reply), body, reply->errorString())));
      return;
    }
    // Keep whatever site the card already names: silently switching sites would
    // repoint every issue already synced from the old one.
    const QVariantMap cfg = integrationConfig(kProviderId);
    QString preferred = cfg.value(QStringLiteral("siteUrl")).toString();
    if(preferred.isEmpty()) {
      preferred = cfg.value(QStringLiteral("baseUrl")).toString();
    }
    const heap::integrations::JiraSite site = heap::integrations::pickJiraSite(body, preferred);
    if(site.cloudId.isEmpty()) {
      // Atlassian only ever grants Cloud sites, so this is also what a
      // self-hosted Jira looks like from here: the sign-in worked and named
      // nothing this token can reach.
      emit toast(tr_("int.noSite").arg(label));
      return;
    }
    setIntegrationFields(kProviderId,
                         {
                             {QStringLiteral("cloudId"), site.cloudId},
                             {QStringLiteral("siteUrl"), site.url},
                             {QStringLiteral("connected"), true},
                         });
    emit toast(tr_("int.browserConnectedSite").arg(label, site.url));
  });
}

void AppController::connectOAuth(const QString& providerId) {
  const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(providerId);
  if(!d || !d->oauth.supported) {
    emit toast(tr_("int.noBrowser"), QStringLiteral("error"));
    emit integrationActionFinished(providerId, QStringLiteral("oauth"), false, tr_("int.noBrowser"));
    return;
  }
  const QVariantMap cfg = integrationConfig(providerId);
  // Prefer a user-entered client ID (self-hosted / Advanced), else the app's
  // baked-in registered client ID. Only if both are empty is there nothing to do.
  QString clientId = cfg.value(QStringLiteral("clientId")).toString().trimmed();
  if(clientId.isEmpty()) {
    clientId = d->oauth.clientId;
  }
  if(clientId.isEmpty()) {
    emit toast(tr_("int.noOAuthApp"));
    emit integrationActionFinished(providerId, QStringLiteral("oauth"), false, tr_("int.noOAuthApp"));
    return;
  }
  QString clientSecret = cfg.value(QStringLiteral("clientSecret")).toString();
  if(clientSecret.isEmpty()) {
    clientSecret = d->oauth.clientSecret;
  }
  // Providers that refuse a public client need a secret alongside the ID. A
  // release build carries one; a build made without the CI credentials does
  // not, so say so instead of opening a browser that will refuse the exchange.
  if(d->oauth.needsSecret && clientSecret.isEmpty()) {
    emit toast(tr_("int.needSecret").arg(d->displayName));
    emit integrationActionFinished(providerId, QStringLiteral("oauth"), false, tr_("int.needSecret").arg(d->displayName));
    return;
  }
  if(d->oauth.effectiveFlow() == heap::integrations::OAuthFlow::Device && !heap::integrations::OAuthManager::deviceFlowAvailable()) {
    emit toast(tr_("int.needQt").arg(d->displayName));
    emit integrationActionFinished(providerId, QStringLiteral("oauth"), false, tr_("int.needQt").arg(d->displayName));
    return;
  }
  // {host} defaults to the descriptor fallback (e.g. gitlab.com) so gitlab.com
  // users never type a host; self-hosted users enter one under Advanced.
  QString host = cfg.value(QStringLiteral("host")).toString().trimmed();
  if(host.isEmpty()) {
    host = d->baseUrlFallback;
  }
  const QString base = cfg.value(QStringLiteral("baseUrl")).toString().trimmed();
  const auto expandHost = [host, base](QString url) {
    QString h = host;
    QString b = base;
    while(h.endsWith('/')) {
      h.chop(1);
    }
    while(b.endsWith('/')) {
      b.chop(1);
    }
    url.replace(QStringLiteral("{host}"), h);
    url.replace(QStringLiteral("{baseUrl}"), b);
    return url;
  };

  auto* mgr = new heap::integrations::OAuthManager(this);
  heap::integrations::OAuthManager::Params p;
  p.tokenUrl = expandHost(d->oauth.tokenUrl);
  p.clientId = clientId;
  p.clientSecret = clientSecret;
  p.scope = d->oauth.scope;
  p.scopeSeparator = d->oauth.scopeSeparator;
  p.usePkce = d->oauth.usePkce;
  p.flow = d->oauth.effectiveFlow();
  p.tokenStyle = d->oauth.tokenStyle;
  p.extraAuthParams = d->oauth.extraAuthParams;
  p.clientIdParam = d->oauth.clientIdParam;
  p.redirectParam = d->oauth.redirectParam;
  p.redirectHost = d->oauth.redirectHost;
  const bool deviceFlow = p.flow == heap::integrations::OAuthFlow::Device;
  // Device flow: authUrl is the device authorization endpoint (no loopback).
  p.authUrl = expandHost(deviceFlow ? d->oauth.deviceAuthUrl : d->oauth.authUrl);

  const QString label = d->displayName;
  // Device flow surfaces a user code the person types in the browser — relay it
  // to the Integrations card as a banner.
  connect(mgr, &heap::integrations::OAuthManager::userCode, this, [this, providerId, label](const QString& code, const QString& uri) {
    emit oauthDeviceCode(providerId, code, uri);
    emit toast(tr_("int.deviceCode").arg(label, uri, code));
  });
  connect(mgr, &heap::integrations::OAuthManager::finished, this, [this, providerId, label, mgr](const heap::integrations::OAuthResult& r) {
    mgr->deleteLater();
    emit oauthDeviceCode(providerId, QString(), QString());  // clear the banner
    if(!r.ok) {
      const QString message = tr_("int.signInFailed").arg(label, providerReason(r.error));
      emit toast(message, QStringLiteral("error"));
      emit integrationActionFinished(providerId, QStringLiteral("oauth"), false, message);
      return;
    }
    emit integrationActionFinished(providerId, QStringLiteral("oauth"), true, QString());
    if(m_secretStore) {
      // Same order as refreshOAuthToken: the refresh token goes in first.
      if(!r.refreshToken.isEmpty()) {
        m_secretStore->setValue(providerId, QStringLiteral("refreshToken"), r.refreshToken);
      }
      m_secretStore->setValue(providerId, QStringLiteral("token"), r.accessToken);
    }
    // Atlassian's token is not bound to a site: the API base is
    // api.atlassian.com/ex/jira/{cloudId}, and the cloudId has to be asked for.
    // Until that answer arrives the card cannot say "connected" — without a
    // cloudId JiraProvider has no API base at all, so a card marked connected
    // here built no provider and synced nothing while claiming to work. That is
    // exactly what a self-hosted Jira hits: the gateway has never heard of it.
    const bool needsSite = providerId == QStringLiteral("jira");
    // One write: each one rebuilds every provider, and doing that three times
    // in a row could tear down a request already in flight.
    setIntegrationFields(providerId,
                         {
                             {QStringLiteral("authMode"), QStringLiteral("oauth")},
                             // When the token expires. Without it the access token was
                             // used until the provider started refusing it, which read
                             // as an empty sync.
                             {QStringLiteral("tokenExpiresAt"), heap::integrations::expiryToString(r.expiresAt)},
                             {QStringLiteral("connected"), !needsSite},
                         });
    if(needsSite) {
      resolveJiraSite(r.accessToken, label);
      return;
    }
    // Signing in says who you are, not what to sync. Asana still needs a
    // workspace, ClickUp a list, Sentry an org and project, Bitbucket a repo —
    // and those live under Advanced, so the card used to read "connected" and
    // then quietly sync nothing.
    const QStringList missing = missingRequiredFields(providerId);
    if(missing.isEmpty()) {
      emit toast(tr_("int.browserConnected").arg(label));
    } else {
      emit toast(tr_("int.signedInNeeds").arg(label, missing.join(QStringLiteral(", "))));
      emit integrationNeedsFields(providerId, missing);
    }
  });
  if(deviceFlow) {
    emit toast(tr_("int.browserStarting").arg(label));
  } else {
    emit toast(tr_("int.browserOpening").arg(label, heap::integrations::OAuthManager::redirectUri()));
  }
  mgr->start(p);
}

void AppController::scheduleSave() {
  if(m_saveBlocked && !m_loading) {
    // From here on a reload would throw work away, so the silent auto-reopen
    // stops. The banner sits above the work, not at the edit, so the edit
    // itself says it is not kept, at most every few seconds (PLAT-8, audit
    // 2026-09-30).
    m_editsWhileBlocked = true;
    constexpr qint64 kBlockedEditToastGapMs = 15 * 1000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if(m_lastBlockedEditToastMs == 0 || now - m_lastBlockedEditToastMs >= kBlockedEditToastGapMs) {
      m_lastBlockedEditToastMs = now;
      emit toast(tr_("storage.editNotKept"));
    }
  }
  if(m_loading || m_saveBlocked || !m_saveTimer) {
    return;
  }
  m_saveTimer->start();
}

// ───────────────── (de)serialisation helpers ─────────────────
// Live state.json (de)serialisation now lives in src/StateSerializer.cpp
// (namespace heap::state) so the round-trip + field-count guards can exercise
// the real save path instead of a look-alike (HEAP-131).

// ───────────────── Profile helpers (private) ─────────────────

int AppController::profileIndexOf(const QString& id) const {
  for(int i = 0; i < m_profiles.size(); ++i) {
    if(m_profiles[i].id == id) {
      return i;
    }
  }
  return -1;
}

QString AppController::makeProfileId(const QString& name) const {
  QString slug;
  // The id is a key everywhere (state.json, the switcher, the CLI): a pasted
  // 10 000-character name made a 10 000-character id (IDIOT-SHELL-16).
  for(const QChar c : name.toLower().left(kMaxProfileName)) {
    slug.append(c.isLetterOrNumber() ? c : QChar('-'));
  }
  while(slug.contains("--")) {
    slug.replace("--", "-");
  }
  if(slug.startsWith('-')) {
    slug = slug.mid(1);
  }
  while(slug.endsWith('-')) {
    slug.chop(1);
  }
  if(slug.isEmpty()) {
    slug = "profile";
  }
  QString id = slug;
  int n = 2;
  while(profileIndexOf(id) >= 0) {
    id = slug + "-" + QString::number(n++);
  }
  return id;
}

void AppController::snapshotActiveProfile() {
  const int i = profileIndexOf(m_activeProfileId);
  if(i < 0) {
    return;
  }
  Profile& p = m_profiles[i];
  p.tasks = m_tasks.items();
  p.people = m_people.items();
  p.statuses = m_statuses;
  p.docsState = m_docsState;
  // The last line of defence for the invariant: whatever reaches disk has to
  // be in `notes`, because the bare `notesState` is not written any more.
  adoptOrphanNotesState();
  syncActiveNoteBody();
  p.notesState = m_notesState;
  p.notes = m_notes.items();
  p.activeNoteId = m_activeNoteId;
  p.docPages = m_docPages.items();
  p.activeDocPageId = m_activeDocPageId;
  p.savedViews = m_savedViews;
  p.statusLog = m_statusLog;
  p.waitingOn = m_waitingOn;
  // Events are global — not snapshotted into the profile.
}

void AppController::applyProfileToModels(const Profile& p) {
  m_statusLog = p.statusLog;
  m_waitingOn = p.waitingOn;
  emit waitingOnChanged();
  // Imports and hand-edited files may still carry rank ties (see Rank.h).
  QVector<Task> tasks = p.tasks;
  heap::board::spreadTiedRanks(tasks);
  // A card whose column is gone (an import, a hand-edited or older file) was
  // drawn by no column and still counted (A2-2): it goes to the first column,
  // as saveTask files an unknown status (R3-138). Only a status nothing knows:
  // a built-in one (an older profile's column list may lack it) and a tracker
  // card's status are data, never rewritten on load.
  if(!p.statuses.isEmpty()) {
    static const QSet<QString> builtIn = {QStringLiteral("backlog"),
                                          QStringLiteral("todo"),
                                          QStringLiteral("prog"),
                                          QStringLiteral("half"),
                                          QStringLiteral("blocked"),
                                          QStringLiteral("review"),
                                          QStringLiteral("done")};
    QSet<QString> columns;
    for(const QVariant& v : p.statuses) {
      columns.insert(v.toMap().value(QStringLiteral("id")).toString());
    }
    const QString first = p.statuses.constFirst().toMap().value(QStringLiteral("id")).toString();
    for(Task& t : tasks) {
      if(!columns.contains(t.status) && !builtIn.contains(t.status) && t.externalProvider.isEmpty() && !first.isEmpty()) {
        t.status = first;
      }
    }
  }
  m_tasks.reset(tasks);
  // The reset dropped every "PR #n" the issues showed, and a coach mark
  // pointing at the last profile's card.
  if(!m_coachMark.isEmpty()) {
    m_coachMark.clear();
    emit coachMarkChanged();
  }
  foldReviewCardsIntoIssues(/*archive=*/false);
  m_people.reset(p.people);
  m_statuses = p.statuses;
  emit statusesChanged();
  m_docsState = p.docsState;
  emit docsStateChanged();
  // A profile written before v8, opened by a build that has not migrated it
  // yet, still carries its notes as one blob. Rather than show an empty list,
  // it becomes the one note it always was.
  QVector<Note> notes = p.notes;
  QString activeId = p.activeNoteId;
  if(notes.isEmpty() && !p.notesState.trimmed().isEmpty()) {
    Note n;
    n.id = QStringLiteral("note-migrated");
    n.title = tr_("notes.untitled");
    n.body = p.notesState;
    n.created = QDateTime::currentDateTime();
    n.updated = n.created;
    notes.append(n);
    activeId = n.id;
  }
  // Notes saved before APP-1 kept "Untitled note" whatever they said; they
  // take the title their text suggests now. Saved with the next write.
  for(Note& n : notes) {
    if(isPlaceholderNoteTitle(n.title)) {
      const QString t = titleFromBody(n.body);
      if(!t.isEmpty() && heap::notes::backlinksTo(n.id, notes).isEmpty()) {
        n.title = t;
      }
    }
  }
  m_notes.reset(notes);
  if(activeId.isEmpty() && !notes.isEmpty()) {
    activeId = notes.first().id;
  }
  m_activeNoteId = activeId;
  const int row = m_notes.indexOfId(m_activeNoteId);
  m_notesState = row >= 0 ? m_notes.items().at(row).body : p.notesState;
  emit activeNoteChanged();
  emit notesStateChanged();
  m_docPages.reset(p.docPages);
  m_activeDocPageId = p.activeDocPageId.isEmpty() && !p.docPages.isEmpty() ? p.docPages.first().id : p.activeDocPageId;
  emit activeDocPageChanged();
  setSavedViews(p.savedViews);
  // Events are global — not reset on profile switch.
}

Profile AppController::makeStartingProfile(const QString& name, const QString& color) const {
  Profile p;
  p.name = name;
  p.color = color.isEmpty() ? "#5cc2dd" : color;
  p.createdAt = QDateTime::currentDateTime();
  // Copy status template from the currently active profile (or sample).
  const int activeIdx = profileIndexOf(m_activeProfileId);
  if(activeIdx >= 0) {
    for(const QVariant& v : m_profiles[activeIdx].statuses) {
      const QVariantMap m = v.toMap();
      // copy color reference as-is
      p.statuses.append(m);
    }
  } else {
    for(const auto& m : SampleData::statuses(m_language == QStringLiteral("ru") ? SampleData::Lang::Ru : SampleData::Lang::En)) {
      p.statuses.append(m);
    }
  }
  // tasks / events / people / docs / saved views — empty. "Мои виды" fills
  // as the user saves a filter (H2-First / Q-First, R2-058); the example
  // profile still brings the starter views as sample content.
  return p;
}

// ───────────────── Save / load ─────────────────

// The writer seam (HEAP-156). saveStateNow() never touches the filesystem
// directly; it hands the serialized bytes to whatever is installed here. The
// default is an atomic QSaveFile write; the fault-injection harness swaps in a
// writer that truncates, corrupts or drops the rename. It runs on the save
// worker thread (PLAT-23).
namespace {
AppController::StateWriter g_stateWriter;

bool defaultStateWriter(const QString& path, const QByteArray& bytes, QString* error) {
  if(!heap::storage::writeAtomically(path, bytes, error)) {
    qWarning("todocpp: cannot write state.json: %s", qUtf8Printable(error ? *error : QString()));
    return false;
  }
  return true;
}

// Root and settings keys this build reads. Anything else found in a document
// at the current schema version is carried through a save untouched (PLAT-26):
// a sibling build, or a hand edit, may have put it there on purpose.
const QStringList& knownRootKeys() {
  static const QStringList keys = {QStringLiteral("schemaVersion"),
                                   QStringLiteral("activeProfileId"),
                                   QStringLiteral("profiles"),
                                   QStringLiteral("events"),
                                   QStringLiteral("settings"),
                                   QStringLiteral("taskSeq"),
                                   QStringLiteral("taskHistory"),
                                   // v1 flat collections
                                   QStringLiteral("tasks"),
                                   QStringLiteral("people"),
                                   QStringLiteral("statuses"),
                                   QStringLiteral("docs")};
  return keys;
}

// Legacy "p-XXXXXXXX" ids (UUID-short, pre-slug scheme) and the sample-data
// ids ("p1".."p6") become slug ids ("e.zaharov"). True when one changed.
bool slugLegacyPersonIds(Profile& pr) {
  static const QRegularExpression kLegacyId(QStringLiteral("^p-?[0-9a-fA-F]+$"));
  bool changed = false;
  QSet<QString> taken;
  for(const Person& pe : pr.people) {
    taken.insert(pe.id);
  }
  for(Person& pe : pr.people) {
    if(!kLegacyId.match(pe.id).hasMatch() && !pe.id.isEmpty()) {
      continue;
    }
    const QString slug = heap::text::slugifyPersonName(pe.name);
    if(slug.isEmpty()) {
      continue;
    }
    QString candidate = slug;
    for(int i = 2; taken.contains(candidate) && i < 1000; ++i) {
      candidate = slug + QChar('-') + QString::number(i);
    }
    taken.remove(pe.id);
    taken.insert(candidate);
    pe.id = candidate;
    changed = true;
  }
  return changed;
}

const QStringList& knownSettingsKeys() {
  static const QStringList keys = {QStringLiteral("theme"),
                                   QStringLiteral("density"),
                                   QStringLiteral("language"),
                                   QStringLiteral("currentView"),
                                   QStringLiteral("workdayStart"),
                                   QStringLiteral("workdayEnd"),
                                   QStringLiteral("crumbProject"),
                                   QStringLiteral("crumbUser"),
                                   QStringLiteral("welcomeSeen"),
                                   QStringLiteral("demoActive"),
                                   QStringLiteral("installId"),
                                   QStringLiteral("shortcuts"),
                                   QStringLiteral("shortcutsSchema"),
                                   QStringLiteral("sectionViews"),
                                   QStringLiteral("app")};
  return keys;
}

QJsonObject unknownKeys(const QJsonObject& o, const QStringList& known) {
  QJsonObject out;
  for(auto it = o.constBegin(); it != o.constEnd(); ++it) {
    if(!known.contains(it.key())) {
      out.insert(it.key(), it.value());
    }
  }
  return out;
}

// Snapshots taken before a restore. They count on their own, so going through
// the backups one restore at a time does not push the rotational copies out
// (PLAT-11, audit 2026-09-30).
constexpr int kPreRestoreCopiesKept = 5;

// Rotational snapshots only, newest first by the stamp in the name (which is
// when the copy was taken). Only files this app named are ever pruned:
// `state-<yyyyMMdd-HHmmss>[-prerestore][-n].json`. Pre-migration copies and
// anything else in the folder (a copy the user or a tool put there, say
// `state-pre-<tag>-<stamp>.json`) are left alone — sorting every `state-*` by
// name once kept an alphabetical handful of those and deleted the newest.
void pruneBackupDir(const QString& dirPath, int keep) {
  static const QRegularExpression kOwnName(QStringLiteral(R"(^state-\d{8}-\d{6}(-prerestore)?(-\d+)?\.json$)"));
  QDir d(dirPath);
  QStringList all = d.entryList({"state-*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Name);
  all.removeIf([](const QString& name) {
    return !kOwnName.match(name).hasMatch();
  });
  // The stamp leads the name and has a fixed width, so name order is time order.
  std::reverse(all.begin(), all.end());
  int kept = 0;
  int keptPreRestore = 0;
  for(const QString& name : all) {
    if(name.contains(QLatin1String("prerestore"))) {
      if(++keptPreRestore > kPreRestoreCopiesKept) {
        d.remove(name);
      }
      continue;
    }
    if(++kept > keep) {
      d.remove(name);
    }
  }
}

// Copies the live file into the backup dir under a stamp that never clobbers
// an earlier copy. Returns the file name, empty on failure.
QString copyStateToBackupDir(const QString& statePath, const QString& dirPath, const QString& tag) {
  if(!QFile::exists(statePath)) {
    return {};
  }
  QDir().mkpath(dirPath);
  const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
  const QString base = QStringLiteral("state-") + stamp + tag;
  QString name = base + QStringLiteral(".json");
  for(int n = 2; QFile::exists(dirPath + "/" + name); ++n) {
    name = base + QChar('-') + QString::number(n) + QStringLiteral(".json");
  }
  return QFile::copy(statePath, dirPath + "/" + name) ? name : QString();
}

// A pre-migration copy for a file from a NEWER build is taken on every launch
// of the older one. Identical bytes are not copied twice, and only the newest
// few are kept per version (PLAT-5).
constexpr int kNewerSchemaCopiesKept = 3;
}  // namespace

void AppController::setStateWriterForTesting(StateWriter writer) {
  g_stateWriter = std::move(writer);
}

bool AppController::backupDueNow(const QDateTime& now) {
  const QVariantMap d = settingsMap().value("data").toMap();
  if(!d.value("autoBackup", true).toBool()) {
    return false;
  }
  if(!QFile::exists(stateFilePath())) {
    return false;
  }
  qint64 intervalSecs = kBackupIntervalSeconds;
  const QString interval = d.value("backupInterval", QStringLiteral("daily")).toString();
  if(interval == QLatin1String("hourly")) {
    intervalSecs = 3600;
  } else if(interval == QLatin1String("daily")) {
    intervalSecs = 24 * 3600;
  } else if(interval == QLatin1String("weekly")) {
    intervalSecs = 7 * 24 * 3600;
  }
  // The clock lives on disk, not only in memory: a fresh process used to see
  // "never backed up", copy on every launch, and let retention push the old
  // copies out — twenty restarts erased the whole history.
  if(!m_lastBackupAt.isValid()) {
    m_lastBackupAt = newestBackupTime();
  }
  return !m_lastBackupAt.isValid() || m_lastBackupAt.secsTo(now) >= intervalSecs;
}

QString AppController::snapshotStateToBackups(const QString& tag) {
  if(m_saver) {
    m_saver->flush();  // never copy a file the worker is replacing
  }
  const QString name = copyStateToBackupDir(stateFilePath(), backupDirPath(), tag);
  if(!name.isEmpty()) {
    pruneBackupDir(backupDirPath(), kBackupRetentionCount);
    m_lastBackupAt = QDateTime::currentDateTime();
  }
  return name;
}

QDateTime AppController::newestBackupTime() const {
  const QDir d(backupDirPath());
  QDateTime newest;
  const QFileInfoList all = d.entryInfoList({"state-*.json"}, QDir::Files | QDir::NoSymLinks);
  for(const QFileInfo& fi : all) {
    if(fi.fileName().contains(QLatin1String("premigration"))) {
      continue;
    }
    // The name carries when the copy was taken. The file time may not: a copy
    // on Windows keeps the source's last-write time.
    QDateTime when = QDateTime::fromString(fi.completeBaseName().mid(6, 15), QStringLiteral("yyyyMMdd-HHmmss"));
    if(!when.isValid()) {
      when = fi.lastModified();
    }
    if(!newest.isValid() || when > newest) {
      newest = when;
    }
  }
  return newest;
}

bool AppController::recoverFromNewestBackup(QJsonObject& out, QString& fromPath) {
  const QDir d(backupDirPath());
  // Newest first — return the most recent backup that is a loadable state
  // document (valid JSON is not enough: `{}` would load as nothing).
  const QFileInfoList backups = d.entryInfoList({"state-*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
  for(const QFileInfo& fi : backups) {
    QFile bf(fi.absoluteFilePath());
    if(!bf.open(QFile::ReadOnly)) {
      continue;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(bf.readAll());
    bf.close();
    if(!doc.isNull() && doc.isObject() && heap::storage::validateShape(doc.object())) {
      out = doc.object();
      fromPath = fi.absoluteFilePath();
      return true;
    }
  }
  return false;
}

QString AppController::quarantineCorruptState(const QString& path, const QByteArray& bytes) {
  const QFileInfo fi(path);
  const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
  QString target = fi.absolutePath() + "/state.corrupt-" + stamp + ".json";
  // Avoid clobbering an earlier quarantine from the same second.
  int n = 1;
  while(QFile::exists(target)) {
    target = fi.absolutePath() + "/state.corrupt-" + stamp + "-" + QString::number(n++) + ".json";
  }
  if(QFile::rename(path, target)) {
    heap::recovery::append(QString::fromLatin1(heap::recovery::kQuarantined),
                           {{QStringLiteral("from"), path}, {QStringLiteral("to"), target}});
    return QFileInfo(target).fileName();
  }
  // A lock that lets us read but not rename (sync clients, some AV) — keep a
  // byte-identical copy of what we read instead. The original stays where it
  // is and is replaced by the next atomic save.
  QString error;
  if(heap::storage::writeAtomically(target, bytes, &error)) {
    heap::recovery::append(QString::fromLatin1(heap::recovery::kQuarantined),
                           {{QStringLiteral("from"), path}, {QStringLiteral("to"), target}, {QStringLiteral("copied"), true}});
    return QFileInfo(target).fileName();
  }
  qWarning("todocpp: could not quarantine corrupt state.json to %s: %s", qUtf8Printable(target), qUtf8Printable(error));
  heap::recovery::append(QString::fromLatin1(heap::recovery::kQuarantineFailed),
                         {{QStringLiteral("path"), path}, {QStringLiteral("error"), error}});
  return {};
}

QJsonObject AppController::buildStateHead() {
  QJsonObject s = m_settingsExtra;
  s["theme"] = m_theme;
  s["density"] = m_density;
  s["language"] = pseudoLocale() ? m_languageUnderPseudo : m_language;
  s["currentView"] = m_currentView;
  {
    QJsonObject sv;
    for(auto it = m_sectionViews.constBegin(); it != m_sectionViews.constEnd(); ++it) {
      sv[it.key()] = it.value();
    }
    if(!sv.isEmpty()) {
      s["sectionViews"] = sv;
    }
  }
  s["workdayStart"] = m_workdayStart;
  s["workdayEnd"] = m_workdayEnd;
  s["crumbProject"] = m_crumbProject;
  s["crumbUser"] = m_crumbUser;
  s["welcomeSeen"] = m_welcomeSeen;
  s["demoActive"] = m_demoActive;
  s["trackerWriteNotice"] = m_trackerWriteNoticeDone;
  s["gitLineDefault"] = m_gitLineDefaultDone;
  s["themeLowkey"] = m_themeLowkeyDone;
  s["lastRunVersion"] = appVersion();
  icsUidDomain();  // mints the id on the first save
  s["installId"] = m_installId;

  // Keyboard shortcut overrides — only entries the user actually changed.
  // Storing every entry pinned each binding to whatever the default happened
  // to be on the day the file was written, so changing a default afterwards
  // reached nobody who had ever launched the app. A cleared binding is still
  // stored: an empty string differs from its default, which is the point.
  QJsonObject shortcutsObj;
  for(const QVariant& v : m_shortcuts) {
    const QVariantMap m = v.toMap();
    if(m.value("sequence").toString() != m.value("defaultSequence").toString()) {
      shortcutsObj[m.value("id").toString()] = m.value("sequence").toString();
    }
  }
  s["shortcuts"] = shortcutsObj;
  s["shortcutsSchema"] = kShortcutsSchema;
  s["keymapScheme"] = m_keymapScheme;

  if(!m_appSettingsJson.isEmpty()) {
    const QJsonDocument d = QJsonDocument::fromJson(m_appSettingsJson.toUtf8());
    if(!d.isNull() && d.isObject()) {
      s["app"] = d.object();
    }
  }

  QJsonObject seq;
  for(auto it = m_taskSeq.constBegin(); it != m_taskSeq.constEnd(); ++it) {
    seq[it.key()] = it.value();
  }

  QJsonObject head = m_rootExtra;
  head["schemaVersion"] = heap::state::kSchemaVersion;
  head["activeProfileId"] = m_activeProfileId;
  head["settings"] = s;
  head["taskSeq"] = seq;
  return head;
}

bool AppController::saveStateCopyTo(const QUrl& fileUrl) {
  const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
  if(path.isEmpty() || m_loading) {
    return false;
  }
  snapshotActiveProfile();
  QJsonObject root = buildStateHead();
  QJsonArray profilesArr;
  for(const Profile& p : m_profiles) {
    profilesArr.append(heap::state::profileToJson(p));
  }
  root["profiles"] = profilesArr;
  root["events"] = heap::state::eventsToJson(m_events.items());
  if(!m_history.isEmpty()) {
    root["taskHistory"] = m_history.toJson();
  }
  QString error;
  const bool ok = heap::storage::writeAtomically(path, QJsonDocument(root).toJson(QJsonDocument::Indented), &error);
  emit toast(ok ? tr_("storage.copySaved").arg(QDir::toNativeSeparators(path)) : tr_("storage.copyFailed").arg(error));
  return ok;
}

void AppController::saveStateNow() {
  // m_saveBlocked is re-checked here, not only in scheduleSave(): flushSave()
  // and the quit path call this directly.
  if(m_loading || m_saveBlocked) {
    return;
  }
  const heap::frame::Span span("saveStateNow");

  // Push live model state back into the active profile.
  snapshotActiveProfile();

  // Everything below the settings object is a snapshot: implicitly shared
  // copies, a refcount bump each. The worker serializes them (PLAT-23), so
  // the UI thread no longer pays ~200 ms per save on a 10k-task profile.
  const QJsonObject head = buildStateHead();

  const QDateTime now = QDateTime::currentDateTime();
  const bool backupDue = backupDueNow(now);
  if(backupDue) {
    m_lastBackupAt = now;
  }

  const QVector<Profile> profiles = m_profiles;
  const QVector<CalEvent> events = m_events.items();
  const QString path = stateFilePath();
  const QString backups = backupDirPath();
  const AppController::StateWriter writer = g_stateWriter;
  // Serialized on the worker with the rest (implicitly shared copy).
  const heap::history::TaskHistory history = m_history;
  // The time machine (APP-162): an hourly compressed copy, taken on the worker
  // after the file landed, so neither the copy nor its retention costs the UI.
  const QString historyDir = heap::history::dirFor(heap::paths::dataDir());
  const heap::history::Policy historyPolicy = heap::history::policyFrom(settingsMap().value("data").toMap());

  auto job = [head, profiles, events, history, path, backups, backupDue, writer, historyDir, historyPolicy]() {
    QJsonObject root = head;
    QJsonArray profilesArr;
    for(const Profile& p : profiles) {
      profilesArr.append(heap::state::profileToJson(p));
    }
    root["profiles"] = profilesArr;
    // Events are global (shown across profiles in the calendar).
    root["events"] = heap::state::eventsToJson(events);
    if(!history.isEmpty()) {
      root["taskHistory"] = history.toJson();
    }

    if(backupDue) {
      copyStateToBackupDir(path, backups, QString());
      pruneBackupDir(backups, kBackupRetentionCount);
    }

    heap::storage::SaveOutcome outcome;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    outcome.bytes = bytes.size();
    QString error;
    outcome.ok = writer ? writer(path, bytes) : defaultStateWriter(path, bytes, &error);
    if(!outcome.ok) {
      outcome.error = error.isEmpty() ? QStringLiteral("write failed") : error;
      // A failed write is the fault the user never sees. Leave a local record
      // so "heap lost my edit" has evidence attached to the next bug report.
      heap::recovery::append(
          QString::fromLatin1(heap::recovery::kWriteFailed),
          {{QStringLiteral("path"), path}, {QStringLiteral("bytes"), bytes.size()}, {QStringLiteral("error"), outcome.error}});
    } else {
      heap::history::maybeSnapshot(historyDir, bytes, heap::history::summarize(root), QDateTime::currentDateTime(), historyPolicy);
    }
    return outcome;
  };
  m_saver->submit(++m_saveGeneration, std::move(job));
}

void AppController::onSaveFinished(const heap::storage::SaveOutcome& outcome) {
  if(outcome.ok) {
    m_saveRetryStep = 0;
    if(m_saveRetryTimer) {
      m_saveRetryTimer->stop();
    }
    if(m_storageState == QLatin1String("writeFailed")) {
      setStorageState(QStringLiteral("ok"), QString());
      emit toast(tr_("storage.savedAgain"));
    }
    return;
  }
  // A newer save already landed: this failure is stale.
  if(outcome.generation < m_saveGeneration && m_storageState == QLatin1String("ok")) {
    return;
  }
  setStorageStateT(QStringLiteral("writeFailed"),
                   {{QStringLiteral("storage.writeFailed"), {QDir::toNativeSeparators(stateFilePath()), outcome.error}}});
  // Keep trying on a backoff: the usual cause (a sync client or AV scan
  // holding the file) goes away by itself, and the edits are still in memory.
  // Every 30 s at most, as the strip says (X-Err-Storage).
  static const int kRetryMs[] = {2000, 5000, 15000, 30000};
  const int step = qMin(m_saveRetryStep, static_cast<int>(std::size(kRetryMs)) - 1);
  ++m_saveRetryStep;
  if(m_saveRetryTimer && !m_saveBlocked) {
    m_saveRetryTimer->start(kRetryMs[step]);
  }
}

void AppController::setStorageStateT(const QString& state, const QVector<QPair<QString, QStringList>>& spec) {
  m_storageSpec = spec;
  QString message;
  for(const auto& part : spec) {
    QString text = tr_(part.first);
    for(const QString& a : part.second) {
      text = text.arg(a);
    }
    message += (message.isEmpty() ? QString() : QStringLiteral(" ")) + text;
  }
  m_storageReason = state == QLatin1String("writeFailed") ? classifyWriteFailure() : QString();
  setStorageState(state, message);
}

void AppController::recomposeStorageMessage() {
  if(m_storageSpec.isEmpty() || m_storageState == QLatin1String("ok")) {
    return;
  }
  const QString before = m_storageMessage;
  const QString reason = m_storageReason;
  setStorageStateT(m_storageState, m_storageSpec);
  if(m_storageMessage == before && m_storageReason != reason) {
    emit storageStateChanged();
  }
}

QString AppController::classifyWriteFailure() const {
  // A few words for the strip's lead (X-Err-Storage): the disk, the
  // folder's rights, or the file itself. Facts read now, not guessed.
  const QString dir = heap::paths::dataDir();
  const QStorageInfo info(dir);
  if(info.isValid() && info.bytesAvailable() >= 0 && info.bytesAvailable() < 8LL * 1024 * 1024) {
    QString drive = QDir::toNativeSeparators(info.rootPath());
    while(drive.size() > 1 && (drive.endsWith(QLatin1Char('\\')) || drive.endsWith(QLatin1Char('/')))) {
      drive.chop(1);
    }
    return tr_("storage.reason.noSpace").arg(drive);
  }
  if(info.isValid() && info.isReadOnly()) {
    return tr_("storage.reason.readOnly");
  }
  const QFileInfo fi(dir);
  if(fi.exists() && !fi.isWritable()) {
    return tr_("storage.reason.noAccess");
  }
  return tr_("storage.reason.locked");
}

void AppController::setStorageState(const QString& state, const QString& message) {
  if(state == QLatin1String("ok")) {
    m_storageSpec.clear();
    m_storageReason.clear();
  }
  if(state == m_storageState && message == m_storageMessage) {
    return;
  }
  // A save that did not reach disk, or a file that would not open, is in
  // the event log too (APP-187): the banner goes once it is fixed.
  if(state == QLatin1String("writeFailed") || state == QLatin1String("unreadable")) {
    logEvent(QStringLiteral("error"), message);
  }
  m_storageState = state;
  m_storageMessage = message;
  emit storageStateChanged();
}

void AppController::dismissStorageNotice() {
  if(m_storageState == QLatin1String("recovered") || m_storageState == QLatin1String("damaged")) {
    setStorageState(QStringLiteral("ok"), QString());
  }
}

void AppController::retryStorage() {
  if(m_storageState == QLatin1String("writeFailed")) {
    if(m_saveTimer) {
      m_saveTimer->stop();
    }
    saveStateNow();
    m_saver->flush();
    return;
  }
  if(m_storageState != QLatin1String("unreadable")) {
    return;
  }
  const heap::storage::ReadResult probe = heap::storage::readWithRetry(stateFilePath(), {});
  if(probe.kind == heap::storage::ReadResult::Unreadable) {
    setStorageStateT(m_storageState, {{QStringLiteral("storage.unreadable"), {QDir::toNativeSeparators(stateFilePath()), probe.error}}});
    emit toast(tr_("storage.stillLocked"));
    return;
  }
  reloadStateFromDisk();
  if(m_storageState == QLatin1String("ok")) {
    emit toast(tr_("storage.reopened"));
  }
}

void AppController::reloadStateFromDisk() {
  if(m_saver) {
    m_saver->flush();
  }
  if(m_storageRetryTimer) {
    m_storageRetryTimer->stop();
  }
  // Whatever the stack held describes the state being replaced (PLAT-15).
  m_undo.clear();
  m_parkedUndo.clear();
  emit pendingUndoChanged();
  clearSelection();
  emit aboutToChangeActiveNote();
  m_profiles.clear();
  m_activeProfileId.clear();
  m_rootExtra = {};
  m_history.clear();
  m_settingsExtra = {};
  m_taskSeq.clear();
  m_saveBlocked = false;
  m_editsWhileBlocked = false;
  setStorageState(QStringLiteral("ok"), QString());
  loadStateOnStart();
  if(m_profiles.isEmpty()) {
    seedStartingWorkspace();
  }
}

void AppController::enterUnreadableMode(const QString& error) {
  // Read-only session. The file on disk is the user's data; the one thing this
  // session must never do is write over it.
  m_saveBlocked = true;
  const QString path = stateFilePath();
  heap::recovery::append(QString::fromLatin1(heap::recovery::kUnreadable),
                         {{QStringLiteral("path"), path}, {QStringLiteral("error"), error}});
  qWarning("state.json could not be opened (%s) — read-only session, nothing will be saved", qUtf8Printable(error));

  // Show the newest backup so the user can still look things up, clearly
  // flagged; without one, an empty workspace — never the demo, which would
  // read as "heap deleted my tasks".
  QJsonObject backup;
  QString from;
  QString shown;
  if(recoverFromNewestBackup(backup, from)) {
    loadStateDocument(backup, /*viewOnly=*/true);
    shown = QFileInfo(from).fileName();
    // The backup's own flag would put "these are demo data — start from a
    // clean slate" under the banner, over the user's real (backed-up) work.
    m_demoActive = false;
    emit onboardingChanged();
  }
  m_editsWhileBlocked = false;
  if(m_profiles.isEmpty()) {
    Profile p = makeStartingProfile(tr_("profile.personal"), QString());
    p.id = QStringLiteral("default");
    m_profiles.push_back(p);
    m_activeProfileId = p.id;
    applyProfileToModels(p);
    m_welcomeSeen = true;
    emit onboardingChanged();
    emit profilesChanged();
    emit activeProfileChanged();
  }
  QVector<QPair<QString, QStringList>> spec{{QStringLiteral("storage.unreadable"), {QDir::toNativeSeparators(path), error}}};
  if(!shown.isEmpty()) {
    spec.append({QStringLiteral("storage.showingBackup"), {shown}});
  }
  setStorageStateT(QStringLiteral("unreadable"), spec);
  // The usual cause is a lock that lifts by itself. Until the user has typed
  // something into this read-only session, open the real file as soon as it
  // can be read.
  if(m_storageRetryTimer) {
    m_storageRetryTimer->start();
  }
}

void AppController::loadStateOnStart() {
  const QString path = stateFilePath();
  // Open failure is not corruption (PLAT-1): a lock held by an AV scan or a
  // sync client clears by itself, so it is retried, and if it never clears
  // the session goes read-only rather than seeding a demo that would later be
  // saved over the real file.
  const heap::storage::ReadResult read = heap::storage::readWithRetry(path, heap::storage::startupBackoff());
  if(read.kind == heap::storage::ReadResult::Missing) {
    return;  // genuine first run — nothing to load, seed demo silently
  }
  if(read.kind == heap::storage::ReadResult::Unreadable) {
    enterUnreadableMode(read.error);
    return;
  }

  // The bytes are in hand, so from here on a failure is damage, NOT a first
  // run. We must never let the caller silently seed demo data and overwrite
  // it: quarantine the damaged file, then recover the newest valid backup.
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(read.bytes, &parseError);
  // The damaged-file card says where the file stops reading (R4-104).
  m_storageDamagedAt = doc.isNull() ? parseError.offset : -1;
  QString shapeError;
  const bool ok = !doc.isNull() && doc.isObject() && heap::storage::validateShape(doc.object(), &shapeError);

  if(!ok) {
    const QString kept = quarantineCorruptState(path, read.bytes);
    if(kept.isEmpty()) {
      // Could not set the damaged file aside. Writing anything now would
      // destroy the only copy, so this session is read-only too.
      enterUnreadableMode(tr_("storage.damagedLocked"));
      return;
    }
    QJsonObject recovered;
    QString recoveredFrom;
    if(recoverFromNewestBackup(recovered, recoveredFrom)) {
      // Promote the recovered backup to be the live state so future saves
      // continue from good data. An atomic write: the original may still be
      // there when the quarantine had to copy rather than move it.
      QString error;
      if(!heap::storage::writeAtomically(path, QJsonDocument(recovered).toJson(QJsonDocument::Indented), &error)) {
        qWarning("todocpp: could not promote backup %s: %s", qUtf8Printable(recoveredFrom), qUtf8Printable(error));
      }
      heap::recovery::append(QString::fromLatin1(heap::recovery::kRecovered),
                             {{QStringLiteral("from"), recoveredFrom}, {QStringLiteral("reason"), shapeError}});
      loadStateDocument(recovered, /*viewOnly=*/false);
      // A banner that stays up, not a toast (PLAT-6): whatever changed after
      // the backup was taken is not in it, and the user has to know where the
      // damaged file went to look for it. A newer-schema backup has already
      // raised its own read-only banner, which says more.
      if(m_storageState == QLatin1String("ok")) {
        setStorageStateT(QStringLiteral("recovered"), {{QStringLiteral("data.recovered"), {QFileInfo(recoveredFrom).fileName(), kept}}});
      }
    } else {
      // No usable backup. The damaged file is preserved under a distinct name.
      // What opens is an empty workspace with a banner saying so — never the
      // demo and the welcome tour, which read as "a new install" and invited
      // working on in sample data (PLAT-6).
      heap::recovery::append(QString::fromLatin1(heap::recovery::kUnrecovered),
                             {{QStringLiteral("path"), path}, {QStringLiteral("reason"), shapeError}});
      Profile p = makeStartingProfile(tr_("profile.personal"), QString());
      p.id = QStringLiteral("default");
      m_profiles.push_back(p);
      m_activeProfileId = p.id;
      applyProfileToModels(p);
      m_welcomeSeen = true;
      m_demoActive = false;
      emit onboardingChanged();
      emit profilesChanged();
      emit activeProfileChanged();
      setStorageStateT(QStringLiteral("damaged"), {{QStringLiteral("data.corruptKept"), {kept}}});
      // Written now, so the next launch opens this workspace too rather than
      // taking the missing file for a first run. The damaged bytes are safe in
      // the quarantined copy.
      scheduleSave();
    }
    return;
  }

  loadStateDocument(doc.object(), /*viewOnly=*/false);
}

void AppController::loadStateDocument(QJsonObject root, bool viewOnly) {
  const QString path = stateFilePath();

  // ----- schema ladder: field-level upgrades, before anything parses a task ---
  // Version-gated and idempotent: a v4 document walks straight past this, so
  // reopening the app never re-migrates. The original file is copied aside first
  // — that copy is what a mid-migration failure reopens to, since it is the
  // newest valid state-*.json in the backup dir.
  const int onDiskSchema = root.value("schemaVersion").toInt(1);
  if(onDiskSchema < heap::state::kSchemaVersion) {
    if(!viewOnly) {
      retainPreMigrationBackup(path, onDiskSchema);
    }
    heap::state::migrateState(root, onDiskSchema);
    if(!viewOnly) {
      heap::recovery::append(QString::fromLatin1(heap::recovery::kMigrated),
                             {{QStringLiteral("from"), onDiskSchema}, {QStringLiteral("to"), heap::state::kSchemaVersion}});
    }
  } else if(onDiskSchema > heap::state::kSchemaVersion && !viewOnly) {
    // Written by a newer build — running two builds against one data dir, or a
    // downgrade. There is no ladder downwards, and this build would silently
    // drop every field it does not know on the next save. Load what parses so
    // the user still sees their work, keep a copy, and block all saving — with
    // a banner that stays up, not a toast that is gone in two seconds.
    m_saveBlocked = true;
    retainPreMigrationBackup(path, onDiskSchema);
    setStorageStateT(
        QStringLiteral("tooNew"),
        {{QStringLiteral("data.schemaTooNew"), {QString::number(onDiskSchema), QString::number(heap::state::kSchemaVersion)}}});
    heap::recovery::append(QString::fromLatin1(heap::recovery::kSchemaTooNew),
                           {{QStringLiteral("onDisk"), onDiskSchema}, {QStringLiteral("supported"), heap::state::kSchemaVersion}});
    qWarning("state.json schema v%d is newer than this build's v%d — saving disabled", onDiskSchema, heap::state::kSchemaVersion);
  }

  // Keys this build does not know: kept (PLAT-26), from the first version that
  // carried them through on, as for profiles and tasks. Only the current
  // version used to qualify, so a 0.7.x (v11) file lost its root and settings
  // extras on the upgrade to v12 (APP-273).
  if(onDiskSchema >= heap::state::kPassThroughSince && onDiskSchema <= heap::state::kSchemaVersion) {
    m_rootExtra = unknownKeys(root, knownRootKeys());
    m_settingsExtra = unknownKeys(root.value("settings").toObject(), knownSettingsKeys());
  }
  // Task history (APP-165): a root key with no schema rung — absent means
  // none, and a build that does not know it carries it through untouched.
  m_history = heap::history::TaskHistory::fromJson(root.value(QStringLiteral("taskHistory")).toObject());
  m_taskSeq.clear();
  const QJsonObject seq = root.value("taskSeq").toObject();
  for(auto it = seq.constBegin(); it != seq.constEnd(); ++it) {
    if(it.value().isDouble() && it.value().toInt() > 0) {
      m_taskSeq.insert(it.key(), it.value().toInt());
    }
  }

  m_loading = true;

  // ----- settings (global) -----
  if(root.contains("settings")) {
    const QJsonObject s = root["settings"].toObject();
    // "What's new" once after an update to a new release line (R2-054): the
    // line this data was last run with, or — for a file from 0.7.x, which
    // did not record it — its schema says it came from before 0.8.
    if(!viewOnly) {
      const auto lineOf = [](const QString& v) {
        return v.section(QLatin1Char('.'), 0, 1);
      };
      const QString prev = s.value(QStringLiteral("lastRunVersion")).toString();
      m_whatsNewDue = prev.isEmpty() ? onDiskSchema < 12 : lineOf(prev) != lineOf(appVersion());
      // A patch release of the same line passes without the dialog, but
      // not without a word (A5-19): 0.8.2 → 0.8.9 renamed the app.
      m_updatedFrom = !m_whatsNewDue && !prev.isEmpty() && prev != appVersion() ? prev : QString();
    }
    if(s.contains("theme")) {
      m_theme = s["theme"].toString();
      emit themeChanged();
    }
    if(s.contains("density")) {
      m_density = s["density"].toString();
      emit densityChanged();
    }
    if(s.contains("language")) {
      const QString v = s["language"].toString();
      m_language = (v == "ru") ? QStringLiteral("ru") : QStringLiteral("en");
      if(pseudoLocale()) {
        m_languageUnderPseudo = m_language;
        m_language = QStringLiteral("en");
      }
      emit languageChanged();
    }
    if(s.contains("currentView")) {
      // A view this build does not have (a hand edit, an older --view typo
      // that got saved) would leave the content area blank on every launch.
      const QString v = s["currentView"].toString();
      m_currentView = heap::views::isKnown(v) ? heap::views::canonical(v) : QStringLiteral("today");
      emit currentViewChanged();
    }
    m_sectionViews.clear();
    const QJsonObject sv = s.value("sectionViews").toObject();
    for(auto it = sv.constBegin(); it != sv.constEnd(); ++it) {
      const QString v = heap::views::canonical(it.value().toString());
      if(heap::views::isKnown(v) && heap::views::sectionOf(v) == it.key()) {
        m_sectionViews.insert(it.key(), v);
      }
    }
    m_sectionViews.insert(heap::views::sectionOf(m_currentView), m_currentView);
    if(s.contains("workdayStart") || s.contains("workdayEnd")) {
      if(s.contains("workdayStart")) {
        m_workdayStart = s["workdayStart"].toInt(m_workdayStart);
      }
      if(s.contains("workdayEnd")) {
        m_workdayEnd = s["workdayEnd"].toInt(m_workdayEnd);
      }
      emit workdayChanged();
    }
    if(s.contains("crumbProject")) {
      m_crumbProject = s["crumbProject"].toString();
      emit crumbProjectChanged();
    }
    if(s.contains("crumbUser")) {
      m_crumbUser = s["crumbUser"].toString();
      emit crumbUserChanged();
    }
    // Onboarding flags. An existing state.json that predates them means a
    // returning user — don't show the welcome again (default welcomeSeen=true),
    // and there is no seeded demo to offer clearing (demoActive=false).
    m_welcomeSeen = s.contains("welcomeSeen") ? s["welcomeSeen"].toBool() : true;
    m_demoActive = s.contains("demoActive") ? s["demoActive"].toBool() : false;
    emit onboardingChanged();
    // Absent in every state.json written before writes became opt-in, which
    // is exactly who the one-time notice is for (APP-243).
    m_trackerWriteNoticeDone = s.value("trackerWriteNotice").toBool();
    if(!viewOnly) {
      m_installId = s.value("installId").toString();
    }
    if(s.contains("shortcuts")) {
      // A file written before kShortcutsSchema 2 stored every binding, not
      // only the rebound ones, so there is no way to tell a deliberate choice
      // from the default of the day. An entry matching the default it had back
      // then is dropped — otherwise the view shortcuts, which have since been
      // renumbered to follow the side rail, would stay pinned to the old
      // layout and leave two views fighting over Ctrl+4.
      const int storedSchema = s.value("shortcutsSchema").toInt(1);
      // The keymap the defaults come from. A file written before the choice
      // was on the Vim-based keymap of 0.8, and stays there: hands that
      // learnt it keep it (dogfood 2026-10-19). Only a new install starts on
      // Classic.
      const QString storedScheme = s.value("keymapScheme").toString();
      const QString scheme =
          storedScheme == QLatin1String("classic") || storedScheme == QLatin1String("vim") ? storedScheme : QStringLiteral("vim");
      if(scheme != m_keymapScheme) {
        m_keymapScheme = scheme;
        m_shortcuts.clear();
        seedShortcutCatalog();
        emit keymapSchemeChanged();
      }
      const bool legacy = storedSchema < 2;
      QVariantMap overrides;
      const QJsonObject shortcutsObj = s["shortcuts"].toObject();
      for(auto it = shortcutsObj.constBegin(); it != shortcutsObj.constEnd(); ++it) {
        if(legacy && isLegacyShortcutDefault(it.key(), it.value().toString())) {
          continue;  // it was the default, not a choice
        }
        overrides.insert(it.key(), it.value().toString());
      }
      if(storedSchema < kShortcutsSchema) {
        applyKeymapMigration(overrides, storedSchema);
      }
      applyShortcutOverrides(overrides);
    }
    QJsonObject app = s.value("app").toObject();
    // APP-167's completion-sound switch is the Sound switch now (APP-177).
    bool appChanged = s.contains("app") && s["app"].isObject();
    heap::platform::migrateLegacySoundSetting(app);
    if(migrateExampleProfileBindings(app)) {
      appChanged = true;
    }
    if(!viewOnly) {
      m_gitLineDefaultDone = s.value(QStringLiteral("gitLineDefault")).toBool();
      if(!m_gitLineDefaultDone && migrateGitWorkingLine(app)) {
        appChanged = true;
      }
      m_gitLineDefaultDone = true;
      if(!s.value(QStringLiteral("themeLowkey")).toBool() && migrateLowkeyTheme(app)) {
        appChanged = true;
      }
      m_themeLowkeyDone = true;
    }
    if(appChanged) {
      m_appSettingsJson = QJsonDocument(app).toJson(QJsonDocument::Compact);
      emit appSettingsJsonChanged();
    }
  } else {
    // A document with no settings object at all is still a returning user.
    m_welcomeSeen = true;
    emit onboardingChanged();
    if(!viewOnly) {
      m_gitLineDefaultDone = true;
      m_themeLowkeyDone = true;
      QJsonObject app;
      const bool themed = migrateLowkeyTheme(app);
      if(migrateGitWorkingLine(app) || themed) {
        m_appSettingsJson = QJsonDocument(app).toJson(QJsonDocument::Compact);
        emit appSettingsJsonChanged();
      }
    }
  }

  // Structural decisions below key off what was on disk, not the migrated value.
  const int schema = onDiskSchema;
  QVector<CalEvent> globalEvents;
  bool exampleRenamed = false;

  if(schema >= 2 && root.value("profiles").isArray()) {
    // ----- schema v2 / v3: profiles array -----
    for(const auto& it : root["profiles"].toArray()) {
      if(!it.isObject()) {
        continue;
      }
      // For v2, profiles still carried their own events — hoist them
      // into the global pool tagged with the source profile id.
      QVector<CalEvent> legacy;
      Profile p = heap::state::profileFromJson(it.toObject(), schema < 3 ? &legacy : nullptr);
      if(schema < heap::state::kPassThroughSince) {
        heap::state::dropPassThrough(p);  // pass-through is for the version this build writes
      }
      // Two profiles under one id cannot both be addressed; the second one
      // gets its own rather than shadowing the first.
      if(p.id.isEmpty() || profileIndexOf(p.id) >= 0) {
        const QString oldId = p.id;
        p.id = makeProfileId(p.name.isEmpty() ? QStringLiteral("profile") : p.name);
        for(CalEvent& e : legacy) {
          if(e.profileId == oldId) {
            e.profileId = p.id;
          }
        }
      }
      m_profiles.push_back(p);
      if(!legacy.isEmpty()) {
        globalEvents.append(legacy);
      }
    }
    m_activeProfileId = root.value("activeProfileId").toString();
    if(profileIndexOf(m_activeProfileId) < 0 && !m_profiles.isEmpty()) {
      m_activeProfileId = m_profiles.first().id;
    }
    // schema v3 keeps events at top level.
    if(schema >= 3 && root.contains("events")) {
      globalEvents = heap::state::eventsFromJson(root["events"].toArray());
    }
    if(schema < heap::state::kPassThroughSince) {
      heap::state::dropPassThrough(globalEvents);
    }
    exampleRenamed = migrateExampleProfileId(globalEvents);
  } else {
    // ----- schema v1: flat fields → wrap into one "Example" profile -----
    Profile p;
    p.id = "default";
    p.name = "Example";
    p.color = "#5cc2dd";
    p.createdAt = QDateTime::currentDateTime();
    if(root.contains("tasks")) {
      p.tasks = heap::state::tasksFromJson(root["tasks"].toArray());
    }
    if(root.contains("people")) {
      p.people = heap::state::peopleFromJson(root["people"].toArray());
    }
    if(root.contains("statuses")) {
      p.statuses = heap::state::statusesFromJson(root["statuses"].toArray());
    }
    if(root.contains("docs")) {
      p.docsState = QJsonDocument(root["docs"].toObject()).toJson(QJsonDocument::Compact);
    }
    // Hoist any legacy top-level events into the global pool.
    if(root.contains("events")) {
      globalEvents = heap::state::eventsFromJson(root["events"].toArray(), p.id);
    }
    heap::state::dropPassThrough(p);
    heap::state::dropPassThrough(globalEvents);
    m_profiles.push_back(p);
    m_activeProfileId = p.id;
  }

  // A profile with no columns is a board nothing can be put on (PLAT-7).
  for(Profile& p : m_profiles) {
    if(p.statuses.isEmpty()) {
      for(const auto& m : SampleData::statuses(m_language == QStringLiteral("ru") ? SampleData::Lang::Ru : SampleData::Lang::En)) {
        p.statuses.append(m);
      }
    }
  }

  m_events.reset(globalEvents);

  if(!m_profiles.isEmpty()) {
    const int ai = qMax(0, profileIndexOf(m_activeProfileId));
    applyProfileToModels(m_profiles[ai]);
  }

  m_loading = false;

  emit profilesChanged();
  emit activeProfileChanged();

  // Force a rewrite so the on-disk file lands at the current schema version
  // (or drops an id an older version wrote).
  if(schema < heap::state::kSchemaVersion || exampleRenamed) {
    scheduleSave();
  }
}

void AppController::retainPreMigrationBackup(const QString& path, int fromVersion) {
  const QString dir = backupDirPath();
  const QString prefix = "state-premigration-v" + QString::number(fromVersion) + "-";
  const bool fromNewer = fromVersion > heap::state::kSchemaVersion;
  if(fromNewer) {
    // An older build opening a newer file does so on every launch, and the
    // file rarely changes in between: one copy per distinct content is enough.
    QFile live(path);
    const QByteArray bytes = live.open(QIODevice::ReadOnly) ? live.readAll() : QByteArray();
    const QDir d(dir);
    for(const QString& name : d.entryList({prefix + "*.json"}, QDir::Files)) {
      QFile other(d.filePath(name));
      if(other.size() == bytes.size() && other.open(QIODevice::ReadOnly) && other.readAll() == bytes) {
        return;
      }
    }
  }
  const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
  const QString target = dir + "/" + prefix + stamp + ".json";
  if(QFile::exists(target) || !QFile::copy(path, target)) {
    return;
  }
  heap::recovery::append(QString::fromLatin1(heap::recovery::kPreMigration),
                         {{QStringLiteral("from"), path}, {QStringLiteral("to"), target}, {QStringLiteral("schema"), fromVersion}});
  if(fromNewer) {
    QDir d(dir);
    QStringList copies = d.entryList({prefix + "*.json"}, QDir::Files, QDir::Name);
    while(copies.size() > kNewerSchemaCopiesKept) {
      d.remove(copies.takeFirst());
    }
  }
}

// ───────────────────────────────────────────────────── Profiles API ──

QVariantList AppController::profiles() const {
  QVariantList out;
  for(const Profile& p : m_profiles) {
    QVariantMap m;
    m["id"] = p.id;
    m["name"] = p.name;
    m["color"] = p.color;
    // The active profile's copy is refreshed on save; its live models are now.
    const bool live = p.id == m_activeProfileId;
    m["tasks"] = live ? m_tasks.items().size() : p.tasks.size();
    m["notes"] = live ? m_notes.items().size() : p.notes.size();
    m["docs"] = p.docsState.size() > 2 ? 1 : 0;
    m["createdAt"] = p.createdAt.isValid() ? p.createdAt.toString(Qt::ISODate) : QString();
    out.append(m);
  }
  return out;
}

void AppController::setActiveProfileId(const QString& id) {
  if(id == m_activeProfileId) {
    return;
  }
  const int next = profileIndexOf(id);
  if(next < 0) {
    return;
  }
  clearSelection();
  // The notes editor's unsaved keystrokes belong to this profile's note.
  emit aboutToChangeActiveNote();
  snapshotActiveProfile();
  // Undo is scoped to the workspace it was recorded in (PLAT-15/TASKS-1): the
  // entries are diffs of the active models, and replaying one onto another
  // profile's models deleted or duplicated that profile's tasks. A deleted
  // profile stays restorable (IDIOT-SHELL-2). The workspace's own history is
  // parked, not dropped, and comes back with it (A3-14, A5-7).
  parkWorkspaceUndo(m_activeProfileId);
  clearWorkspaceUndo();
  m_activeProfileId = id;
  applyProfileToModels(m_profiles[next]);
  restoreWorkspaceUndo(id);
  emit activeProfileChanged();
  scheduleSave();
}

bool AppController::profileNameTaken(const QString& name, const QString& exceptId) const {
  for(const Profile& p : m_profiles) {
    if(p.id != exceptId && p.name.trimmed().compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

QString AppController::uniqueProfileName(const QString& base) const {
  if(!profileNameTaken(base, QString())) {
    return base;
  }
  // "Work", "Work (2)", "Work (3)" — the switcher shows names, so two equal
  // ones cannot be told apart there.
  int n = 2;
  QString candidate;
  do {
    candidate = QStringLiteral("%1 (%2)").arg(base).arg(n++);
  } while(profileNameTaken(candidate, QString()));
  return candidate;
}

QString AppController::createProfile(const QString& rawName, const QString& color) {
  const QString name = rawName.trimmed().left(kMaxProfileName);
  if(name.trimmed().isEmpty()) {
    return QString();
  }
  if(profileNameTaken(name, QString())) {
    emit toast(tr_("profile.nameTaken").arg(name.trimmed()), QStringLiteral("warning"));
    return QString();
  }
  // Snapshot current active before creating so we don't lose unsaved edits.
  snapshotActiveProfile();
  parkWorkspaceUndo(m_activeProfileId);
  clearWorkspaceUndo();  // undo is scoped to the active workspace
  Profile p = makeStartingProfile(name.trimmed(), color);
  p.id = makeProfileId(name.trimmed());
  m_profiles.push_back(p);
  m_activeProfileId = p.id;
  applyProfileToModels(p);
  emit profilesChanged();
  emit activeProfileChanged();
  emit toast(tr_("profile.created").arg(p.name));
  scheduleSave();
  return p.id;
}

void AppController::renameProfile(const QString& id, const QString& rawName) {
  const QString newName = rawName.trimmed().left(kMaxProfileName);
  const int i = profileIndexOf(id);
  if(i < 0 || newName.trimmed().isEmpty()) {
    return;
  }
  if(m_profiles[i].name == newName) {
    return;
  }
  if(profileNameTaken(newName, id)) {
    emit toast(tr_("profile.nameTaken").arg(newName.trimmed()), QStringLiteral("warning"));
    emit profilesChanged();  // the editor falls back to the stored name
    return;
  }
  m_profiles[i].name = newName.trimmed();
  emit profilesChanged();
  if(id == m_activeProfileId) {
    emit activeProfileChanged();
  }
  scheduleSave();
}

void AppController::setProfileColor(const QString& id, const QString& color) {
  const int i = profileIndexOf(id);
  if(i < 0) {
    return;
  }
  const QColor c(color);
  if(!c.isValid()) {
    return;
  }
  m_profiles[i].color = c.name();
  emit profilesChanged();
  if(id == m_activeProfileId) {
    emit activeProfileChanged();
  }
  scheduleSave();
}

void AppController::deleteProfile(const QString& id) {
  const int i = profileIndexOf(id);
  if(i < 0 || m_profiles.size() <= 1) {
    return;  // never let the app run out of profiles
  }
  snapshotActiveProfile();
  // The dialog promises a snapshot to restore from (IDIOT-SHELL-2): the
  // in-memory undo is only good until the app closes.
  takeSnapshotNow(QStringLiteral("profile"));
  // The history of the workspace being deleted must not be replayed onto the
  // one that takes its place (PLAT-15); only the deletion itself stays undoable.
  if(id == m_activeProfileId) {
    m_undo.clear();
  }
  m_parkedUndo.remove(id);
  // Events that were attributed to this profile become "unassigned"
  // (still visible in the calendar, but with no feature dot). Their ids go
  // into the entry so the undo can give them back.
  QStringList detached;
  for(int r = 0; r < m_events.rowCount(); ++r) {
    const QModelIndex mi = m_events.index(r, 0);
    if(m_events.data(mi, EventModel::ProfileIdRole).toString() == id) {
      const CalEvent& e = m_events.items().at(r);
      CalEvent copy = e;
      detached.append(copy.id);
      copy.profileId.clear();
      m_events.upsert(copy);
    }
  }

  // Restoring a profile swaps every collection at once, so this is not a diff.
  // It also invalidates whatever the stack held about the old workspace, which
  // is why it goes in alone.
  heap::undo::Entry entry;
  entry.profileRemoved = true;
  entry.profile = m_profiles[i];
  entry.profileRow = i;
  entry.profileEventIds = detached;
  entry.label = tr_("profile.restored").arg(m_profiles[i].name);
  m_undo.pushProfileRemoval(std::move(entry));
  emit pendingUndoChanged();
  const QString name = m_profiles[i].name;
  m_profiles.removeAt(i);

  // If we deleted the active one, fall back to its neighbour.
  if(id == m_activeProfileId) {
    const int fallback = qMin(i, m_profiles.size() - 1);
    m_activeProfileId = m_profiles[fallback].id;
    applyProfileToModels(m_profiles[fallback]);
    emit activeProfileChanged();
  }
  emit profilesChanged();
  emit undoableToast(tr_("profile.deleted").arg(name), 5);
  scheduleSave();
}

QVariantMap AppController::profileById(const QString& id) const {
  const int i = profileIndexOf(id);
  if(i < 0) {
    return {};
  }
  const Profile& p = m_profiles[i];
  QVariantMap m;
  m["id"] = p.id;
  m["name"] = p.name;
  m["color"] = p.color;
  return m;
}

QHash<QString, QString> AppController::reissueSharedTaskIds(Profile& p, QVector<CalEvent>* events) {
  // A task id is a key across the whole app, not per profile (TASKS-1): the
  // reminder log, notification actions, undo and event links all look a task
  // up by id alone. A copied or imported profile that kept its ids silenced
  // the other profile's reminders and let "Mark done" close the wrong task
  // (PLAT-9). Every id another profile already holds is given a fresh one.
  QSet<QString> taken;
  for(const Task& t : m_tasks.items()) {
    taken.insert(t.id);
  }
  for(const Profile& other : m_profiles) {
    for(const Task& t : other.tasks) {
      taken.insert(t.id);
    }
  }
  // The ids that stay are claimed first, so no fresh one lands on them.
  for(const Task& t : p.tasks) {
    if(!taken.contains(t.id)) {
      noteTaskIdUsed(t.id);
    }
  }
  QSet<QString> used = taken;
  for(const Task& t : p.tasks) {
    used.insert(t.id);
  }
  QHash<QString, QString> remap;
  for(Task& t : p.tasks) {
    if(!taken.contains(t.id)) {
      continue;
    }
    QString stem;
    int n = 0;
    QString fresh;
    if(t.externalId.isEmpty() && splitTaskId(t.id, stem, n)) {
      // The next number under the same prefix, the way a new task gets one.
      do {
        fresh = mintTaskId(stem);
        noteTaskIdUsed(fresh);
      } while(used.contains(fresh));
    } else {
      // A mirrored issue's id spells its tracker key (jira-LUX-1), and
      // renumbering it would name another issue: suffix it instead, the way
      // a second pull of the same key is told apart.
      int k = 2;
      do {
        fresh = t.id + QChar('-') + QString::number(k++);
      } while(used.contains(fresh));
    }
    used.insert(fresh);
    remap.insert(t.id, fresh);
    t.id = fresh;
  }
  if(remap.isEmpty()) {
    return remap;
  }

  // Everything inside the profile that names a task by id follows it: the
  // dependency links, and the #KEY-1 references of descriptions, notes and
  // pages (the same pattern the markdown renderer turns into a task link).
  static const QRegularExpression kTicketRef(QStringLiteral("(?<![A-Za-z0-9_])#([A-Z][A-Z0-9]*-\\d+)"));
  const auto rewrite = [&remap](QString& text) {
    if(!text.contains(QLatin1Char('#'))) {
      return;
    }
    QString out;
    qsizetype last = 0;
    for(auto it = kTicketRef.globalMatch(text); it.hasNext();) {
      const QRegularExpressionMatch m = it.next();
      const auto hit = remap.constFind(m.captured(1));
      if(hit == remap.constEnd()) {
        continue;
      }
      out += QStringView(text).mid(last, m.capturedStart(1) - last);
      out += hit.value();
      last = m.capturedEnd(1);
    }
    if(last > 0) {
      out += QStringView(text).mid(last);
      text = out;
    }
  };
  for(Task& t : p.tasks) {
    for(TaskLink& l : t.links) {
      l.targetId = remap.value(l.targetId, l.targetId);
    }
    rewrite(t.desc);
  }
  for(Note& n : p.notes) {
    rewrite(n.body);
  }
  rewrite(p.notesState);
  for(DocPage& d : p.docPages) {
    rewrite(d.body);
  }
  if(events) {
    for(CalEvent& e : *events) {
      e.taskId = remap.value(e.taskId, e.taskId);
    }
  }
  return remap;
}

void AppController::addEventsAsCopies(const QVector<CalEvent>& events, const QString& profileId) {
  QHash<QString, QString> eventIds;
  for(const CalEvent& e : events) {
    eventIds.insert(e.id, mintEventId());
  }
  for(CalEvent e : events) {
    e.id = eventIds.value(e.id);
    // An override stands in for an occurrence of its own copy's series. Left
    // on the original's, it hid the original's own override for that day.
    e.masterId = eventIds.value(e.masterId, e.masterId);
    e.profileId = profileId;
    m_events.upsert(e);
  }
}

QString AppController::duplicateProfile(const QString& id, const QString& newName) {
  const int i = profileIndexOf(id);
  if(i < 0) {
    return QString();
  }
  if(id == m_activeProfileId) {
    snapshotActiveProfile();
  }
  Profile copy = m_profiles[i];
  copy.name = uniqueProfileName(newName.trimmed().isEmpty() ? (m_profiles[i].name + " copy") : newName.trimmed().left(kMaxProfileName));
  copy.id = makeProfileId(copy.name);
  copy.createdAt = QDateTime::currentDateTime();
  // Events live in the global pool, attributed to a profile by id: the copy
  // gets its own of each, the way an export carries them (PLAT-10).
  QVector<CalEvent> events;
  for(const CalEvent& e : m_events.items()) {
    if(!id.isEmpty() && e.profileId == id) {
      events.append(e);
    }
  }
  reissueSharedTaskIds(copy, &events);  // the copy's tasks are new tasks (PLAT-9)
  addEventsAsCopies(events, copy.id);
  parkWorkspaceUndo(m_activeProfileId);
  clearPendingUndo();  // undo is scoped to the active workspace
  m_profiles.push_back(copy);
  m_activeProfileId = copy.id;
  applyProfileToModels(copy);
  emit profilesChanged();
  emit activeProfileChanged();
  emit toast(tr_("profile.duplicated").arg(copy.name));
  scheduleSave();
  return copy.id;
}

int AppController::renameTaskIdPrefix(const QString& oldPrefix, const QString& newPrefix) {
  const QString from = oldPrefix.trimmed().toUpper();
  const QString to = newPrefix.trimmed().toUpper();
  if(from.isEmpty() || to.isEmpty() || from == to) {
    return 0;
  }

  const QRegularExpression rx(QStringLiteral("^") + QRegularExpression::escape(from) + QStringLiteral("-(\\d+)$"),
                              QRegularExpression::CaseInsensitiveOption);

  // This profile's own tasks only (IDIOT-TASKS-2): the prefix is this
  // profile's setting, and a tracker ticket keyed APP-123 is the tracker's.
  QHash<QString, QString> remap;  // old id → new id
  QSet<QString> kept;             // the ids that stay as they are
  for(const Task& t : m_tasks.items()) {
    const auto m = rx.match(t.id);
    if(m.hasMatch() && t.externalProvider.isEmpty()) {
      remap.insert(t.id, to + QChar('-') + m.captured(1));
    } else {
      kept.insert(t.id);
    }
  }
  if(remap.isEmpty()) {
    return 0;
  }
  // A new id another task already holds made two rows with one id, and a
  // delete of one of them could not be undone (IDIOT-TASKS-1).
  QStringList taken;
  for(const QString& next : std::as_const(remap)) {
    if(kept.contains(next)) {
      taken << next;
    }
  }
  if(!taken.isEmpty()) {
    taken.sort();
    emit toast(tr_("tasks.renameTaken").arg(taken.constFirst()).arg(taken.size()));
    return 0;
  }

  // One undo step, links included (IDIOT-TASKS-2).
  const UndoScope scope(this, tr_("tasks.renamed").arg(remap.size()));
  const auto mapped = [&remap](const QString& id) {
    return remap.value(id, id);
  };
  QVector<Task> tasks = m_tasks.items();
  for(Task& t : tasks) {
    t.id = mapped(t.id);
    for(TaskLink& l : t.links) {
      l.targetId = mapped(l.targetId);
    }
    for(LocalLink& l : t.local.related) {
      if(l.profileId.isEmpty() || l.profileId == m_activeProfileId) {
        l.target = mapped(l.target);
      }
    }
    for(LocalCheckItem& c : t.local.checklist) {
      if(!c.cardId.isEmpty()) {
        c.cardId = mapped(c.cardId);
      }
    }
    noteTaskIdUsed(t.id);
  }
  m_tasks.reset(tasks);
  for(const CalEvent& event : m_events.items()) {
    if(remap.contains(event.taskId)) {
      CalEvent copy = event;
      copy.taskId = remap.value(event.taskId);
      m_events.upsert(copy);
    }
  }
  snapshotActiveProfile();
  scheduleSave();
  emit toast(tr_("tasks.renamed").arg(remap.size()));
  return static_cast<int>(remap.size());
}

// ───────────────────────────────────────────────────── Backups API ──

QVariantList AppController::listBackups() const {
  QVariantList out;
  const QDir d(backupDirPath());
  const QStringList files = d.entryList({"state-*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
  for(const QString& name : files) {
    const QFileInfo fi(d.filePath(name));
    QVariantMap m;
    m["fileName"] = name;
    m["sizeKb"] = (fi.size() / 1024);
    m["mtime"] = fi.lastModified().toString(Qt::ISODate);
    out.append(m);
  }
  return out;
}

bool AppController::restoreFromBackup(const QString& fileName) {
  // Names come from listBackups(); anything with a path in it is not one.
  if(fileName.isEmpty() || fileName.contains('/') || fileName.contains(QChar(0x5C))) {
    return false;
  }
  const QString src = backupDirPath() + "/" + fileName;
  QFile in(src);
  if(!in.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QByteArray bytes = in.readAll();
  in.close();
  const QJsonDocument doc = QJsonDocument::fromJson(bytes);
  if(doc.isNull() || !doc.isObject() || !heap::storage::validateShape(doc.object())) {
    emit toast(tr_("backup.invalid").arg(fileName));
    return false;
  }

  // The current state is snapshotted first, every time (PLAT-3). The rotation
  // clock is the wrong gate here: with a daily backup the newest copy is
  // almost always under 24 h old, so "restore" used to throw away everything
  // since it for good. A read-only session writes nothing of its own, but the
  // file it could not save over is still copied aside — and if even that is
  // impossible (it is locked), the restore does not go ahead.
  if(!m_saveBlocked) {
    if(m_saveTimer) {
      m_saveTimer->stop();
    }
    saveStateNow();
  }
  if(QFile::exists(stateFilePath()) && snapshotStateToBackups(QStringLiteral("-prerestore")).isEmpty()) {
    emit toast(tr_("backup.snapshotFailed"));
    return false;
  }
  if(!replaceStateFile(bytes)) {
    return false;
  }
  emit toast(tr_("backup.restored").arg(fileName));
  return true;
}

bool AppController::replaceStateFile(const QByteArray& bytes) {
  // Every task id handed out so far, in any profile, before the state that
  // holds them goes (TM-2).
  const QHash<QString, int> seqBefore = m_taskSeq;
  QStringList idsBefore;
  for(const Profile& p : m_profiles) {
    for(const Task& t : p.id == m_activeProfileId ? m_tasks.items() : p.tasks) {
      idsBefore.append(t.id);
    }
  }
  QString error;
  if(!heap::storage::writeAtomically(stateFilePath(), bytes, &error)) {
    setStorageStateT(QStringLiteral("writeFailed"),
                     {{QStringLiteral("storage.writeFailed"), {QDir::toNativeSeparators(stateFilePath()), error}}});
    return false;
  }
  // Reload from disk. The undo history described the state that was just
  // replaced, so it goes with it (reloadStateFromDisk clears it).
  reloadStateFromDisk();
  // The id counter never goes back, whether the state came from the time
  // machine or from backups/. Restored to the older file's, it handed the next
  // new task the id of one the restore had just removed — and that one is
  // still in the "before restore" copy, where it then read as an edit of the
  // new task, and "Use this version" overwrote the new task with it.
  const QHash<QString, int> restored = m_taskSeq;
  for(auto it = seqBefore.constBegin(); it != seqBefore.constEnd(); ++it) {
    m_taskSeq.insert(it.key(), qMax(it.value(), m_taskSeq.value(it.key(), 1)));
  }
  for(const QString& id : std::as_const(idsBefore)) {
    noteTaskIdUsed(id);
  }
  if(m_taskSeq != restored) {
    scheduleSave();
  }
  return true;
}

// ───────────────────────────────────────────── Command palette source ──

QVariantList AppController::fullTextEntries(const Profile& p) const {
  QVariantList out;
  // One entry per heading rather than one per document: a section gives the
  // reader a place to land and a snippet worth showing.
  const auto sectionsOf =
      [&out, &p](
          const QString& kind, const QString& idKey, const QString& id, const QString& title, const QString& where, const QString& body) {
        bool any = false;
        for(const heap::md::MdSection& section : heap::md::searchSections(body)) {
          if(section.body.trimmed().isEmpty() && section.title.trimmed().isEmpty()) {
            continue;
          }
          // The note's own H1 is its title; repeating it as "Standup › Standup"
          // says nothing.
          QString sec = section.title;
          if(sec.startsWith(title + QStringLiteral(" · "), Qt::CaseInsensitive)) {
            sec = sec.mid(title.size() + 3);
          }
          const bool isTitle = sec.isEmpty() || sec.compare(title, Qt::CaseInsensitive) == 0;
          QVariantMap m;
          m["kind"] = kind;
          m["label"] = isTitle ? title : QStringLiteral("%1 › %2").arg(title, sec);
          m["sub"] = where;
          m["body"] = section.body;
          m["profileId"] = p.id;
          m[idKey] = id;
          m["line"] = section.line;
          out.append(m);
          any = true;
        }
        if(!any) {
          // An empty note is still somewhere to go by its title.
          QVariantMap m;
          m["kind"] = kind;
          m["label"] = title;
          m["sub"] = where;
          m["body"] = QString();
          m["profileId"] = p.id;
          m[idKey] = id;
          m["line"] = 0;
          out.append(m);
        }
      };
  for(const Note& n : p.notes) {
    const QString where = n.folder.isEmpty() ? QStringLiteral("%1 · %2").arg(p.name, tr_("search.notes"))
                                             : QStringLiteral("%1 · %2 / %3").arg(p.name, tr_("search.notes"), n.folder);
    sectionsOf(QStringLiteral("note"), QStringLiteral("noteId"), n.id, n.title, where, n.body);
  }
  for(const DocPage& d : p.docPages) {
    sectionsOf(QStringLiteral("docPage"),
               QStringLiteral("pageId"),
               d.id,
               d.title,
               QStringLiteral("%1 · %2").arg(p.name, tr_("search.docs")),
               d.body);
  }
  return out;
}

QVariantList AppController::searchFullText(const QString& query, int limit) const {
  const QStringList terms = query.simplified().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  QVariantList out;
  if(terms.isEmpty()) {
    return out;
  }
  emit const_cast<AppController*>(this)->flushEditorsRequested();
  const_cast<AppController*>(this)->snapshotActiveProfile();
  for(const Profile& p : m_profiles) {
    for(const QVariant& v : fullTextEntries(p)) {
      QVariantMap m = v.toMap();
      const QString hay = (m.value("label").toString() + QLatin1Char(' ') + m.value("body").toString()).toLower();
      bool all = true;
      for(const QString& t : terms) {
        if(!hay.contains(t)) {
          all = false;
          break;
        }
      }
      if(!all) {
        continue;
      }
      m["color"] = p.color;
      out.append(m);
      if(limit > 0 && out.size() >= limit) {
        return out;
      }
    }
  }
  return out;
}

namespace {
// The same keys in the other keyboard layout, ЙЦУКЕН ↔ QWERTY: "cjpdjy" is
// "созвон" typed with the wrong layout on (A4-20). Empty when nothing swaps.
QString swapKeyboardLayout(const QString& text) {
  static const QString kLat = QStringLiteral("`qwertyuiop[]asdfghjkl;'zxcvbnm,");
  static const QString kCyr = QStringLiteral("ёйцукенгшщзхъфывапролджэячсмитьб");
  int latin = 0;
  int cyr = 0;
  for(const QChar c : text) {
    const QChar l = c.toLower();
    latin += l >= QLatin1Char('a') && l <= QLatin1Char('z') ? 1 : 0;
    cyr += kCyr.contains(l) ? 1 : 0;
  }
  if(latin == 0 && cyr == 0) {
    return {};
  }
  const QString& from = latin >= cyr ? kLat : kCyr;
  const QString& to = latin >= cyr ? kCyr : kLat;
  QString out;
  out.reserve(text.size());
  for(const QChar c : text) {
    const QChar l = c.toLower();
    const qsizetype at = from.indexOf(l);
    if(at < 0) {
      out.append(c);
      continue;
    }
    out.append(c == l ? to.at(at) : to.at(at).toUpper());
  }
  return out == text ? QString() : out;
}
}  // namespace

QStringList AppController::notesMatching(const QString& query) const {
  QStringList out = notesMatchingExactly_(query);
  // Nothing: perhaps the right words in the wrong layout (A4-20).
  if(out.isEmpty()) {
    const QString swapped = swapKeyboardLayout(query);
    if(!swapped.isEmpty()) {
      out = notesMatchingExactly_(swapped);
    }
  }
  return out;
}

QStringList AppController::notesMatchingExactly_(const QString& query) const {
  // Title, folder and the whole body, every word required. The list filter
  // used to look at the title and a one-line excerpt only, so a word from the
  // middle of a note found nothing.
  const QStringList terms = query.simplified().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  QStringList out;
  for(const Note& n : m_notes.items()) {
    bool all = true;
    QStringList tags;
    bool tagsRead = false;
    for(const QString& t : terms) {
      // "#hr" (a label chip, a tag clicked in a note): that tag exactly.
      if(t.size() > 1 && t.startsWith(QLatin1Char('#'))) {
        if(!tagsRead) {
          tags = heap::notes::noteTags(n.id == m_activeNoteId ? m_notesState : n.body);
          tagsRead = true;
        }
        if(!tags.contains(t.mid(1))) {
          all = false;
          break;
        }
        continue;
      }
      if(!n.title.contains(t, Qt::CaseInsensitive) && !n.folder.contains(t, Qt::CaseInsensitive) &&
         !n.body.contains(t, Qt::CaseInsensitive)) {
        all = false;
        break;
      }
    }
    if(all) {
      out << n.id;
    }
  }
  return out;
}

QStringList AppController::docPagesMatching(const QString& query) const {
  const QStringList terms = query.simplified().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  QStringList out;
  for(const DocPage& d : m_docPages.items()) {
    bool all = true;
    for(const QString& t : terms) {
      if(!d.title.contains(t, Qt::CaseInsensitive) && !d.body.contains(t, Qt::CaseInsensitive)) {
        all = false;
        break;
      }
    }
    if(all) {
      out << d.id;
    }
  }
  return out;
}

QVariantList AppController::commandPaletteEntries() const {
  // The palette searches the persisted profiles, not the live models, and the
  // live models only reach a profile when a save runs. That save is debounced
  // by 300 ms, so a task created or edited a moment ago was simply missing
  // from Ctrl+K. Push the active profile's rows across first — it is the same
  // snapshot a save would take, and it costs nothing when nothing changed.
  // The editors go first: a word typed a moment ago is still only in a field.
  emit const_cast<AppController*>(this)->flushEditorsRequested();
  const_cast<AppController*>(this)->snapshotActiveProfile();

  QVariantList out;

  // Today's note. A daily note that has to be made by hand is one people
  // stop making, so it is one keystroke away from anywhere.
  {
    QVariantMap m;
    m["kind"] = "dailyNote";
    m["label"] = tr_("notes.daily");
    m["sub"] = QDate::currentDate().toString(Qt::ISODate);
    m["color"] = QStringLiteral("#7bc47f");
    out.append(m);
  }

  // Task templates (HEAP-77) — "New from template: …" actions.
  for(const auto& t : builtinTemplates()) {
    QVariantMap m;
    m["kind"] = "template";
    m["label"] = tr_("palette.fromTemplate").arg(t.name);
    m["sub"] = tr_("palette.templateSub");
    m["templateName"] = t.name;
    m["color"] = QStringLiteral("#b58ad7");
    out.append(m);
  }

  // Profiles themselves.
  for(const Profile& p : m_profiles) {
    QVariantMap m;
    m["kind"] = "profile";
    m["label"] = p.name;
    m["sub"] = tr_("palette.profileTasks").arg(p.tasks.size());
    m["profileId"] = p.id;
    m["color"] = p.color;
    out.append(m);
  }

  auto statusName = [](const QVariantList& statuses, const QString& id) {
    for(const QVariant& v : statuses) {
      const QVariantMap m = v.toMap();
      if(m.value("id").toString() == id) {
        return m.value("name").toString();
      }
    }
    return id;
  };

  // Full-text search (HEAP-80): each entry carries a `body` of the searchable
  // text that is NOT already in label/sub (task descriptions, note/doc/snippet
  // bodies, …). The palette scores over label+sub+body and shows a snippet when
  // the hit is in the body. Kept bounded so rebuilding the list stays cheap.
  constexpr int kBodyCap = 8000;
  const auto cap = [](QString s) {
    if(s.size() > kBodyCap) {
      s.truncate(kBodyCap);
    }
    return s;
  };

  for(const Profile& p : m_profiles) {
    // Every note of the profile, and every doc page. This used to index
    // `notesState` — the one note that happened to be open — so Ctrl+K could
    // not find a word in any other note, or in the docs at all.
    for(const QVariant& v : fullTextEntries(p)) {
      QVariantMap m = v.toMap();
      m["body"] = cap(m.value("body").toString());
      m["color"] = p.color;
      out.append(m);
    }
    // Tasks
    for(const Task& t : p.tasks) {
      QVariantMap m;
      m["kind"] = "task";
      // A mirrored ticket is named by its tracker key ("PROJ-123", "#42"),
      // as its card names it; its local id is only a lookup (A3-10).
      const QString key = externalKeyOf(t);
      m["label"] = QString("%1 · %2").arg(key.isEmpty() ? t.id : key, t.title);
      // The column as the board names it, not its raw id (A1-16).
      QString column = statusName(p.statuses, t.status);
      if(column == t.status) {
        column = statusName(m_statuses, t.status);
      }
      m["sub"] = QString("%1 · %2").arg(p.name, column);
      if(!t.externalProvider.isEmpty()) {
        m["sub"] = QString("%1 · %2").arg(m["sub"].toString(), providerDisplayName(t.externalProvider));
      }
      m["body"] = cap(t.desc);
      m["profileId"] = p.id;
      m["taskId"] = t.id;
      m["color"] = p.color;
      out.append(m);
    }
    // Docs / snippets / contacts (parsed from docsState JSON blob)
    if(!p.docsState.isEmpty()) {
      const QJsonDocument d = QJsonDocument::fromJson(p.docsState.toUtf8());
      if(!d.isNull() && d.isObject()) {
        const QJsonObject root = d.object();
        for(const auto& sIt : root["sections"].toArray()) {
          const QJsonObject sec = sIt.toObject();
          const QString secId = sec["id"].toString();
          const QString secTitle = sec["title"].toString();
          for(const auto& iIt : sec["items"].toArray()) {
            const QJsonObject it = iIt.toObject();
            QVariantMap m;
            m["kind"] = "doc";
            m["label"] = QString("%1 · %2").arg(it["ref"].toString(), it["title"].toString());
            m["title"] = it["title"].toString();
            m["sub"] = QString("%1 · %2").arg(p.name, secTitle);
            m["body"] = cap(it["desc"].toString() + QChar(' ') + it["source"].toString());
            m["profileId"] = p.id;
            m["sectionId"] = secId;
            m["color"] = p.color;
            out.append(m);
          }
        }
        int snipIdx = 0;
        for(const auto& snIt : root["snippets"].toArray()) {
          const QJsonObject sn = snIt.toObject();
          QVariantMap m;
          m["kind"] = "snippet";
          m["label"] = sn["title"].toString();
          m["sub"] = QString("%1 · %2").arg(p.name, sn["lang"].toString());
          // Full-text (HEAP-79): language + tags + code so the palette can find
          // a snippet by tag or language, not just its title.
          QStringList tagList;
          for(const auto& tg : sn["tags"].toArray()) {
            const QString s = tg.toString().trimmed();
            if(!s.isEmpty()) {
              tagList << s;
            }
          }
          m["body"] = cap(sn["lang"].toString() + QChar(' ') + tagList.join(QChar(' ')) + QChar(' ') + sn["code"].toString());
          m["profileId"] = p.id;
          m["idx"] = snipIdx++;
          m["color"] = p.color;
          out.append(m);
        }
        int contactIdx = 0;
        for(const auto& cIt : root["contacts"].toArray()) {
          const QJsonObject c = cIt.toObject();
          QVariantMap m;
          m["kind"] = "contact";
          m["label"] = c["name"].toString();
          m["sub"] = QString("%1 · %2").arg(p.name, c["role"].toString());
          m["body"] = cap(c["channel"].toString() + QChar(' ') + c["mattermost"].toString());
          m["profileId"] = p.id;
          m["idx"] = contactIdx++;
          m["color"] = p.color;
          out.append(m);
        }
      }
    }
    // People (pending contacts list)
    for(const Person& person : p.people) {
      QVariantMap m;
      m["kind"] = "person";
      m["label"] = person.name;
      m["sub"] = QString("%1 · %2").arg(p.name, person.role);
      m["body"] = cap(person.question);
      m["profileId"] = p.id;
      m["personId"] = person.id;
      m["color"] = p.color;
      out.append(m);
    }
  }

  // Events. The calendar was the one surface Ctrl+K could not reach: a meeting
  // the user knew the name of could only be found by paging to the week it was
  // in. Events are global with a profile attribution rather than owned by a
  // profile, so this is one pass, not one per profile.
  //
  // Stored events only. Expanding every series over every horizon would make
  // the palette's cost depend on how far ahead people plan, and a master's
  // entry lands the reader on the series anyway.
  for(const CalEvent& e : m_events.items()) {
    if(e.title.trimmed().isEmpty()) {
      continue;
    }
    QString profileName;
    QString profileColor;
    for(const Profile& p : m_profiles) {
      if(p.id == e.profileId) {
        profileName = p.name;
        profileColor = p.color;
        break;
      }
    }
    QVariantMap m;
    m["kind"] = "event";
    m["label"] = e.title;
    // In the UI language: QDate::toString always wrote English month names.
    // "15 окт 2026", without the locale's dot (a-31, DG-031).
    const QString day = heap::text::withoutMonthDot(dateLabel(e.date, QStringLiteral("dayMonthYear")), m_language);
    const QString when = day.isEmpty() ? QString() : (e.allDay ? day : QStringLiteral("%1 %2").arg(day, eventHourLabel(e.start)));
    QStringList sub;
    if(!profileName.isEmpty()) {
      sub << profileName;
    }
    if(!when.isEmpty()) {
      sub << when;
    }
    if(!e.rrule.isEmpty()) {
      sub << tr_("palette.repeats");
    }
    m["sub"] = sub.join(QStringLiteral(" · "));
    m["body"] = cap(e.attendees + QLatin1Char(' ') + e.context);
    m["profileId"] = e.profileId;
    m["eventId"] = e.id;
    m["eventDate"] = e.date;
    m["color"] = profileColor.isEmpty() ? QStringLiteral("#6aa9e9") : profileColor;
    out.append(m);
  }

  return out;
}

// ───────────────────────────────────── JSON import / export of profile ──

QString AppController::exportActiveProfileJson() const {
  const int i = profileIndexOf(m_activeProfileId);
  if(i < 0) {
    return QString();
  }
  // The editors debounce their writes: ask them to hand over what is still
  // only in a text field, then take the same snapshot a save would. Copying
  // just notesState here left the note list and the doc pages as of the last
  // save, so a note written a moment ago was missing from the export.
  auto* self = const_cast<AppController*>(this);
  emit self->flushEditorsRequested();
  self->snapshotActiveProfile();
  Profile p = m_profiles[i];
  QJsonObject profObj = heap::state::profileToJson(p);

  // Events live in the global pool, so profileToJson() cannot see them — a
  // profile export used to silently drop the whole calendar. Include the
  // events attributed to this profile so the export is a complete snapshot;
  // import re-attributes them to the (possibly re-slugged) imported profile.
  QVector<CalEvent> profileEvents;
  for(const CalEvent& e : m_events.items()) {
    if(e.profileId == m_activeProfileId) {
      profileEvents.append(e);
    }
  }
  profObj["events"] = heap::state::eventsToJson(profileEvents);

  QJsonObject root;
  root["schemaVersion"] = heap::state::kSchemaVersion;
  root["kind"] = "todocpp.profile";
  // Which build wrote it, for the import preview (R3-080).
  root["appVersion"] = QString::fromLatin1(HEAP_VERSION);
  root["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  root["profile"] = profObj;
  // The files the profile's tasks and notes use, so the export stands on its
  // own on another machine. Next to "profile", not in it: they are not part
  // of the profile's state. Past the cap the export still carries every task
  // and note, and says what it left out.
  qint64 omittedBytes = 0;
  int omittedCount = 0;
  const QJsonArray files = attachmentsForExport(p, &omittedBytes, &omittedCount);
  if(!files.isEmpty()) {
    root["attachments"] = files;
  }
  if(omittedCount > 0) {
    root["attachmentsOmitted"] = omittedCount;
    emit self->toast(
        attText("export.omitted")
            .arg(omittedCount)
            .arg(formatBytes(static_cast<double>(omittedBytes)), formatBytes(static_cast<double>(heap::attachments::kMaxExportBytes))),
        QStringLiteral("warning"));
  }
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool AppController::exportActiveProfileToFile(const QUrl& fileUrl) const {
  const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
  if(path.isEmpty()) {
    return false;
  }
  const QString json = exportActiveProfileJson();
  if(json.isEmpty()) {
    return false;
  }
  QSaveFile f(path);
  if(!f.open(QIODevice::WriteOnly)) {
    qWarning("todocpp: cannot open %s for writing: %s", qUtf8Printable(path), qUtf8Printable(f.errorString()));
    return false;
  }
  f.write(json.toUtf8());
  if(!f.commit()) {
    return false;
  }
  const_cast<AppController*>(this)->emit toast(tr_("profile.exported").arg(QFileInfo(path).fileName()));
  return true;
}

QString AppController::importProfileFromJson(const QString& jsonText, bool activate) {
  if(jsonText.trimmed().isEmpty()) {
    return tr_("import.emptyJson");
  }
  snapshotBeforeChange(QStringLiteral("import"));
  const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
  if(doc.isNull() || !doc.isObject()) {
    return tr_("import.invalidJson");
  }
  const QJsonObject root = doc.object();

  // Accept either { "profile": {...} } wrapper or a bare profile object.
  QJsonObject profileObj;
  if(root.contains("profile") && root["profile"].isObject()) {
    profileObj = root["profile"].toObject();
  } else if(root.contains("id") && root.contains("name")) {
    profileObj = root;
  } else {
    return tr_("import.missingProfile");
  }

  QVector<CalEvent> importedEvents;
  Profile imported = heap::state::profileFromJson(profileObj, &importedEvents);
  // The export's files go into the store first. One whose bytes do not match
  // its id is stored under the right id and every reference follows it.
  QStringList attachmentProblems;
  heap::attachments::remapProfile(imported, importAttachmentBlobs(root.value("attachments").toArray(), &attachmentProblems));
  if(!attachmentProblems.isEmpty()) {
    emit toast(attText("import.problems").arg(attachmentProblems.join(QStringLiteral("; "))), QStringLiteral("warning"));
  }
  if(imported.name.trimmed().isEmpty()) {
    imported.name = QStringLiteral("Imported");
  }
  // An import of a profile that is already here arrives as "Name (2)".
  imported.name = uniqueProfileName(imported.name.trimmed());

  // Resolve id collisions — re-slug so we never overwrite an existing profile.
  if(imported.id.isEmpty() || profileIndexOf(imported.id) >= 0) {
    imported.id = makeProfileId(imported.name);
  }
  imported.createdAt = QDateTime::currentDateTime();
  if(imported.color.isEmpty()) {
    imported.color = QStringLiteral("#5cc2dd");
  }
  if(imported.statuses.isEmpty()) {
    for(const auto& m : SampleData::statuses(m_language == QStringLiteral("ru") ? SampleData::Lang::Ru : SampleData::Lang::En)) {
      imported.statuses.append(m);
    }
  }

  if(activate) {
    snapshotActiveProfile();
  }
  // Before the profile joins the list, and with the events it brought, whose
  // task links follow the renamed tasks (PLAT-9).
  reissueSharedTaskIds(imported, &importedEvents);
  m_profiles.push_back(imported);

  // Hoist the imported calendar events into the global pool, re-attributed to
  // the (possibly re-slugged) imported profile and given fresh ids so they can
  // never collide with existing events — including on a same-instance
  // round-trip where the source ids are already present. A series' moved
  // occurrence follows its own copy of the series: left on the source id it
  // took over the source profile's occurrence (DATA-3).
  addEventsAsCopies(importedEvents, imported.id);

  if(activate) {
    parkWorkspaceUndo(m_activeProfileId);
    clearPendingUndo();  // undo is scoped to the active workspace
    m_activeProfileId = imported.id;
    applyProfileToModels(imported);
    emit activeProfileChanged();
  }
  emit profilesChanged();
  emit toast(tr_("profile.imported").arg(imported.name));
  scheduleSave();
  return QString();
}

QString AppController::importProfileFromFile(const QUrl& fileUrl, bool activate) {
  const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
  if(path.isEmpty()) {
    return tr_("import.emptyPath");
  }
  QFile f(path);
  if(!f.open(QIODevice::ReadOnly)) {
    return tr_("import.openFail") + f.errorString();
  }
  const QString text = QString::fromUtf8(f.readAll());
  return importProfileFromJson(text, activate);
}

namespace {

// A profile export's root and its profile object, or an error text.
QString readProfileExport(
    const QUrl& fileUrl, QJsonObject* root, QJsonObject* profileObj, QString* fileName, const std::function<QString(const char*)>& tr) {
  const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
  if(path.isEmpty()) {
    return tr("import.emptyPath");
  }
  QFile f(path);
  if(!f.open(QIODevice::ReadOnly)) {
    return tr("import.openFail") + f.errorString();
  }
  *fileName = QFileInfo(path).fileName();
  const QByteArray bytes = f.readAll();
  if(bytes.trimmed().isEmpty()) {
    return tr("import.emptyJson");
  }
  const QJsonDocument doc = QJsonDocument::fromJson(bytes);
  if(doc.isNull() || !doc.isObject()) {
    return tr("import.invalidJson");
  }
  *root = doc.object();
  if(root->contains("profile") && (*root)["profile"].isObject()) {
    *profileObj = (*root)["profile"].toObject();
  } else if(root->contains("id") && root->contains("name")) {
    *profileObj = *root;
  } else {
    return tr("import.missingProfile");
  }
  return {};
}

}  // namespace

QVariantMap AppController::previewProfileImport(const QUrl& fileUrl) {
  QJsonObject root;
  QJsonObject profileObj;
  QString fileName;
  const QString err = readProfileExport(fileUrl, &root, &profileObj, &fileName, [this](const char* k) {
    return tr_(QString::fromLatin1(k));
  });
  QVariantMap out;
  if(!err.isEmpty()) {
    out["error"] = err;
    return out;
  }
  QVector<CalEvent> events;
  const Profile p = heap::state::profileFromJson(profileObj, &events);
  out["file"] = fileName;
  out["name"] = p.name.trimmed().isEmpty() ? QStringLiteral("Imported") : p.name.trimmed();
  out["version"] = root.value("appVersion").toString();
  out["schema"] = root.value("schemaVersion").toInt();
  out["tasks"] = static_cast<int>(p.tasks.size());
  out["notes"] = static_cast<int>(p.notes.size());
  out["views"] = static_cast<int>(p.savedViews.size());
  out["events"] = static_cast<int>(events.size());
  const int ai = profileIndexOf(m_activeProfileId);
  out["activeName"] = ai >= 0 ? m_profiles[ai].name : QString();
  return out;
}

QString AppController::mergeProfileFromFile(const QUrl& fileUrl) {
  QJsonObject root;
  QJsonObject profileObj;
  QString fileName;
  const QString err = readProfileExport(fileUrl, &root, &profileObj, &fileName, [this](const char* k) {
    return tr_(QString::fromLatin1(k));
  });
  if(!err.isEmpty()) {
    return err;
  }
  const int ai = profileIndexOf(m_activeProfileId);
  if(ai < 0) {
    return importProfileFromFile(fileUrl, true);
  }
  snapshotBeforeChange(QStringLiteral("import"));
  emit flushEditorsRequested();
  snapshotActiveProfile();

  QVector<CalEvent> importedEvents;
  Profile in = heap::state::profileFromJson(profileObj, &importedEvents);
  QStringList attachmentProblems;
  heap::attachments::remapProfile(in, importAttachmentBlobs(root.value("attachments").toArray(), &attachmentProblems));
  if(!attachmentProblems.isEmpty()) {
    emit toast(attText("import.problems").arg(attachmentProblems.join(QStringLiteral("; "))), QStringLiteral("warning"));
  }
  Profile& target = m_profiles[ai];

  // Matching ids are never overwritten: they stay as they are and are listed.
  QStringList kept;
  int added = 0;
  const auto idsOf = [](const auto& items) {
    QSet<QString> s;
    for(const auto& x : items) {
      s.insert(x.id);
    }
    return s;
  };
  const QSet<QString> haveTasks = idsOf(target.tasks);
  QVector<Task> newTasks;
  for(const Task& t : in.tasks) {
    if(haveTasks.contains(t.id)) {
      kept << t.id;
    } else {
      newTasks.append(t);
    }
  }
  in.tasks = newTasks;
  // A task id another profile holds gets a fresh one (PLAT-9).
  reissueSharedTaskIds(in, &importedEvents);
  // A column the profile does not have comes with its tasks.
  QSet<QString> haveStatuses;
  for(const QVariant& v : target.statuses) {
    haveStatuses.insert(v.toMap().value("id").toString());
  }
  for(const QVariant& v : in.statuses) {
    const QString sid = v.toMap().value("id").toString();
    if(!sid.isEmpty() && !haveStatuses.contains(sid)) {
      bool used = false;
      for(const Task& t : in.tasks) {
        used = used || t.status == sid;
      }
      if(used) {
        target.statuses.append(v);
        haveStatuses.insert(sid);
      }
    }
  }
  for(const Task& t : in.tasks) {
    target.tasks.append(t);
    ++added;
  }
  const auto mergeById = [&kept, &added](auto& into, const auto& from) {
    QSet<QString> have;
    for(const auto& x : into) {
      have.insert(x.id);
    }
    for(const auto& x : from) {
      if(have.contains(x.id)) {
        kept << x.id;
      } else {
        into.append(x);
        ++added;
      }
    }
  };
  mergeById(target.notes, in.notes);
  mergeById(target.docPages, in.docPages);
  mergeById(target.people, in.people);
  mergeById(target.savedViews, in.savedViews);
  QSet<QString> haveEvents;
  for(const CalEvent& e : m_events.items()) {
    haveEvents.insert(e.id);
  }
  for(CalEvent& e : importedEvents) {
    if(haveEvents.contains(e.id)) {
      kept << e.title;
      continue;
    }
    e.profileId = target.id;
    m_events.upsert(e);
    ++added;
  }

  clearPendingUndo();
  applyProfileToModels(target);
  emit profilesChanged();
  emit toast(kept.isEmpty() ? tr_("profile.merged").arg(target.name).arg(added)
                            : tr_("profile.mergedKept").arg(target.name).arg(added).arg(kept.join(QStringLiteral(", "))));
  scheduleSave();
  return {};
}

// ─────────────────────────────────────────────── Shortcuts catalog ──

void AppController::seedShortcutCatalog() {
  // Snapshot any user-set sequences before the labels are rebuilt so a
  // language flip preserves rebinds.
  QMap<QString, QString> existingOverrides;
  for(const QVariant& v : m_shortcuts) {
    const QVariantMap m = v.toMap();
    const QString id = m.value("id").toString();
    const QString def = m.value("defaultSequence").toString();
    const QString cur = m.value("sequence").toString();
    if(cur != def) {
      existingOverrides.insert(id, cur);
    }
  }

  // Every action names its key in both keymaps (dogfood 2026-10-19): Vim
  // first, then Classic. "" is a deliberate "no key" in that scheme.
  const bool vim = m_keymapScheme == QLatin1String("vim");
  auto add = [this, vim](const char* id, const char* vimSeq, const char* classicSeq) {
    const char* defaultSeq = vim ? vimSeq : classicSeq;
    QVariantMap m;
    const QString sid = QString::fromUtf8(id);
    // A second key for an action ("undo.alt") reads as the action itself.
    const auto text = [this, &sid](const char* part) {
      const QString own = QStringLiteral("shortcut.%1.%2").arg(sid, QLatin1String(part));
      const QString t = tr_(own);
      return t != own ? t : tr_(QStringLiteral("shortcut.%1.%2").arg(baseShortcutId(sid), QLatin1String(part)));
    };
    m["id"] = sid;
    m["label"] = text("label");
    // The English name too: the command line finds a command by it in any
    // UI language ("sett" in Russian found nothing, F3-EYES-7).
    {
      const QString own = QStringLiteral("shortcut.%1.label").arg(sid);
      const QString t = trIn_(own, false);
      m["labelEn"] = t != own ? t : trIn_(QStringLiteral("shortcut.%1.label").arg(baseShortcutId(sid)), false);
    }
    m["description"] = text("desc");
    const QString def = normalizeSequence(QString::fromUtf8(defaultSeq));
    m["defaultSequence"] = def;
    m["sequence"] = def;
    // Both schemes' keys, for the settings search and the scheme switch.
    m["vimSequence"] = normalizeSequence(QString::fromUtf8(vimSeq));
    m["classicSequence"] = normalizeSequence(QString::fromUtf8(classicSeq));
    m_shortcuts.append(m);
  };

  m_shortcuts.clear();
  // The keymap of 0.8.0 (keymap.md): Vim-based. A bare letter acts on the
  // task under the cursor and only while the focus is in the content; Ctrl
  // chords work everywhere; g / y / z / c start a two-key sequence. KeyRouter
  // reads every key by its physical place, so the Russian layout works too.
  add("palette.open", "Ctrl+K", "Ctrl+K");
  // Ctrl+P was a fixed alias of the command line (APP-279): a catalogue entry
  // now, rebindable and removable like any other.
  add("palette.open.alt", "Ctrl+P", "Ctrl+P");
  // ":" opens the command line on its commands, as ">" does inside it.
  add("palette.commands", ":", "F1");
  add("task.new", "Ctrl+N", "Ctrl+N");
  // Done in one key (APP-268, keymap.md "d").
  add("task.done", "D", "Ctrl+D");
  // heap 2 (APP-258): Ctrl+1..3 are the sidebar's sections top to bottom,
  // Ctrl+, is Settings; g t / g n are the same places from the keymap.
  add("section.today", "Ctrl+1", "Ctrl+1");
  add("section.today.alt", "G, T", "");
  add("section.tasks", "Ctrl+2", "Ctrl+2");
  add("section.knowledge", "Ctrl+3", "Ctrl+3");
  add("section.knowledge.alt", "G, N", "");
  // The lenses of Tasks: g b / g l / g c; the calendar's zoom: z d / z w / z m.
  add("view.board", "G, B", "Alt+B");
  add("view.timeline", "G, L", "Alt+L");
  add("view.calendar", "G, C", "Alt+C");
  add("cal.zoomDay", "Z, D", "Alt+Shift+D");
  add("view.week", "Z, W", "Alt+Shift+W");
  add("view.month", "Z, M", "Alt+Shift+M");
  add("view.archive", "", "");
  // Docs is a sidebar row like the others, with a key like them (A7-10).
  add("view.docs", "G, D", "Alt+D");
  add("view.notes", "", "");
  add("view.settings", "Ctrl+,", "Ctrl+,");
  // Interface scale (APP-168) from the keyboard, as in a browser. Ctrl++,
  // Ctrl+Shift+= and the numpad keys are fixed aliases (kBuiltinKeys).
  add("zoom.in", "Ctrl+=", "Ctrl+=");
  add("zoom.out", "Ctrl+-", "Ctrl+-");
  add("zoom.reset", "Ctrl+0", "Ctrl+0");
  add("profile.next", "Ctrl+]", "Ctrl+]");
  add("profile.prev", "Ctrl+[", "Ctrl+[");
  add("profile.exportMd", "Ctrl+Shift+E", "Ctrl+Shift+E");
  add("profile.weeklyReport", "Ctrl+Shift+W", "Ctrl+Shift+W");
  // Every tracker now (the sync indicator's popover, R2-048).
  add("sync.all", "Ctrl+Shift+R", "Ctrl+Shift+R");
  add("tweaks.open", "", "");
  // The cheat sheet (APP-272): "?" and Ctrl+/; changing keys is its own mode.
  add("hotkeys.open", "Ctrl+/", "Ctrl+/");
  add("hotkeys.open.alt", "?", "?");
  add("hotkeys.edit", "", "");
  add("undo", "Ctrl+Z", "Ctrl+Z");
  add("undo.alt", "U", "");
  add("redo", "Ctrl+Shift+Z", "Ctrl+Shift+Z");
  add("redo.alt", "Ctrl+R", "Ctrl+Y");
  // The section's filter line: "/", as Ctrl+F and Ctrl+L.
  add("search.focus", "Ctrl+F", "Ctrl+F");
  add("search.focus.alt", "/", "/");
  add("search.focus.alt2", "Ctrl+L", "Ctrl+L");
  add("quick-capture", "Ctrl+Shift+Space", "Ctrl+Shift+Space");
  add("quick-capture-notes", "Ctrl+Shift+N", "Ctrl+Shift+N");
  add("theme.toggle", "Ctrl+Shift+T", "Ctrl+Shift+T");
  // The legacy right panel is gone (DG-002); its people have a dialog.
  add("people.open", "", "");
  add("rail.toggle", "Ctrl+Shift+B", "Ctrl+Shift+B");
  add("person.new", "Ctrl+Shift+U", "Ctrl+Shift+U");
  add("profile.new", "Ctrl+Shift+P", "Ctrl+Shift+P");
  // Moving around, as in Vim: to the first / last item, half a screen, and
  // back / forward through where you have been (the jumplist).
  add("cursor.first", "G, G", "Home");
  add("cursor.last", "Shift+G", "End");
  add("cursor.pageDown", "Ctrl+D", "PgDown");
  add("cursor.pageUp", "Ctrl+U", "PgUp");
  add("nav.back", "Ctrl+O", "Alt+Left");
  add("nav.forward", "Ctrl+I", "Alt+Right");
  // The task under the cursor (keymap.md "Задача под курсором").
  add("task.newBelow", "O", "Ins");
  add("task.newAbove", "Shift+O", "Shift+Ins");
  add("task.rename", "I", "F2");
  add("task.schedule", "S", "Ctrl+Shift+S");
  add("task.due", "Shift+S", "Ctrl+Shift+D");
  add("task.priority0", "1", "Alt+1");
  add("task.priority1", "2", "Alt+2");
  add("task.priority2", "3", "Alt+3");
  add("task.priority3", "4", "Alt+4");
  add("task.timer", "T", "Ctrl+T");
  add("task.copyId", "Y, Y", "Ctrl+Shift+C");
  add("task.copyBranch", "Y, B", "Ctrl+Shift+G");
  add("task.copyLink", "Y, L", "Ctrl+Shift+Y");
  add("task.createBranch", "C, B", "Ctrl+Alt+B");
  // To another profile (A5-14): M is the menu, Shift M moves it out.
  add("task.moveProfile", "Shift+M", "Ctrl+Alt+M");
  // Open the issue in its tracker, or the link under the cursor (Vim's gx).
  add("task.openExternal", "G, X", "Ctrl+Shift+O");
  add("selection.toggle", "V", "");
  add("selection.range", "Shift+V", "");
  add("selection.selectAll", "Ctrl+A", "Ctrl+A");
  add("selection.clearSel", "Esc", "Esc");
  add("selection.deleteSel", "Del", "Del");
  // Board keyboard cursor. Bare letters: KeyRouter holds them back while a
  // text field has the focus, and QML while any overlay is open. Arrow keys
  // are wired alongside these in Main.qml.
  add("board.cursorDown", "J", "Down");
  add("board.cursorUp", "K", "Up");
  add("board.cursorLeft", "H", "Left");
  add("board.cursorRight", "L", "Right");
  add("board.open", "Return", "Return");
  add("board.toggleSelect", "Space", "Space");
  add("board.moveDown", "Shift+J", "Ctrl+Down");
  add("board.moveUp", "Shift+K", "Ctrl+Up");
  add("board.moveLeft", "Shift+H", "Ctrl+Left");
  add("board.moveRight", "Shift+L", "Ctrl+Right");
  add("board.cardMenu", "M", "Shift+F10");
  add("board.archive", "E", "Ctrl+E");
  // Fold, as Vim's za.
  add("board.collapseColumn", "Z, A", "Alt+F");
  // On a column header only (keymap.md, DG-133): Ctrl+Shift+L elsewhere is the log.
  add("board.columnLeft", "Ctrl+Shift+H", "Ctrl+Shift+Left");
  add("board.columnRight", "Ctrl+Shift+L", "Ctrl+Shift+Right");
  // Selecting from the keyboard (APP-128): Shift+Up/Down grows the selection
  // a card at a time, Shift+Left/Right takes the whole column and steps on.
  // Moving cards went from Shift+arrows to Ctrl+arrows (Main.qml) to make room.
  add("board.selectDown", "Shift+Down", "Shift+Down");
  add("board.selectUp", "Shift+Up", "Shift+Up");
  add("board.selectColumnLeft", "Shift+Left", "Shift+Left");
  add("board.selectColumnRight", "Shift+Right", "Shift+Right");
  // The period: [ and ] a day on Today, a week or month in the calendar; 0
  // back to today. Going to a date is ":" and the date now.
  add("cal.today", "0", "Alt+T");
  add("cal.prev", "[", "[");
  add("cal.next", "]", "]");
  add("cal.prev.alt", "P", "");
  add("cal.next.alt", "N", "");
  add("cal.goToDate", "", "Ctrl+G");
  // Notes, live only in the Notes view. Ctrl+PgUp/PgDn is how every tabbed
  // editor steps between documents; F2 renames, as in a file manager.
  add("notes.new", "Ctrl+Alt+N", "Ctrl+Alt+N");
  add("notes.next", "Ctrl+PgDown", "Ctrl+PgDown");
  add("notes.prev", "Ctrl+PgUp", "Ctrl+PgUp");
  add("notes.rename", "F2", "F2");
  add("notes.toggleList", "Ctrl+Alt+L", "Ctrl+Alt+L");
  // Deleting from the keyboard (dogfood 2026-10-19): the note or doc page the
  // Knowledge list is on, and the block of a document. Both undo. Live only
  // where the keyboard is on that list or document (QML claims them), so they
  // share keys with the tasks' Del and d (keySurface).
  add("notes.delete", "D, D", "Del");
  add("notes.delete.alt", "Del", "");
  add("editor.deleteBlock", "D, D", "Ctrl+Shift+K");
  // A table's rows and columns at the caret, in the note's text only
  // (dogfood 2026-10-19). Arrows say where the new row or column goes;
  // Shift is the column family, clear of the Classic day keys Ctrl+Alt+Left/Right.
  add("notes.table.rowAbove", "Ctrl+Alt+Up", "Ctrl+Alt+Up");
  add("notes.table.rowBelow", "Ctrl+Alt+Down", "Ctrl+Alt+Down");
  add("notes.table.columnLeft", "Ctrl+Alt+Shift+Left", "Ctrl+Alt+Shift+Left");
  add("notes.table.columnRight", "Ctrl+Alt+Shift+Right", "Ctrl+Alt+Shift+Right");
  add("notes.table.deleteRow", "Ctrl+Alt+Backspace", "Ctrl+Alt+Backspace");
  add("notes.table.deleteColumn", "Ctrl+Alt+Shift+Backspace", "Ctrl+Alt+Shift+Backspace");
  // A day at a time, in any view the day panel sits beside; and a new event
  // from the keyboard, at the next free slot of the selected day.
  add("cal.prevDay", "Alt+Left", "Ctrl+Alt+Left");
  add("cal.nextDay", "Alt+Right", "Ctrl+Alt+Right");
  add("cal.newEvent", "Ctrl+Alt+E", "Ctrl+Alt+E");
  // Moving the task under the keyboard (or the pointer) in Week, Month and
  // Timeline: the keys for what a drag does there (APP-249). Ctrl+arrows are
  // the board's own card moves; these are live only in those three views.
  add("cal.taskEarlier", "Ctrl+Left", "Ctrl+Left");
  add("cal.taskLater", "Ctrl+Right", "Ctrl+Right");
  add("cal.taskEarlierWeek", "Ctrl+Shift+Left", "Ctrl+Shift+Left");
  add("cal.taskLaterWeek", "Ctrl+Shift+Right", "Ctrl+Shift+Right");
  add("cal.taskTimeEarlier", "Ctrl+Up", "Ctrl+Up");
  add("cal.taskTimeLater", "Ctrl+Down", "Ctrl+Down");
  // A block's length without dragging its edge (APP-278): a grid step.
  add("cal.longer", "Ctrl+Shift+J", "Ctrl+Shift+Down");
  add("cal.longer.alt", "Ctrl+Shift+Down", "");
  add("cal.shorter", "Ctrl+Shift+K", "Ctrl+Shift+Up");
  add("cal.shorter.alt", "Ctrl+Shift+Up", "");
  // Regions instead of a long Tab (APP-277): sidebar, content, panel, header.
  add("region.next", "F6", "F6");
  add("region.prev", "Shift+F6", "Shift+F6");
  // Focus mode (APP-160); live only once Settings → Safety net turns it on.
  add("focus.immersion", "Ctrl+Shift+F", "Ctrl+Shift+F");
  // The event log (APP-187): the toasts of this session, to read again.
  add("log.open", "Ctrl+Shift+L", "Ctrl+Shift+L");
  // The other 0.6 tools (APP-192): no key by default, so nothing is taken
  // from anyone's muscle memory; bindable in Settings → Hotkeys, and the
  // palette shows the key once there is one.
  add("timeMachine.open", "", "");
  add("standup.draft", "", "");
  // The week's summary had no key and was found only on the last working day
  // (dogfood 2026-10-16): "g w", beside "g t" / "g n".
  add("recap.open", "G, W", "Alt+R");
  add("endOfDay.open", "", "");
  add("welcome.replay", "", "");
  // The example profile from anywhere (PERSONA-1): the first-run link was the
  // only way, and it is gone once any task exists.
  add("example.open", "", "");
  // My views in sidebar order (APP-281 A4): Ctrl+4…9 after the three
  // sections, and g 1…g 9.
  add("savedView.1", "Ctrl+4", "Ctrl+4");
  add("savedView.1.alt", "G, 1", "");
  add("savedView.2", "Ctrl+5", "Ctrl+5");
  add("savedView.2.alt", "G, 2", "");
  add("savedView.3", "Ctrl+6", "Ctrl+6");
  add("savedView.3.alt", "G, 3", "");
  add("savedView.4", "Ctrl+7", "Ctrl+7");
  add("savedView.4.alt", "G, 4", "");
  add("savedView.5", "Ctrl+8", "Ctrl+8");
  add("savedView.5.alt", "G, 5", "");
  add("savedView.6", "Ctrl+9", "Ctrl+9");
  add("savedView.6.alt", "G, 6", "");
  add("savedView.7", "", "");
  add("savedView.7.alt", "G, 7", "");
  add("savedView.8", "", "");
  add("savedView.8.alt", "G, 8", "");
  add("savedView.9", "", "");
  add("savedView.9.alt", "G, 9", "");

  if(!existingOverrides.isEmpty()) {
    QVariantMap asMap;
    for(auto it = existingOverrides.constBegin(); it != existingOverrides.constEnd(); ++it) {
      asMap.insert(it.key(), it.value());
    }
    applyShortcutOverrides(asMap);
  }
  emit shortcutsChanged();
}

void AppController::registerGlobalHotkeys() {
  if(!m_globalHotkey) {
    return;
  }
  // Each global capture hotkey mirrors its in-app catalog binding, so one place
  // in Settings → Hotkeys controls both. RegisterHotKey intercepts the
  // combination even when the window is focused, so the in-app QML Shortcut
  // only fires as a fallback when the OS registration is refused.
  const auto arm = [this](int id, const QString& catalogId) {
    const QString seq = shortcutFor(catalogId);
    if(seq.isEmpty() || !m_globalHotkey->registerHotkey(id, seq)) {
      m_globalHotkey->unregister(id);
    }
  };
  arm(HotkeyQuickCapture, QStringLiteral("quick-capture"));
  arm(HotkeyQuickCaptureNotes, QStringLiteral("quick-capture-notes"));
}

void AppController::onGlobalHotkey(int id) {
  switch(id) {
    case HotkeyQuickCapture:
      heap::perf::begin(QStringLiteral("capture"));
      emit quickCaptureRequested();
      break;
    case HotkeyQuickCaptureNotes:
      heap::perf::begin(QStringLiteral("capture-notes"));
      emit quickCaptureNotesRequested();
      break;
    default:
      break;
  }
}

QStringList AppController::yieldDefaultsTo(QVariantMap& overrides) const {
  // A user's own bindings are theirs: a key they gave an action keeps that
  // action, and a default that would take it — the same key, or a sequence
  // one of them starts (g with g b) — steps aside.
  QStringList kept;
  for(const QVariant& v : std::as_const(m_shortcuts)) {
    const QVariantMap m = v.toMap();
    const QString id = m.value(QStringLiteral("id")).toString();
    const QString def = m.value(QStringLiteral("defaultSequence")).toString();
    if(def.isEmpty() || overrides.contains(id)) {
      continue;
    }
    for(auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
      const QString own = normalizeSequence(it.value().toString());
      if(it.key() == id || own.isEmpty() || surfacesApart(id, it.key(), own != def)) {
        continue;
      }
      if(own == def || heap::keys::isPrefixOf(own, def) || heap::keys::isPrefixOf(def, own)) {
        overrides.insert(id, QString());
        kept << keyText(own) + QStringLiteral(" ") + tr_(QStringLiteral("shortcut.%1.label").arg(baseShortcutId(it.key())));
        break;
      }
    }
  }
  return kept;
}

void AppController::setKeymapScheme(const QString& scheme) {
  if((scheme != QLatin1String("classic") && scheme != QLatin1String("vim")) || scheme == m_keymapScheme) {
    return;
  }
  // The keys the user gave actions. A key taken off an action is not carried
  // over: it was the old scheme's key (often freed by a swap), and the new
  // scheme gives that action a key of its own.
  QVariantMap overrides;
  for(const QVariant& v : std::as_const(m_shortcuts)) {
    const QVariantMap m = v.toMap();
    const QString seq = m.value(QStringLiteral("sequence")).toString();
    if(!seq.isEmpty() && seq != m.value(QStringLiteral("defaultSequence")).toString()) {
      overrides.insert(m.value(QStringLiteral("id")).toString(), m.value(QStringLiteral("sequence")));
    }
  }
  m_keymapScheme = scheme;
  // Seeded with no rebindings, then given them back on the new defaults.
  m_shortcuts.clear();
  seedShortcutCatalog();
  static_cast<void>(yieldDefaultsTo(overrides));
  applyShortcutOverrides(overrides);
  emit keymapSchemeChanged();
  emit shortcutsChanged();
  registerGlobalHotkeys();
  scheduleSave();
  emit toast(tr_(QStringLiteral("keymap.scheme.switched")).arg(tr_(QStringLiteral("keymap.scheme.") + scheme)));
}

QString AppController::schemeShortcutFor(const QString& id, const QString& scheme) const {
  const int i = shortcutIndexOf(id);
  if(i < 0) {
    return {};
  }
  const QVariantMap m = m_shortcuts[i].toMap();
  return m.value(scheme == QLatin1String("vim") ? QStringLiteral("vimSequence") : QStringLiteral("classicSequence")).toString();
}

bool AppController::keyMatches(const QString& id, int key, int modifiers, const QString& text, quint32 scanCode) const {
  const QString chord = keyChord(key, modifiers, text, scanCode);
  if(chord.isEmpty()) {
    return false;
  }
  for(const QVariant& v : m_shortcuts) {
    const QVariantMap m = v.toMap();
    if(baseShortcutId(m.value(QStringLiteral("id")).toString()) == id && m.value(QStringLiteral("sequence")).toString() == chord) {
      return true;
    }
  }
  return false;
}

void AppController::applyKeymapMigration(QVariantMap& overrides, int storedSchema) {
  const QStringList kept = yieldDefaultsTo(overrides);
  // The old keys a hand still reaches for: said once each, on the first press
  // (APP-281 A4). Not a key the user had moved the old action off, nor one
  // they use for an action of their own.
  QJsonArray notice = m_settingsExtra.value(QStringLiteral("keymapNotice")).toArray();
  for(const LegacyKey& k : kLegacyKeys) {
    const QString seq = QString::fromLatin1(k.sequence);
    if(storedSchema >= k.changedIn || overrides.contains(QString::fromLatin1(k.oldId))) {
      continue;
    }
    bool own = false;
    for(auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
      own = own || normalizeSequence(it.value().toString()) == seq;
    }
    if(!own && !notice.contains(seq)) {
      notice.append(seq);
    }
  }
  if(!notice.isEmpty()) {
    m_settingsExtra.insert(QStringLiteral("keymapNotice"), notice);
  }
  if(!kept.isEmpty()) {
    m_shellNotice = tr_(QStringLiteral("keymap.notice.kept")).arg(kept.join(QStringLiteral(", ")));
    emit shellNoticeChanged();
  }
}

bool AppController::hasKeymapNotice() const {
  return !m_settingsExtra.value(QStringLiteral("keymapNotice")).toArray().isEmpty();
}

void AppController::noteKeyPressed(const QString& chord) {
  QJsonArray notice = m_settingsExtra.value(QStringLiteral("keymapNotice")).toArray();
  const QString seq = normalizeSequence(chord);
  if(seq.isEmpty() || !notice.contains(seq)) {
    return;
  }
  for(int i = static_cast<int>(notice.size()) - 1; i >= 0; --i) {
    if(notice.at(i).toString() == seq) {
      notice.removeAt(i);
    }
  }
  if(notice.isEmpty()) {
    m_settingsExtra.remove(QStringLiteral("keymapNotice"));
  } else {
    m_settingsExtra.insert(QStringLiteral("keymapNotice"), notice);
  }
  scheduleSave();
  QString what;
  for(const LegacyKey& k : kLegacyKeys) {
    if(QString::fromLatin1(k.sequence) == seq) {
      what = tr_(QStringLiteral("keymap.was.") + QString::fromLatin1(k.oldId));
    }
  }
  emit toast(tr_(QStringLiteral("keymap.notice.moved")).arg(keyText(seq), what));
}

QString AppController::keyText(const QString& sequence) const {
#ifdef Q_OS_MACOS
  constexpr bool kMac = true;
#else
  constexpr bool kMac = false;
#endif
  return heap::keys::displayKeys(sequence, kMac);
}

QString AppController::shortcutText(const QString& id) const {
  return keyText(shortcutFor(id));
}

QString AppController::prefixShortcutConflict(const QString& id, const QString& sequence) const {
  const QString want = normalizeSequence(sequence);
  if(want.isEmpty()) {
    return {};
  }
  for(const QVariant& v : m_shortcuts) {
    const QVariantMap m = v.toMap();
    const QString seq = m.value(QStringLiteral("sequence")).toString();
    const QString other = m.value(QStringLiteral("id")).toString();
    if(other != id && !surfacesApart(id, other, true) && (heap::keys::isPrefixOf(seq, want) || heap::keys::isPrefixOf(want, seq))) {
      return m.value(QStringLiteral("id")).toString();
    }
  }
  return {};
}

QString AppController::keyChord(int key, int modifiers, const QString& text, quint32 scanCode) const {
  heap::keys::KeyInput in;
  in.key = key;
  in.modifiers = Qt::KeyboardModifiers(modifiers);
  in.text = text;
  // QML's KeyEvent carries the scan code, not the virtual key: on Windows a
  // set-1 scan code is the X keycode less 8.
  const QString platform = QGuiApplication::platformName();
  heap::keys::NativeKeys native = heap::keys::NativeKeys::None;
  if(platform == QLatin1String("windows") && scanCode != 0) {
    in.nativeScanCode = scanCode + 8;
    native = heap::keys::NativeKeys::X11;
  } else if(platform == QLatin1String("xcb")) {
    in.nativeScanCode = scanCode;
    native = heap::keys::NativeKeys::X11;
  }
  const QInputMethod* im = QGuiApplication::inputMethod();
  const QLocale::Script script = im != nullptr ? im->locale().script() : QLocale::LatinScript;
  return heap::keys::chordFor(in, native, script == QLocale::LatinScript || script == QLocale::AnyScript);
}

QString AppController::commonShortcutWarning(const QString& id, const QString& sequence) const {
  const QString seq = normalizeSequence(sequence);
  const QString why = heap::keys::commonReason(seq);
  if(why.isEmpty()) {
    return {};
  }
  // The action's own default ("undo" on Ctrl+Z) is not a surprise.
  const int i = shortcutIndexOf(id);
  if(i >= 0) {
    const QString def = m_shortcuts[i].toMap().value(QStringLiteral("defaultSequence")).toString();
    if(heap::keys::chordsOf(def).contains(seq)) {
      return {};
    }
  }
  return tr_(QStringLiteral("hotkeys.common")).arg(keyText(seq), tr_(QStringLiteral("hotkeys.common.") + why));
}

QString AppController::reservedShortcutReason(const QString& sequence) const {
  const QString why = heap::keys::reservedReason(normalizeSequence(sequence));
  return why.isEmpty() ? QString() : tr_(QStringLiteral("hotkeys.reserved.") + why);
}

int AppController::shortcutIndexOf(const QString& id) const {
  for(int i = 0; i < m_shortcuts.size(); ++i) {
    if(m_shortcuts[i].toMap().value("id").toString() == id) {
      return i;
    }
  }
  return -1;
}

QString AppController::normalizeSequence(const QString& raw) const {
  // A space typed as the key itself (" ", "Ctrl+Shift+ ") is the Space key;
  // trimming it away left nothing, or a dangling "+", and unbound the action
  // (SHELL-5).
  QString spelled = raw;
  if(spelled == QStringLiteral(" ") || spelled.endsWith(QStringLiteral("+ "))) {
    spelled.chop(1);
    spelled += QStringLiteral("Space");
  }
  const QString trimmed = spelled.trimmed();
  if(trimmed.isEmpty()) {
    return QString();
  }
  const QKeySequence ks(trimmed, QKeySequence::PortableText);
  if(ks.isEmpty()) {
    return QString();
  }
  return ks.toString(QKeySequence::PortableText);
}

void AppController::applyShortcutOverrides(const QVariantMap& overrides) {
  bool changed = false;
  for(auto& m_shortcut : m_shortcuts) {
    QVariantMap m = m_shortcut.toMap();
    const QString id = m.value("id").toString();
    if(!overrides.contains(id)) {
      continue;
    }
    const QString seq = normalizeSequence(overrides.value(id).toString());
    if(seq == m.value("sequence").toString()) {
      continue;
    }
    m["sequence"] = seq;
    m_shortcut = m;
    changed = true;
  }
  if(changed) {
    emit shortcutsChanged();
  }
}

QString AppController::shortcutFor(const QString& id) const {
  const int i = shortcutIndexOf(id);
  return i < 0 ? QString() : m_shortcuts[i].toMap().value("sequence").toString();
}

namespace {

QList<double> uiScaleSteps(const QVariantList& steps) {
  QList<double> values;
  for(const QVariant& v : steps) {
    bool ok = false;
    const double d = v.toDouble(&ok);
    if(ok && std::isfinite(d) && d > 0) {
      values.append(d);
    }
  }
  return values;
}

}  // namespace

void AppController::setWindowFrameDark(QObject* window, bool dark) const {
  heap::platform::setWindowFrameDark(qobject_cast<QWindow*>(window), dark);
}

double AppController::systemPixelRatio() const {
  const QScreen* screen = QGuiApplication::primaryScreen();
  return screen != nullptr ? screen->devicePixelRatio() : 1.0;
}

double AppController::systemUiScale(const QVariantList& steps) const {
  if(m_systemTextScale <= 0) {
    const bool forced = qEnvironmentVariableIsSet("HEAP_TEXT_SCALE");
    // A test run reads the same layout on every machine.
    m_systemTextScale = QStandardPaths::isTestModeEnabled() && !forced ? 1.0 : heap::platform::systemTextScale();
  }
  return heap::ui::uiScaleForTextScale(m_systemTextScale, uiScaleSteps(steps));
}

double AppController::screenUiScale() const {
  const QScreen* screen = QGuiApplication::primaryScreen();
  if(screen == nullptr) {
    return 1.1;
  }
  // A test run reads the same layout on every machine: no screen size.
  const QSizeF mm = QStandardPaths::isTestModeEnabled() ? QSizeF() : screen->physicalSize();
  const double inches = std::hypot(mm.width(), mm.height()) / 25.4;
  return heap::ui::screenDefaultScale(screen->size().width(), screen->size().height(), inches, screen->devicePixelRatio());
}

double AppController::defaultUiScale(const QVariantList& steps) const {
  return std::max(screenUiScale(), systemUiScale(steps));
}

double AppController::stepUiScale(int direction, const QVariantList& steps) {
  const QList<double> values = uiScaleSteps(steps);
  if(values.isEmpty()) {
    return 1.0;
  }
  const auto [lo, hi] = std::minmax_element(values.cbegin(), values.cend());
  QJsonObject settings = QJsonDocument::fromJson(m_appSettingsJson.toUtf8()).object();
  QJsonObject appearance = settings.value(QStringLiteral("appearance")).toObject();
  // Reset is the size the app starts at — what the system's text size asks
  // for (110 % on many screens), not 100 %, which the keyboard could then
  // never get back from (IDIOT-SHELL-14, PERSONA-22).
  if(direction == 0) {
    if(appearance.contains(QStringLiteral("uiScale"))) {
      appearance.remove(QStringLiteral("uiScale"));
      settings.insert(QStringLiteral("appearance"), appearance);
      setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
    }
    return defaultUiScale(steps);
  }
  // Read the way Theme.scale does: unset is the default on screen (the first
  // Ctrl+= stored the 110 % already shown, SHELL-3), anything outside the
  // steps' range is 1.
  const QJsonValue stored = appearance.value(QStringLiteral("uiScale"));
  double current = stored.isDouble() ? stored.toDouble() : defaultUiScale(steps);
  if(!std::isfinite(current) || current < *lo - 1e-6 || current > *hi + 1e-6) {
    current = 1.0;
  }
  const double next = heap::ui::nextUiScale(current, direction, values);
  if(!stored.isDouble() || std::abs(stored.toDouble() - next) > 1e-9) {
    appearance.insert(QStringLiteral("uiScale"), next);
    settings.insert(QStringLiteral("appearance"), appearance);
    setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
  }
  return next;
}

QString AppController::globalHotkeyBackend() const {
  return m_globalHotkey ? m_globalHotkey->backend() : QStringLiteral("none");
}

void AppController::noteMouseAction(const QString& shortcutId) {
  const bool enabled = heap::hints::hintsEnabled(QJsonDocument::fromJson(m_appSettingsJson.toUtf8()).object());
  const QString sequence = shortcutFor(shortcutId);
  QJsonObject uses = m_settingsExtra.value(QStringLiteral("shortcutHints")).toObject();
  const heap::hints::MouseUseResult r = heap::hints::recordMouseUse(uses, shortcutId, !sequence.isEmpty(), enabled);
  if(!r.changed) {
    return;
  }
  m_settingsExtra.insert(QStringLiteral("shortcutHints"), uses);
  scheduleSave();
  if(r.showHint) {
    emit shortcutHintRequested(shortcutId, sequence, shortcutLabel(shortcutId));
  }
}

QString AppController::defaultShortcutFor(const QString& id) const {
  const int i = shortcutIndexOf(id);
  return i < 0 ? QString() : m_shortcuts[i].toMap().value("defaultSequence").toString();
}

QString AppController::shortcutDescription(const QString& id) const {
  const int i = shortcutIndexOf(id);
  return i < 0 ? QString() : m_shortcuts[i].toMap().value("description").toString();
}

QString AppController::shortcutLabel(const QString& id) const {
  const int i = shortcutIndexOf(id);
  if(i < 0) {
    // The built-in keys that have no catalog entry of their own.
    return id.startsWith(QStringLiteral("builtin.")) ? tr_(QStringLiteral("hotkeys.") + id) : QString();
  }
  return m_shortcuts[i].toMap().value("label").toString();
}

namespace {

// Keys wired in QML next to the catalog's own binding, which no rebinding
// moves: Main.qml's Ctrl+P and the board's arrows, Enter, Menu and Ctrl+arrows,
// and the editors' fixed Ctrl+Shift+M / Ctrl+Shift+A. Two live shortcuts on
// one sequence are ambiguous to Qt and neither fires, so a catalog action
// that would share one with them in the same place is refused (SHELL-4).
struct BuiltinKey {
  const char* sequence;
  const char* owner;  // the catalog action it aliases, or a builtin.* label
  const char* scope;  // where it is live
};

constexpr BuiltinKey kBuiltinKeys[] = {
    {"Down", "board.cursorDown", "board"},
    {"Up", "board.cursorUp", "board"},
    {"Left", "board.cursorLeft", "board"},
    {"Right", "board.cursorRight", "board"},
    {"Enter", "board.open", "board"},
    {"Menu", "board.cardMenu", "board"},
    {"Ctrl+Down", "board.moveDown", "board"},
    {"Ctrl+Up", "board.moveUp", "board"},
    {"Ctrl+Left", "board.moveLeft", "board"},
    {"Ctrl+Right", "board.moveRight", "board"},
    {"Ctrl+Shift+M", "builtin.notesMode", "notes"},
    {"Ctrl+Shift+A", "builtin.attach", "editor"},
    // Zoom: "+" is Shift+= on most layouts, and the numpad has its own keys.
    {"Ctrl++", "zoom.in", "app"},
    {"Ctrl+Shift+=", "zoom.in", "app"},
    {"Ctrl+Num++", "zoom.in", "app"},
    {"Ctrl+Num+-", "zoom.out", "app"},
    {"Ctrl+Num+0", "zoom.reset", "app"},
};

// Where a catalog action is live: the board's, calendar's and notes' keys only
// in their view, everything else everywhere.
QString shortcutScope(const QString& id) {
  for(const char* scope : {"board", "cal", "notes"}) {
    if(id.startsWith(QLatin1String(scope) + QLatin1Char('.'))) {
      return QString::fromLatin1(scope);
    }
  }
  return QStringLiteral("app");
}

}  // namespace

QString AppController::builtinShortcutOwner(const QString& id, const QString& normalized) {
  const QString scope = shortcutScope(id);
  for(const BuiltinKey& k : kBuiltinKeys) {
    if(normalized != QLatin1String(k.sequence) || id == QLatin1String(k.owner)) {
      continue;
    }
    // An editor's keys are live over every view, an app-wide action's over
    // every scope; otherwise only the same view's keys meet.
    const QLatin1String keyScope(k.scope);
    if(scope == QLatin1String("app") || keyScope == QLatin1String("app") || keyScope == QLatin1String("editor") || scope == keyScope) {
      return QString::fromLatin1(k.owner);
    }
  }
  return QString();
}

namespace {
// Keys live only on a column header that holds the keyboard (keymap.md: "на
// заголовке колонки"). The board's router takes them there and leaves them
// alone everywhere else, so they meet only the board's own keys: Ctrl+Shift+L
// is the column move on a header and the log anywhere else (DG-133).
bool isHeaderKey(const QString& id) {
  return id == QLatin1String("board.columnLeft") || id == QLatin1String("board.columnRight");
}

// Where an action can be live at all (dogfood 2026-10-19). Main's _keyLive
// keeps these apart at run time, so two actions on surfaces that never hold
// the keyboard together may share a key: Del deletes the selected tasks on
// a task view and the note on the Knowledge list; F2 renames the task under
// the cursor and the open note; Ctrl+arrows move a card on the board and a
// task in the calendar. "" is everywhere.
QString keySurface(const QString& id) {
  const QString base = baseShortcutId(id);
  if(base == QLatin1String("notes.delete")) {
    return QStringLiteral("noteList");
  }
  if(base.startsWith(QLatin1String("editor."))) {
    return QStringLiteral("editor");
  }
  if(base.startsWith(QLatin1String("notes.")) && base != QLatin1String("notes.new")) {
    return QStringLiteral("knowledge");
  }
  if((base.startsWith(QLatin1String("task.")) && base != QLatin1String("task.new")) || base == QLatin1String("selection.deleteSel")) {
    return QStringLiteral("tasks");
  }
  if(base.startsWith(QLatin1String("board.move"))) {
    return QStringLiteral("boardMove");
  }
  if(base.startsWith(QLatin1String("cal.task"))) {
    return QStringLiteral("calMove");
  }
  return {};
}

// Whether the keys of `a` and `b` can never be live at once. `prefix`: the
// question is whether one may start the other's sequence — a document's
// "d d" beside the task's "d" in the task document, where KeyRouter runs
// the single key after the wait for the second one.
bool surfacesApart(const QString& a, const QString& b, bool prefix) {
  const QString sa = keySurface(a);
  const QString sb = keySurface(b);
  if(sa.isEmpty() || sb.isEmpty() || sa == sb) {
    return false;
  }
  const auto pair = [&](const char* x, const char* y) {
    return (sa == QLatin1String(x) && sb == QLatin1String(y)) || (sa == QLatin1String(y) && sb == QLatin1String(x));
  };
  if(pair("tasks", "knowledge") || pair("tasks", "noteList") || pair("noteList", "editor") || pair("boardMove", "calMove")) {
    return true;
  }
  return prefix && pair("tasks", "editor");
}
}  // namespace

QString AppController::findShortcutConflict(const QString& id, const QString& sequence) const {
  const QString want = normalizeSequence(sequence);
  if(want.isEmpty()) {
    return QString();
  }
  for(const auto& m_shortcut : m_shortcuts) {
    const QVariantMap m = m_shortcut.toMap();
    const QString other = m.value("id").toString();
    if(other == id) {
      continue;
    }
    if(isHeaderKey(id) != isHeaderKey(other) && !(isHeaderKey(id) ? other : id).startsWith(QLatin1String("board."))) {
      continue;
    }
    const QString seq = m.value("sequence").toString();
    if(surfacesApart(id, other, seq != want)) {
      continue;
    }
    // g with g b: the one key would fire before the sequence could be typed.
    if(seq == want || heap::keys::isPrefixOf(seq, want) || heap::keys::isPrefixOf(want, seq)) {
      return m.value("id").toString();
    }
  }
  return builtinShortcutOwner(id, want);
}

bool AppController::setShortcut(const QString& id, const QString& sequence) {
  const int i = shortcutIndexOf(id);
  if(i < 0) {
    return false;
  }
  const QString seq = normalizeSequence(sequence);
  // Something typed that is no key sequence must not read as "unbind".
  if(seq.isEmpty() && !sequence.trimmed().isEmpty()) {
    return false;
  }
  QVariantMap m = m_shortcuts[i].toMap();
  if(m.value("sequence").toString() == seq) {
    return true;
  }

  // The system keeps some keys, and Tab drives every dialog (APP-272).
  if(!seq.isEmpty()) {
    const QString reserved = reservedShortcutReason(seq);
    if(!reserved.isEmpty()) {
      emit toast(reserved, QStringLiteral("warning"));
      return false;
    }
  }
  // A key that starts another's sequence, or a sequence that starts with
  // another's key, cannot be swapped: g would make every g … dead.
  if(!seq.isEmpty()) {
    const QString prefixOwner = prefixShortcutConflict(id, seq);
    if(!prefixOwner.isEmpty()) {
      emit toast(
          tr_(QStringLiteral("hotkeys.prefixTaken")).arg(keyText(seq), keyText(shortcutFor(prefixOwner)), shortcutLabel(prefixOwner)),
          QStringLiteral("warning"));
      return false;
    }
  }
  // A built-in key stays where it is whatever the catalog says, so there is
  // nothing to swap: taking it would only make both dead (SHELL-4).
  if(!seq.isEmpty()) {
    const QString builtin = builtinShortcutOwner(id, seq);
    if(!builtin.isEmpty()) {
      emit toast(tr_("hotkeys.builtinTaken").arg(seq, shortcutLabel(builtin)), QStringLiteral("warning"));
      return false;
    }
  }

  // VS-Code-style swap: clear the conflicting owner so the new binding wins.
  // Every one: a sequence can collide with several that start with it.
  int guard = 0;
  for(QString conflictId = seq.isEmpty() ? QString() : findShortcutConflict(id, seq); !conflictId.isEmpty() && guard < 64;
      conflictId = findShortcutConflict(id, seq), ++guard) {
    const int j = shortcutIndexOf(conflictId);
    if(j < 0) {
      break;
    }
    QVariantMap o = m_shortcuts[j].toMap();
    const QString freedLabel = o.value("label").toString();
    o["sequence"] = QString();
    m_shortcuts[j] = o;
    emit toast(tr_("slot.freed").arg(freedLabel));
  }

  m["sequence"] = seq;
  m_shortcuts[i] = m;
  emit shortcutsChanged();
  // Re-arm both capture hotkeys — the change may have retargeted a capture
  // binding or freed one via conflict resolution above.
  registerGlobalHotkeys();
  scheduleSave();
  return true;
}

void AppController::resetShortcut(const QString& id) {
  const int i = shortcutIndexOf(id);
  if(i < 0) {
    return;
  }
  QVariantMap m = m_shortcuts[i].toMap();
  const QString def = m.value("defaultSequence").toString();
  if(m.value("sequence").toString() == def) {
    return;
  }
  // If the default would conflict with another action, swap it out — each
  // one, since a sequence can collide with several that start with it.
  int guard = 0;
  for(QString conflictId = findShortcutConflict(id, def); !conflictId.isEmpty() && guard < 64;
      conflictId = findShortcutConflict(id, def), ++guard) {
    const int j = shortcutIndexOf(conflictId);
    if(j < 0) {
      break;
    }
    QVariantMap o = m_shortcuts[j].toMap();
    o["sequence"] = QString();
    m_shortcuts[j] = o;
  }
  m["sequence"] = def;
  m_shortcuts[i] = m;
  emit shortcutsChanged();
  registerGlobalHotkeys();
  scheduleSave();
}

void AppController::resetAllShortcuts() {
  bool changed = false;
  for(auto& m_shortcut : m_shortcuts) {
    QVariantMap m = m_shortcut.toMap();
    const QString def = m.value("defaultSequence").toString();
    if(m.value("sequence").toString() != def) {
      m["sequence"] = def;
      m_shortcut = m;
      changed = true;
    }
  }
  if(changed) {
    emit shortcutsChanged();
    scheduleSave();
    registerGlobalHotkeys();
    emit toast(tr_("hotkeys.reset"));
  }
}

// ── Notifications, transitions, automation ─────────────────────────────

double AppController::snapStepHours() const {
  return heap::cal::stepHours(settingsMap().value("calendar").toMap().value("snapMinutes", 15).toInt());
}

int AppController::deadlineHorizon() const {
  return heap::plan::clampDeadlineHorizon(settingsMap().value("calendar").toMap().value("deadlineHorizon", 0).toInt());
}

QVariantList AppController::deadlineHorizonChoices() const {
  QVariantList out;
  for(const int c : heap::plan::deadlineHorizonChoices()) {
    out.append(c);
  }
  return out;
}

QVariantMap AppController::settingsMap() const {
  // Parsed on demand and cached against the string it came from. Twenty-odd
  // call sites read this, including two per debounced save and one inside a
  // QML loop (eventHourLabel, called per linked event), so re-parsing the
  // whole settings document each time was the shape of the cost rather than
  // its size. Keying the cache on the source string rather than invalidating
  // it at each of the four writers means no writer can forget to.
  if(m_settingsCacheSource == m_appSettingsJson) {
    return m_settingsCache;
  }
  m_settingsCacheSource = m_appSettingsJson;
  m_settingsCache = {};
  if(m_appSettingsJson.isEmpty()) {
    return m_settingsCache;
  }
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(m_appSettingsJson.toUtf8(), &err);
  if(err.error != QJsonParseError::NoError || !doc.isObject()) {
    return m_settingsCache;
  }
  m_settingsCache = doc.object().toVariantMap();
  return m_settingsCache;
}

QString AppController::workflowTransitionsKey(const QString& providerId,
                                              const QString& project,
                                              const QString& issueType,
                                              const QString& status) {
  return QStringList{providerId, project, issueType, status.toCaseFolded()}.join(QChar('\n'));
}

bool AppController::canTransitionStatus(const QString& taskId, const QString& newStatus) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return false;
  }
  const Task& t = m_tasks.items().at(row);
  if(newStatus == QStringLiteral("review")) {
    const QVariantMap s = settingsMap();
    const QVariantMap tasks = s.value("tasks").toMap();
    if(tasks.value("requireBranchOnReview", false).toBool() && t.branch.trimmed().isEmpty()) {
      emit toast(tr_("branch.required"), QStringLiteral("warning"));
      playSound_(static_cast<int>(heap::platform::SoundCue::Refuse));
      return false;
    }
  }
  // A merge / pull request's column is its stage in the forge (APP-242). It
  // moves only when the user said such cards may, and then only here.
  if(isReviewItem(t) && newStatus != t.status && !reviewCardsMovable(t.externalProvider)) {
    const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(t.externalProvider);
    const QString label = d ? d->displayName : t.externalProvider;
    emit trackerReadOnlyMove(taskId, tr_("int.reviewFixed").arg(externalKeyOf(t), label), t.externalUrl);
    playSound_(static_cast<int>(heap::platform::SoundCue::Refuse));
    return false;
  }
  // Only a tracker heap writes to has a say in where its cards go. With the
  // switch off (the default) a move is a local one, and neither the workflow
  // nor the filter below can refuse it (APP-243).
  const bool writes =
      !t.externalProvider.isEmpty() && !t.externalId.isEmpty() && newStatus != t.status && trackerWriteEnabled(t.externalProvider);
  // Outside the filter or gone: the tracker is read-only for this card. The
  // drop is refused rather than sent, so heap never changes the status of an
  // issue that is not the user's any more (APP-204).
  if(writes && (t.externalMeta.outOfScope || t.externalMeta.goneUpstream)) {
    const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(t.externalProvider);
    const QString label = d ? d->displayName : t.externalProvider;
    const QString message = t.externalMeta.goneUpstream ? tr_("int.readOnlyGone").arg(externalKeyOf(t), label)
                                                        : tr_("int.readOnlyOutOfScope").arg(externalKeyOf(t), label);
    emit trackerReadOnlyMove(taskId, message, t.externalUrl);
    playSound_(static_cast<int>(heap::platform::SoundCue::Refuse));
    return false;
  }
  // A tracker whose workflow decides the moves (Jira) said, on the last pull,
  // which statuses this issue can go to. A column none of them maps to would
  // only be refused after the round trip and leave the card "unsynced" —
  // say so on the drop instead (INT-8).
  if(writes) {
    const auto known = m_trackerTransitions.constFind(t.externalProvider + QChar('\n') + t.externalId);
    if(known != m_trackerTransitions.constEnd()) {
      using heap::integrations::StatusMap;
      const QHash<QString, QString> overrides = statusOverridesFor(t.externalProvider);
      // Back to where the tracker already has it: nothing to transition.
      if(!t.externalMeta.status.isEmpty() && trackerColumn(t.externalProvider, t.externalMeta.status, overrides, QString()) == newStatus) {
        return true;
      }
      const auto columnName = [this](const QString& id) {
        for(const QVariant& v : m_statuses) {
          const QVariantMap m = v.toMap();
          if(m.value(QStringLiteral("id")).toString() == id) {
            return m.value(QStringLiteral("name")).toString();
          }
        }
        return id;
      };
      QStringList reachable;
      for(const QString& status : *known) {
        const QString column = trackerColumn(t.externalProvider, status, overrides, QString());
        if(column == newStatus) {
          return true;
        }
        if(!column.isEmpty() && !reachable.contains(columnName(column))) {
          reachable.append(columnName(column));
        }
      }
      const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(t.externalProvider);
      const QString label = d ? d->displayName : t.externalProvider;
      emit toast(
          reachable.isEmpty()
              ? tr_("int.transitionNone").arg(externalKeyOf(t), label, columnName(newStatus))
              : tr_("int.transitionRefused").arg(externalKeyOf(t), label, columnName(newStatus), reachable.join(QStringLiteral(", "))));
      playSound_(static_cast<int>(heap::platform::SoundCue::Refuse));
      return false;
    }
  }
  return true;
}

void AppController::setArchived(const QString& taskId, bool archived) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const bool prev = m_tasks.items().at(row).archived;
  if(prev == archived) {
    return;
  }
  // The scope records the change, so an accidental (un)archive is reversible
  // without this function knowing anything about undo.
  const UndoScope scope(this, tr_("task.archiveUndone").arg(taskId));
  m_tasks.setArchived(taskId, archived);
  if(archived) {
    dropFutureFocusBlocks(taskId);
  }

  QString msg = tr_(archived ? "task.archived" : "task.unarchived").arg(displayKey(taskId));
  // e is "complete" in Todoist: the toast names the key for done (A5-20).
  const QString doneKey = shortcutText(QStringLiteral("task.done"));
  if(archived && !doneKey.isEmpty()) {
    msg += tr_("task.archived.doneKey").arg(doneKey);
  }
  emit undoableToast(msg, 5);
  scheduleSave();
}

// ─────────────────────────────────────────────────── Multi-select ──

bool AppController::isTaskSelected(const QString& id) const {
  return m_selectedTaskIds.contains(id);
}

void AppController::rebuildSelectionList_() {
  // Stable order: by current row in m_tasks. Tasks no longer present (e.g.
  // deleted while selected) drop out of the list.
  QVector<QPair<int, QString>> rows;
  rows.reserve(m_selectedTaskIds.size());
  for(const QString& id : m_selectedTaskIds) {
    const int r = m_tasks.indexOfId(id);
    if(r >= 0) {
      rows.append({r, id});
    }
  }
  std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
    return a.first < b.first;
  });
  QSet<QString> live;
  QStringList list;
  list.reserve(rows.size());
  for(const auto& p : rows) {
    list.append(p.second);
    live.insert(p.second);
  }
  m_selectedTaskIds = live;
  m_selectedTaskIdsList = list;
}

void AppController::toggleTaskSelection(const QString& id) {
  if(id.isEmpty()) {
    return;
  }
  if(m_selectedTaskIds.contains(id)) {
    m_selectedTaskIds.remove(id);
  } else {
    m_selectedTaskIds.insert(id);
  }
  rebuildSelectionList_();
  emit selectedTaskIdsChanged();
}

void AppController::setTaskSelected(const QString& id, bool selected) {
  if(id.isEmpty()) {
    return;
  }
  const bool had = m_selectedTaskIds.contains(id);
  if(selected == had) {
    return;
  }
  if(selected) {
    m_selectedTaskIds.insert(id);
  } else {
    m_selectedTaskIds.remove(id);
  }
  rebuildSelectionList_();
  emit selectedTaskIdsChanged();
}

void AppController::setSelectedTaskIds(const QStringList& ids) {
  const QSet<QString> next(ids.constBegin(), ids.constEnd());
  if(next == m_selectedTaskIds) {
    return;
  }
  m_selectedTaskIds = next;
  rebuildSelectionList_();
  emit selectedTaskIdsChanged();
}

void AppController::clearSelection() {
  if(m_selectedTaskIds.isEmpty() && m_selectedTaskIdsList.isEmpty()) {
    return;
  }
  m_selectedTaskIds.clear();
  m_selectedTaskIdsList.clear();
  emit selectedTaskIdsChanged();
}

void AppController::deleteSelectedTasks() {
  // A card selected and then hidden by the search was deleted with the ones
  // on screen: "1 visible, 3 deleted" (TASKS-13, audit 2026-09-30).
  pruneSelectionToFilter_();
  if(m_selectedTaskIdsList.isEmpty()) {
    return;
  }

  // Snapshot tasks + rows in ascending-row order for clean re-insertion.
  QVector<QPair<int, ::Task>> snap;
  snap.reserve(m_selectedTaskIdsList.size());
  for(const QString& id : m_selectedTaskIdsList) {
    const int row = m_tasks.indexOfId(id);
    if(row < 0) {
      continue;
    }
    snap.append({row, m_tasks.items().at(row)});
  }
  if(snap.isEmpty()) {
    clearSelection();
    return;
  }
  std::sort(snap.begin(), snap.end(), [](const auto& a, const auto& b) {
    return a.first < b.first;
  });

  const UndoScope scope(this, tr_("selection.toast.restored").arg(snap.size()));
  QSet<QString> ids;
  for(const auto& p : snap) {
    ids.insert(p.second.id);
  }
  QStringList linkedEventIds;
  for(const CalEvent& e : m_events.items()) {
    if(ids.contains(e.taskId)) {
      linkedEventIds << e.id;
    }
  }
  // Remove the linked events with their tasks (HEAP-104), then the tasks.
  // Removal order does not matter: the scope records each row for undo.
  for(const QString& eventId : linkedEventIds) {
    m_events.removeById(eventId);
  }
  for(const auto& p : snap) {
    // Same "not mine" note a single delete records, so a bulk delete of
    // mirrored issues is not undone by the next pull.
    dismissExternalTask(p.second.externalProvider, p.second.externalId);
    m_tasks.removeById(p.second.id);
  }

  const int n = snap.size();
  emit undoableToast(tr_("selection.toast.deleted").arg(n), 5);
  // Five or more gone at once: the toast says, once, where they can come
  // back from after it is gone (coach mark).
  if(n >= heap::hints::kBulkDeleteHintAt) {
    offerCoachMark(QStringLiteral("timeMachine"), {{QStringLiteral("count"), n}});
  }
  clearSelection();
  scheduleSave();
}

void AppController::moveSelectedTasksToStatus(const QString& statusId) {
  pruneSelectionToFilter_();
  if(m_selectedTaskIdsList.isEmpty() || statusId.isEmpty()) {
    return;
  }
  if(statusIndexOf(statusId) < 0) {
    return;
  }
  // A bulk move used to record nothing at all, so dragging a wrong selection
  // across the board was unrecoverable. The scope covers it for free.
  UndoScope scope(this, tr_("selection.toast.moved").arg(m_selectedTaskIdsList.size()));
  // Each card goes through moveTask, so a bulk move does what a drag does: the
  // tracker hears about it, the review-branch rule applies, a finished
  // recurring task spawns its next one. Only the per-card toast is held back.
  int moved = 0;
  ++m_bulkMoveDepth;
  const QStringList ids = m_selectedTaskIdsList;
  for(const QString& id : ids) {
    const int row = m_tasks.indexOfId(id);
    if(row < 0 || m_tasks.items().at(row).status == statusId) {
      continue;
    }
    moveTask(id, statusId);
    const int after = m_tasks.indexOfId(id);
    if(after >= 0 && m_tasks.items().at(after).status == statusId) {
      ++moved;
    }
  }
  --m_bulkMoveDepth;
  if(m_soundPending >= 0) {
    const int cue = m_soundPending;
    m_soundPending = -1;
    playSound_(cue);
  }
  if(moved > 0) {
    scope.setLabel(tr_("selection.toast.moved").arg(moved));
    emit undoableToast(tr_("selection.toast.moved").arg(moved), 5);
    scheduleSave();
  }
}

void AppController::setSelectedTasksArchived(bool archived) {
  pruneSelectionToFilter_();
  if(m_selectedTaskIdsList.isEmpty()) {
    return;
  }
  // Same as the bulk move above: this recorded nothing before, so archiving
  // the wrong selection meant restoring each ticket by hand.
  UndoScope scope(this, tr_(archived ? "selection.toast.archived" : "selection.toast.unarchived").arg(m_selectedTaskIdsList.size()));
  int n = 0;
  for(const QString& id : m_selectedTaskIdsList) {
    if(m_tasks.indexOfId(id) < 0) {
      continue;
    }
    m_tasks.setArchived(id, archived);
    if(archived) {
      dropFutureFocusBlocks(id);
    }
    ++n;
  }
  if(n > 0) {
    scope.setLabel(tr_(archived ? "selection.toast.archived" : "selection.toast.unarchived").arg(n));
    emit undoableToast(tr_(archived ? "selection.toast.archived" : "selection.toast.unarchived").arg(n), 5);
    // Drop the selection after a bulk archive/restore. Tickets just left
    // the current view (archive view loses unarchived ones; board loses
    // archived ones), so keeping the prior selection is confusing.
    clearSelection();
    scheduleSave();
  }
}

void AppController::notify(const QString& title, const QString& body, const QString& kind) {
  emit notification(title, body, kind);
}

bool AppController::inQuietHours(const QDateTime& when) const {
  const QVariantMap s = settingsMap();
  const QVariantMap notif = s.value("notifications").toMap();
  // "Не беспокоить 1 ч" from the tray holds everything until it runs out.
  const QDateTime dnd = QDateTime::fromString(notif.value("dndUntil").toString(), Qt::ISODate);
  if(dnd.isValid() && when < dnd) {
    return true;
  }
  if(!notif.value("quietHours", true).toBool()) {
    return false;
  }
  const QTime from = heap::cal::clockTime(notif.value("quietFrom", "19:00").toString());
  const QTime to = heap::cal::clockTime(notif.value("quietTo", "09:00").toString());
  // The window wraps midnight in the usual case (19:00..09:00).
  return heap::cal::inQuietWindow(from, to, when.time());
}

double AppController::nextQuarterHour(const QDateTime& when) {
  const QTime t = when.time();
  const double cur = t.hour() + t.minute() / 60.0;
  const double q = std::ceil(cur * 4.0) / 4.0;
  return std::min(q, 24.0);
}

bool AppController::isDoingStatus(const QString& statusId) const {
  if(statusId == QStringLiteral("prog")) {
    return true;
  }
  for(const QVariant& v : m_statuses) {
    const QVariantMap m = v.toMap();
    if(m.value("id").toString() == statusId) {
      return m.value("doing").toBool();
    }
  }
  return false;
}

void AppController::setStatusDoing(const QString& statusId, bool doing) {
  for(int i = 0; i < m_statuses.size(); ++i) {
    QVariantMap m = m_statuses.at(i).toMap();
    if(m.value("id").toString() != statusId) {
      continue;
    }
    if(m.value("doing").toBool() == doing) {
      return;
    }
    const UndoScope scope(this, tr_("status.doingUndone").arg(m.value("name").toString()));
    m["doing"] = doing;
    m_statuses[i] = m;
    emit statusesChanged();
    scheduleSave();
    return;
  }
}

void AppController::focusBlockOnStatusChange(const QString& taskId, const QString& from, const QString& to) {
  const bool wasDoing = !from.isEmpty() && isDoingStatus(from);
  const bool isDoing = isDoingStatus(to);
  if(isDoing && !wasDoing) {
    // A fact, not a refusal (APP-240): it still waits on something open.
    noteOpenBlockers_(taskId);
    if(settingsMap().value("calendar").toMap().value("autoFocusBlock", true).toBool()) {
      scheduleFocusBlockFor(taskId);
    }
  } else if(wasDoing && !isDoing) {
    // Time set aside for work that stopped being in progress — finished, put
    // back, parked in review — is time given back.
    dropFutureFocusBlocks(taskId);
  }
}

void AppController::scheduleFocusBlockFor(const QString& taskId) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  const QDateTime now = QDateTime::currentDateTime();
  // One planned block per task is enough: a card dragged back and forth
  // through In Progress must not stack them up.
  for(const CalEvent& e : m_events.items()) {
    if(e.taskId == taskId && e.type == QStringLiteral("focus") && QDateTime(e.date, heap::cal::hourToTime(qMin(e.end, 23.99))) > now) {
      return;
    }
  }
  // As long as the task's estimate when it has one (APP-246), else the
  // focus-block length from settings — the same rule as a drop on the grid.
  const int durMin = taskBlockMinutes(taskId);
  const double dur = std::max(0.25, durMin / 60.0);
  // The first free stretch inside working hours, on a working day, within the
  // coming week. A Saturday-night drag used to book 21:00 that same night.
  // The same search as "next free window" (APP-253): task blocks are busy too.
  heap::plan::WindowAsk ask;
  ask.date = now.date();
  ask.now = now;
  ask.hours = dur;
  ask.workStart = m_workdayStart;
  ask.workEnd = m_workdayEnd;
  ask.lunch = lunchSpan();
  ask.step = snapStepHours();
  ask.maxDays = 6;
  const auto [day, start] = heap::plan::nextWindow(
      ask,
      [&](const QDate& d) {
        return busySpans(d, taskId, now);
      },
      [this](const QDate& d) {
        return isWorkDay(d);
      });
  if(day.isValid()) {
    CalEvent e;
    e.id = QStringLiteral("ev-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    e.title = QStringLiteral("Focus · %1").arg(t.title);
    e.type = QStringLiteral("focus");
    e.start = start;
    e.end = start + dur;
    e.date = day;
    e.taskId = taskId;
    e.profileId = m_activeProfileId;
    m_events.upsert(e);
    // The task is scheduled for the block, unless it already has a time of
    // its own. Otherwise the week and the rail still said "needs a slot".
    Task copy = m_tasks.items().at(row);
    if(!copy.scheduledAt.isValid()) {
      copy.scheduledAt = QDateTime(day, heap::cal::hourToTime(start));
      copy.scheduledHasTime = true;  // the deadline keeps its own flag (schema v10)
      m_tasks.upsert(copy);
    }
    scheduleSave();
    return;
  }
}

void AppController::dropFutureFocusBlocks(const QString& taskId) {
  const QDateTime now = QDateTime::currentDateTime();
  QVector<CalEvent> doomed;
  for(const CalEvent& e : m_events.items()) {
    if(e.taskId == taskId && e.type == QStringLiteral("focus") && QDateTime(e.date, heap::cal::hourToTime(e.start)) > now) {
      doomed.append(e);
    }
  }
  for(const CalEvent& e : doomed) {
    m_events.removeById(e.id);
    followFocusBlock(e, nullptr);
  }
  if(!doomed.isEmpty()) {
    scheduleSave();
  }
}

void AppController::runAutomation() {
  runAutomationAt(QDateTime::currentDateTime());
  // The tray tooltip carries the running timer (R3-027).
  refreshTray();
}

void AppController::runAutomationAt(const QDateTime& now) {
  const heap::frame::Span span("runAutomation");
  // Reminders sent during this tick reach reminders.json once, at its end.
  ++m_reminderBatchDepth;
  const auto flushReminders = qScopeGuard([this]() {
    if(--m_reminderBatchDepth == 0 && m_reminderSavePending) {
      m_reminderSavePending = false;
      saveSentReminders();
    }
  });
  // The minute tick is also what notices that the date moved on — after
  // midnight, a sleep or a time-zone change.
  refreshToday(now.date());
  const QDate today = now.date();
  const QVariantMap s = settingsMap();
  const QVariantMap tasksCfg = s.value("tasks").toMap();
  const QVariantMap notif = s.value("notifications").toMap();

  // 1. Re-compute blocked-stuck (status=blocked older than threshold).
  const int stuckDays = qMax(0, tasksCfg.value("autoMoveBlockedAfterDays", 3).toInt());
  QSet<QString> stuck;
  for(const Task& t : m_tasks.items()) {
    if(t.archived) {
      continue;
    }
    if(t.status != QStringLiteral("blocked")) {
      continue;
    }
    if(!t.statusChangedAt.isValid()) {
      continue;
    }
    if(t.statusChangedAt.daysTo(now) >= stuckDays) {
      stuck.insert(t.id);
    }
  }
  if(stuck != m_blockedStuckIds) {
    m_blockedStuckIds = stuck;
    m_tasks.setBlockedStuckIds(m_blockedStuckIds);
    emit blockedStuckChanged();
  }

  // Automation covers every profile, not only the one on screen (PLAT-9): a
  // task in a workspace the user has not opened today is still due today.
  // The active profile's rows live in the models, the others' in m_profiles.
  QSet<QString> stuckEverywhere = m_blockedStuckIds;
  for(const Profile& p : m_profiles) {
    if(p.id == m_activeProfileId) {
      continue;
    }
    for(const Task& t : p.tasks) {
      if(!t.archived && t.status == QStringLiteral("blocked") && t.statusChangedAt.isValid() &&
         t.statusChangedAt.daysTo(now) >= stuckDays) {
        stuckEverywhere.insert(t.id);
      }
    }
  }

  // 1b. Daily digest of blocked-stuck tasks — one notification per day.
  if(notif.value("blockedDailyDigest", false).toBool() && !stuckEverywhere.isEmpty()) {
    const QString sentinel = QStringLiteral("digest:") + today.toString(Qt::ISODate);
    if(!reminderSent(sentinel)) {
      markReminderSent(sentinel, now);
      QStringList ids;
      ids.reserve(stuckEverywhere.size());
      for(const QString& id : stuckEverywhere) {
        ids << id;
      }
      std::sort(ids.begin(), ids.end());
      const QString body = QStringLiteral("Stuck: %1").arg(ids.join(QStringLiteral(", ")));
      notify(QStringLiteral("Blocked daily digest (%1)").arg(ids.size()), body, QStringLiteral("digest"));
    }
  }

  // 2. Auto-archive: a card that has sat in its column past that column's
  // limit (APP-122). Done's limit is the Settings → Tasks slider; any other
  // column's is set from its menu, and is never by default.
  const int doneDays = qMax(0, tasksCfg.value("archiveDoneAfterDays", 7).toInt());
  const auto archiveDue = [&now](const Task& t, const QHash<QString, int>& days) {
    const int limit = days.value(t.status, 0);
    return !t.archived && limit > 0 && t.statusChangedAt.isValid() && t.statusChangedAt.daysTo(now) >= limit;
  };
  bool persistedAny = false;
  {
    const QHash<QString, int> days = archiveDaysByStatus(m_statuses, doneDays);
    QStringList toArchive;
    for(const Task& t : m_tasks.items()) {
      if(archiveDue(t, days)) {
        toArchive << t.id;
      }
    }
    for(const QString& id : toArchive) {
      m_tasks.setArchived(id, true);
      dropFutureFocusBlocks(id);
      persistedAny = true;
    }
    for(Profile& p : m_profiles) {
      if(p.id == m_activeProfileId) {
        continue;
      }
      const QHash<QString, int> theirs = archiveDaysByStatus(p.statuses, doneDays);
      for(Task& t : p.tasks) {
        if(archiveDue(t, theirs)) {
          t.archived = true;
          persistedAny = true;
        }
      }
    }
  }
  if(persistedAny) {
    scheduleSave();
  }

  // Reminders are held, not dropped, while quiet hours last: nothing below
  // marks a reminder sent until it is delivered, and each stays due until
  // shortly after what it is about, so the first tick after the quiet window
  // delivers it. A meeting or the standup is an appointment and is not held.
  const bool quiet = inQuietHours(now);

  // 2b. Reminders put off by a snooze button come back (APP-155); each goes
  // through the same quiet-hours rule as a fresh one.
  fireDueSnoozes(now);

  // 3. Deadline reminders — once when the deadline comes inside the lead, once
  // more when it has passed.
  if(notif.value("deadlineReminders", true).toBool() && !quiet) {
    const int leadHours = qMax(1, notif.value("deadlineLeadHours", 24).toInt());

    // Each with the profile it is in: the reminder's buttons act there (PRES-2).
    // Only the ones due are kept, and only what the reminder says: copying
    // every task of every profile each minute was most of the tick (APP-203).
    struct DueTask {
      QString profileId;
      QString id;
      QString title;
      QDate dueDay;
      heap::cal::DeadlineCall call;
    };

    QVector<DueTask> dueTasks;
    // A reminder is due from `leadHours` before the deadline to a day after
    // it. Most deadlines are days away, and the calendar date says so without
    // the time-zone arithmetic a QDateTime difference costs, every minute,
    // for every task.
    const qint64 lastDueDay = leadHours / 24 + 2;
    // Finished = a column of the Done kind on that task's own board
    // (IDIOT-CAL-4): a "Shipped" card kept nagging about its deadline.
    QSet<QString> doneIds = heap::board::doneColumnIds(m_statuses);
    const auto consider = [&](const QString& profileId, const Task& t) {
      const QDateTime dueAt = heap::local::effectiveDueAt(t);
      if(t.archived || !dueAt.isValid() || doneIds.contains(t.status)) {
        return;
      }
      const qint64 days = today.daysTo(dueAt.date());
      if(days < -2 || days > lastDueDay) {
        return;
      }
      // A task due at a parsed clock time fires then; a bare due date keeps the
      // old end-of-day horizon.
      const QDateTime deadlineAt = heap::local::effectiveDueHasTime(t) ? dueAt : QDateTime(dueAt.date(), QTime(23, 59));
      heap::cal::DeadlineCall call = heap::cal::deadlineReminder(t.id, deadlineAt, now, leadHours);
      if(call.due && !reminderSent(call.key)) {
        dueTasks.append({profileId, t.id, t.title, dueAt.date(), std::move(call)});
      }
    };
    for(const Task& t : m_tasks.items()) {
      consider(m_activeProfileId, t);
    }
    for(const Profile& p : m_profiles) {
      if(p.id != m_activeProfileId) {
        doneIds = heap::board::doneColumnIds(p.statuses);
        for(const Task& t : p.tasks) {
          consider(p.id, t);
        }
      }
    }
    for(const DueTask& t : dueTasks) {
      const heap::cal::DeadlineCall& call = t.call;
      if(reminderSent(call.key)) {
        continue;  // the same id in two profiles is one reminder
      }
      markReminderSent(call.key, now);
      const QString when = call.overdue
                               ? (call.hours < 1 ? tr_("notify.deadlineWhen.overdue") : tr_("notify.deadlineWhen.overdueH").arg(call.hours))
                           : (call.hours <= 1) ? tr_("notify.deadlineWhen.h1")
                                               : tr_("notify.deadlineWhen.hN").arg(call.hours);
      // N/X-Ntf-OS (R3-026): "Срок сегодня" / "Оформление заказа · ждёт
      // ответа: …" — the day, not the hours, and no priority in brackets.
      QString title = call.overdue ? tr_("notify.overdueTitle").arg(when) : tr_("notify.deadlineTitle").arg(when);
      if(!call.overdue && t.dueDay == today) {
        title = tr_("notify.dueToday");
      } else if(!call.overdue && t.dueDay == today.addDays(1)) {
        title = tr_("notify.dueTomorrow");
      }
      QString body = t.title;
      if(t.profileId == m_activeProfileId) {
        for(const WaitingOn& w : m_waitingOn) {
          const int row = w.taskId == t.id ? m_people.indexOfId(w.personId) : -1;
          if(row >= 0) {
            body += QStringLiteral(" · ") + tr_("notify.waitingOn").arg(m_people.items().at(row).name);
            break;
          }
        }
      }
      notifyTaskAt(heap::notify::taskRef(t.profileId, t.id), title, body, QStringLiteral("deadline"), now);
    }
  }

  // 3b. The start of a planned task block (APP-256): off unless switched on,
  // held by quiet hours and focus mode like the deadlines. It only says so:
  // no timer starts and the task stays in its column.
  if(notif.value("taskBlockReminders", false).toBool() && !quiet) {
    const int lead = qBound(0, notif.value("taskBlockLead", 0).toInt(), 60);
    const auto consider = [&](const QString& profileId, const Task& t, bool own) {
      if(t.archived || !t.scheduledHasTime || !t.scheduledAt.isValid()) {
        return;
      }
      if(qAbs(today.daysTo(t.scheduledAt.date())) > 1) {
        return;
      }
      const QString cat = own ? statusCategory(t.status) : heap::board::defaultCategoryFor(t.status);
      if(cat == QLatin1String("done")) {
        return;
      }
      const heap::cal::BlockCall call = heap::cal::taskBlockReminder(t.id, t.scheduledAt, now, lead);
      if(!call.due || reminderSent(call.key)) {
        return;
      }
      markReminderSent(call.key, now);
      // As long as Today draws it, whichever profile it is in (F1-new-11).
      const int minutes = heap::integrations::planMinutes(t) > 0 ? heap::integrations::planMinutes(t) : taskBlockMinutes(QString());
      const QDateTime end = t.scheduledAt.addSecs(60LL * minutes);
      const QString title =
          call.minutesLeft <= 0 ? tr_("notify.blockNow").arg(t.title) : tr_("notify.blockSoon").arg(t.title).arg(call.minutesLeft);
      notifyTaskAt(
          heap::notify::taskRef(profileId, t.id),
          title,
          tr_("notify.blockBody")
              .arg(heap::text::formatTime(t.scheduledAt.time(), twelveHourClock()), heap::text::formatTime(end.time(), twelveHourClock())),
          QStringLiteral("taskBlock"),
          now);
    };
    for(const Task& t : m_tasks.items()) {
      consider(m_activeProfileId, t, true);
    }
    for(const Profile& p : m_profiles) {
      if(p.id != m_activeProfileId) {
        for(const Task& t : p.tasks) {
          consider(p.id, t, false);
        }
      }
    }
  }

  // 3c. "Remind me" on a task (APP-319): only where one was set. Held by
  // quiet hours like the rest; one whose minute passed while the app was
  // closed still comes, once, and says so.
  if(!quiet) {
    const auto remind = [&](const QString& profileId, const Task& t, bool own) {
      if(t.archived || (!t.local.remindAt.isValid() && t.local.remindLead < 0)) {
        return;
      }
      const QString cat = own ? statusCategory(t.status) : heap::board::defaultCategoryFor(t.status);
      if(cat == QLatin1String("done")) {
        return;
      }
      const QDateTime fireAt = heap::cal::taskReminderFireAt(t.local.remindAt,
                                                             t.local.remindLead,
                                                             t.scheduledAt,
                                                             t.scheduledHasTime,
                                                             heap::local::effectiveDueAt(t),
                                                             heap::local::effectiveDueHasTime(t));
      const heap::cal::TaskRemindCall call = heap::cal::taskReminder(t.id, fireAt, now);
      if(!call.due || reminderSent(call.key)) {
        return;
      }
      markReminderSent(call.key, now);
      const QString at = heap::text::formatTime(fireAt.time(), twelveHourClock());
      notifyTaskAt(heap::notify::taskRef(profileId, t.id),
                   tr_("notify.remindTitle").arg(t.title),
                   call.late ? tr_("notify.remindLate").arg(at) : tr_("notify.remindBody").arg(at),
                   QStringLiteral("remind"),
                   now);
    };
    for(const Task& t : m_tasks.items()) {
      remind(m_activeProfileId, t, true);
    }
    for(const Profile& p : m_profiles) {
      if(p.id != m_activeProfileId) {
        for(const Task& t : p.tasks) {
          remind(p.id, t, false);
        }
      }
    }
  }

  // 4. Meeting reminders — one per occurrence.
  //
  // heap had a "minutes before a meeting" setting and exactly one meeting it
  // applied to: the standup, at a fixed time from settings. Every real event
  // in the calendar went unannounced, which made the setting read like a
  // promise the app did not keep. The occurrences are what is on the calendar
  // — a series is one stored row, and a deleted occurrence is not a meeting —
  // and tomorrow is included, for a 00:05 call and a 23:55 reminder.
  if(notif.value("meetingReminders", true).toBool()) {
    const int lead = qMax(0, notif.value("meetingLead", 5).toInt());
    const QVector<CalEvent> occurrences = heap::cal::expandedEvents(m_events.items(), today.addDays(-1), today.addDays(1));
    for(const heap::cal::DueReminder& due : heap::cal::dueMeetingReminders(occurrences, now, lead, sentReminderKeys())) {
      markReminderSent(due.key, now);
      // N/X-Ntf-OS (R3-025): "1:1 с Олегом через 5 мин" / "11:00–11:30 · Zoom".
      const QString name = due.title.isEmpty() ? tr_("event.newDefault") : due.title;
      const QString title =
          due.minutesLeft <= 0 ? tr_("notify.meetingNowTitled").arg(name) : tr_("notify.meetingSoonTitled").arg(name).arg(due.minutesLeft);
      QString body = QStringLiteral("%1–%2").arg(heap::text::formatTime(heap::cal::hourToTime(due.start), twelveHourClock()),
                                                 heap::text::formatTime(heap::cal::hourToTime(due.end), twelveHourClock()));
      if(!due.location.trimmed().isEmpty()) {
        body += QStringLiteral(" · ") + reminderPlace(due.location);
      }
      // The key ends in the occurrence's start: "Open" goes to that day.
      const QString routeId = heap::notify::routingId(QStringLiteral("meeting"), due.eventId);
      m_shownReminders[routeId].date = QDateTime::fromString(due.key.section(QChar('@'), -1), Qt::ISODate).date();
      emit notification(title, body, QStringLiteral("meeting"), routeId);
    }
  }

  // 5. Standup reminder, on working days only.
  if(notif.value("standupReminder", true).toBool() && isWorkDay(today)) {
    const QVariantMap cal = s.value("calendar").toMap();
    const QTime standup = heap::cal::clockTime(cal.value("standupTime", "10:00").toString());
    const int lead = qMax(0, notif.value("meetingLead", 5).toInt());
    // A standup on the calendar that day has its own reminder; a second
    // "Standup soon" was noise (IDIOT-CAL-9) — and at its own hour, when the
    // real one is at 02:00 for a team in New York, it was wrong (A6-19).
    bool onCalendar = false;
    if(standup.isValid()) {
      for(const CalEvent& e : heap::cal::expandedEvents(m_events.items(), today, today)) {
        if(e.type == QStringLiteral("standup") && !e.allDay && e.date == today) {
          onCalendar = true;
          break;
        }
      }
    }
    if(standup.isValid() && !onCalendar) {
      CalEvent st;
      st.id = QStringLiteral("standup");
      st.title = tr_("notify.standupTitle");
      st.type = QStringLiteral("standup");
      st.date = today;
      st.start = standup.hour() + (standup.minute() / 60.0);
      st.end = st.start + 0.25;
      for(const heap::cal::DueReminder& due : heap::cal::dueMeetingReminders({st}, now, lead, sentReminderKeys())) {
        markReminderSent(due.key, now);
        const QString routeId = heap::notify::routingId(QStringLiteral("standup"), QStringLiteral("standup"));
        m_shownReminders[routeId].date = today;
        emit notification(tr_("notify.standupTitle"),
                          due.minutesLeft <= 0 ? tr_("notify.meetingNow") : tr_("notify.standupBody").arg(due.minutesLeft),
                          QStringLiteral("standup"),
                          routeId);
      }
    }
  }

  // 5b. The chimes as a meeting comes closer (APP-178).
  meetingChimesAt(now);

  // 6. The safety net (APP-157…): each off unless switched on.
  checkEndOfDayAt(now);
  checkWaitingAt(now);

  // Anything else that arrived during quiet hours goes out now.
  if(!quiet) {
    flushHeldNotifications(now);
  }
}

bool AppController::isWorkDay(const QDate& day) const {
  // calendar.workDays: Qt weekday numbers (Mon=1 … Sun=7). Absent means the
  // usual Monday to Friday.
  const QVariantList days = settingsMap().value("calendar").toMap().value("workDays").toList();
  if(days.isEmpty()) {
    return day.dayOfWeek() <= 5;
  }
  for(const QVariant& v : days) {
    if(v.toInt() == day.dayOfWeek()) {
      return true;
    }
  }
  return false;
}

// ── Periodic tracker sync (APP-123) ──

QString AppController::autoSyncFilePath() const {
  return heap::paths::dataDir() + QStringLiteral("/autosync.json");
}

void AppController::loadLastTrackerSync() {
  QFile f(autoSyncFilePath());
  if(!f.open(QIODevice::ReadOnly)) {
    return;
  }
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  m_lastTrackerSync = QDateTime::fromString(o.value(QStringLiteral("lastSync")).toString(), Qt::ISODate);
}

void AppController::saveLastTrackerSync() const {
  QDir().mkpath(heap::paths::dataDir());
  QSaveFile f(autoSyncFilePath());
  if(f.open(QIODevice::WriteOnly)) {
    f.write(
        QJsonDocument(QJsonObject{{QStringLiteral("lastSync"), m_lastTrackerSync.toString(Qt::ISODate)}}).toJson(QJsonDocument::Compact));
    f.commit();
  }
}

void AppController::autoSyncTickAt(const QDateTime& now) {
  const int mins = qBound(0,
                          settingsMap().value(QStringLiteral("integrations")).toMap().value(QStringLiteral("autoSyncMinutes")).toInt(),
                          heap::integrations::kMaxAutoSyncMinutes);
  if(m_syncProviders.empty() || !heap::integrations::autoSyncDue(m_lastTrackerSync, now, mins)) {
    return;
  }
  syncNow(/*here=*/false);
  m_lastTrackerSync = now;  // the clock the check ran on, so a test's `now` holds
  saveLastTrackerSync();
}

// ── Live tracker updates (dogfood 2026-10-19, item 14) ──
//
// No webhooks for a desktop app (see integrations/LiveSync.h): a probe for
// the trackers that can answer "what changed?" (Jira), a catch-up pull for
// all of them on return, wake and network back. Reads only.

bool AppController::liveUpdatesEnabled() const {
  return settingsMap().value(QStringLiteral("integrations")).toMap().value(QStringLiteral("liveUpdates"), true).toBool();
}

void AppController::liveTickAt(const QDateTime& now) {
  namespace live = heap::integrations::live;
  const bool slept = live::sleptBetween(m_liveLastTick, now, live::kTickSecs);
  m_liveLastTick = now;
  if(!liveUpdatesEnabled() || m_syncProviders.empty()) {
    return;
  }
  if(slept) {
    liveCatchUpAt(now);
    return;
  }
  const bool saver = heap::platform::systemEnergySaverOn();
  // Ids first: a pull or a token refresh may rebuild m_syncProviders.
  QStringList probeable;
  for(const auto& provider : m_syncProviders) {
    if(provider->canProbeChanges()) {
      probeable.append(provider->id());
    }
  }
  for(const QString& id : std::as_const(probeable)) {
    live::ProbeState& s = m_liveProbe[id];
    switch(live::nextAction(s, m_livePresence, saver, m_syncInFlight.contains(id), now)) {
      case live::Action::None:
        break;
      case live::Action::Pull:
        livePull(id, now);
        break;
      case live::Action::Probe: {
        live::probeSent(s, now);
        const int minutes = live::probeWindowMinutes(s.lastPull, now);
        ensureFreshToken(
            id,
            [this, id, minutes]() {
              for(const auto& provider : m_syncProviders) {
                if(provider->id() == id) {
                  provider->probeChanges(minutes);
                  return;
                }
              }
              m_liveProbe[id].inFlight = false;
            },
            [this, id]() {
              onChangesProbed(id, false, 0, 0, QDateTime());
            });
        break;
      }
    }
  }
}

void AppController::liveCatchUpAt(const QDateTime& now) {
  namespace live = heap::integrations::live;
  if(!liveUpdatesEnabled() || m_syncProviders.empty()) {
    return;
  }
  if(!live::catchUpDue(QDateTime(), m_liveLastCatchUp, now)) {
    return;
  }
  m_liveLastCatchUp = now;
  QStringList ids;
  for(const auto& provider : m_syncProviders) {
    ids.append(provider->id());
  }
  for(const QString& id : std::as_const(ids)) {
    if(live::catchUpDue(m_liveProbe.value(id).lastPull, QDateTime(), now)) {
      livePull(id, now);
    }
  }
}

void AppController::livePull(const QString& providerId, const QDateTime& now, bool afterRunningPull) {
  heap::integrations::live::ProbeState& s = m_liveProbe[providerId];
  // A 429 or a run of failures asked for a pause, catch-up or not.
  if(!heap::integrations::live::mayPull(s, now)) {
    return;
  }
  if(m_syncInFlight.contains(providerId)) {
    // The pull under way may have left before the change the probe saw.
    if(afterRunningPull) {
      s.pullQueued = true;
    }
    return;
  }
  // The probe's cadence restarts from here; the pull's answer sets lastPull.
  s.lastProbe = now;
  // Quiet like the settle pull: a toast only when something changed, none
  // for a failure (the next pull by hand or by timer says it).
  m_settlePulls.insert(providerId);
  syncProviderNow(providerId);
}

void AppController::onChangesProbed(const QString& providerId, bool ok, int httpStatus, int retryAfter, const QDateTime& newest) {
  const QDateTime now = QDateTime::currentDateTime();
  if(heap::integrations::live::probeAnswered(m_liveProbe[providerId], ok, httpStatus, retryAfter, newest, now)) {
    livePull(providerId, now, /*afterRunningPull=*/true);
  }
}

// ── Reminders already sent ──
//
// Kept in reminders.json next to state.json rather than in memory: a restart
// inside a reminder's window used to announce it again, and the state file
// is the wrong place for bookkeeping that changes every minute.

QString AppController::remindersFilePath() const {
  return heap::paths::dataDir() + QStringLiteral("/reminders.json");
}

void AppController::loadSentReminders() {
  m_sentReminders.clear();
  QFile f(remindersFilePath());
  if(!f.open(QIODevice::ReadOnly)) {
    return;
  }
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  const QDateTime horizon = QDateTime::currentDateTime().addDays(-3);
  for(auto it = o.constBegin(); it != o.constEnd(); ++it) {
    const QDateTime at = QDateTime::fromString(it.value().toString(), Qt::ISODate);
    if(at.isValid() && at > horizon) {
      m_sentReminders.insert(it.key(), at);
    }
  }
}

void AppController::saveSentReminders() const {
  QJsonObject o;
  for(auto it = m_sentReminders.constBegin(); it != m_sentReminders.constEnd(); ++it) {
    o.insert(it.key(), it.value().toString(Qt::ISODate));
  }
  QDir().mkpath(heap::paths::dataDir());
  QSaveFile f(remindersFilePath());
  if(f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.commit();
  }
}

bool AppController::reminderSent(const QString& key) const {
  return m_sentReminders.contains(key);
}

QSet<QString> AppController::sentReminderKeys() const {
  QSet<QString> keys;
  keys.reserve(m_sentReminders.size());
  for(auto it = m_sentReminders.constBegin(); it != m_sentReminders.constEnd(); ++it) {
    keys.insert(it.key());
  }
  return keys;
}

void AppController::markReminderSent(const QString& key, const QDateTime& at) {
  m_sentReminders.insert(key, at);
  // Three days is longer than any reminder stays due.
  const QDateTime horizon = at.addDays(-3);
  for(auto it = m_sentReminders.begin(); it != m_sentReminders.end();) {
    it = it.value() < horizon ? m_sentReminders.erase(it) : std::next(it);
  }
  if(m_reminderBatchDepth > 0) {
    m_reminderSavePending = true;
    return;
  }
  saveSentReminders();
}

void AppController::holdNotification(const HeldNotification& n) {
  // A night of git branch switches is not worth fifty toasts in the morning:
  // the newest few are kept, and one of each is enough.
  for(const HeldNotification& h : m_heldNotifications) {
    if(h.title == n.title && h.body == n.body && h.kind == n.kind && h.taskId == n.taskId) {
      return;
    }
  }
  constexpr int kMaxHeld = 20;
  if(m_heldNotifications.size() >= kMaxHeld) {
    m_heldNotifications.removeFirst();
  }
  m_heldNotifications.append(n);
}

void AppController::flushHeldNotifications(const QDateTime& now) {
  const QVector<HeldNotification> held = std::exchange(m_heldNotifications, {});
  for(const HeldNotification& h : held) {
    if(h.taskId.isEmpty()) {
      emit notification(h.title, h.body, h.kind);
    } else {
      notifyTaskAt(h.taskId, h.title, h.body, h.kind, now);
    }
  }
}

// ---- Git watcher integration ----

QStringList AppController::collectPrefixes() const {
  return heap::git::branchPrefixes(taskIdPrefix(), m_tasks.items());
}

QString AppController::taskIdForBranchMatch(const QString& matchedId) const {
  return heap::git::taskIdForBranchMatch(matchedId, m_tasks.items());
}

void AppController::applyGitSettingsFromMap(const QVariantMap& g) {
  const bool showMove = g.value("showWhoseMove", true).toBool();
  if(showMove != m_showWhoseMove) {
    m_showWhoseMove = showMove;
    emit showWhoseMoveChanged();
  }
  if(!m_gitWatcher) {
    return;
  }
  QStringList repos;
  const QVariantList raw = g.value("watchedRepos").toList();
  for(const QVariant& v : raw) {
    const QString p = v.toString().trimmed();
    if(!p.isEmpty()) {
      repos << p;
    }
  }
  m_gitWatcher->setPrefixes(collectPrefixes());
  m_gitWatcher->setWatchedRepos(repos);
  m_gitWatcher->setPrFetchEnabled(g.value("watchPrState", true).toBool());
  const bool owners = g.value("codeOwners", true).toBool();
  m_gitWatcher->setCodeOwnersEnabled(owners);
  if(!owners && !m_codeOwners.isEmpty()) {
    m_codeOwners.clear();
    emit codeOwnersChanged();
  }
  dropCodeOwnersOutside(m_gitWatcher->watchedRepos());
  // A prefix edit lands here (idPrefix lives in app settings) but leaves HEAD
  // untouched, so re-match the branch we're already on for the banner.
  refreshFocusedTaskId();
}

void AppController::refreshFocusedTaskId() {
  if(m_focusedBranch.isEmpty() || m_focusedBranch == QStringLiteral("(detached HEAD)")) {
    return;
  }
  const heap::git::BranchTaskMatcher m(collectPrefixes());
  const auto mr = m.extract(m_focusedBranch);
  const QString newId = mr.matched && gitLinksBranches() ? taskIdForBranchMatch(mr.taskId) : QString();
  if(newId == m_focusedTaskId) {
    return;
  }
  m_focusedTaskId = newId;
  m_dismissedBranches.remove(m_focusedBranch);  // re-arm banner for a now-matching branch
  emit focusedGitChanged();
}

void AppController::onGitBranchChanged(const QString& repo, const QString& branch, const QString& matchedId) {
  // The watcher reports the key it found in the branch name. A tracker-mirrored
  // task is stored under its provider-prefixed id (jira-LUX-1 for LUX-1), so
  // resolve it the way the banner refresh and the git badges do (PLAT-14).
  // Settings → Git "link a branch to its task" off (DG-099): the branch is
  // shown, no task is picked from its name.
  const QString taskId = gitLinksBranches() ? taskIdForBranchMatch(matchedId) : QString();
  m_focusedRepo = repo;
  m_focusedBranch = branch;
  m_focusedTaskId = taskId;
  if(m_gitWatcher) {
    m_focusedRepoState = m_gitWatcher->snapshot().value(repo).toMap();
  }
  m_dismissedBranches.remove(branch);  // re-arm banner on every branch change
  emit focusedGitChanged();
  // The watcher reports each repo's branch once at launch: that is not a
  // switch the user just made, and no coach mark comes on startup.
  const bool launchReport = !m_gitReposReported.contains(repo);
  m_gitReposReported.insert(repo);

  if(taskId.isEmpty() || branch == QStringLiteral("(detached HEAD)")) {
    return;
  }
  const QVariantMap g = settingsMap().value("git").toMap();
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }

  // Checking out a branch means work is starting — never that finished work
  // is starting again (PLAT-6). The watcher reports the current branch on
  // every launch, so moving a Done or In-review card back to In Progress
  // undid the user's own move (and told the tracker) each time the app
  // opened. Only a card in a column before In Progress moves forward; an
  // archived, done or in-review one is left alone, with no focus block.
  const Task& task = m_tasks.items().at(row);
  const int at = statusIndexOf(task.status);
  const int prog = statusIndexOf(QStringLiteral("prog"));
  const QString cat = statusCategory(task.status);
  const bool finished =
      task.archived || cat == QLatin1String("done") || cat == QLatin1String("review") || (at >= 0 && at == m_statuses.size() - 1);
  if(finished) {
    return;
  }
  const bool move = g.value("autoMoveToInProgress", true).toBool() && prog >= 0 && at >= 0 && at < prog;
  // A switch made while heap runs: the first one says, once, why the task
  // was picked and where that is turned off (coach mark). Offered before the
  // move, which may want a coach mark of its own (trackerMove).
  if(!launchReport) {
    offerCoachMark(QStringLiteral("branchLink"),
                   {{QStringLiteral("taskId"), taskId},
                    {QStringLiteral("key"), task.externalId.isEmpty() ? matchedId : task.externalId},
                    {QStringLiteral("branch"), branch},
                    {QStringLiteral("moved"), move}});
  }
  if(move) {
    moveTask(taskId, QStringLiteral("prog"));
  }
  if(g.value("autoCreateFocusBlock", false).toBool()) {
    scheduleFocusBlockFor(taskId);
  }
  emit notification(QStringLiteral("Working on ") + taskId, QStringLiteral("Branch ") + branch, QStringLiteral("git"));
}

void AppController::onGitRepoState(const QString& repo, const QVariantMap& state) {
  if(state.value("remote").toBool()) {
    emit remoteReposChanged();
  }
  if(repo == m_focusedRepo) {
    m_focusedRepoState = state;
    emit focusedGitChanged();
  }
  const heap::git::BranchTaskMatcher m(collectPrefixes());
  const auto mr = m.extract(state.value("branch").toString());
  if(!mr.matched) {
    return;
  }
  const QVariantMap pr = state.value("pr").toMap();
  QVariantMap entry;
  entry["ahead"] = state.value("ahead");
  entry["behind"] = state.value("behind");
  entry["prState"] = pr.value("state");
  entry["prNumber"] = pr.value("number");
  entry["prUrl"] = pr.value("url");
  entry["prMove"] = pr.value("move");
  entry["prMoveReason"] = pr.value("moveReason");
  entry["prChecks"] = pr.value("checks");
  m_tasks.setGitInfoForId(taskIdForBranchMatch(mr.taskId), entry);
}

void AppController::dismissGitBanner() {
  if(m_focusedBranch.isEmpty()) {
    return;
  }
  m_dismissedBranches.insert(m_focusedBranch);
  emit focusedGitChanged();
}

void AppController::openFocusedTask() {
  if(!m_focusedTaskId.isEmpty()) {
    emit openTaskRequested(m_focusedTaskId, m_activeProfileId);
  }
}

void AppController::refreshGitForTaskBranch(const QString& taskId) {
  if(!m_gitWatcher) {
    return;
  }
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const QString br = m_tasks.items().at(row).branch;
  if(br.isEmpty()) {
    return;
  }
  const QStringList repos = m_gitWatcher->snapshot().keys();
  for(const QString& repo : repos) {
    m_gitWatcher->requestPrFetch(repo, br);
  }
}

void AppController::createBranchForTask(const QString& taskId) {
  if(!m_gitWatcher) {
    return;
  }
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const Task t = m_tasks.items().at(row);  // copy — mutated below on success

  // Prefer the currently focused repo; otherwise the first watched repo.
  QString repo = m_focusedRepo;
  const QStringList repos = m_gitWatcher->snapshot().keys();
  if(repo.isEmpty() && !repos.isEmpty()) {
    repo = repos.first();
  }
  if(repo.isEmpty()) {
    emit toast(tr_("git.noRepo"), QStringLiteral("warning"));
    return;
  }

  const QString templ = settingsMap().value("integrations").toMap().value("github").toMap().value("branchTemplate").toString();
  // Name the branch after the tracker's key, not the heap id. A branch called
  // `feature/jira-proj-123-…` matches nothing on the way back; `feature/proj-123`
  // resolves to this very task, which is the point of creating it from here.
  const QString key = externalKeyOf(t);
  static const QRegularExpression branchableKey(QStringLiteral("^[A-Za-z][A-Za-z0-9]*-\\d+$"));
  const QString branchId = branchableKey.match(key).hasMatch() ? key : t.id;
  const QString branch = heap::git::BranchTaskMatcher::branchNameForTask(branchId, t.title, templ);
  if(branch.isEmpty()) {
    emit toast(tr_("git.noBranchName").arg(t.id));
    return;
  }

  // The checkout runs asynchronously, so the task is only marked once git
  // says it worked. Recording it up front would leave a branch on the card
  // that does not exist if the checkout fails.
  const QString pendingTaskId = t.id;
  auto* connection = new QMetaObject::Connection;
  *connection = connect(m_gitWatcher.get(),
                        &heap::git::GitWatcher::branchCreated,
                        this,
                        [this, connection, pendingTaskId, branch](const QString&, const QString& created, bool ok, const QString& error) {
                          if(created != branch) {
                            return;  // a different request
                          }
                          disconnect(*connection);
                          delete connection;

                          if(!ok) {
                            emit toast(tr_("git.branchFailed").arg(error), QStringLiteral("error"));
                            return;
                          }
                          const int taskRow = m_tasks.indexOfId(pendingTaskId);
                          if(taskRow >= 0) {
                            Task updated = m_tasks.items().at(taskRow);
                            updated.branch = branch;
                            m_tasks.upsert(updated);
                            scheduleSave();
                          }
                          emit toast(tr_("git.branchCreated").arg(branch));
                        });

  QString err;
  if(!m_gitWatcher->createBranch(repo, branch, &err)) {
    // createBranch() emits branchCreated() itself on a refused request, so
    // the handler above has already reported it.
    return;
  }
}

void AppController::onGitCommits(const QString& repo, const QVariantMap& commitsByTask) {
  Q_UNUSED(repo);
  for(auto it = commitsByTask.constBegin(); it != commitsByTask.constEnd(); ++it) {
    // The newest commit naming a task is a sign of life (APP-157), and its
    // commits are part of the standup draft (APP-170).
    m_taskCommits.insert(taskIdForBranchMatch(it.key()), it.value().toList());
    for(const QVariant& c : it.value().toList()) {
      const QDateTime at = c.toMap().value(QStringLiteral("at")).toDateTime();
      if(!at.isValid()) {
        continue;
      }
      QDateTime& last = m_lastCommitAt[taskIdForBranchMatch(it.key())];
      if(!last.isValid() || at > last) {
        last = at;
      }
    }
    if(m_tasks.indexOfId(it.key()) < 0) {
      continue;
    }
    QVariantMap entry;
    entry["recentCommits"] = it.value();
    m_tasks.setGitInfoForId(it.key(), entry);
  }
}

// ── Native notifications with action buttons ─────────────────────

void AppController::notifyTask(const QString& taskId, const QString& title, const QString& body, const QString& kind) {
  notifyTaskAt(taskId, title, body, kind, QDateTime::currentDateTime());
}

void AppController::notifyTaskAt(
    const QString& taskId, const QString& title, const QString& body, const QString& kind, const QDateTime& now) {
  if(holdForImmersion({.title = title, .body = body, .kind = kind, .taskId = taskId})) {
    return;
  }
  if(inQuietHours(now)) {
    holdNotification({title, body, kind, taskId});
    return;
  }
  const QString inApp = title.isEmpty() ? body : title + QStringLiteral(" · ") + body;
  const QVariantMap notif = settingsMap().value("notifications").toMap();
  // The start of a task block (APP-256) carries its buttons in the window too:
  // open, in 15 minutes, the next free window.
  const auto inAppToast = [&]() {
    if(kind == QLatin1String("taskBlock") || kind == QLatin1String("remind")) {
      const QString id = heap::notify::routingId(kind, taskId);
      ShownReminder& shown = m_shownReminders[id];
      shown.title = title;
      shown.body = body;
      shown.kind = kind;
      emit reminderToast(id, inApp, kind);
    } else {
      emit toast(inApp);
    }
  };
  // Suppress the OS toast when notifications are disabled, unavailable, or the
  // window is currently focused. In the focused case the in-app Toast bar below
  // already shows the message — emitting both is the double-notification bug on
  // Windows (HEAP-47): one styled in-app toast and one plain system balloon.
  const bool appActive = QGuiApplication::applicationState() == Qt::ApplicationActive;
  if(!notif.value("desktopNotif", true).toBool() || !m_notifier || appActive) {
    // Fallback path — still surface via in-app toast for visibility.
    inAppToast();
    return;
  }

  heap::notify::Notification n;
  // The id encodes both kind and task id so the action handler can route
  // back without bookkeeping ("deadline:LTE-2398" → kind=deadline, task=LTE-2398).
  n.id = heap::notify::routingId(kind, taskId);
  n.title = title;
  n.body = body;
  n.iconPath = QStringLiteral(":/brand/tabless/tabless-icon.svg");
  n.category = kind;
  ShownReminder& shown = m_shownReminders[n.id];
  shown.title = title;
  shown.body = body;
  shown.kind = kind;

  if(m_notifier->supportsActions()) {
    n.actions = reminderActions(kind);
  }
  m_notifier->post(n);

  if(notif.value("soundOnPing", false).toBool()) {
    QApplication::beep();
  }
  inAppToast();
}

void AppController::notifyCapture(const QString& taskId, const QString& title, const QString& body) {
  const QVariantMap notif = settingsMap().value("notifications").toMap();
  if(!notif.value("desktopNotif", true).toBool() || !m_notifier) {
    emit toast(title + QStringLiteral(" — ") + QString(body).replace(QChar('\n'), QStringLiteral(" · ")));
    return;
  }
  heap::notify::Notification n;
  n.id = heap::notify::routingId(QStringLiteral("capture"),
                                 taskId.isEmpty() ? QStringLiteral("-") : heap::notify::taskRef(m_activeProfileId, taskId));
  n.title = title;
  n.body = body;
  n.iconPath = QStringLiteral(":/brand/tabless/tabless-icon.svg");
  n.category = QStringLiteral("capture");
  m_notifier->post(n);
}

void AppController::snoozeDeadline(const QString& taskId, int seconds) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  Task t = m_tasks.items().at(row);
  const QDateTime dueAt = heap::local::effectiveDueAt(t);
  if(!dueAt.isValid()) {
    return;
  }
  const UndoScope scope(this, tr_("undo.snooze").arg(taskId));
  // Reminders are date-grained — bump to the next day so the dl: sentinel
  // for "today" stops firing. The clock time rides along. On a tracker card
  // the snoozed date is mine; the tracker's stays (APP-238).
  const int days = (seconds + 86399) / 86400;
  heap::local::setMyDue(t, dueAt.addDays(days), heap::local::effectiveDueHasTime(t));
  if(t.scheduledAt.isValid()) {
    t.scheduledAt = t.scheduledAt.addDays(days);
  }
  m_tasks.upsert(t);
  // The reminder keys carry the deadline itself, so the new horizon is armed
  // on its own.
  emit toast(tr_("deadline.snoozed").arg(taskId));
  scheduleSave();
}

void AppController::onNotifierAction(const QString& notificationId, const QString& actionId) {
  const auto [kind, taskId] = heap::notify::parseRoutingId(notificationId);
  Q_UNUSED(kind);
  if(taskId.isEmpty()) {
    return;
  }
  // A snooze puts the reminder off and leaves the task as it is (APP-155):
  // the deadline is the user's, a button on a toast does not move it.
  const QVariantMap notif = settingsMap().value(QStringLiteral("notifications")).toMap();
  const int snoozeMinutes =
      heap::notify::snoozeMinutesFor(actionId,
                                     notif.value(QStringLiteral("snoozeShortMin"), heap::notify::kDefaultSnoozeShortMin).toInt(),
                                     notif.value(QStringLiteral("snoozeLongMin"), heap::notify::kDefaultSnoozeLongMin).toInt());
  if(snoozeMinutes > 0) {
    snoozeReminderAt(notificationId, snoozeMinutes, QDateTime::currentDateTime());
    return;
  }
  if(actionId == QLatin1String(heap::notify::kOpen)) {
    openReminder(notificationId);
    return;
  }
  if(actionId == QLatin1String(heap::notify::kJoin)) {
    const QUrl url = QUrl::fromUserInput(meetingJoinUrl(taskId));
    if(url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))) {
      QDesktopServices::openUrl(url);
    } else {
      openReminder(notificationId);
    }
    return;
  }
  // A task block's own buttons (APP-256).
  if(actionId == QLatin1String(heap::notify::kSnoozeBlock)) {
    snoozeReminderAt(notificationId, 15, QDateTime::currentDateTime());
    return;
  }
  if(actionId == QLatin1String(heap::notify::kNextWindow)) {
    const ReminderTask target = reminderTask(taskId);
    if(target.profileId == m_activeProfileId) {
      scheduleTaskAtNextFreeSlot(target.task.id, today());
    } else if(!target.profileId.isEmpty()) {
      emit openTaskRequested(target.task.id, target.profileId);
    }
    return;
  }
  if(actionId != QLatin1String(heap::notify::kDone)) {
    return;
  }
  // Done is done to the task the reminder was about, in its own profile, and
  // the window stays where it is: switching to that profile for good pulled
  // the workspace out from under an open editor, whose Save then wrote the
  // edited task into the other profile (PRES-1). The switch there and back is
  // what `heap done` does for a task of another profile.
  const ReminderTask target = reminderTask(taskId);
  if(target.profileId.isEmpty()) {
    return;
  }
  const QString current = m_activeProfileId;
  setActiveProfileId(target.profileId);
  moveTask(target.task.id, QStringLiteral("done"));
  setActiveProfileId(current);
}

void AppController::reminderAction(const QString& notificationId, const QString& actionId) {
  onNotifierAction(notificationId, actionId);
}

void AppController::onNotifierActivated(const QString& notificationId) {
  // The end-of-day notice is about several tasks at once (APP-157).
  const auto [kind, taskId] = heap::notify::parseRoutingId(notificationId);
  if(kind == QLatin1String("endOfDay")) {
    emit safetyOpenTasksRequested(taskId == QLatin1String("-") ? QStringList() : taskId.split(QLatin1Char(',')));
    return;
  }
  openReminder(notificationId);
}
