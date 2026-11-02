// Editing what came from the tracker (owner item 16, 2026-10-23): a ticket's
// description, title, due date, assignee and Smart Checklist, so the user
// need not open Jira at all.
//
// The long-standing rules, all of them here:
// - Off by default, per tracker (integrations.<id>.writeContent): with the
//   switch off nothing is sent, not even the read before a write.
// - Every write is preceded by a fresh read of the issue, and goes only when
//   the tracker still holds what the card last saw. Changed there meanwhile
//   is a conflict: nothing is overwritten, the user sees both versions.
// - The card shows the new value at once and goes back, with a message,
//   when the tracker does not take it.
// - The one retry rule and the breaker (net/FeatureGate.h, feature
//   "edit:<id>"), a cap of edits a minute, and the same failing write never
//   sent again before its back-off (FetchBackoff as a negative cache).
// - Comments are never sent (APP-241); nothing here writes one.

#include "AppController.h"

#include "integrations/IntegrationProvider.h"
#include "integrations/JiraContentWrite.h"
#include "integrations/JiraProvider.h"
#include "integrations/SmartChecklist.h"
#include "integrations/SmartChecklistEdit.h"
#include "markdown/JiraWiki.h"
#include "net/FeatureGate.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace {

using heap::integrations::ContentEdit;
using heap::integrations::ContentEditResult;
using heap::integrations::JiraDeployment;

// An edit or search whose provider went away before it answered.
QString interruptedReason() {
  return QStringLiteral("cut off before the tracker answered — check the ticket, then try again");
}

// The tracker's name for a field the UI edits.
QString providerFieldOf(const QString& field) {
  if(field == QLatin1String("title")) {
    return QStringLiteral("summary");
  }
  if(field == QLatin1String("due")) {
    return QStringLiteral("duedate");
  }
  return field;
}

QString uiFieldOf(const QString& providerField) {
  if(providerField == QLatin1String("summary")) {
    return QStringLiteral("title");
  }
  if(providerField == QLatin1String("duedate")) {
    return QStringLiteral("due");
  }
  return providerField;
}

QString dueText(const QDateTime& due) {
  return due.isValid() ? due.date().toString(Qt::ISODate) : QString();
}

QJsonArray checklistOf(const Task& t) {
  return t.externalMeta.details.value(QStringLiteral("smartChecklist")).toArray();
}

void setChecklist(Task& t, const QJsonArray& items) {
  if(items.isEmpty()) {
    t.externalMeta.details.remove(QStringLiteral("smartChecklist"));
  } else {
    t.externalMeta.details.insert(QStringLiteral("smartChecklist"), items);
  }
}

// Put `field` of `to` back to `from`'s, but only while it still shows what
// the edit showed: a later change by the user stays.
void restoreField(Task& to, const Task& shown, const Task& from, const QString& field) {
  if(field == QLatin1String("title")) {
    if(to.title == shown.title) {
      to.title = from.title;
    }
  } else if(field == QLatin1String("description")) {
    if(to.desc == shown.desc) {
      to.desc = from.desc;
    }
  } else if(field == QLatin1String("due")) {
    if(to.dueAt == shown.dueAt && to.dueHasTime == shown.dueHasTime) {
      to.dueAt = from.dueAt;
      to.dueHasTime = from.dueHasTime;
    }
  } else if(field == QLatin1String("assignee")) {
    if(to.assignee == shown.assignee) {
      to.assignee = from.assignee;
    }
  } else if(field == QLatin1String("checklist")) {
    if(checklistOf(to) == checklistOf(shown)) {
      setChecklist(to, checklistOf(from));
    }
  }
}

}  // namespace

bool AppController::trackerCanEditContent(const QString& providerId) const {
  // Jira only for now: the one tracker with a write for each of these
  // (JiraProvider::editContent). Another provider answers "unsupported".
  return providerId == QLatin1String("jira");
}

bool AppController::trackerContentWriteEnabled(const QString& providerId) const {
  // Off unless the user turned it on for this tracker: an install that has
  // no such key reads as off.
  return trackerCanEditContent(providerId) && settingsMap()
                                                  .value(QStringLiteral("integrations"))
                                                  .toMap()
                                                  .value(providerId)
                                                  .toMap()
                                                  .value(QStringLiteral("writeContent"))
                                                  .toBool();
}

void AppController::setTrackerContentWriteEnabled(const QString& providerId, bool enabled) {
  if(!trackerCanEditContent(providerId) || trackerContentWriteEnabled(providerId) == enabled) {
    return;
  }
  setIntegrationField(providerId, QStringLiteral("writeContent"), enabled);
  if(!enabled) {
    // Off is off; on again starts clean.
    heap::net::FeatureGate::instance().forget(heap::net::features::edit(providerId));
  }
}

int AppController::jiraDeploymentOf(const Task& t) const {
  // The card's own address says which Jira it came from (only Atlassian
  // runs *.atlassian.net) — the settings may point at another one since;
  // else the connected provider, else the settings' address.
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
  return static_cast<int>(deployment);
}

QString AppController::trackerEditBlock(const Task& t) const {
  if(t.externalId.isEmpty() || t.externalProvider.isEmpty() || isReviewItem(t) || !trackerCanEditContent(t.externalProvider)) {
    return QStringLiteral("unsupported");
  }
  if(!trackerContentWriteEnabled(t.externalProvider)) {
    return QStringLiteral("off");
  }
  // The whole tracker switched off (owner item 7): its cards are read only.
  if(!trackerEnabled(t.externalProvider)) {
    return QStringLiteral("disabled");
  }
  // Outside the filter or gone: read only for the tracker (APP-204), and an
  // issue whose own project is unknown has nowhere to be written.
  if(t.externalMeta.outOfScope || t.externalMeta.goneUpstream || (t.externalMeta.crossProject && t.externalMeta.project.isEmpty())) {
    return QStringLiteral("readOnly");
  }
  return {};
}

bool AppController::canEditTrackerContent(const QString& taskId) const {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return false;
  }
  const Task& t = m_tasks.items().at(row);
  if(!trackerEditBlock(t).isEmpty()) {
    return false;
  }
  return std::any_of(m_syncProviders.cbegin(), m_syncProviders.cend(), [&t](const auto& provider) {
    return provider->id() == t.externalProvider && provider->canEditContent();
  });
}

QString AppController::trackerDescriptionSource(const QString& taskId) const {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return {};
  }
  const Task& t = m_tasks.items().at(row);
  // As trackerTextAsMarkdown reads it, without the local copies of pictures:
  // what is edited goes back to the tracker.
  if(t.externalProvider != QLatin1String("jira") || !trackerWikiToMarkdown(t.externalProvider)) {
    return t.desc;
  }
  const auto deployment = static_cast<JiraDeployment>(jiraDeploymentOf(t));
  if(deployment == JiraDeployment::Cloud) {
    return t.desc;
  }
  if(deployment == JiraDeployment::Server || heap::md::looksLikeJiraWiki(t.desc)) {
    return heap::md::jiraWikiToMarkdown(t.desc);
  }
  return t.desc;
}

void AppController::finishTrackerEdit(
    const QString& taskId, const QString& field, const QString& outcome, const QString& message, const QVariantMap& detail) {
  if(!message.isEmpty()) {
    // Visible whatever the UI does with the signal: a refused or failed
    // edit is never silent.
    const bool bad = outcome == QLatin1String("error") || outcome == QLatin1String("conflict") || outcome == QLatin1String("refused");
    if(bad) {
      toastAndLog(message, outcome == QLatin1String("error") ? QStringLiteral("error") : QStringLiteral("warning"), {taskId});
    } else {
      emit toast(message, QStringLiteral("warning"));
    }
  }
  emit trackerEditFinished(taskId, field, outcome, message, detail);
}

void AppController::editTrackerDescription(const QString& taskId, const QString& text) {
  const QString field = QStringLiteral("description");
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  if(text == trackerDescriptionSource(taskId)) {
    finishTrackerEdit(taskId, field, QStringLiteral("unchanged"), QString());
    return;
  }
  PendingTrackerEdit p;
  p.taskId = taskId;
  p.field = field;
  p.edit.field = QStringLiteral("description");
  p.edit.value = text;
  p.edit.base = t.externalMeta.body;
  p.before = t;
  p.shown = t;
  p.mine = text;
  const auto deployment = static_cast<JiraDeployment>(jiraDeploymentOf(t));
  const bool wiki =
      deployment == JiraDeployment::Server || (deployment == JiraDeployment::Unknown && heap::md::looksLikeJiraWiki(t.externalMeta.body));
  // Shown as the tracker's own markup: then the edit is that markup.
  p.edit.raw = deployment != JiraDeployment::Cloud && !trackerWikiToMarkdown(t.externalProvider);
  if(p.edit.raw || !wiki) {
    p.shown.desc = text;
  } else {
    // The card keeps wiki markup on Server/DC; what it shows meanwhile is
    // the write as it will most likely go.
    const heap::integrations::JiraTextWrite w = heap::integrations::jiraDescriptionForServer(text, t.externalMeta.body);
    if(w.ok) {
      p.shown.desc = w.stored;
    }
  }
  sendTrackerEdit(p);
}

void AppController::editTrackerField(const QString& taskId, const QString& field, const QVariant& value) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  PendingTrackerEdit p;
  p.taskId = taskId;
  p.field = field;
  p.edit.field = providerFieldOf(field);
  p.before = t;
  p.shown = t;
  if(field == QLatin1String("title")) {
    const QString title = value.toString().trimmed();
    p.edit.base = t.externalMeta.title.isEmpty() ? t.title : t.externalMeta.title;
    if(title == t.title && title == p.edit.base) {
      finishTrackerEdit(taskId, field, QStringLiteral("unchanged"), QString());
      return;
    }
    p.edit.value = title;
    p.shown.title = title.isEmpty() ? t.title : title;
    p.mine = title;
  } else if(field == QLatin1String("due")) {
    const QString due = value.toString().trimmed();
    const QDate date = QDate::fromString(due, Qt::ISODate);
    if(!due.isEmpty() && !date.isValid()) {
      finishTrackerEdit(taskId,
                        field,
                        QStringLiteral("refused"),
                        tr_("int.edit.refused.date").arg(externalKeyOf(t), providerDisplayName(t.externalProvider), due),
                        {{QStringLiteral("reason"), QStringLiteral("date")}, {QStringLiteral("detail"), due}});
      return;
    }
    p.edit.base = dueText(t.externalMeta.dueAt);
    if(due == p.edit.base && dueText(t.dueAt) == due) {
      finishTrackerEdit(taskId, field, QStringLiteral("unchanged"), QString());
      return;
    }
    p.edit.value = due;
    p.shown.dueAt = date.isValid() ? QDateTime(date, QTime(0, 0)) : QDateTime();
    p.shown.dueHasTime = false;
    p.mine = due;
  } else if(field == QLatin1String("assignee")) {
    const QVariantMap who = value.toMap();
    const QString id = who.value(QStringLiteral("id")).toString().trimmed();
    const QString name = id.isEmpty() ? QString() : who.value(QStringLiteral("name"), id).toString();
    p.edit.base = t.assignee;
    if(id.isEmpty() && t.assignee.isEmpty()) {
      finishTrackerEdit(taskId, field, QStringLiteral("unchanged"), QString());
      return;
    }
    p.edit.value = id;
    p.shown.assignee = name;
    p.mine = name;
  } else {
    finishTrackerEdit(taskId, field, QStringLiteral("unsupported"), QString());
    return;
  }
  sendTrackerEdit(p);
}

namespace {

// Why a checklist item cannot take an edit, before anything is asked.
QString checklistItemProblem(const QJsonArray& items, int index) {
  if(index < 0 || index >= items.size()) {
    return QStringLiteral("index");
  }
  if(items.at(index).toObject().value(QStringLiteral("state")).toString() == QLatin1String("header")) {
    return QStringLiteral("header");
  }
  return {};
}

}  // namespace

void AppController::setTrackerChecklistItemStatus(const QString& taskId, int index, const QString& statusName) {
  namespace sc = heap::integrations::smartchecklist;
  const QString field = QStringLiteral("checklist");
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  QJsonArray items = sc::withStatuses(checklistOf(t));
  const QString wanted = statusName.simplified();
  QString problem = checklistItemProblem(items, index);
  // The status as the picker names it: a built-in by its own name, a known
  // custom one with its category; an unknown name goes by its words.
  QString name = wanted;
  QString category;
  if(problem.isEmpty()) {
    for(const QVariant& v : smartChecklistStatuses(taskId)) {
      const QVariantMap s = v.toMap();
      const QString known = s.value(QStringLiteral("name")).toString();
      if(known.compare(wanted, Qt::CaseInsensitive) == 0 ||
         (s.value(QStringLiteral("builtin")).toBool() && sc::builtinMarker(known) == sc::builtinMarker(wanted) &&
          !sc::builtinMarker(wanted).isNull())) {
        name = known;
        category = s.value(QStringLiteral("category")).toString();
        break;
      }
    }
    if(category.isEmpty()) {
      category = sc::categoryOfName(wanted);
      if(category.isEmpty()) {
        category = sc::categoryOpen();
      }
    }
    if(wanted.size() < 2 || wanted.size() > 50 || wanted.contains(QLatin1Char('[')) || wanted.contains(QLatin1Char(']'))) {
      problem = QStringLiteral("state");
    }
  }
  if(!problem.isEmpty()) {
    finishTrackerEdit(taskId, field, QStringLiteral("refused"), QString(), {{QStringLiteral("reason"), problem}});
    return;
  }
  QJsonObject item = items.at(index).toObject();
  if(item.value(QStringLiteral("statusName")).toString() == name) {
    finishTrackerEdit(taskId, field, QStringLiteral("unchanged"), QString());
    return;
  }
  PendingTrackerEdit p;
  p.taskId = taskId;
  p.field = field;
  p.edit.field = field;
  p.edit.checklistOp = QStringLiteral("state");
  p.edit.index = index;
  p.edit.value = name;
  p.edit.checklistCategory = sc::builtinMarker(name).isNull() ? category : QString();
  p.edit.baseChecklist = checklistOf(t);
  p.before = t;
  p.shown = t;
  item.insert(QStringLiteral("statusName"), name);
  item.insert(QStringLiteral("category"), category);
  item.insert(QStringLiteral("state"), sc::stateOfCategory(category));
  items.replace(index, item);
  setChecklist(p.shown, items);
  p.mine = name;
  sendTrackerEdit(p);
}

void AppController::setTrackerChecklistItemText(const QString& taskId, int index, const QString& text) {
  const QString field = QStringLiteral("checklist");
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  QJsonArray items = checklistOf(t);
  const QString words = text.trimmed();
  QString problem = checklistItemProblem(items, index);
  if(problem.isEmpty() && (words.isEmpty() || words.contains(QLatin1Char('\n')))) {
    problem = QStringLiteral("text");
  }
  if(!problem.isEmpty()) {
    finishTrackerEdit(taskId, field, QStringLiteral("refused"), QString(), {{QStringLiteral("reason"), problem}});
    return;
  }
  QJsonObject item = items.at(index).toObject();
  if(item.value(QStringLiteral("text")).toString() == words) {
    finishTrackerEdit(taskId, field, QStringLiteral("unchanged"), QString());
    return;
  }
  PendingTrackerEdit p;
  p.taskId = taskId;
  p.field = field;
  p.edit.field = field;
  p.edit.checklistOp = QStringLiteral("text");
  p.edit.index = index;
  p.edit.value = words;
  p.edit.baseChecklist = items;
  p.before = t;
  p.shown = t;
  item.insert(QStringLiteral("text"), words);
  items.replace(index, item);
  setChecklist(p.shown, items);
  p.mine = words;
  sendTrackerEdit(p);
}

void AppController::addTrackerChecklistItem(const QString& taskId, const QString& text) {
  const QString field = QStringLiteral("checklist");
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  const QString words = text.trimmed();
  if(words.isEmpty() || words.contains(QLatin1Char('\n'))) {
    finishTrackerEdit(taskId, field, QStringLiteral("refused"), QString(), {{QStringLiteral("reason"), QStringLiteral("text")}});
    return;
  }
  QJsonArray items = checklistOf(t);
  PendingTrackerEdit p;
  p.taskId = taskId;
  p.field = field;
  p.edit.field = field;
  p.edit.checklistOp = QStringLiteral("add");
  p.edit.value = words;
  p.edit.baseChecklist = items;
  p.before = t;
  p.shown = t;
  items.append(QJsonObject{{QStringLiteral("text"), words},
                           {QStringLiteral("state"), QStringLiteral("todo")},
                           {QStringLiteral("statusName"), QStringLiteral("To Do")},
                           {QStringLiteral("category"), heap::integrations::smartchecklist::categoryOpen()}});
  setChecklist(p.shown, items);
  p.mine = words;
  sendTrackerEdit(p);
}

void AppController::sendTrackerEdit(PendingTrackerEdit p) {
  const int row = m_tasks.indexOfId(p.taskId);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  const QString providerId = t.externalProvider;
  const QString tracker = providerDisplayName(providerId);
  const QString keyName = externalKeyOf(t);
  const QString block = trackerEditBlock(t);
  if(!block.isEmpty()) {
    QString message;
    if(block == QLatin1String("off")) {
      message = tr_("int.edit.off").arg(tracker);
    } else if(block == QLatin1String("disabled")) {
      message = tr_("int.edit.disabled").arg(tracker);
    } else if(block == QLatin1String("readOnly")) {
      message = tr_("int.edit.readOnly").arg(keyName, tracker);
    } else {
      message = tr_("int.edit.unsupported").arg(tracker.isEmpty() ? keyName : tracker);
    }
    finishTrackerEdit(p.taskId, p.field, block, message);
    return;
  }
  p.providerId = providerId;
  p.edit.externalId = t.externalId;
  p.edit.project = t.externalMeta.project;
  const QString key = pushKey(providerId, p.edit.project, p.edit.externalId) + QChar('\n') + p.edit.field;
  if(m_trackerEdits.contains(key)) {
    finishTrackerEdit(p.taskId, p.field, QStringLiteral("busy"), tr_("int.edit.busy").arg(keyName));
    return;
  }
  const bool connected = std::any_of(m_syncProviders.cbegin(), m_syncProviders.cend(), [&providerId](const auto& provider) {
    return provider->id() == providerId && provider->canEditContent();
  });
  if(!connected) {
    finishTrackerEdit(p.taskId,
                      p.field,
                      QStringLiteral("error"),
                      tr_("int.edit.failed").arg(keyName, tracker, providerReason(QStringLiteral("not configured"))),
                      {{QStringLiteral("httpStatus"), 0}});
    return;
  }
  // The breaker: a tracker whose sync or edits keep failing is not written
  // to, and after a failure the next write waits out the retry rule.
  heap::net::FeatureGate& gate = heap::net::FeatureGate::instance();
  const QString feature = heap::net::features::edit(providerId);
  const heap::net::Decision decision = gate.peek(feature, /*backoff=*/true);
  if(gate.isPaused(heap::net::features::sync(providerId)) || decision == heap::net::Decision::Paused ||
     decision == heap::net::Decision::Wait) {
    finishTrackerEdit(p.taskId, p.field, QStringLiteral("paused"), tr_("int.edit.paused").arg(tracker));
    return;
  }
  // The cap: a burst of edits (a script, a key held down) is not a burst of
  // requests.
  const QDateTime now = fetchNow();
  QVector<QDateTime>& times = m_trackerEditTimes[providerId];
  times.erase(std::remove_if(times.begin(),
                             times.end(),
                             [&now](const QDateTime& at) {
                               return at.secsTo(now) >= 60;
                             }),
              times.end());
  if(times.size() >= kTrackerEditsPerMinute) {
    finishTrackerEdit(p.taskId, p.field, QStringLiteral("busy"), tr_("int.edit.rate").arg(tracker));
    return;
  }
  // The negative cache: this very write failed or was refused a moment ago.
  p.noRepeat = key + QChar('\n') + p.edit.checklistOp + QChar('\n') + QString::number(p.edit.index) + QChar('\n') + p.edit.value;
  if(!m_trackerEditBackoff.due(p.noRepeat, now, 0)) {
    const QString why = m_trackerEditBackoff.error(p.noRepeat);
    finishTrackerEdit(p.taskId,
                      p.field,
                      QStringLiteral("error"),
                      tr_("int.edit.failed").arg(keyName, tracker, providerReason(why.isEmpty() ? QStringLiteral("failed") : why)),
                      {{QStringLiteral("httpStatus"), 0}, {QStringLiteral("repeat"), true}});
    return;
  }
  if(!gate.allow(feature, /*backoff=*/true)) {
    finishTrackerEdit(p.taskId, p.field, QStringLiteral("paused"), tr_("int.edit.paused").arg(tracker));
    return;
  }
  times.append(now);
  m_trackerEditBackoff.asked(p.noRepeat, now);
  // The card shows it at once; the answer settles it.
  if(!(p.shown == t)) {
    m_tasks.upsert(p.shown);
    scheduleSave();
  }
  m_trackerEdits.insert(key, p);
  const auto failNow = [this, providerId, key](const QString& error) {
    const auto it = m_trackerEdits.constFind(key);
    if(it == m_trackerEdits.constEnd()) {
      return;
    }
    ContentEditResult r;
    r.externalId = it->edit.externalId;
    r.project = it->edit.project;
    r.field = it->edit.field;
    r.outcome = QStringLiteral("error");
    r.error = error;
    onContentEdited(providerId, r);
  };
  ensureFreshToken(
      providerId,
      [this, providerId, key, failNow]() {
        const auto it = m_trackerEdits.constFind(key);
        if(it == m_trackerEdits.constEnd()) {
          return;
        }
        const ContentEdit edit = it->edit;
        for(const auto& provider : m_syncProviders) {
          if(provider->id() == providerId) {
            provider->editContent(edit);
            return;
          }
        }
        // Rebuilt away while the token renewed (signed out meanwhile).
        failNow(QStringLiteral("not configured"));
      },
      [failNow]() {
        failNow(QStringLiteral("HTTP 401 — the browser session is no longer valid; sign in again"));
      });
}

void AppController::onContentEdited(const QString& providerId, const ContentEditResult& r) {
  const QString key = pushKey(providerId, r.project, r.externalId) + QChar('\n') + r.field;
  const auto found = m_trackerEdits.find(key);
  if(found == m_trackerEdits.end()) {
    return;
  }
  PendingTrackerEdit p = *found;
  m_trackerEdits.erase(found);
  const bool wrote = r.outcome == QLatin1String("ok");
  const bool answered = wrote || r.outcome == QLatin1String("conflict") || r.outcome == QLatin1String("refused");
  // Cut off on our side (abandonTrackerEdits): says nothing about the
  // tracker's health, and the same edit may go again at once.
  const bool interrupted = r.outcome == QLatin1String("error") && r.error == interruptedReason();
  // The tracker's health: a conflict or a refusal is a read that went fine.
  if(r.outcome != QLatin1String("unsupported") && !interrupted) {
    heap::net::FeatureGate::instance().report(
        heap::net::features::edit(providerId), answered ? 200 : r.httpStatus, featureHostOf(providerId), r.retryAfterSecs);
  }
  if(interrupted) {
    m_trackerEditBackoff.forget(p.noRepeat);
  } else if(wrote || r.outcome == QLatin1String("conflict")) {
    m_trackerEditBackoff.succeeded(p.noRepeat, fetchNow());
  } else {
    m_trackerEditBackoff.failed(p.noRepeat, r.error.isEmpty() ? r.outcome : r.error);
  }
  const int row = m_tasks.indexOfId(p.taskId);
  if(row < 0) {
    return;
  }
  Task cur = m_tasks.items().at(row);
  const QString tracker = providerDisplayName(providerId);
  const QString keyName = externalKeyOf(cur);
  const QString field = p.field.isEmpty() ? uiFieldOf(r.field) : p.field;

  if(wrote) {
    // The tracker's value is the base the next pull compares against, so
    // the edit does not read as "changed here" or "changed there".
    QVariantMap detail;
    if(field == QLatin1String("title")) {
      if(cur.title == p.shown.title) {
        cur.title = r.remote;
      }
      cur.externalMeta.title = r.remote;
      detail.insert(QStringLiteral("value"), r.remote);
    } else if(field == QLatin1String("description")) {
      if(cur.desc == p.shown.desc) {
        cur.desc = r.remote;
      }
      cur.externalMeta.body = r.remote;
      detail.insert(QStringLiteral("value"), r.remote);
    } else if(field == QLatin1String("due")) {
      const QDate date = QDate::fromString(r.remote, Qt::ISODate);
      const QDateTime due = date.isValid() ? QDateTime(date, QTime(0, 0)) : QDateTime();
      if(cur.dueAt == p.shown.dueAt) {
        cur.dueAt = due;
        cur.dueHasTime = false;
      }
      cur.externalMeta.dueAt = due;
      detail.insert(QStringLiteral("value"), r.remote);
    } else if(field == QLatin1String("assignee")) {
      detail.insert(QStringLiteral("value"), p.shown.assignee);
    } else if(field == QLatin1String("checklist")) {
      setChecklist(cur, r.remoteChecklist);
      detail.insert(QStringLiteral("items"), r.remoteChecklist.toVariantList());
    }
    m_tasks.upsert(cur);
    scheduleSave();
    finishTrackerEdit(p.taskId, field, QStringLiteral("ok"), QString(), detail);
    return;
  }

  // Not taken: the card goes back to what it showed before.
  restoreField(cur, p.shown, p.before, field);
  QVariantMap detail;
  QString message;
  QString outcome = r.outcome;
  if(r.outcome == QLatin1String("conflict")) {
    message = tr_("int.edit.conflict").arg(keyName, tracker);
    if(field == QLatin1String("checklist")) {
      // The list is the tracker's own, replaced on every pull: the card
      // shows it as it is now, and the user does the edit again on it.
      detail.insert(QStringLiteral("mine"), checklistOf(p.shown).toVariantList());
      detail.insert(QStringLiteral("theirs"), r.remoteChecklist.toVariantList());
      setChecklist(cur, r.remoteChecklist);
    } else {
      detail.insert(QStringLiteral("mine"), p.mine);
      detail.insert(QStringLiteral("theirs"), field == QLatin1String("description") ? trackerTextAsMarkdown(cur, r.remote) : r.remote);
      // Kept for "overwrite": against the value the user was shown.
      PendingTrackerEdit again = p;
      again.edit.base = r.remote;
      m_trackerEditConflicts.insert(p.taskId + QChar('\n') + field, again);
    }
  } else if(r.outcome == QLatin1String("refused")) {
    const QString reason = r.error;
    detail.insert(QStringLiteral("reason"), reason);
    detail.insert(QStringLiteral("detail"), r.detail);
    static const QStringList own{
        QStringLiteral("roundTrip"), QStringLiteral("lossy"), QStringLiteral("image"), QStringLiteral("title"), QStringLiteral("date")};
    message = tr_(own.contains(reason) ? QStringLiteral("int.edit.refused.") + reason : QStringLiteral("int.edit.refused.other"))
                  .arg(keyName,
                       tracker,
                       reason == QLatin1String("lossy") || reason == QLatin1String("roundTrip") || reason == QLatin1String("image") ||
                               reason == QLatin1String("date")
                           ? r.detail
                           : reason);
  } else if(r.outcome == QLatin1String("unsupported")) {
    message = tr_("int.edit.unsupported").arg(tracker);
  } else {
    outcome = QStringLiteral("error");
    detail.insert(QStringLiteral("httpStatus"), r.httpStatus);
    message = tr_("int.edit.failed").arg(keyName, tracker, providerReason(r.error));
  }
  m_tasks.upsert(cur);
  scheduleSave();
  finishTrackerEdit(p.taskId, field, outcome, message, detail);
}

void AppController::overwriteTrackerEdit(const QString& taskId, const QString& field) {
  const auto it = m_trackerEditConflicts.constFind(taskId + QChar('\n') + field);
  if(it == m_trackerEditConflicts.constEnd()) {
    return;
  }
  PendingTrackerEdit p = *it;
  m_trackerEditConflicts.erase(it);
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  // From the card as it is now, with the user's value shown again.
  const Task& t = m_tasks.items().at(row);
  const Task shownBefore = p.shown;
  p.before = t;
  p.shown = t;
  if(field == QLatin1String("title")) {
    p.shown.title = shownBefore.title;
  } else if(field == QLatin1String("description")) {
    p.shown.desc = shownBefore.desc;
  } else if(field == QLatin1String("due")) {
    p.shown.dueAt = shownBefore.dueAt;
    p.shown.dueHasTime = shownBefore.dueHasTime;
  } else if(field == QLatin1String("assignee")) {
    p.shown.assignee = shownBefore.assignee;
  }
  sendTrackerEdit(p);
}

void AppController::discardTrackerEdit(const QString& taskId, const QString& field) {
  m_trackerEditConflicts.remove(taskId + QChar('\n') + field);
}

void AppController::searchTrackerAssignees(const QString& taskId, const QString& query) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  const QString providerId = t.externalProvider;
  const QString q = query.trimmed();
  const QString block = trackerEditBlock(t);
  if(!block.isEmpty()) {
    QString why = block;
    if(block == QLatin1String("off")) {
      why = tr_("int.edit.off").arg(providerDisplayName(providerId));
    } else if(block == QLatin1String("disabled")) {
      why = tr_("int.edit.disabled").arg(providerDisplayName(providerId));
    }
    emit trackerAssigneesFound(taskId, q, {}, why);
    return;
  }
  const QString key = providerId + QChar('\n') + t.externalId + QChar('\n') + q;
  const QDateTime now = fetchNow();
  // A minute's answer is reused; a failure waits out its back-off.
  if(!m_assigneeSearchBackoff.due(key, now, 60)) {
    const QString error = m_assigneeSearchBackoff.error(key);
    if(error.isEmpty() && m_assigneeSearchBackoff.pending(key, now)) {
      return;  // on its way
    }
    emit trackerAssigneesFound(taskId,
                               q,
                               error.isEmpty() ? m_assigneeSearchFound.value(key) : QVariantList{},
                               error.isEmpty() ? QString() : providerReason(error));
    return;
  }
  heap::net::FeatureGate& gate = heap::net::FeatureGate::instance();
  if(gate.isPaused(heap::net::features::sync(providerId)) || gate.isPaused(heap::net::features::edit(providerId))) {
    emit trackerAssigneesFound(taskId, q, {}, tr_("int.edit.paused").arg(providerDisplayName(providerId)));
    return;
  }
  QVector<QDateTime>& times = m_trackerEditTimes[QStringLiteral("search:") + providerId];
  times.erase(std::remove_if(times.begin(),
                             times.end(),
                             [&now](const QDateTime& at) {
                               return at.secsTo(now) >= 60;
                             }),
              times.end());
  if(times.size() >= 3 * kTrackerEditsPerMinute) {
    emit trackerAssigneesFound(taskId, q, {}, tr_("int.edit.rate").arg(providerDisplayName(providerId)));
    return;
  }
  times.append(now);
  m_assigneeSearchBackoff.asked(key, now);
  m_assigneeSearches.insert(key, taskId);
  const QString externalId = t.externalId;
  const auto fail = [this, key, taskId, q](const QString& error) {
    m_assigneeSearches.remove(key);
    m_assigneeSearchBackoff.failed(key, error);
    emit trackerAssigneesFound(taskId, q, {}, providerReason(error));
  };
  ensureFreshToken(
      providerId,
      [this, providerId, externalId, q, fail]() {
        for(const auto& provider : m_syncProviders) {
          if(provider->id() == providerId) {
            provider->searchAssignableUsers(externalId, q);
            return;
          }
        }
        fail(QStringLiteral("not configured"));
      },
      [fail]() {
        fail(QStringLiteral("HTTP 401 — the browser session is no longer valid; sign in again"));
      });
}

void AppController::abandonTrackerEdits(const QString& providerId) {
  // The provider that was asked is gone (switched off, signed out, or
  // rebuilt by a settings write): it will not answer. Each edit ends as a
  // failure — the card goes back, the user sees why — and each search
  // answers empty.
  QList<PendingTrackerEdit> lost;
  for(auto it = m_trackerEdits.cbegin(); it != m_trackerEdits.cend(); ++it) {
    if(it->providerId == providerId) {
      lost.append(*it);
    }
  }
  for(const PendingTrackerEdit& p : std::as_const(lost)) {
    ContentEditResult r;
    r.externalId = p.edit.externalId;
    r.project = p.edit.project;
    r.field = p.edit.field;
    r.outcome = QStringLiteral("error");
    r.error = interruptedReason();
    onContentEdited(providerId, r);
  }
  const QString prefix = providerId + QChar('\n');
  QStringList searches;
  for(auto it = m_assigneeSearches.cbegin(); it != m_assigneeSearches.cend(); ++it) {
    if(it.key().startsWith(prefix)) {
      searches.append(it.key());
    }
  }
  for(const QString& key : std::as_const(searches)) {
    const QString taskId = m_assigneeSearches.take(key);
    m_assigneeSearchBackoff.forget(key);
    const QString query = key.section(QChar('\n'), 2);
    emit trackerAssigneesFound(taskId, query, {}, providerReason(interruptedReason()));
  }
}
