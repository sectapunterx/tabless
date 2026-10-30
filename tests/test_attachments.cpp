// File attachments: the content-addressed store, the references notes and
// descriptions write, and everything AppController does with them — attach
// and detach as undo steps, profile export/import, the notes folder, the
// cleanup, paste, and the rules for opening a file.

#include "AppController.h"
#include "Models.h"
#include "StateSerializer.h"

#include "markdown/MdHtml.h"
#include "storage/Attachments.h"
#include "sync/SyncSerializer.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>

#include <gtest/gtest.h>

namespace att = heap::attachments;

namespace {

// Distinct bytes per call, so a test never finds its file already stored by
// an earlier run (the test profile persists between runs).
QByteArray uniqueBytes(const QByteArray& tag = QByteArray()) {
  return tag + ' ' + QUuid::createUuid().toByteArray();
}

// The store keeps its files read-only; a plain removeRecursively can stall on
// them.
void wipe(const QString& dir) {
  QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
  while(it.hasNext()) {
    const QString f = it.next();
    QFile::setPermissions(f, QFile::ReadOwner | QFile::WriteOwner);
  }
  QDir(dir).removeRecursively();
}

class AttachmentStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    store_ = std::make_unique<att::Store>(dir_.path() + QStringLiteral("/attachments"));
  }

  void TearDown() override {
    wipe(dir_.path());
  }

  QString write(const QString& rel, const QByteArray& bytes) const {
    const QString p = dir_.path() + QLatin1Char('/') + rel;
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    return p;
  }

  QTemporaryDir dir_;
  std::unique_ptr<att::Store> store_;
};

// ── Ids ──

TEST(AttachmentIds, OnlyTheFixedShapeIsAnId) {
  EXPECT_TRUE(att::isValidId(QStringLiteral("0123456789abcdef0123456789abcdef")));
  EXPECT_TRUE(att::isValidId(QStringLiteral("0123456789abcdef0123456789abcdef.png")));
  EXPECT_TRUE(att::isValidId(QStringLiteral("0123456789abcdef0123456789abcdef.tar7")));
  // Anything that could become a path somewhere else.
  EXPECT_FALSE(att::isValidId(QString()));
  EXPECT_FALSE(att::isValidId(QStringLiteral("../state.json")));
  EXPECT_FALSE(att::isValidId(QStringLiteral("0123456789abcdef0123456789abcdef/../../x")));
  EXPECT_FALSE(att::isValidId(QStringLiteral("..\\0123456789abcdef0123456789abcdef")));
  EXPECT_FALSE(att::isValidId(QStringLiteral("0123456789ABCDEF0123456789ABCDEF")));
  EXPECT_FALSE(att::isValidId(QStringLiteral("0123456789abcdef0123456789abcde")));
  EXPECT_FALSE(att::isValidId(QStringLiteral("0123456789abcdef0123456789abcdef.")));
  EXPECT_FALSE(att::isValidId(QStringLiteral("0123456789abcdef0123456789abcdef.png.exe")));
  EXPECT_FALSE(att::isValidId(QStringLiteral("0123456789abcdef0123456789abcdef.waytoolongextension")));
  EXPECT_FALSE(att::isValidId(QStringLiteral("C:0123456789abcdef0123456789abcdef")));
}

TEST_F(AttachmentStoreTest, NoPathIsBuiltFromAnInvalidId) {
  EXPECT_TRUE(store_->pathFor(QStringLiteral("../state.json")).isEmpty());
  EXPECT_FALSE(store_->contains(QStringLiteral("../state.json")));
  EXPECT_EQ(store_->sizeOf(QStringLiteral("..")), -1);
  EXPECT_FALSE(store_->remove(QStringLiteral("../state.json")));
}

TEST_F(AttachmentStoreTest, TheIdIsTheContentHashPlusTheExtension) {
  const QByteArray bytes = uniqueBytes("shot");
  const att::AddResult r = store_->addFile(write(QStringLiteral("in/Screen Shot.PNG"), bytes));
  ASSERT_TRUE(r.ok());
  const QString hex = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()).left(32);
  EXPECT_EQ(r.attachment.id, hex + QStringLiteral(".png"));
  EXPECT_EQ(r.attachment.name, QStringLiteral("Screen Shot.PNG"));
  EXPECT_EQ(r.attachment.size, bytes.size());
  EXPECT_FALSE(r.deduplicated);
  QFile stored(store_->pathFor(r.attachment.id));
  ASSERT_TRUE(stored.open(QIODevice::ReadOnly));
  EXPECT_EQ(stored.readAll(), bytes);
  // Immutable on disk: an editor it is opened in cannot change the bytes.
  EXPECT_FALSE(QFileInfo(store_->pathFor(r.attachment.id)).isWritable());
}

TEST_F(AttachmentStoreTest, TheSameBytesAreStoredOnce) {
  const QByteArray bytes = uniqueBytes("dup");
  const att::AddResult a = store_->addFile(write(QStringLiteral("a/report.pdf"), bytes));
  const att::AddResult b = store_->addFile(write(QStringLiteral("b/copy of report.pdf"), bytes));
  ASSERT_TRUE(a.ok());
  ASSERT_TRUE(b.ok());
  EXPECT_EQ(a.attachment.id, b.attachment.id);
  EXPECT_TRUE(b.deduplicated);
  EXPECT_EQ(b.attachment.name, QStringLiteral("copy of report.pdf"));
  EXPECT_EQ(store_->ids().size(), 1);
  // No temp files left behind by either copy.
  EXPECT_EQ(QDir(store_->dir()).entryList(QDir::Files | QDir::Hidden).size(), 1);
}

TEST_F(AttachmentStoreTest, BytesFromNoFileAreStoredToo) {
  const QByteArray bytes = uniqueBytes("paste");
  const att::AddResult r = store_->addBytes(bytes, QStringLiteral("pasted.png"), QStringLiteral("image/png"));
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.attachment.id.endsWith(QStringLiteral(".png")));
  EXPECT_EQ(r.attachment.mime, QStringLiteral("image/png"));
  EXPECT_EQ(store_->sizeOf(r.attachment.id), bytes.size());
}

TEST_F(AttachmentStoreTest, RefusesWhatIsNotAPlainFileWithinTheCap) {
  EXPECT_EQ(store_->addFile(dir_.path() + QStringLiteral("/nope.txt")).error, att::AddResult::NotFound);
  QDir().mkpath(dir_.path() + QStringLiteral("/folder"));
  EXPECT_EQ(store_->addFile(dir_.path() + QStringLiteral("/folder")).error, att::AddResult::NotAFile);
  const QString big = write(QStringLiteral("big.bin"), QByteArray(4096, 'x'));
  const att::AddResult r = store_->addFile(big, QString(), 1024);
  EXPECT_EQ(r.error, att::AddResult::TooLarge);
  EXPECT_TRUE(store_->ids().isEmpty());
  EXPECT_EQ(store_->addBytes(QByteArray(4096, 'x'), QStringLiteral("x.bin"), QString(), 1024).error, att::AddResult::TooLarge);
}

TEST_F(AttachmentStoreTest, NeverFollowsALink) {
  const QString target = write(QStringLiteral("secret.txt"), uniqueBytes("secret"));
  // A .lnk shortcut on Windows, a symbolic link elsewhere.
  const QString link = dir_.path() + QStringLiteral("/secret-link") +
#ifdef Q_OS_WIN
                       QStringLiteral(".lnk");
#else
                       QString();
#endif
  ASSERT_TRUE(QFile::link(target, link));
  EXPECT_EQ(store_->addFile(link).error, att::AddResult::Link);
  EXPECT_TRUE(store_->ids().isEmpty());
}

TEST_F(AttachmentStoreTest, AMissingFileIsReportedNotAssumed) {
  const att::AddResult r = store_->addBytes(uniqueBytes("gone"), QStringLiteral("gone.txt"));
  ASSERT_TRUE(r.ok()) << r.error;
  ASSERT_TRUE(store_->remove(r.attachment.id));
  EXPECT_FALSE(store_->contains(r.attachment.id));
  EXPECT_EQ(store_->sizeOf(r.attachment.id), -1);
  EXPECT_FALSE(store_->remove(r.attachment.id));
}

TEST_F(AttachmentStoreTest, IgnoresFilesThatAreNotIds) {
  write(QStringLiteral("attachments/notes.txt"), "hand-dropped");
  write(QStringLiteral("attachments/.incoming-abc"), "temp");
  EXPECT_TRUE(store_->ids().isEmpty());
}

// ── Markdown references ──

TEST(AttachmentRefs, ImagesEmbedAndOtherFilesLink) {
  const Attachment png{
      QStringLiteral("0123456789abcdef0123456789abcdef.png"), QStringLiteral("shot [v2].png"), 10, QStringLiteral("image/png")};
  EXPECT_EQ(att::markdownRef(png), QStringLiteral("![shot \\[v2\\].png](attachments/0123456789abcdef0123456789abcdef.png)"));
  const Attachment pdf{
      QStringLiteral("0123456789abcdef0123456789abcdef.pdf"), QStringLiteral("spec.pdf"), 10, QStringLiteral("application/pdf")};
  EXPECT_EQ(att::markdownRef(pdf), QStringLiteral("[spec.pdf](attachments/0123456789abcdef0123456789abcdef.pdf)"));
}

TEST(AttachmentRefs, FindsLabelsAndRemapsIds) {
  const QString a = QStringLiteral("0123456789abcdef0123456789abcdef.png");
  const QString b = QStringLiteral("fedcba9876543210fedcba9876543210");
  const QString md = QStringLiteral(
                         "x ![shot \\[v2\\].png](attachments/%1) and [notes](<attachments/%2>)\nagain attachments/%1\n"
                         "not one: attachments/0123.png")
                         .arg(a, b);
  EXPECT_EQ(att::refsIn(md), QStringList({a, b}));
  const QHash<QString, QString> labels = att::refLabelsIn(md);
  EXPECT_EQ(labels.value(a), QStringLiteral("shot [v2].png"));
  EXPECT_EQ(labels.value(b), QStringLiteral("notes"));
  const QString c = QStringLiteral("11111111111111111111111111111111.png");
  const QString out = att::remapRefs(md, {{a, c}});
  EXPECT_EQ(att::refsIn(out), QStringList({c, b}));
  EXPECT_FALSE(out.contains(QStringLiteral("attachments/") + a + QLatin1Char(')')));
}

// The notes preview and the description preview resolve heap's own
// "attachments/<id>" against the attachments folder (KNOW-25's base dir).
TEST(AttachmentRefs, TheRendererFindsTheStoredFile) {
  const QString base = QStringLiteral("C:/data/attachments");
  EXPECT_EQ(heap::md::resolveImage(QStringLiteral("attachments/0123456789abcdef0123456789abcdef.png"), base).url,
            QStringLiteral("file:///C:/data/attachments/0123456789abcdef0123456789abcdef.png"));
  // A plain relative path still means a path inside the folder.
  EXPECT_EQ(heap::md::resolveImage(QStringLiteral("a/x.png"), base).url, QStringLiteral("file:///C:/data/attachments/a/x.png"));
  // The prefix does not open a way out.
  EXPECT_TRUE(heap::md::resolveImage(QStringLiteral("attachments/../../state.json"), base).url.isEmpty());
}

TEST(AttachmentRefs, RunnableTypesNeedAConfirmation) {
  for(const char* n :
      {"setup.exe", "run.BAT", "x.ps1", "deploy.sh", "a.lnk", "b.js",  "noextension", "console.msc", "help.chm",  "app.jnlp",
       "addin.xll", "x.scf",   "s.wsc", "s.sct",     "q.iqy", "q.slk", "disk.iso",    "disk.vhdx",   "page.html", "macro.docm"}) {
    EXPECT_TRUE(att::needsOpenConfirmation(QString::fromLatin1(n))) << n;
  }
  for(const char* n : {"shot.png", "spec.pdf", "notes.txt", "data.csv", "log.zip", "Plan.DOCX", "demo.mp4"}) {
    EXPECT_FALSE(att::needsOpenConfirmation(QString::fromLatin1(n))) << n;
  }
}

TEST(AttachmentRefs, SizesReadLikeAFileManager) {
  EXPECT_EQ(att::formatSize(512, false), QStringLiteral("512 B"));
  EXPECT_EQ(att::formatSize(1536, false), QStringLiteral("1.5 KB"));
  EXPECT_EQ(att::formatSize(3 * 1024 * 1024, true), QStringLiteral("3.0 МБ"));
  EXPECT_EQ(att::formatSize(250LL * 1024 * 1024, false), QStringLiteral("250 MB"));
}

// ── Task field: both serializers ──

TEST(AttachmentSerialization, TaskAttachmentsSurviveBothSerializers) {
  Task t;
  t.id = QStringLiteral("ATT-1");
  t.statusChangedAt = QDateTime(QDate(2026, 9, 30), QTime(10, 0));
  t.attachments = {Attachment{
      QStringLiteral("0123456789abcdef0123456789abcdef.png"), QStringLiteral("a.png"), 6000000000LL, QStringLiteral("image/png")}};
  EXPECT_EQ(heap::state::taskFromJson(heap::state::taskToJson(t)), t);
  EXPECT_EQ(heap::sync::SyncSerializer::taskFromJson(heap::sync::SyncSerializer::taskToJson(t)), t);
  // A task without files writes no key: files from before this field are
  // unchanged, and no schema bump was needed.
  Task bare = t;
  bare.attachments.clear();
  EXPECT_FALSE(heap::state::taskToJson(bare).contains(QStringLiteral("attachments")));
}

// ── Through AppController ──

class AttachmentAppTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    wipe(app_->attachmentsDir());
    app_->notes()->reset({});
    app_->setNotesState(QString());
    app_->docPages()->reset({});
    app_->clearPendingUndo();
    ASSERT_TRUE(dir_.isValid());
    att::setShellForTesting([this](const QString& path, bool reveal) {
      (reveal ? revealed_ : opened_) << path;
      return true;
    });
  }

  void TearDown() override {
    att::setShellForTesting({});
    app_.reset();
  }

  QString write(const QString& rel, const QByteArray& bytes) const {
    const QString p = dir_.path() + QLatin1Char('/') + rel;
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    return p;
  }

  QString newTask(const QString& title) {
    QVariantMap d = app_->newTaskDraft(QStringLiteral("todo"));
    d["title"] = title;
    EXPECT_TRUE(app_->saveTask(d));
    return d.value("id").toString();
  }

  const Task* task(const QString& id) const {
    const int row = app_->tasks()->indexOfId(id);
    return row >= 0 ? &app_->tasks()->items().at(row) : nullptr;
  }

  QVariantList urls(const QStringList& paths) const {
    QVariantList out;
    for(const QString& p : paths) {
      out << QUrl::fromLocalFile(p);
    }
    return out;
  }

  att::Store store() const {
    return att::Store(app_->attachmentsDir());
  }

  std::unique_ptr<AppController> app_;
  QTemporaryDir dir_;
  QStringList opened_;
  QStringList revealed_;
};

TEST_F(AttachmentAppTest, AttachIsOneUndoStepAndDetachKeepsTheFile) {
  const QString id = newTask(QStringLiteral("attach probe"));
  const QString a = write(QStringLiteral("a.txt"), uniqueBytes("a"));
  const QString b = write(QStringLiteral("b.pdf"), uniqueBytes("b"));
  const int depth = app_->undoDepth();
  QStringList toasts;
  QObject::connect(app_.get(), &AppController::toast, [&](const QString& m, const QString&) {
    toasts << m;
  });

  EXPECT_EQ(app_->attachFilesToTask(id, urls({a, b})), 2) << toasts.join(QStringLiteral(" | ")).toStdString();
  ASSERT_EQ(task(id)->attachments.size(), 2);
  EXPECT_EQ(app_->undoDepth(), depth + 1);
  EXPECT_EQ(app_->tasks()->data(app_->tasks()->index(app_->tasks()->indexOfId(id)), TaskModel::AttachmentCountRole).toInt(), 2);
  const QString first = task(id)->attachments.at(0).id;
  EXPECT_EQ(task(id)->attachments.at(0).name, QStringLiteral("a.txt"));

  // Attaching the same file again adds nothing and records nothing.
  EXPECT_EQ(app_->attachFilesToTask(id, urls({a})), 1);
  EXPECT_EQ(task(id)->attachments.size(), 2);
  EXPECT_EQ(app_->undoDepth(), depth + 1);

  app_->undo();
  EXPECT_TRUE(task(id)->attachments.isEmpty());
  app_->redo();
  EXPECT_EQ(task(id)->attachments.size(), 2);

  ASSERT_TRUE(app_->removeTaskAttachment(id, first));
  EXPECT_EQ(task(id)->attachments.size(), 1);
  EXPECT_EQ(app_->undoDepth(), depth + 2);
  EXPECT_TRUE(store().contains(first)) << "detaching must not delete the file";
  app_->undo();
  EXPECT_EQ(task(id)->attachments.size(), 2);
  EXPECT_FALSE(app_->removeTaskAttachment(id, QStringLiteral("0123456789abcdef0123456789abcdef")));
}

TEST_F(AttachmentAppTest, TooLargeAndLinkedFilesAreRefusedWithAToast) {
  const QString id = newTask(QStringLiteral("refuse probe"));
  const QString target = write(QStringLiteral("real.txt"), uniqueBytes("real"));
  const QString link = dir_.path() + QStringLiteral("/real-link.lnk");
  ASSERT_TRUE(QFile::link(target, link));
  QStringList toasts;
  QObject::connect(app_.get(), &AppController::toast, [&](const QString& m, const QString&) {
    toasts << m;
  });
  EXPECT_EQ(app_->attachFilesToTask(id, urls({link, dir_.path() + QStringLiteral("/missing.txt")})), 0);
  EXPECT_TRUE(task(id)->attachments.isEmpty());
  EXPECT_EQ(toasts.size(), 2);
}

TEST_F(AttachmentAppTest, ANewTaskTakesItsFilesFromTheDraftAndAnEditKeepsThem) {
  const QVariantList stored = app_->importAttachments(urls({write(QStringLiteral("draft.md.txt"), uniqueBytes("d"))}));
  ASSERT_EQ(stored.size(), 1);
  QVariantMap d = app_->newTaskDraft(QStringLiteral("todo"));
  d["title"] = QStringLiteral("draft probe");
  d["attachments"] = stored;
  ASSERT_TRUE(app_->saveTask(d));
  const QString id = d.value("id").toString();
  ASSERT_EQ(task(id)->attachments.size(), 1);

  // The editor saves an existing task without the list; the files stay.
  QVariantMap edit = app_->taskById(id);
  edit["title"] = QStringLiteral("draft probe, renamed");
  ASSERT_TRUE(app_->saveTask(edit));
  EXPECT_EQ(task(id)->attachments.size(), 1);
}

TEST_F(AttachmentAppTest, AMissingFileShowsAsBroken) {
  const QString id = newTask(QStringLiteral("missing probe"));
  app_->attachFilesToTask(id, urls({write(QStringLiteral("m.txt"), uniqueBytes("m"))}));
  const QString attId = task(id)->attachments.at(0).id;
  ASSERT_TRUE(store().remove(attId));
  const QVariantList chips = app_->taskAttachments(id);
  ASSERT_EQ(chips.size(), 1);
  EXPECT_FALSE(chips.at(0).toMap().value("exists").toBool());
  EXPECT_EQ(chips.at(0).toMap().value("name").toString(), QStringLiteral("m.txt"));
  EXPECT_EQ(app_->openAttachment(attId), QStringLiteral("missing"));
  EXPECT_FALSE(app_->revealAttachment(attId));
  EXPECT_TRUE(opened_.isEmpty());
}

TEST_F(AttachmentAppTest, OpeningFollowsTheSafeOpenRules) {
  const QVariantList txt = app_->importAttachments(urls({write(QStringLiteral("readme.pdf"), uniqueBytes("t"))}));
  const QVariantList log = app_->importAttachments(urls({write(QStringLiteral("run.log"), uniqueBytes("l"))}));
  // A table or a text file is shown in the app (APP-315), never handed out.
  EXPECT_EQ(app_->openAttachment(log.at(0).toMap().value("id").toString()), QStringLiteral("view"));
  EXPECT_TRUE(opened_.isEmpty());
  const QVariantList sh = app_->importAttachments(urls({write(QStringLiteral("deploy.sh"), uniqueBytes("s"))}));
  const QString txtId = txt.at(0).toMap().value("id").toString();
  const QString shId = sh.at(0).toMap().value("id").toString();
  EXPECT_EQ(app_->openAttachment(QStringLiteral("../state.json")), QStringLiteral("invalid"));
  EXPECT_EQ(app_->openAttachment(txtId), QStringLiteral("opened"));
  EXPECT_EQ(app_->openAttachment(shId), QStringLiteral("confirm"));
  EXPECT_EQ(opened_.size(), 1) << "a script is not handed to the shell before the user confirms";
  EXPECT_EQ(app_->openAttachment(shId, true), QStringLiteral("opened"));
  EXPECT_EQ(opened_.size(), 2);
  EXPECT_TRUE(app_->revealAttachment(txtId));
  EXPECT_EQ(revealed_.size(), 1);
}

TEST_F(AttachmentAppTest, NoteReferencesBecomeChips) {
  const QVariantList stored = app_->importAttachments(urls({write(QStringLiteral("diagram.png"), uniqueBytes("png"))}));
  const QString ref = stored.at(0).toMap().value("ref").toString();
  EXPECT_TRUE(ref.startsWith(QStringLiteral("![diagram.png](attachments/"))) << ref.toStdString();
  const QVariantList chips = app_->markdownAttachments(QStringLiteral("text\n\n") + ref + QStringLiteral("\n"));
  ASSERT_EQ(chips.size(), 1);
  EXPECT_EQ(chips.at(0).toMap().value("name").toString(), QStringLiteral("diagram.png"));
  EXPECT_TRUE(chips.at(0).toMap().value("exists").toBool());
}

TEST_F(AttachmentAppTest, PasteSavesABareImageAsPngAndLeavesTextAlone) {
  QImage img(8, 8, QImage::Format_ARGB32);
  img.fill(Qt::red);
  QGuiApplication::clipboard()->setImage(img);
  ASSERT_TRUE(app_->clipboardHasAttachment());
  const QVariantList added = app_->importClipboardAttachments();
  ASSERT_EQ(added.size(), 1);
  const QVariantMap a = added.at(0).toMap();
  EXPECT_TRUE(a.value("id").toString().endsWith(QStringLiteral(".png")));
  EXPECT_EQ(a.value("mime").toString(), QStringLiteral("image/png"));
  EXPECT_TRUE(a.value("ref").toString().startsWith(QStringLiteral("![")));
  EXPECT_TRUE(QImage(store().pathFor(a.value("id").toString())).size() == QSize(8, 8));

  // A picture with its address beside it (a browser, a messenger) is still
  // the picture (dogfood 2026-10-30).
  auto* withUrl = new QMimeData;
  withUrl->setImageData(img);
  withUrl->setText(QStringLiteral("https://files.example/s/9f2c1/image.png"));
  withUrl->setHtml(QStringLiteral("<img src=\"https://files.example/s/9f2c1/image.png\">"));
  QGuiApplication::clipboard()->setMimeData(withUrl);
  EXPECT_TRUE(app_->clipboardHasAttachment());
  EXPECT_EQ(app_->importClipboardAttachments().size(), 1);

  // A spreadsheet's cells come with a picture of themselves: the cells win.
  auto* cells = new QMimeData;
  cells->setImageData(img);
  cells->setText(QStringLiteral("A1\tB1\nA2\tB2"));
  QGuiApplication::clipboard()->setMimeData(cells);
  EXPECT_FALSE(app_->clipboardHasAttachment());
  EXPECT_TRUE(app_->importClipboardAttachments().isEmpty());
  auto* cell = new QMimeData;
  cell->setImageData(img);
  cell->setText(QStringLiteral("42"));
  cell->setHtml(QStringLiteral("<html xmlns:x=\"urn:schemas-microsoft-com:office:excel\"><table><tr><td>42</td></tr></table></html>"));
  QGuiApplication::clipboard()->setMimeData(cell);
  EXPECT_FALSE(app_->clipboardHasAttachment());

  // Files copied in a file manager are stored as they are.
  auto* files = new QMimeData;
  files->setUrls({QUrl::fromLocalFile(write(QStringLiteral("copied.log"), uniqueBytes("log")))});
  QGuiApplication::clipboard()->setMimeData(files);
  ASSERT_TRUE(app_->clipboardHasAttachment());
  const QVariantList copied = app_->importClipboardAttachments();
  ASSERT_EQ(copied.size(), 1);
  EXPECT_EQ(copied.at(0).toMap().value("name").toString(), QStringLiteral("copied.log"));
  QGuiApplication::clipboard()->clear();
}

TEST_F(AttachmentAppTest, ProfileExportCarriesTheFilesAndImportBringsThemBack) {
  const QString id = newTask(QStringLiteral("export probe"));
  const QByteArray taskBytes = uniqueBytes("task file");
  app_->attachFilesToTask(id, urls({write(QStringLiteral("spec.pdf"), taskBytes)}));
  const QString taskAtt = task(id)->attachments.at(0).id;
  const QByteArray noteBytes = uniqueBytes("note file");
  const QVariantList noteAtt = app_->importAttachments(urls({write(QStringLiteral("note.txt"), noteBytes)}));
  const QString noteAttId = noteAtt.at(0).toMap().value("id").toString();
  const QString noteId = app_->newNote(QStringLiteral("Export note"));
  app_->setNoteBody(noteId, QStringLiteral("see ") + noteAtt.at(0).toMap().value("ref").toString());

  const QString json = app_->exportActiveProfileJson();
  const QJsonObject root = QJsonDocument::fromJson(json.toUtf8()).object();
  const QJsonArray files = root.value("attachments").toArray();
  ASSERT_EQ(files.size(), 2);

  // Another machine: none of the files are there.
  ASSERT_TRUE(store().remove(taskAtt));
  ASSERT_TRUE(store().remove(noteAttId));
  ASSERT_TRUE(app_->importProfileFromJson(json, true).isEmpty());
  EXPECT_TRUE(store().contains(taskAtt));
  EXPECT_TRUE(store().contains(noteAttId));
  QFile f(store().pathFor(taskAtt));
  ASSERT_TRUE(f.open(QIODevice::ReadOnly));
  EXPECT_EQ(f.readAll(), taskBytes);
  // The import is a profile beside the exported one, so its copy of the task
  // has an id of its own (PLAT-9): find it by what it carries.
  const Task* imported = nullptr;
  for(const Task& t : app_->tasks()->items()) {
    if(t.title == QStringLiteral("export probe") && !t.attachments.isEmpty() && t.attachments.at(0).id == taskAtt) {
      imported = &t;
    }
  }
  ASSERT_NE(imported, nullptr);
  EXPECT_NE(imported->id, id);
  ASSERT_EQ(imported->attachments.size(), 1);
  EXPECT_EQ(imported->attachments.at(0).id, taskAtt);
  EXPECT_EQ(imported->attachments.at(0).name, QStringLiteral("spec.pdf"));
}

TEST_F(AttachmentAppTest, ImportedBytesThatDoNotMatchTheirIdAreRekeyed) {
  const QString claimed = QStringLiteral("0123456789abcdef0123456789abcdef.txt");
  QJsonObject task;
  task["id"] = QStringLiteral("REKEY-1");
  task["title"] = QStringLiteral("rekey");
  task["status"] = QStringLiteral("todo");
  task["priority"] = QStringLiteral("P2");
  task["desc"] = QStringLiteral("[f](attachments/%1)").arg(claimed);
  QJsonObject a;
  a["id"] = claimed;
  a["name"] = QStringLiteral("f.txt");
  a["size"] = 3;
  a["mime"] = QStringLiteral("text/plain");
  task["attachments"] = QJsonArray({a});
  QJsonObject profile;
  profile["id"] = QStringLiteral("rekey");
  profile["name"] = QStringLiteral("Rekey");
  profile["tasks"] = QJsonArray({task});
  const QByteArray bytes = uniqueBytes("real bytes");
  QJsonObject blob;
  blob["id"] = claimed;
  blob["name"] = QStringLiteral("../../f.txt");  // a name is never a path
  blob["data"] = QString::fromLatin1(bytes.toBase64());
  QJsonObject bad;
  bad["id"] = QStringLiteral("../../evil");
  bad["data"] = QStringLiteral("!!!not base64!!!");
  QJsonObject root;
  root["profile"] = profile;
  root["attachments"] = QJsonArray({blob, bad});
  ASSERT_TRUE(app_->importProfileFromJson(QString::fromUtf8(QJsonDocument(root).toJson()), true).isEmpty());

  // REKEY-1 unless an earlier run of this suite left one in the profile, in
  // which case the import gets the next id (PLAT-9).
  const Task* t = nullptr;
  for(const Task& x : app_->tasks()->items()) {
    if(x.title == QStringLiteral("rekey")) {
      t = &x;
    }
  }
  ASSERT_NE(t, nullptr);
  ASSERT_EQ(t->attachments.size(), 1);
  const QString real = t->attachments.at(0).id;
  EXPECT_NE(real, claimed);
  EXPECT_TRUE(store().contains(real));
  EXPECT_FALSE(store().contains(claimed));
  EXPECT_TRUE(t->desc.contains(real));
  // Nothing escaped the folder.
  EXPECT_FALSE(QFileInfo::exists(QDir(app_->attachmentsDir()).filePath(QStringLiteral("../../evil"))));
  EXPECT_FALSE(QFileInfo::exists(QDir(app_->attachmentsDir()).filePath(QStringLiteral("../../f.txt"))));
}

TEST_F(AttachmentAppTest, AnExportOverTheCapKeepsTheDataAndSaysWhatItLeftOut) {
  const QString id = newTask(QStringLiteral("cap probe"));
  QByteArray big(static_cast<int>(att::kMaxExportBytes) + 1024, 'z');
  big.append(uniqueBytes());
  app_->attachFilesToTask(id, urls({write(QStringLiteral("big.bin"), big)}));
  ASSERT_EQ(task(id)->attachments.size(), 1);
  QStringList warnings;
  QObject::connect(app_.get(), &AppController::toast, [&](const QString& m, const QString& kind) {
    if(kind == QStringLiteral("warning")) {
      warnings << m;
    }
  });
  const QJsonObject root = QJsonDocument::fromJson(app_->exportActiveProfileJson().toUtf8()).object();
  EXPECT_FALSE(root.contains(QStringLiteral("attachments")));
  EXPECT_EQ(root.value("attachmentsOmitted").toInt(), 1);
  EXPECT_EQ(warnings.size(), 1);
  // The task still says which file it had.
  bool found = false;
  for(const QJsonValue& v : root.value("profile").toObject().value("tasks").toArray()) {
    if(v.toObject().value("id").toString() == id) {
      found = v.toObject().value("attachments").toArray().size() == 1;
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(AttachmentAppTest, TheNotesFolderCarriesItsFilesBothWays) {
  const QByteArray bytes = uniqueBytes("vault image");
  const QVariantList stored = app_->importAttachments(urls({write(QStringLiteral("src/pic.png"), bytes)}));
  const QString attId = stored.at(0).toMap().value("id").toString();
  const QString noteId = app_->newNote(QStringLiteral("Vault note"));
  app_->setNoteBody(noteId, QStringLiteral("look: ") + stored.at(0).toMap().value("ref").toString());

  QDir().mkpath(dir_.path() + QStringLiteral("/out"));
  const QVariantMap r = app_->exportNotesFolder(QUrl::fromLocalFile(dir_.path() + QStringLiteral("/out")), QStringLiteral("vault"));
  ASSERT_FALSE(r.contains("error"));
  EXPECT_EQ(r.value("attachments").toInt(), 1);
  const QString copy = dir_.path() + QStringLiteral("/out/vault/attachments/") + attId;
  ASSERT_TRUE(QFileInfo::exists(copy));
  EXPECT_TRUE(QFileInfo(copy).isWritable()) << "the exported copy belongs to the user";

  // Somewhere else: the store is empty, the folder brings the file back.
  app_->notes()->reset({});
  app_->setNotesState(QString());
  ASSERT_TRUE(store().remove(attId));
  const QVariantMap in = app_->importNotesFolder(QUrl::fromLocalFile(dir_.path() + QStringLiteral("/out/vault")));
  ASSERT_FALSE(in.contains("error"));
  EXPECT_EQ(in.value("attachments").toInt(), 1);
  EXPECT_TRUE(store().contains(attId));
  ASSERT_EQ(app_->notes()->rowCount(), 1);
  EXPECT_TRUE(app_->notes()->items().at(0).body.contains(QStringLiteral("attachments/") + attId));
}

// KNOW-10 (audit 2026-09-30): a note in a subfolder links its files relative
// to itself, so the export opens with pictures in any other editor, and the
// folder still comes back into heap unchanged.
TEST_F(AttachmentAppTest, Know10_ANoteInASubfolderLinksUpToTheAttachments) {
  const QByteArray bytes = uniqueBytes("nested vault image");
  const QVariantList stored = app_->importAttachments(urls({write(QStringLiteral("src/up.png"), bytes)}));
  const QString attId = stored.at(0).toMap().value("id").toString();
  const QString ref = stored.at(0).toMap().value("ref").toString();
  const QString rootNote = app_->newNote(QStringLiteral("Root"));
  app_->setNoteBody(rootNote, QStringLiteral("top ") + ref);
  const QString nested = app_->newNote(QStringLiteral("Relative"), QStringLiteral("team/deep"));
  app_->setNoteBody(nested, QStringLiteral("nested ") + ref + QStringLiteral("\nmentioned: attachments/") + attId);

  QDir().mkpath(dir_.path() + QStringLiteral("/out"));
  const QVariantMap r = app_->exportNotesFolder(QUrl::fromLocalFile(dir_.path() + QStringLiteral("/out")), QStringLiteral("vault"));
  ASSERT_FALSE(r.contains("error"));
  const QString vault = dir_.path() + QStringLiteral("/out/vault/");
  const auto read = [](const QString& p) {
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
  };
  const QString top = read(vault + QStringLiteral("Root.md"));
  const QString deep = read(vault + QStringLiteral("team/deep/Relative.md"));
  EXPECT_TRUE(top.contains(QStringLiteral("](attachments/") + attId + QLatin1Char(')'))) << top.toStdString();
  EXPECT_TRUE(deep.contains(QStringLiteral("](../../attachments/") + attId + QLatin1Char(')'))) << deep.toStdString();
  // Text that is not a link is not a path; it stays as written.
  EXPECT_TRUE(deep.contains(QStringLiteral("mentioned: attachments/") + attId)) << deep.toStdString();
  // Relative to the file, the link names the exported copy.
  EXPECT_TRUE(QFileInfo::exists(vault + QStringLiteral("team/deep/../../attachments/") + attId));

  // Imported back over the same notes: nothing changed on either side.
  const QVariantMap again = app_->importNotesFolder(QUrl::fromLocalFile(vault));
  EXPECT_EQ(again.value("unchanged").toInt(), 2) << again.value("conflicts").toInt();
  EXPECT_EQ(app_->noteBody(nested), QStringLiteral("nested ") + ref + QStringLiteral("\nmentioned: attachments/") + attId);
}

TEST_F(AttachmentAppTest, AnotherEditorsVaultLinksBecomeAttachments) {
  const QByteArray img = uniqueBytes("obsidian image");
  write(QStringLiteral("vault/assets/shot 1.png"), img);
  write(QStringLiteral("vault/outside-target.txt"), "x");
  write(QStringLiteral("elsewhere/secret.txt"), uniqueBytes("secret"));
  write(QStringLiteral("vault/team/Plan.md"),
        "![shot](../assets/shot%201.png)\n\n[other](Other.md) [out](../../elsewhere/secret.txt) [web](https://example.com/a.png)\n");
  const QVariantMap preview = app_->previewNotesFolder(QUrl::fromLocalFile(dir_.path() + QStringLiteral("/vault")));
  EXPECT_EQ(preview.value("attachments").toInt(), 1);
  EXPECT_TRUE(store().ids().isEmpty()) << "a preview stores nothing";

  app_->importNotesFolder(QUrl::fromLocalFile(dir_.path() + QStringLiteral("/vault")));
  ASSERT_EQ(app_->notes()->rowCount(), 1);
  const QString body = app_->notes()->items().at(0).body;
  const QStringList refs = att::refsIn(body);
  ASSERT_EQ(refs.size(), 1) << body.toStdString();
  EXPECT_TRUE(refs.at(0).endsWith(QStringLiteral(".png")));
  EXPECT_TRUE(store().contains(refs.at(0)));
  // Other notes, the web and anything outside the folder are left as written.
  EXPECT_TRUE(body.contains(QStringLiteral("(Other.md)")));
  EXPECT_TRUE(body.contains(QStringLiteral("(../../elsewhere/secret.txt)")));
  EXPECT_TRUE(body.contains(QStringLiteral("(https://example.com/a.png)")));
  EXPECT_EQ(store().ids().size(), 1);
}

TEST_F(AttachmentAppTest, CleanupDeletesOnlyWhatNothingPointsAt) {
  const QString id = newTask(QStringLiteral("cleanup probe"));
  const QString kept = write(QStringLiteral("kept.txt"), uniqueBytes("kept"));
  const QString detached = write(QStringLiteral("detached.txt"), uniqueBytes("detached"));
  const QString inNote = write(QStringLiteral("in-note.txt"), uniqueBytes("note"));
  app_->attachFilesToTask(id, urls({kept, detached}));
  const QString detachedId = task(id)->attachments.at(1).id;
  const QVariantList noteAtt = app_->importAttachments(urls({inNote}));
  const QString noteId = app_->newNote(QStringLiteral("Cleanup note"));
  app_->setNoteBody(noteId, noteAtt.at(0).toMap().value("ref").toString());
  const QVariantList orphan = app_->importAttachments(urls({write(QStringLiteral("orphan.bin"), uniqueBytes("orphan"))}));
  const QString orphanId = orphan.at(0).toMap().value("id").toString();

  app_->removeTaskAttachment(id, detachedId);
  // Undo can still bring the detached file back, so it is in use.
  QVariantMap unused = app_->unusedAttachments();
  EXPECT_EQ(unused.value("count").toInt(), 1);
  EXPECT_EQ(static_cast<qint64>(unused.value("bytes").toDouble()), store().sizeOf(orphanId));

  app_->clearPendingUndo();
  unused = app_->unusedAttachments();
  EXPECT_EQ(unused.value("count").toInt(), 2);
  const QVariantMap done = app_->cleanUpUnusedAttachments();
  EXPECT_EQ(done.value("count").toInt(), 2);
  EXPECT_FALSE(store().contains(orphanId));
  EXPECT_FALSE(store().contains(detachedId));
  EXPECT_TRUE(store().contains(task(id)->attachments.at(0).id));
  EXPECT_TRUE(store().contains(noteAtt.at(0).toMap().value("id").toString()));
  EXPECT_EQ(app_->unusedAttachments().value("count").toInt(), 0);
}

// 2026-09-30 audit, TASKS-16: a backup holds only state.json, so the cleanup
// used to delete a file only a backup still named, and restoring that backup
// brought the attachment back broken.
TEST_F(AttachmentAppTest, Audit0930Tasks16_CleanupKeepsAFileABackupNames) {
  const QString id = newTask(QStringLiteral("backup probe"));
  app_->attachFilesToTask(id, urls({write(QStringLiteral("Shot.PNG"), uniqueBytes("shot"))}));
  const QString attId = task(id)->attachments.at(0).id;
  app_->flushSave();
  const QString backups = app_->dataDir() + QStringLiteral("/backups");
  QDir().mkpath(backups);
  ASSERT_TRUE(QFile::copy(app_->dataDir() + QStringLiteral("/state.json"), backups + QStringLiteral("/state-20260101-000000.json")));

  app_->removeTaskAttachment(id, attId);
  app_->clearPendingUndo();

  EXPECT_EQ(app_->unusedAttachments().value("count").toInt(), 0);
  app_->cleanUpUnusedAttachments();
  EXPECT_TRUE(store().contains(attId));
  QFile::remove(backups + QStringLiteral("/state-20260101-000000.json"));
}

// 2026-09-30 audit, KNOW-3: a link taken out of a note can come back with the
// editor's own Ctrl+Z, which the app's undo stack knows nothing about. The
// cleanup used to delete the file in between, and the link came back to
// nothing — for a pasted screenshot, the only copy there was.
TEST_F(AttachmentAppTest, Audit0930Know3_CleanupKeepsAFileUnlinkedFromTextThisSession) {
  const QVariantList added = app_->importAttachments(urls({write(QStringLiteral("plain.png"), uniqueBytes("png"))}), true);
  ASSERT_EQ(added.size(), 1);
  const QString attId = added.at(0).toMap().value("id").toString();
  const QString ref = added.at(0).toMap().value("ref").toString();
  app_->newNote(QStringLiteral("Att"));
  app_->setNotesState(QStringLiteral("# Att\n\nIntro\n\n") + ref + QStringLiteral("\n"));
  app_->setNotesState(QStringLiteral("# Att\n\nIntro\n\n"));  // the chip's Remove, an editor edit
  app_->clearPendingUndo();

  EXPECT_EQ(app_->unusedAttachments().value("count").toInt(), 0);
  app_->cleanUpUnusedAttachments();
  EXPECT_TRUE(store().contains(attId));
  // What the editor's Ctrl+Z puts back still finds its file.
  app_->setNotesState(QStringLiteral("# Att\n\nIntro\n\n") + ref + QStringLiteral("\n"));
  EXPECT_FALSE(app_->attachmentUrl(attId).isEmpty());
  const QVariantList chips = app_->markdownAttachments(app_->notesState());
  ASSERT_EQ(chips.size(), 1);
  EXPECT_TRUE(chips.at(0).toMap().value("exists").toBool());
}

// Linked and unlinked before the editor handed its text over: the app never
// saw the link, but the file was stored for the text, so it is kept as well.
TEST_F(AttachmentAppTest, Audit0930Know3_AFileStoredForTextIsKeptEvenIfTheLinkNeverArrived) {
  const QVariantList added = app_->importAttachments(urls({write(QStringLiteral("quick.png"), uniqueBytes("quick"))}), true);
  ASSERT_EQ(added.size(), 1);
  const QString attId = added.at(0).toMap().value("id").toString();
  app_->cleanUpUnusedAttachments();
  EXPECT_TRUE(store().contains(attId));
}

// The next start has no editor history to protect: such a file is cleaned up
// then, like any other orphan.
TEST_F(AttachmentAppTest, Audit0930Know3_TheNextSessionCleansItUp) {
  const QVariantList added = app_->importAttachments(urls({write(QStringLiteral("later.png"), uniqueBytes("later"))}), true);
  ASSERT_EQ(added.size(), 1);
  const QString attId = added.at(0).toMap().value("id").toString();
  const QString ref = added.at(0).toMap().value("ref").toString();
  app_->newNote(QStringLiteral("Later"));
  app_->setNotesState(QStringLiteral("# Later\n\n") + ref + QStringLiteral("\n"));
  app_->setNotesState(QStringLiteral("# Later\n\n"));
  app_->flushSave();
  app_.reset();

  app_ = std::make_unique<AppController>();
  app_->clearPendingUndo();
  ASSERT_TRUE(store().contains(attId));
  const QVariantMap done = app_->cleanUpUnusedAttachments();
  EXPECT_FALSE(store().contains(attId)) << "count " << done.value("count").toInt();
}

}  // namespace

int main(int argc, char** argv) {
  QStandardPaths::setTestModeEnabled(true);
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
