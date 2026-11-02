#include "integrations/IntegrationI18n.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QRegularExpression>

namespace heap::integrations {

namespace {

struct Entry {
  const char* en;
  const char* ru;
};

const QHash<QString, Entry>& table() {
  static const QHash<QString, Entry> t = {
      {QStringLiteral("sync.headline"), {"%1: %2", "%1: %2"}},
      {QStringLiteral("sync.outOfScope"),
       {"%1 outside the current filter (kept — archive them from Settings → Integrations)",
        "%1 вне текущего фильтра (оставлены — архивировать можно в Настройки → Интеграции)"}},
      {QStringLiteral("sync.queued"),
       {"%1: the tracker is out of reach — the move goes out after the next sync",
        "%1: трекер недоступен — перемещение уйдёт после следующей синхронизации"}},
      {QStringLiteral("sync.queuedDisconnected"),
       {"%1: %2 is not connected — the move goes out once you reconnect and sync",
        "%1: %2 не подключён — перемещение уйдёт после переподключения и синхронизации"}},
      {QStringLiteral("int.offline"),
       {"%1 is out of reach — still signed in, retrying", "%1 недоступен — вход сохранён, повторяю попытку"}},
      {QStringLiteral("int.backOnline"), {"%1 is reachable again", "%1 снова доступен"}},
      {QStringLiteral("int.transitionRefused"),
       {"%1: the %2 workflow has no step to “%3” from here — it can go to %4",
        "%1: в workflow %2 отсюда нет перехода в «%3» — доступно: %4"}},
      {QStringLiteral("int.transitionNone"),
       {"%1: the %2 workflow has no step to “%3” from here", "%1: в workflow %2 отсюда нет перехода в «%3»"}},
      {QStringLiteral("int.outOfScopeArchived"), {"Archived %1 card(s) outside the filter", "В архив: %1 карточек вне фильтра"}},
      // APP-243: writing to a tracker is opt-in, per tracker.
      {QStringLiteral("int.writeOffNotice"),
       {"The status in %1 no longer changes by itself. To turn it on: Settings → Integrations → %1",
        "Статус в %1 больше не меняется сам. Включить: Настройки → Интеграции → %1"}},
      // APP-204: a card outside the filter is read-only for the tracker.
      // APP-242: a merge / pull request card stays in the column of its stage.
      {QStringLiteral("int.reviewFixed"),
       {"%1 is a %2 merge request: its column follows its stage there. To move such cards yourself: Settings → Integrations → %2",
        "%1 — merge request в %2: колонка повторяет его стадию. Двигать такие карточки самому: Настройки → Интеграции → %2"}},
      {QStringLiteral("int.readOnlyOutOfScope"),
       {"%1 is no longer in your %2 filter — not changing its status in the tracker",
        "%1 больше не в вашем фильтре %2 — статус в трекере не меняю"}},
      {QStringLiteral("int.readOnlyGone"),
       {"%1 is no longer in %2 — not changing its status in the tracker", "%1 больше нет в %2 — статус в трекере не меняю"}},
      {QStringLiteral("int.heldOutOfScope"), {"outside your filter — the status was not sent", "вне вашего фильтра — статус не отправлен"}},
      {QStringLiteral("int.checkConflict"),
       {"%1: in %2 it is %3 now, here it is %4 — nothing sent, open the card to choose",
        "%1: в %2 сейчас %3, у вас: %4 — ничего не отправлено, откройте карточку и выберите"}},
      // Owner item 16: editing a ticket's content from Tabless.
      {QStringLiteral("int.edit.off"),
       {"Edits to %1 are off. To turn them on: Settings → Integrations → %1",
        "Правки в %1 выключены. Включить: Настройки → Интеграции → %1"}},
      {QStringLiteral("int.edit.disabled"),
       {"%1 is switched off — nothing is sent until you switch it on", "%1 отключён — ничего не отправляется, пока вы его не включите"}},
      // Owner item 7: a tracker switched off and on again.
      {QStringLiteral("int.disabledNow"),
       {"%1 switched off: nothing is asked or sent, the cards stay. Sign-in kept",
        "%1 отключён: ничего не запрашивается и не отправляется, карточки остались. Вход сохранён"}},
      {QStringLiteral("int.enabledNow"), {"%1 switched on again", "%1 снова включён"}},
      {QStringLiteral("int.disabledSync"),
       {"%1 is switched off — switch it on to sync", "%1 отключён — включите его, чтобы синхронизировать"}},
      {QStringLiteral("int.edit.unsupported"), {"%1 cannot take this edit from Tabless", "%1 не принимает эту правку из Tabless"}},
      {QStringLiteral("int.edit.readOnly"),
       {"%1 is no longer in your %2 filter — read only", "%1 больше не в вашем фильтре %2 — только чтение"}},
      {QStringLiteral("int.edit.busy"), {"%1: the previous edit is still on its way", "%1: предыдущая правка ещё отправляется"}},
      {QStringLiteral("int.edit.rate"),
       {"Too many edits to %1 in a minute — try again shortly", "Слишком много правок в %1 за минуту — повторите чуть позже"}},
      {QStringLiteral("int.edit.paused"),
       {"%1 keeps failing — edits wait until it answers again", "%1 не отвечает — правки подождут, пока он не ответит"}},
      {QStringLiteral("int.edit.conflict"),
       {"%1: changed in %2 meanwhile — nothing overwritten, choose which to keep",
        "%1: тем временем изменили в %2 — ничего не перезаписано, выберите, что оставить"}},
      {QStringLiteral("int.edit.failed"), {"%1: not saved in %2 — %3", "%1: не сохранено в %2 — %3"}},
      {QStringLiteral("int.edit.refused.roundTrip"),
       {"%1: “%3” would not look the same in %2 — change that part in %2 itself",
        "%1: «%3» в %2 будет выглядеть иначе — поправьте это место в самом %2"}},
      {QStringLiteral("int.edit.refused.lossy"),
       {"%1: “%3” would lose what %2 keeps in it (a mention, a file, a colour) — change that part in %2 itself",
        "%1: «%3» потеряет то, что в нём хранит %2 (упоминание, файл, цвет) — поправьте это место в самом %2"}},
      {QStringLiteral("int.edit.refused.image"),
       {"%1: a new picture can only be a web address (“%3”) — add files in %2",
        "%1: новая картинка может быть только ссылкой («%3») — файлы добавляйте в %2"}},
      {QStringLiteral("int.edit.refused.title"), {"%1: a title cannot be empty", "%1: заголовок не может быть пустым"}},
      {QStringLiteral("int.edit.refused.date"), {"%1: not a date — %3", "%1: это не дата — %3"}},
      {QStringLiteral("int.edit.refused.other"),
       {"%1: this edit cannot be written to %2 as it is (%3)", "%1: эту правку нельзя записать в %2 как есть (%3)"}},
      {QStringLiteral("task.conflictTookTracker"), {"%1: took the tracker's version", "%1: взята версия из трекера"}},
      {QStringLiteral("task.conflictKeptMine"), {"%1: kept your version", "%1: оставлена ваша версия"}},
      {QStringLiteral("update.couldNotCheck"), {"Couldn't check for updates — %1", "Не удалось проверить обновления — %1"}},
      {QStringLiteral("update.rateLimited"),
       {"GitHub's rate limit was hit, try again later", "исчерпан лимит запросов GitHub, попробуйте позже"}},
      {QStringLiteral("update.offline"), {"no connection", "нет соединения"}},
      {QStringLiteral("issue.logCopied"),
       {"Full diagnostics copied — paste them into the issue if they help",
        "Полная диагностика скопирована — вставьте её в issue, если пригодится"}},
  };
  return t;
}

// Whole sentences heap's providers write, English → Russian.
const QList<QPair<QString, QString>>& phrases() {
  static const QList<QPair<QString, QString>> p = {
      // What ReplyError adds when the tracker's answer says nothing useful
      // (design audit DES-14: a Russian UI showed "HTTP 401 — unauthorized —
      // check the token").
      {QStringLiteral("the repo or project does not exist, or this token has no access to it"),
       QStringLiteral("репозиторий или проект не существует, либо у токена нет к нему доступа")},
      {QStringLiteral("the token was rejected — sign in again or paste a new one"),
       QStringLiteral("токен отклонён — войдите снова или вставьте новый")},
      // The check before a status write (APP-204).
      {QStringLiteral("the tracker cannot be checked before a write"), QStringLiteral("трекер нельзя проверить перед записью")},
      {QStringLiteral("the tracker's answer did not describe the issue"), QStringLiteral("ответ трекера не описывает задачу")},
      {QStringLiteral("could not tell who is signed in"), QStringLiteral("не удалось понять, кто вошёл")},
      {QStringLiteral("the token lacks access to this, or a required scope"),
       QStringLiteral("у токена нет доступа к этому или нужного разрешения")},
      {QStringLiteral("forbidden — the token is missing a required scope"),
       QStringLiteral("доступ запрещён — у токена нет нужного разрешения")},
      {QStringLiteral("not found — check the URL, project or repo"),
       QStringLiteral("не найдено — проверьте адрес, проект или репозиторий")},
      {QStringLiteral("rate limited — try again in a few minutes"),
       QStringLiteral("превышен лимит запросов — повторите через несколько минут")},
      {QStringLiteral("bad request — check the query or filter"), QStringLiteral("некорректный запрос — проверьте запрос или фильтр")},
      {QStringLiteral("the network proxy rejected the request"), QStringLiteral("прокси-сервер отклонил запрос")},
      {QStringLiteral("unauthorized — check the token"), QStringLiteral("нет авторизации — проверьте токен")},
      {QStringLiteral("token refresh failed"), QStringLiteral("не удалось обновить токен")},
      {QStringLiteral("the browser session is no longer valid; sign in again"),
       QStringLiteral("сессия браузера больше не действительна — войдите снова")},
      {QStringLiteral("use an API token from id.atlassian.com, with the email of that same Atlassian account"),
       QStringLiteral("используйте API-токен с id.atlassian.com и email того же аккаунта Atlassian")},
      {QStringLiteral("Jira browser sign-in is incomplete — sign in again"),
       QStringLiteral("вход в Jira через браузер не завершён — войдите снова")},
      {QStringLiteral("Jira URL/email/token not configured"), QStringLiteral("не заданы адрес, email или токен Jira")},
      {QStringLiteral("Mattermost server URL or token is missing"), QStringLiteral("не заданы адрес сервера Mattermost или токен")},
      {QStringLiteral("Mattermost server URL must be https"), QStringLiteral("адрес сервера Mattermost должен начинаться с https")},
      {QStringLiteral("Mattermost is not configured"), QStringLiteral("Mattermost не настроен")},
      {QStringLiteral("the server accepted the sign-in but returned no session token"),
       QStringLiteral("сервер принял вход, но не вернул токен сессии")},
      {QStringLiteral("timed out waiting for browser sign-in"), QStringLiteral("не дождались входа в браузере")},
      {QStringLiteral("timed out waiting for device authorization"), QStringLiteral("не дождались подтверждения устройства")},
      {QStringLiteral("browser sign-in for this provider needs Qt 6.9 or newer — use an access token"),
       QStringLiteral("вход через браузер для этого трекера требует Qt 6.9 или новее — используйте токен доступа")},
      {QStringLiteral("authorization was denied, expired, or failed"), QStringLiteral("доступ отклонён, истёк или не выдан")},
      {QStringLiteral("you declined the authorization request"), QStringLiteral("вы отклонили запрос доступа")},
      {QStringLiteral("no access token in the response"), QStringLiteral("в ответе нет токена доступа")},
      {QStringLiteral("nothing to refresh with"), QStringLiteral("нечем обновить сессию")},
      {QStringLiteral("nothing to exchange"), QStringLiteral("нечего обменивать")},
      {QStringLiteral("the issue's repo is unknown — sync it again first"),
       QStringLiteral("репозиторий задачи неизвестен — сначала синхронизируйте её ещё раз")},
      {QStringLiteral("cut off before the tracker answered — check the ticket, then try again"),
       QStringLiteral("прервано до ответа трекера — проверьте задачу и повторите")},
      {QStringLiteral("not configured"), QStringLiteral("не настроено")},
      {QStringLiteral("unsupported"), QStringLiteral("не поддерживается")},
  };
  return p;
}

}  // namespace

QString integrationText(const QString& key, bool ru) {
  const auto it = table().constFind(key);
  if(it == table().constEnd()) {
    return {};
  }
  return QString::fromUtf8(ru ? it->ru : it->en);
}

QString translateProviderReason(const QString& reason, bool ru) {
  if(!ru || reason.isEmpty()) {
    return reason;
  }
  // Parameterised sentences first; each recurses on its own tail.
  static const QRegularExpression noTransition(
      QStringLiteral(R"(^this issue has no transition to a status mapped to '([^']*)' — map one in Settings → Integrations$)"));
  if(const auto m = noTransition.match(reason); m.hasMatch()) {
    return QStringLiteral("у задачи нет перехода в статус, сопоставленный с «%1», — сопоставьте его в Настройки → Интеграции")
        .arg(m.captured(1));
  }
  static const QRegularExpression pagesOnly(QStringLiteral(R"(^(\w+): only (\d+) page\(s\) — (.*)$)"),
                                            QRegularExpression::DotMatchesEverythingOption);
  if(const auto m = pagesOnly.match(reason); m.hasMatch()) {
    return QStringLiteral("%1: получено страниц — %2; %3").arg(m.captured(1), m.captured(2), translateProviderReason(m.captured(3), ru));
  }
  static const QRegularExpression pageLimit(QStringLiteral(R"(^stopped at the (\d+)-page limit$)"));
  if(const auto m = pageLimit.match(reason); m.hasMatch()) {
    return QStringLiteral("остановлено на пределе в %1 стр.").arg(m.captured(1));
  }
  static const QRegularExpression portBusy(QStringLiteral(R"(^redirect port (\d+) is busy — close the other app and retry$)"));
  if(const auto m = portBusy.match(reason); m.hasMatch()) {
    return QStringLiteral("порт %1 для возврата из браузера занят — закройте другое приложение и повторите").arg(m.captured(1));
  }
  // Whole known sentences, possibly behind an "HTTP 401 — " prefix or joined
  // with " · ". Exact phrases only, longest first by construction of the list.
  QString out = reason;
  bool any = false;
  for(const auto& [en, rus] : phrases()) {
    const qsizetype at = out.indexOf(en);
    if(at < 0) {
      continue;
    }
    // A short phrase ("not configured") must be the whole reason or a whole
    // clause, never a fragment of the tracker's own sentence.
    const qsizetype end = at + en.size();
    // ": " too: ReplyError joins its explanation to the tracker's bare
    // reason that way ("Not Found: the repo or project does not exist…").
    const bool startsClause = at == 0 || out.mid(0, at).endsWith(QStringLiteral("— ")) || out.mid(0, at).endsWith(QStringLiteral("· ")) ||
                              out.mid(0, at).endsWith(QStringLiteral(": "));
    const bool endsClause = end == out.size() || out.mid(end).startsWith(QStringLiteral(" ·"));
    if(!startsClause || !endsClause) {
      continue;
    }
    out.replace(at, en.size(), rus);
    any = true;
  }
  return any ? out : reason;
}

}  // namespace heap::integrations
