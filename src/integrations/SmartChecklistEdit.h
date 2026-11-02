#pragma once

#include <QJsonArray>
#include <QJsonValue>
#include <QString>

#include <cstdint>

// Writing a Smart Checklist back (owner item 16, 2026-10-23): one item's
// status, one item's text, or a new item, applied to the checklist text as
// the tracker holds it right now. Only the line of that item changes (or one
// line is added): headers, "> details", blank lines and lines the reader
// does not understand stay exactly as they were. The result is read back
// with smartchecklist::parse and has to say what the edit meant, else the
// edit is refused rather than sent.
//
// Items are numbered the way parse() lists them, headers included — the
// index the card's details.smartChecklist array has.
namespace heap::integrations::smartchecklist {

struct Edit {
  enum class Op : std::uint8_t { State, Text, Add };
  Op op = Op::State;
  int index = -1;  // State / Text: the item
  // State: the status as Jira names it — a built-in (To Do, In Progress,
  // Done, Skipped; the old words todo / progress / done / skipped too) or a
  // team's own ("IN QA"), as smartChecklistStatuses lists them.
  QString state;
  // State, a custom status: its category (SmartChecklist.h). A finished one
  // (done, cancelled) is written checked, "+ [NAME] item", any other
  // "- [NAME] item". Empty: what the name's words say.
  QString category;
  QString text;  // Text / Add: one line
};

// A built-in status by its name or old word ("Done", "done"): its marker,
// one of - ~ + x; a null QChar for a custom status. Pure.
QChar builtinMarker(const QString& status);

struct EditResult {
  bool ok = false;
  // Why not: "index" (no such item), "header" (a header has no status),
  // "state" (not a status the list can hold: empty, brackets, a line
  // break, longer than 50), "text" (empty or several
  // lines), "roundTrip" (the list would not read back as meant).
  QString reason;
  QString text;      // the whole checklist text after the edit
  QJsonArray items;  // parse(text)
};

EditResult applyEdit(const QString& text, const Edit& edit);

// The checklist text an issue property value holds: the text itself, or an
// object keeping it as a string under value / checklist / text. False for
// any other shape (an array of item objects is not written back).
bool textOfPropertyValue(const QJsonValue& value, QString* text);

// `original` with its checklist text replaced, same shape and every other
// key kept. `original` must be a shape textOfPropertyValue accepts.
QJsonValue withPropertyText(const QJsonValue& original, const QString& text);

}  // namespace heap::integrations::smartchecklist
