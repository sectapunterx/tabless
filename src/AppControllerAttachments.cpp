// Files attached to tasks and linked from notes. The store itself — ids,
// hashing, the folder — is storage/Attachments.h; this is the part that knows
// about tasks, notes, undo and the UI. Kept out of AppController.cpp, which is
// long enough; these are ordinary members.

#include "AppController.h"

#include "integrations/TrackerImages.h"
#include "markdown/TableImport.h"
#include "markdown/TablePaste.h"
#include "markdown/TableText.h"
#include "markdown/Xlsx.h"
#include "storage/Attachments.h"

#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QMimeData>
#include <QStringDecoder>
#include <QUrl>

#include <algorithm>

namespace att = heap::attachments;

namespace {

struct AttText {
  const char* en;
  const char* ru;
};

// The feature's own strings, in the UI language. Kept here rather than in the
// shared table so this file can be read, and merged, on its own.
const QHash<QString, AttText>& attTable() {
  static const QHash<QString, AttText> table = {
      {"attached.one", {"Attached %1 to %2", "«%1» прикреплён к %2"}},
      {"attached.many", {"Attached %1 files to %2", "К %2 прикреплено файлов: %1"}},
      {"attached.undo", {"Attachment undone: %1", "Вложение отменено: %1"}},
      {"removed", {"Detached %1 from %2", "«%1» откреплён от %2"}},
      {"removed.undo", {"Reattached %1 to %2", "«%1» снова прикреплён к %2"}},
      {"missing", {"%1 is no longer in the attachments folder", "«%1» больше нет в папке вложений"}},
      {"pasted", {"pasted-%1.png", "вставка-%1.png"}},
      {"export.omitted",
       {"Exported without attachments: %1 files (%2) are over the %3 export limit. Copy the attachments folder from Tabless's data "
        "folder next to the file, or export the notes as a folder.",
        "Экспорт без вложений: %1 файлов (%2) больше предела %3. Скопируйте папку attachments из папки данных Tabless рядом с "
        "файлом или экспортируйте заметки папкой."}},
      {"import.problems", {"Some attachments were not imported: %1", "Часть вложений не импортирована: %1"}},
      {"cleanup.done", {"Deleted %1 unused attachments (%2)", "Удалено неиспользуемых вложений: %1 (%2)"}},
      {"cleanup.none", {"No unused attachments", "Неиспользуемых вложений нет"}},
      {"table.cut",
       {"%1: the first %2 of %3 rows went into the text; the whole file is linked under the table",
        "%1: в текст вставлены первые %2 из %3 строк; весь файл — по ссылке под таблицей"}},
      {"table.unreadable", {"%1 could not be read as a table: %2", "%1 не удалось прочитать как таблицу: %2"}},
  };
  return table;
}

// What the app shows itself rather than handing to another program (APP-315).
enum class FileKind { Other, Delimited, Workbook, Text, Log };

FileKind kindOf(const QString& name) {
  const QString ext = QFileInfo(name).suffix().toLower();
  if(ext == QLatin1String("csv") || ext == QLatin1String("tsv")) {
    return FileKind::Delimited;
  }
  if(ext == QLatin1String("xlsx")) {
    return FileKind::Workbook;
  }
  if(ext == QLatin1String("log")) {
    return FileKind::Log;
  }
  if(ext == QLatin1String("txt")) {
    return FileKind::Text;
  }
  return FileKind::Other;
}

// A text file's characters: UTF-8 (its BOM dropped), else the system's
// code page (a csv from Excel on a Russian Windows is cp1251).
QString decodeText(const QByteArray& bytes) {
  QByteArray b = bytes;
  if(b.startsWith("\xEF\xBB\xBF")) {
    b.remove(0, 3);
  }
  QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
  QString s = utf8.decode(b);
  if(!utf8.hasError()) {
    return s;
  }
  QStringDecoder system(QStringDecoder::System);
  return system.decode(b);
}

// The rows of a .csv / .tsv / .xlsx: one sheet for delimited text, the
// workbook's sheets for .xlsx. `error` says why there are none.
QList<heap::md::XlsxSheet> tableSheets(const QString& path, const QString& name, int maxRows, QString* error) {
  QFile f(path);
  if(!f.open(QIODevice::ReadOnly)) {
    *error = f.errorString();
    return {};
  }
  // A viewer and a paste, not a database: past this the file opens outside.
  constexpr qint64 kMaxBytes = 64LL * 1024 * 1024;
  const QByteArray data = f.read(kMaxBytes);
  if(kindOf(name) == FileKind::Workbook) {
    return heap::md::readXlsx(data, maxRows, error);
  }
  const QString text = decodeText(data);
  const QChar sep = QFileInfo(name).suffix().toLower() == QLatin1String("tsv") ? QLatin1Char('\t') : heap::md::guessSeparator(text);
  heap::md::XlsxSheet sheet;
  sheet.name = QFileInfo(name).completeBaseName();
  sheet.rows = heap::md::parseDelimited(text, sep);
  if(maxRows > 0 && sheet.rows.size() > maxRows) {
    sheet.rows = sheet.rows.mid(0, maxRows);
  }
  return {sheet};
}

QString localPathOf(const QVariant& v) {
  const QUrl url = v.toUrl();
  if(url.isValid() && url.isLocalFile()) {
    return url.toLocalFile();
  }
  const QString s = v.toString();
  if(s.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
    return QUrl(s).toLocalFile();
  }
  return s;
}

}  // namespace

QString AppController::attText(const char* key) const {
  const auto it = attTable().constFind(QString::fromLatin1(key));
  if(it == attTable().constEnd()) {
    return QString::fromLatin1(key);
  }
  return QString::fromUtf8(m_language == QStringLiteral("ru") ? it->ru : it->en);
}

QString AppController::attachmentsDir() const {
  return dataDir() + QStringLiteral("/attachments");
}

QString AppController::formatBytes(double bytes) const {
  return att::formatSize(static_cast<qint64>(bytes), m_language == QStringLiteral("ru"));
}

void AppController::toastAddFailure(const QString& name, int error) {
  emit toast(att::errorText(static_cast<att::AddResult::Error>(error), name, m_language == QStringLiteral("ru")),
             QStringLiteral("warning"));
}

QVariantList AppController::describeAttachments(const QVariantList& attachments) const {
  const att::Store store(attachmentsDir());
  const bool ru = m_language == QStringLiteral("ru");
  QVariantList out;
  for(const ::Attachment& a : att::fromVariantList(attachments)) {
    QVariantMap m;
    m["id"] = a.id;
    m["name"] = a.name.isEmpty() ? a.id : a.name;
    const qint64 onDisk = store.sizeOf(a.id);
    m["exists"] = onDisk >= 0;
    m["size"] = static_cast<double>(onDisk >= 0 ? onDisk : a.size);
    m["sizeText"] = att::formatSize(onDisk >= 0 ? onDisk : a.size, ru);
    m["mime"] = a.mime;
    m["isImage"] = att::isDisplayableImage(a.mime);
    m["ref"] = att::markdownRef(a);
    m["url"] = onDisk >= 0 ? QUrl::fromLocalFile(store.pathFor(a.id)) : QUrl();
    out.append(m);
  }
  return out;
}

QVariantList AppController::importAttachments(const QVariantList& urls, bool intoText) {
  const att::Store store(attachmentsDir());
  QVector<::Attachment> stored;
  for(const QVariant& v : urls) {
    const QString path = localPathOf(v);
    if(path.isEmpty()) {
      continue;
    }
    const att::AddResult r = store.addFile(path);
    if(!r.ok()) {
      toastAddFailure(QFileInfo(path).fileName(), r.error);
      continue;
    }
    stored.append(r.attachment);
    if(intoText) {
      m_attachmentsLinkedThisSession.insert(r.attachment.id);
    }
  }
  QVariantList out = describeAttachments(att::toVariantList(stored));
  if(intoText) {
    // A .csv / .tsv / .xlsx goes into the text as its table (APP-315), the
    // file linked under it: the text holds what it can, the file the rest.
    for(QVariant& v : out) {
      QVariantMap m = v.toMap();
      const QString name = m.value(QStringLiteral("name")).toString();
      const FileKind kind = kindOf(name);
      if(kind != FileKind::Delimited && kind != FileKind::Workbook) {
        continue;
      }
      constexpr int kRowsIntoText = 200;
      constexpr int kCellChars = 300;
      QString error;
      const QList<heap::md::XlsxSheet> sheets = tableSheets(store.pathFor(m.value(QStringLiteral("id")).toString()), name, 0, &error);
      if(sheets.isEmpty()) {
        emit toast(attText("table.unreadable").arg(name, error), QStringLiteral("warning"));
        continue;
      }
      QStringList parts;
      for(const heap::md::XlsxSheet& sheet : sheets) {
        if(sheet.rows.isEmpty()) {
          continue;
        }
        if(sheets.size() > 1) {
          parts << QStringLiteral("#### ") + sheet.name;
        }
        parts << heap::md::markdownTable(sheet.rows, kRowsIntoText, kCellChars);
        const qsizetype records = sheet.rows.size() - 1;
        if(records > kRowsIntoText) {
          emit toast(attText("table.cut")
                         .arg(sheets.size() > 1 ? name + QStringLiteral(" / ") + sheet.name : name)
                         .arg(kRowsIntoText)
                         .arg(records),
                     QStringLiteral("warning"));
        }
      }
      if(parts.isEmpty()) {
        continue;
      }
      parts << m.value(QStringLiteral("ref")).toString();
      m[QStringLiteral("ref")] = parts.join(QStringLiteral("\n\n"));
      v = m;
    }
  }
  return out;
}

int AppController::attachFilesToTask(const QString& taskId, const QVariantList& urls) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return 0;
  }
  // Stored first, outside the undo scope: the bytes are not part of what undo
  // puts back, only the task's list is.
  const QVariantList added = importAttachments(urls);
  if(added.isEmpty()) {
    return 0;
  }
  ::Task t = m_tasks.items().at(row);
  QStringList names;
  for(const ::Attachment& a : att::fromVariantList(added)) {
    const bool already = std::any_of(t.attachments.cbegin(), t.attachments.cend(), [&](const ::Attachment& x) {
      return x.id == a.id;
    });
    if(!already) {
      t.attachments.append(a);
    }
    names << a.name;
  }
  if(t == m_tasks.items().at(row)) {
    // Every file was attached already: nothing to record, but say so rather
    // than swallow the drop.
    emit toast(names.size() == 1 ? attText("attached.one").arg(names.first(), taskId)
                                 : attText("attached.many").arg(names.size()).arg(taskId));
    return static_cast<int>(names.size());
  }
  {
    const UndoScope scope(this, attText("attached.undo").arg(taskId));
    m_tasks.upsert(t);
    emit undoableToast(
        names.size() == 1 ? attText("attached.one").arg(names.first(), taskId) : attText("attached.many").arg(names.size()).arg(taskId), 5);
  }
  scheduleSave();
  return static_cast<int>(names.size());
}

bool AppController::removeTaskAttachment(const QString& taskId, const QString& attachmentId) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return false;
  }
  ::Task t = m_tasks.items().at(row);
  const auto it = std::find_if(t.attachments.begin(), t.attachments.end(), [&](const ::Attachment& a) {
    return a.id == attachmentId;
  });
  if(it == t.attachments.end()) {
    return false;
  }
  const QString name = it->name.isEmpty() ? it->id : it->name;
  t.attachments.erase(it);
  {
    // Detaching only drops the entry: the file stays in the store, so undo is
    // the task's list coming back and nothing else.
    const UndoScope scope(this, attText("removed.undo").arg(name, taskId));
    m_tasks.upsert(t);
    emit undoableToast(attText("removed").arg(name, taskId), 5);
  }
  scheduleSave();
  return true;
}

QVariantList AppController::taskAttachments(const QString& taskId) const {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return {};
  }
  return describeAttachments(att::toVariantList(m_tasks.items().at(row).attachments));
}

QVariantList AppController::markdownAttachments(const QString& markdown) const {
  const att::Store store(attachmentsDir());
  const QHash<QString, QString> labels = att::refLabelsIn(markdown);
  QVector<::Attachment> xs;
  for(const QString& id : att::refsIn(markdown)) {
    ::Attachment a;
    a.id = id;
    a.name = labels.value(id);
    a.size = qMax<qint64>(0, store.sizeOf(id));
    a.mime = att::mimeFor(a.name.isEmpty() ? id : a.name);
    // The id keeps the real extension; a label may have none.
    if(a.mime == QStringLiteral("application/octet-stream")) {
      a.mime = att::mimeFor(id);
    }
    xs.append(a);
  }
  return describeAttachments(att::toVariantList(xs));
}

QString AppController::openAttachment(const QString& id, bool confirmed) {
  const att::Store store(attachmentsDir());
  if(!att::isValidId(id)) {
    return QStringLiteral("invalid");
  }
  if(!store.contains(id)) {
    emit toast(attText("missing").arg(id), QStringLiteral("warning"));
    return QStringLiteral("missing");
  }
  // The same rule links follow (LinkConfirmDialog): anything that would run
  // rather than show is asked about first. The id carries the extension the
  // shell decides by.
  if(!confirmed && att::needsOpenConfirmation(id)) {
    return QStringLiteral("confirm");
  }
  // Tables and text the app reads itself (APP-315).
  if(kindOf(id) != FileKind::Other) {
    return QStringLiteral("view");
  }
  att::shell(store.pathFor(id), false);
  return QStringLiteral("opened");
}

bool AppController::openAttachmentExternally(const QString& id) {
  const att::Store store(attachmentsDir());
  if(!att::isValidId(id) || !store.contains(id)) {
    return false;
  }
  return att::shell(store.pathFor(id), false);
}

QVariantMap AppController::attachmentPreview(const QString& id) const {
  QVariantMap out{{"kind", "none"}};
  const att::Store store(attachmentsDir());
  if(!att::isValidId(id) || !store.contains(id)) {
    out["error"] = attText("missing").arg(id);
    return out;
  }
  const QString path = store.pathFor(id);
  const FileKind kind = kindOf(id);
  out["name"] = QFileInfo(path).fileName();
  if(kind == FileKind::Delimited || kind == FileKind::Workbook) {
    constexpr int kViewRows = 5000;
    QString error;
    const QList<heap::md::XlsxSheet> sheets = tableSheets(path, id, 0, &error);
    QVariantList list;
    bool truncated = false;
    for(const heap::md::XlsxSheet& sheet : sheets) {
      QVariantList rows;
      qsizetype columns = 0;
      for(qsizetype i = 0; i < sheet.rows.size() && i < kViewRows; ++i) {
        rows << QVariant(sheet.rows.at(i));
        columns = std::max(columns, sheet.rows.at(i).size());
      }
      truncated = truncated || sheet.rows.size() > kViewRows;
      list << QVariantMap{
          {"name", sheet.name}, {"rows", rows}, {"total", static_cast<int>(sheet.rows.size())}, {"columns", static_cast<int>(columns)}};
    }
    out["kind"] = QStringLiteral("table");
    out["sheets"] = list;
    out["truncated"] = truncated;
    if(sheets.isEmpty()) {
      out["error"] = error;
    }
    return out;
  }
  // A picture, for the viewer a picture in a text opens (dogfood
  // 2026-10-20). Opening a picture from its chip still hands it over.
  if(att::isDisplayableImage(att::mimeFor(path))) {
    out["kind"] = QStringLiteral("image");
    out["url"] = QUrl::fromLocalFile(path).toString();
    return out;
  }
  if(kind == FileKind::Text || kind == FileKind::Log) {
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly)) {
      out["error"] = f.errorString();
      return out;
    }
    constexpr qint64 kViewBytes = 4LL * 1024 * 1024;
    out["kind"] = QStringLiteral("text");
    out["text"] = decodeText(f.read(kViewBytes));
    out["truncated"] = f.size() > kViewBytes;
    out["language"] = kind == FileKind::Log ? QStringLiteral("log") : QString();
  }
  return out;
}

bool AppController::revealAttachment(const QString& id) {
  const att::Store store(attachmentsDir());
  if(!store.contains(id)) {
    if(att::isValidId(id)) {
      emit toast(attText("missing").arg(id), QStringLiteral("warning"));
    }
    return false;
  }
  return att::shell(store.pathFor(id), true);
}

QUrl AppController::attachmentUrl(const QString& id) const {
  const att::Store store(attachmentsDir());
  return store.contains(id) ? QUrl::fromLocalFile(store.pathFor(id)) : QUrl();
}

namespace {

// Local files on the clipboard (copied in a file manager), or nothing.
QStringList clipboardFiles(const QMimeData* mime) {
  QStringList out;
  if(mime == nullptr || !mime->hasUrls()) {
    return out;
  }
  for(const QUrl& u : mime->urls()) {
    if(!u.isLocalFile() || !QFileInfo(u.toLocalFile()).isFile()) {
      return {};
    }
    out << u.toLocalFile();
  }
  return out;
}

// A picture on the clipboard is what a paste means, even with text beside
// it: a browser, Office and the messengers always put its address or name
// there too, and the text won (dogfood 2026-10-30; Ctrl+Shift+V still pastes
// the text). Except a spreadsheet's cells: they come with a picture of
// themselves, and the cells are what is meant.
bool clipboardImageWins(const QMimeData* mime) {
  if(mime == nullptr || !mime->hasImage()) {
    return false;
  }
  const QString html = mime->hasHtml() ? mime->html() : QString();
  if(html.contains(QLatin1String("<table"), Qt::CaseInsensitive) ||
     html.contains(QLatin1String("urn:schemas-microsoft-com:office:excel"), Qt::CaseInsensitive)) {
    return false;
  }
  return heap::md::tableMarkdownForPaste(html, mime->hasText() ? mime->text() : QString()).isEmpty();
}

}  // namespace

bool AppController::clipboardHasAttachment() const {
  const QClipboard* cb = QGuiApplication::clipboard();
  const QMimeData* mime = cb != nullptr ? cb->mimeData() : nullptr;
  return !clipboardFiles(mime).isEmpty() || clipboardImageWins(mime);
}

QString AppController::clipboardTableMarkdown() const {
  const QClipboard* cb = QGuiApplication::clipboard();
  const QMimeData* mime = cb != nullptr ? cb->mimeData() : nullptr;
  if(mime == nullptr || !clipboardFiles(mime).isEmpty()) {
    return {};
  }
  return heap::md::tableMarkdownForPaste(mime->hasHtml() ? mime->html() : QString(), mime->hasText() ? mime->text() : QString());
}

QString AppController::clipboardPlainText() const {
  const QClipboard* cb = QGuiApplication::clipboard();
  return cb != nullptr ? cb->text() : QString();
}

// ── Notes → Import → a table (dogfood 2026-10-23 #9) ──────────────────────

namespace {

// What an import source holds, read once for the preview and the import.
struct ImportSource {
  QString name;                       // file name, or "" for the clipboard
  QList<heap::md::XlsxSheet> sheets;  // .xlsx / .csv / .tsv
  QString text;                       // anything else: plain text
  bool isText = false;
  QString error;
};

ImportSource readImportSource(const QVariant& source, int sheetRows) {
  ImportSource src;
  if(source.toString() == QLatin1String("clipboard")) {
    const QClipboard* cb = QGuiApplication::clipboard();
    src.text = cb != nullptr ? cb->text() : QString();
    src.isText = true;
    return src;
  }
  const QString path = localPathOf(source);
  src.name = QFileInfo(path).fileName();
  const FileKind kind = kindOf(path);
  if(kind == FileKind::Workbook || kind == FileKind::Delimited) {
    src.sheets = tableSheets(path, src.name, sheetRows, &src.error);
    return src;
  }
  QFile f(path);
  if(!f.open(QIODevice::ReadOnly)) {
    src.error = f.errorString();
    return src;
  }
  constexpr qint64 kMaxBytes = 64LL * 1024 * 1024;
  src.text = decodeText(f.read(kMaxBytes));
  src.isText = true;
  return src;
}

heap::md::TextTableOptions textOptions(const QVariantMap& o) {
  heap::md::TextTableOptions opt;
  opt.separator = heap::md::textSeparatorFromName(o.value(QStringLiteral("separator")).toString());
  const QString custom = o.value(QStringLiteral("custom")).toString();
  opt.custom = custom.isEmpty() ? QChar() : custom.at(0);
  opt.skipLinesWithout = o.value(QStringLiteral("skipLinesWithout"), true).toBool();
  opt.collapseRepeats = o.value(QStringLiteral("collapse"), false).toBool();
  return opt;
}

// The rows to import, the header first: the source's own first row, or
// "A", "B", … when the first row is data.
QList<QStringList> withHeader(QList<QStringList> rows, int columns, bool firstRowIsHeader) {
  if(!firstRowIsHeader && columns > 0) {
    rows.prepend(heap::md::letterHeader(columns));
  }
  return rows;
}

}  // namespace

QVariantMap AppController::tableImportPreview(const QVariant& source, const QVariantMap& options) const {
  constexpr int kPreviewRows = 50;
  constexpr int kSkippedShown = 200;
  QVariantMap out;
  const ImportSource src = readImportSource(source, 0);
  out[QStringLiteral("name")] = src.name;
  out[QStringLiteral("error")] = src.error;
  QList<QStringList> rows;
  int columns = 0;
  if(src.isText) {
    const heap::md::TextTable t = heap::md::parseTextTable(src.text, textOptions(options));
    out[QStringLiteral("kind")] = QStringLiteral("text");
    out[QStringLiteral("separator")] = heap::md::textSeparatorName(t.separator);
    out[QStringLiteral("lines")] = t.lines;
    out[QStringLiteral("matched")] = t.matched;
    out[QStringLiteral("skipped")] = static_cast<int>(t.skipped.size());
    out[QStringLiteral("skippedLines")] = QStringList(t.skipped.mid(0, kSkippedShown));
    out[QStringLiteral("markdown")] = t.markdown;
    out[QStringLiteral("isTable")] = t.isTable();
    rows = t.rows;
    columns = t.columns;
  } else {
    QStringList names;
    for(const heap::md::XlsxSheet& sh : src.sheets) {
      names << sh.name;
    }
    const int sheet = std::clamp(options.value(QStringLiteral("sheet")).toInt(), 0, std::max(0, static_cast<int>(src.sheets.size()) - 1));
    out[QStringLiteral("kind")] = QStringLiteral("sheets");
    out[QStringLiteral("sheets")] = names;
    out[QStringLiteral("sheet")] = sheet;
    if(!src.sheets.isEmpty()) {
      rows = src.sheets.at(sheet).rows;
      for(const QStringList& r : rows) {
        columns = std::max(columns, static_cast<int>(r.size()));
      }
    }
    out[QStringLiteral("isTable")] = !rows.isEmpty() && columns >= 1;
  }
  rows = withHeader(rows, columns, options.value(QStringLiteral("header"), true).toBool());
  QVariantList shown;
  for(qsizetype i = 0; i < rows.size() && i < kPreviewRows + 1; ++i) {
    shown << QVariant(rows.at(i));
  }
  out[QStringLiteral("rows")] = shown;
  out[QStringLiteral("total")] = std::max(0, static_cast<int>(rows.size()) - 1);
  out[QStringLiteral("columns")] = columns;
  return out;
}

QString AppController::tableImportMarkdown(const QVariant& source, const QVariantMap& options) const {
  const ImportSource src = readImportSource(source, 0);
  const bool header = options.value(QStringLiteral("header"), true).toBool();
  if(src.isText) {
    if(options.value(QStringLiteral("asText")).toBool()) {
      return src.text;
    }
    const heap::md::TextTable t = heap::md::parseTextTable(src.text, textOptions(options));
    if(!t.isTable()) {
      return {};
    }
    // Every row, one table: the owner's call, however long (dogfood 2026-10-23).
    return heap::md::markdownTable(withHeader(t.rows, t.columns, header));
  }
  if(src.sheets.isEmpty()) {
    return {};
  }
  const int sheet = std::clamp(options.value(QStringLiteral("sheet")).toInt(), 0, static_cast<int>(src.sheets.size()) - 1);
  const QList<QStringList>& rows = src.sheets.at(sheet).rows;
  int columns = 0;
  for(const QStringList& r : rows) {
    columns = std::max(columns, static_cast<int>(r.size()));
  }
  return rows.isEmpty() ? QString() : heap::md::markdownTable(withHeader(rows, columns, header));
}

QVariantList AppController::importClipboardAttachments() {
  const QClipboard* cb = QGuiApplication::clipboard();
  const QMimeData* mime = cb != nullptr ? cb->mimeData() : nullptr;
  const QStringList files = clipboardFiles(mime);
  if(!files.isEmpty()) {
    QVariantList urls;
    for(const QString& f : files) {
      urls << QUrl::fromLocalFile(f);
    }
    return importAttachments(urls, /*intoText=*/true);
  }
  if(!clipboardImageWins(mime)) {
    return {};
  }
  const QImage image = qvariant_cast<QImage>(mime->imageData());
  if(image.isNull()) {
    return {};
  }
  QByteArray png;
  QBuffer buf(&png);
  buf.open(QIODevice::WriteOnly);
  if(!image.save(&buf, "PNG")) {
    return {};
  }
  const QString name = attText("pasted").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
  const att::AddResult r = att::Store(attachmentsDir()).addBytes(png, name, QStringLiteral("image/png"));
  if(!r.ok()) {
    toastAddFailure(name, r.error);
    return {};
  }
  m_attachmentsLinkedThisSession.insert(r.attachment.id);
  return describeAttachments(att::toVariantList({r.attachment}));
}

// ── What still points at a file ──

namespace {

void collectTask(const ::Task& t, QSet<QString>* ids) {
  for(const ::Attachment& a : t.attachments) {
    ids->insert(a.id);
  }
  for(const QString& id : att::refsIn(t.desc)) {
    ids->insert(id);
  }
}

void collectText(const QString& text, QSet<QString>* ids) {
  for(const QString& id : att::refsIn(text)) {
    ids->insert(id);
  }
}

void collectProfile(const Profile& p, QSet<QString>* ids) {
  for(const ::Task& t : p.tasks) {
    collectTask(t, ids);
  }
  for(const Note& n : p.notes) {
    collectText(n.body, ids);
  }
  for(const DocPage& d : p.docPages) {
    collectText(d.body, ids);
  }
  collectText(p.notesState, ids);
  collectText(p.docsState, ids);
}

}  // namespace

void AppController::trackAttachmentRefsInText() {
  const auto note = [this](const QString& text) {
    collectText(text, &m_attachmentsLinkedThisSession);
  };
  // Rows as they are inserted, changed or reset: that covers typing, undo,
  // imports and a profile switch alike.
  const auto watch = [this](auto* model, auto textOf) {
    const auto scan = [model, textOf, this](int first, int last) {
      const auto& items = model->items();
      for(int row = qMax(0, first); row <= last && row < items.size(); ++row) {
        collectText(textOf(items.at(row)), &m_attachmentsLinkedThisSession);
      }
    };
    connect(model, &QAbstractItemModel::dataChanged, this, [scan](const QModelIndex& from, const QModelIndex& to) {
      scan(from.row(), to.row());
    });
    connect(model, &QAbstractItemModel::rowsInserted, this, [scan](const QModelIndex&, int first, int last) {
      scan(first, last);
    });
    connect(model, &QAbstractItemModel::modelReset, this, [scan, model]() {
      scan(0, model->rowCount() - 1);
    });
  };
  watch(&m_notes, [](const Note& n) -> const QString& {
    return n.body;
  });
  watch(&m_docPages, [](const DocPage& d) -> const QString& {
    return d.body;
  });
  watch(&m_tasks, [](const ::Task& t) -> const QString& {
    return t.desc;
  });
  connect(this, &AppController::notesStateChanged, this, [this, note]() {
    note(m_notesState);
  });
  connect(this, &AppController::docsStateChanged, this, [this, note]() {
    note(m_docsState);
  });
}

QSet<QString> AppController::referencedAttachmentIds() const {
  QSet<QString> ids = m_attachmentsLinkedThisSession;
  for(const Profile& p : m_profiles) {
    if(p.id != m_activeProfileId) {
      collectProfile(p, &ids);
    }
  }
  // The active profile lives in the models, which are ahead of its snapshot.
  for(const ::Task& t : m_tasks.items()) {
    collectTask(t, &ids);
  }
  // A ticket's pictures fetched from its tracker (dogfood 2026-10-20), while
  // the ticket is here and still has the file.
  loadTrackerImages();
  for(const ::Task& t : m_tasks.items()) {
    if(t.externalProvider.isEmpty()) {
      continue;
    }
    for(const heap::integrations::TrackerAttachment& f : heap::integrations::trackerAttachments(t.externalMeta.details)) {
      const QString local = m_trackerImageIds.value(t.externalProvider + QChar('\n') + f.id);
      if(!local.isEmpty()) {
        ids.insert(local);
      }
    }
  }
  // Comment authors' pictures (dogfood 2026-10-23): a few kilobytes each,
  // kept while they are known, so the cleanup does not make them fetched
  // again.
  const QString avatarMark = trackerAvatarKey(QString(), QString());
  for(auto it = m_trackerImageIds.constBegin(); it != m_trackerImageIds.constEnd(); ++it) {
    if(it.key().contains(avatarMark)) {
      ids.insert(it.value());
    }
  }
  for(const Note& n : m_notes.items()) {
    collectText(n.body, &ids);
  }
  for(const DocPage& d : m_docPages.items()) {
    collectText(d.body, &ids);
  }
  collectText(m_notesState, &ids);
  collectText(m_docsState, &ids);
  // Undo and redo can bring back anything they hold, the other profiles'
  // parked stacks included (A3-14).
  const auto collectEntry = [&](const heap::undo::Entry& e) {
    for(const auto& x : e.tasks) {
      collectTask(x.before, &ids);
      collectTask(x.after, &ids);
    }
    for(const auto& x : e.notes) {
      collectText(x.before.body, &ids);
      collectText(x.after.body, &ids);
    }
    for(const auto& x : e.docPages) {
      collectText(x.before.body, &ids);
      collectText(x.after.body, &ids);
    }
    collectText(e.docsStateBefore, &ids);
    collectText(e.docsStateAfter, &ids);
    if(e.profileRemoved) {
      collectProfile(e.profile, &ids);
    }
  };
  m_undo.forEachEntry(collectEntry);
  for(const heap::undo::UndoStack& parked : std::as_const(m_parkedUndo)) {
    parked.forEachEntry(collectEntry);
  }
  // A backup holds only state.json, so a file it names is gone for good once
  // cleaned up, and restoring it brings back a broken attachment (TASKS-16,
  // audit 2026-09-30). An id is a hash plus extension, which no other text in
  // the file can spell, so finding it in the raw bytes is enough.
  const QStringList stored = att::Store(attachmentsDir()).ids();
  QStringList unseen;
  for(const QString& id : stored) {
    if(!ids.contains(id)) {
      unseen.append(id);
    }
  }
  if(!unseen.isEmpty()) {
    const QDir backups(backupDirPath());
    for(const QString& name : backups.entryList({QStringLiteral("state-*.json")}, QDir::Files | QDir::NoSymLinks)) {
      QFile f(backups.filePath(name));
      if(!f.open(QIODevice::ReadOnly)) {
        continue;
      }
      const QByteArray bytes = f.readAll();
      for(qsizetype i = unseen.size() - 1; i >= 0; --i) {
        if(bytes.contains(unseen.at(i).toLatin1())) {
          ids.insert(unseen.takeAt(i));
        }
      }
      if(unseen.isEmpty()) {
        break;
      }
    }
  }
  return ids;
}

QVariantMap AppController::unusedAttachments() const {
  const att::Store store(attachmentsDir());
  const QSet<QString> used = referencedAttachmentIds();
  int count = 0;
  qint64 bytes = 0;
  for(const QString& id : store.ids()) {
    if(!used.contains(id)) {
      ++count;
      bytes += qMax<qint64>(0, store.sizeOf(id));
    }
  }
  QVariantMap out;
  out["count"] = count;
  out["bytes"] = static_cast<double>(bytes);
  out["sizeText"] = att::formatSize(bytes, m_language == QStringLiteral("ru"));
  return out;
}

QVariantMap AppController::cleanUpUnusedAttachments() {
  // What an editor has typed but not yet handed over is a reference too.
  emit flushEditorsRequested();
  syncActiveNoteBody();
  const att::Store store(attachmentsDir());
  const QSet<QString> used = referencedAttachmentIds();
  int count = 0;
  qint64 bytes = 0;
  for(const QString& id : store.ids()) {
    if(used.contains(id)) {
      continue;
    }
    const qint64 size = qMax<qint64>(0, store.sizeOf(id));
    if(store.remove(id)) {
      ++count;
      bytes += size;
    }
  }
  QVariantMap out;
  out["count"] = count;
  out["bytes"] = static_cast<double>(bytes);
  out["sizeText"] = att::formatSize(bytes, m_language == QStringLiteral("ru"));
  emit toast(count > 0 ? attText("cleanup.done").arg(count).arg(out["sizeText"].toString()) : attText("cleanup.none"));
  return out;
}

// ── Profile export / import ──

QStringList AppController::profileAttachmentIds(const Profile& p) const {
  QSet<QString> ids;
  collectProfile(p, &ids);
  QStringList out(ids.cbegin(), ids.cend());
  out.sort();
  return out;
}

QJsonArray AppController::attachmentsForExport(const Profile& p, qint64* omittedBytes, int* omittedCount) const {
  const att::Store store(attachmentsDir());
  // The names a task kept for its files, so a file only a note links still
  // gets a sensible one from the note's label.
  QHash<QString, ::Attachment> meta;
  for(const ::Task& t : p.tasks) {
    for(const ::Attachment& a : t.attachments) {
      meta.insert(a.id, a);
    }
  }
  const auto labelsFrom = [&](const QString& text) {
    const QHash<QString, QString> labels = att::refLabelsIn(text);
    for(auto it = labels.cbegin(); it != labels.cend(); ++it) {
      if(!meta.contains(it.key()) && !it.value().isEmpty()) {
        meta.insert(it.key(), ::Attachment{it.key(), it.value(), 0, QString()});
      }
    }
  };
  for(const Note& n : p.notes) {
    labelsFrom(n.body);
  }
  for(const DocPage& d : p.docPages) {
    labelsFrom(d.body);
  }
  const QStringList ids = profileAttachmentIds(p);
  qint64 total = 0;
  int present = 0;
  for(const QString& id : ids) {
    const qint64 size = store.sizeOf(id);
    if(size >= 0) {
      total += size;
      ++present;
    }
  }
  *omittedBytes = 0;
  *omittedCount = 0;
  QJsonArray out;
  if(total > att::kMaxExportBytes) {
    *omittedBytes = total;
    *omittedCount = present;
    return out;
  }
  for(const QString& id : ids) {
    QFile f(store.pathFor(id));
    if(!store.contains(id) || !f.open(QIODevice::ReadOnly)) {
      continue;  // missing here too: the task still says what it was
    }
    const ::Attachment a = meta.value(id, ::Attachment{id, id, 0, QString()});
    QJsonObject o;
    o["id"] = id;
    o["name"] = a.name.isEmpty() ? id : a.name;
    o["mime"] = a.mime.isEmpty() ? att::mimeFor(id) : a.mime;
    const QByteArray bytes = f.readAll();
    o["size"] = static_cast<double>(bytes.size());
    o["data"] = QString::fromLatin1(bytes.toBase64());
    out.append(o);
  }
  return out;
}

QHash<QString, QString> AppController::importAttachmentBlobs(const QJsonArray& blobs, QStringList* problems) {
  const att::Store store(attachmentsDir());
  const bool ru = m_language == QStringLiteral("ru");
  QHash<QString, QString> remap;
  for(const QJsonValue& v : blobs) {
    const QJsonObject o = v.toObject();
    const QString claimed = o.value("id").toString();
    QString name = o.value("name").toString();
    if(name.trimmed().isEmpty()) {
      name = claimed;
    }
    // Never a path: the name is metadata, the id is recomputed from the bytes.
    name = QFileInfo(name).fileName();
    const QByteArray b64 = o.value("data").toString().toLatin1();
    // A base64 string four thirds the size of the cap is the cap.
    if(b64.size() > att::kMaxFileBytes / 3 * 4 + 4) {
      problems->append(att::errorText(att::AddResult::TooLarge, name, ru));
      continue;
    }
    const auto decoded = QByteArray::fromBase64Encoding(b64);
    if(!decoded) {
      problems->append(att::errorText(att::AddResult::Unreadable, name, ru));
      continue;
    }
    // The stored name decides the extension, and with it the id: keep the
    // claimed id's extension when the name lost it.
    if(att::isValidId(claimed) && att::extensionOf(name) != att::extensionOf(claimed) && !att::extensionOf(claimed).isEmpty()) {
      name += QLatin1Char('.') + att::extensionOf(claimed);
    }
    const att::AddResult r = store.addBytes(*decoded, name, o.value("mime").toString());
    if(!r.ok()) {
      problems->append(att::errorText(r.error, name, ru));
      continue;
    }
    if(att::isValidId(claimed) && claimed != r.attachment.id) {
      remap.insert(claimed, r.attachment.id);
    }
  }
  return remap;
}
