// Regressions from the 2026-09-30 audit, notes / docs / markdown area.
//
// Each block names the audit item it pins. The pure halves (vault planning,
// markdown ops) are covered next to their code; these go through a real
// AppController and, for the vault, through the disk.

#include "AppController.h"
#include "CodeHighlighter.h"
#include "Models.h"

#include "notes/ChecklistItems.h"
#include "notes/NoteGraph.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QUrl>

#include <gtest/gtest.h>

namespace {

class KnowAuditTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    app_->notes()->reset({});
    app_->setNotesState(QString());
    // The test profile persists between runs; start from no pages either.
    app_->docPages()->reset({});
    ASSERT_TRUE(dir_.isValid());
  }

  void TearDown() override {
    app_.reset();
  }

  QString path(const QString& rel) const {
    return dir_.path() + QLatin1Char('/') + rel;
  }

  void writeFile(const QString& rel, const QByteArray& bytes) const {
    QDir().mkpath(QFileInfo(path(rel)).absolutePath());
    QFile f(path(rel));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
  }

  QString readFile(const QString& full) const {
    QFile f(full);
    if(!f.open(QIODevice::ReadOnly)) {
      return {};
    }
    return QString::fromUtf8(f.readAll());
  }

  const Note* byTitle(const QString& title) const {
    for(const Note& n : app_->notes()->items()) {
      if(n.title == title) {
        return &n;
      }
    }
    return nullptr;
  }

  QUrl url(const QString& rel = QString()) const {
    return QUrl::fromLocalFile(rel.isEmpty() ? dir_.path() : path(rel));
  }

  QTemporaryDir dir_;
  std::unique_ptr<AppController> app_;
};

}  // namespace

// ── KNOW-1: a re-import never silently overwrites an edit made in heap ──

TEST_F(KnowAuditTest, Know1_ReimportKeepsANoteEditedInHeap) {
  writeFile(QStringLiteral("vault/Standup.md"), "from disk");
  app_->importNotesFolder(url(QStringLiteral("vault")));
  ASSERT_EQ(app_->notes()->rowCount(), 1);
  const QString id = app_->notes()->items().at(0).id;

  app_->setNoteBody(id, QStringLiteral("edited in heap"));
  const QVariantMap r = app_->importNotesFolder(url(QStringLiteral("vault")));

  EXPECT_EQ(r.value("kept").toInt(), 1);
  EXPECT_EQ(app_->noteBody(id), QStringLiteral("edited in heap"));
  EXPECT_EQ(app_->notes()->rowCount(), 1);
}

TEST_F(KnowAuditTest, Know1_EditedOnBothSidesKeepsBothVersions) {
  writeFile(QStringLiteral("vault/Standup.md"), "v1");
  app_->importNotesFolder(url(QStringLiteral("vault")));
  const QString id = app_->notes()->items().at(0).id;

  app_->setNoteBody(id, QStringLiteral("heap side"));
  writeFile(QStringLiteral("vault/Standup.md"), "disk side");
  const QVariantMap r = app_->importNotesFolder(url(QStringLiteral("vault")));

  EXPECT_EQ(r.value("conflicts").toInt(), 1);
  ASSERT_EQ(app_->notes()->rowCount(), 2);
  EXPECT_EQ(app_->noteBody(id), QStringLiteral("heap side"));
  bool sawDisk = false;
  for(const Note& n : app_->notes()->items()) {
    sawDisk = sawDisk || n.body == QStringLiteral("disk side");
  }
  EXPECT_TRUE(sawDisk);
}

// The editor debounces: what is typed but not yet written must count as heap's.
TEST_F(KnowAuditTest, Know1_UnflushedTypingCountsAsAnEdit) {
  writeFile(QStringLiteral("vault/Standup.md"), "v1");
  app_->importNotesFolder(url(QStringLiteral("vault")));
  const QString id = app_->notes()->items().at(0).id;
  app_->setActiveNoteId(id);

  // What NotesView does on aboutToChangeActiveNote: flush the text field.
  QObject::connect(app_.get(), &AppController::aboutToChangeActiveNote, app_.get(), [this]() {
    app_->setNotesState(QStringLiteral("typed a moment ago"));
  });
  writeFile(QStringLiteral("vault/Standup.md"), "v2 on disk");
  app_->importNotesFolder(url(QStringLiteral("vault")));

  EXPECT_EQ(app_->noteBody(id), QStringLiteral("typed a moment ago"));
}

TEST_F(KnowAuditTest, Know1_TheWholeImportIsOneUndoStep) {
  writeFile(QStringLiteral("vault/A.md"), "a");
  writeFile(QStringLiteral("vault/B.md"), "b");
  writeFile(QStringLiteral("vault/sub/C.md"), "c");
  const int before = app_->undoDepth();

  app_->importNotesFolder(url(QStringLiteral("vault")));
  ASSERT_EQ(app_->notes()->rowCount(), 3);
  EXPECT_EQ(app_->undoDepth(), before + 1);
  EXPECT_TRUE(app_->hasPendingUndo());

  app_->undo();
  EXPECT_EQ(app_->notes()->rowCount(), 0);
}

TEST_F(KnowAuditTest, Know1_PreviewChangesNothing) {
  writeFile(QStringLiteral("vault/A.md"), "a");
  const QVariantMap r = app_->previewNotesFolder(url(QStringLiteral("vault")));
  EXPECT_EQ(r.value("imported").toInt(), 1);
  EXPECT_EQ(r.value("files").toInt(), 1);
  EXPECT_EQ(app_->notes()->rowCount(), 0);
}

// ── KNOW-2: heap's own export comes back whole ──

TEST_F(KnowAuditTest, Know2_DuplicateTitlesSurviveExportAndReimport) {
  const QString a = app_->newNote(QStringLiteral("Meeting"));
  const QString b = app_->newNote(QStringLiteral("Meeting"));
  const QString c = app_->newNote(QStringLiteral("meeting"));
  app_->setNoteBody(a, QStringLiteral("alpha"));
  app_->setNoteBody(b, QStringLiteral("bravo"));
  app_->setNoteBody(c, QStringLiteral("charlie"));

  const QString out = app_->exportNotesFolder(url(), QStringLiteral("export")).value("folder").toString();
  // Into an empty profile: every file becomes its own note again.
  app_->notes()->reset({});
  app_->setNotesState(QString());
  app_->importNotesFolder(QUrl::fromLocalFile(out));
  ASSERT_EQ(app_->notes()->rowCount(), 3);
  EXPECT_EQ(app_->noteBody(a), QStringLiteral("alpha"));
  EXPECT_EQ(app_->noteBody(b), QStringLiteral("bravo"));
  EXPECT_EQ(app_->noteBody(c), QStringLiteral("charlie"));

  // And into the same profile: nothing doubles, nothing is merged.
  const QVariantMap again = app_->importNotesFolder(QUrl::fromLocalFile(out));
  EXPECT_EQ(app_->notes()->rowCount(), 3);
  EXPECT_EQ(again.value("unchanged").toInt(), 3);
}

// ── KNOW-3: export never overwrites ──

TEST_F(KnowAuditTest, Know3_ExportNeverOverwritesAnExistingFile) {
  writeFile(QStringLiteral("team/Meeting.md"), "the team's own notes");
  app_->newNote(QStringLiteral("Meeting"));

  const QVariantMap r = app_->exportNotesFolder(url(QStringLiteral("team")));

  EXPECT_EQ(readFile(path(QStringLiteral("team/Meeting.md"))), QStringLiteral("the team's own notes"));
  EXPECT_EQ(r.value("written").toInt(), 1);
  const QString folder = r.value("folder").toString();
  EXPECT_TRUE(QFile::exists(QDir(folder).filePath(QStringLiteral("Meeting.md"))));
}

TEST_F(KnowAuditTest, Know3_ATakenExportNameGetsASuffix) {
  writeFile(QStringLiteral("out/keep.md"), "keep");
  app_->newNote(QStringLiteral("One"));

  const QString folder = app_->exportNotesFolder(url(), QStringLiteral("out")).value("folder").toString();

  EXPECT_NE(QDir::fromNativeSeparators(folder), path(QStringLiteral("out")));
  EXPECT_EQ(readFile(path(QStringLiteral("out/keep.md"))), QStringLiteral("keep"));
}

// ── KNOW-9 / KNOW-10: what an import refuses ──

TEST_F(KnowAuditTest, Know9_ImportDoesNotFollowAJunctionOutOfTheVault) {
#ifdef Q_OS_WIN
  writeFile(QStringLiteral("vault/Real.md"), "real");
  writeFile(QStringLiteral("elsewhere/Outside.md"), "outside");
  const int rc = QProcess::execute(QStringLiteral("cmd"),
                                   {QStringLiteral("/c"),
                                    QStringLiteral("mklink"),
                                    QStringLiteral("/J"),
                                    QDir::toNativeSeparators(path(QStringLiteral("vault/link"))),
                                    QDir::toNativeSeparators(path(QStringLiteral("elsewhere")))});
  if(rc != 0) {
    GTEST_SKIP() << "could not create a junction";
  }
  // And one pointing back at the vault itself, the loop the audit hit.
  QProcess::execute(QStringLiteral("cmd"),
                    {QStringLiteral("/c"),
                     QStringLiteral("mklink"),
                     QStringLiteral("/J"),
                     QDir::toNativeSeparators(path(QStringLiteral("vault/loop"))),
                     QDir::toNativeSeparators(path(QStringLiteral("vault")))});

  app_->importNotesFolder(url(QStringLiteral("vault")));

  ASSERT_EQ(app_->notes()->rowCount(), 1);
  EXPECT_EQ(app_->notes()->items().at(0).title, QStringLiteral("Real"));
#else
  GTEST_SKIP() << "junctions are Windows-only";
#endif
}

// KNOW-31 (audit 2026-09-30): a picture reached through a junction inside the
// vault is a file outside it and stays a plain link.
TEST_F(KnowAuditTest, Know31_AnImageBehindAJunctionIsNotTaken) {
#ifdef Q_OS_WIN
  writeFile(QStringLiteral("vault/N.md"), "![sec](linked/secret.png)\n");
  writeFile(QStringLiteral("outside/secret.png"), "PNGDATA");
  const int rc = QProcess::execute(QStringLiteral("cmd"),
                                   {QStringLiteral("/c"),
                                    QStringLiteral("mklink"),
                                    QStringLiteral("/J"),
                                    QDir::toNativeSeparators(path(QStringLiteral("vault/linked"))),
                                    QDir::toNativeSeparators(path(QStringLiteral("outside")))});
  if(rc != 0) {
    GTEST_SKIP() << "could not create a junction";
  }

  // The test profile is shared between runs, so judge what the import added.
  const QStringList before = QDir(app_->attachmentsDir()).entryList(QDir::Files);

  app_->importNotesFolder(url(QStringLiteral("vault")));

  ASSERT_EQ(app_->notes()->rowCount(), 1);
  EXPECT_TRUE(app_->notes()->items().at(0).body.contains(QStringLiteral("](linked/secret.png)")))
      << app_->notes()->items().at(0).body.toStdString();
  EXPECT_EQ(QDir(app_->attachmentsDir()).entryList(QDir::Files), before);
#else
  GTEST_SKIP() << "junctions are Windows-only";
#endif
}

TEST_F(KnowAuditTest, Know10_BinaryAndHugeFilesAreSkippedWithAWarning) {
  writeFile(QStringLiteral("vault/Real.md"), "real");
  writeFile(QStringLiteral("vault/blob.md"), QByteArray("PK\x03\x04\0\0\0", 7));
  writeFile(QStringLiteral("vault/huge.md"), QByteArray(5 * 1024 * 1024, 'x'));

  const QVariantMap r = app_->importNotesFolder(url(QStringLiteral("vault")));

  EXPECT_EQ(app_->notes()->rowCount(), 1);
  EXPECT_EQ(r.value("skipped").toInt(), 2);
  EXPECT_EQ(r.value("warnings").toStringList().size(), 2);
}

TEST_F(KnowAuditTest, Know10_ObsidianTagsSurviveARoundTrip) {
  writeFile(QStringLiteral("vault/Plan.md"), "---\ntags: [work, q3]\naliases:\n  - Plan B\n---\nbody");
  app_->importNotesFolder(url(QStringLiteral("vault")));
  const QString out = app_->exportNotesFolder(url(), QStringLiteral("back")).value("folder").toString();
  const QString text = readFile(QDir(out).filePath(QStringLiteral("Plan.md")));
  EXPECT_TRUE(text.contains(QStringLiteral("tags: [work, q3]")));
  EXPECT_TRUE(text.contains(QStringLiteral("  - Plan B")));
}

// ── PLAT-21: a profile export carries what was just typed ──

TEST_F(KnowAuditTest, Plat21_ProfileExportIncludesTheLatestNoteAndPageEdits) {
  const QString id = app_->newNote(QStringLiteral("Fresh"));
  app_->setNoteBody(id, QStringLiteral("written a moment ago"));
  const QString page = app_->newDocPage(QStringLiteral("Runbook"));
  app_->setDocPageBody(page, QStringLiteral("page body"));

  // The editors flush on request; model one that still has keystrokes.
  QObject::connect(app_.get(), &AppController::flushEditorsRequested, app_.get(), [this]() {
    app_->setNotesState(QStringLiteral("typed, not yet saved"));
  });

  const QJsonObject root = QJsonDocument::fromJson(app_->exportActiveProfileJson().toUtf8()).object();
  const QJsonObject prof = root.value("profile").toObject();
  bool sawNote = false;
  for(const auto& v : prof.value("notes").toArray()) {
    sawNote = sawNote || v.toObject().value("body").toString() == QStringLiteral("typed, not yet saved");
  }
  bool sawPage = false;
  for(const auto& v : prof.value("docPages").toArray()) {
    sawPage = sawPage || v.toObject().value("body").toString() == QStringLiteral("page body");
  }
  EXPECT_TRUE(sawNote);
  EXPECT_TRUE(sawPage);
}

// ── KNOW-11 / C: rename keeps links, H1 and undo ──

TEST_F(KnowAuditTest, Know11_RenameRewritesLinksAndHeadingAndIsUndoable) {
  const QString target = app_->newNote(QStringLiteral("Plan"));
  const QString other = app_->newNote(QStringLiteral("Diary"));
  app_->setNoteBody(other, QStringLiteral("see [[Plan]] and [[plan#Risks]]"));

  app_->renameNote(target, QStringLiteral("Roadmap"));

  EXPECT_EQ(app_->noteBody(other), QStringLiteral("see [[Roadmap]] and [[Roadmap#Risks]]"));
  EXPECT_TRUE(app_->noteBody(target).startsWith(QStringLiteral("# Roadmap")));
  app_->undo();
  EXPECT_EQ(app_->noteBody(other), QStringLiteral("see [[Plan]] and [[plan#Risks]]"));
  const int row = app_->notes()->indexOfId(target);
  EXPECT_EQ(app_->notes()->items().at(row).title, QStringLiteral("Plan"));
}

// KNOW-7 (audit 2026-09-30): two "Meeting" notes in different folders; a
// rename of one must not repoint links that resolve to the other.
TEST_F(KnowAuditTest, Know7_RenameLeavesLinksToASameTitledNoteElsewhere) {
  const QString a = app_->newNote(QStringLiteral("Meeting"), QStringLiteral("teamA"));
  const QString b = app_->newNote(QStringLiteral("Meeting"), QStringLiteral("teamB"));
  const QString inB = app_->newNote(QStringLiteral("Notes B"), QStringLiteral("teamB"));
  const QString inA = app_->newNote(QStringLiteral("Notes A"), QStringLiteral("teamA"));
  app_->setNoteBody(inB, QStringLiteral("see [[Meeting]] and [[Meeting#Risks]]"));
  app_->setNoteBody(inA, QStringLiteral("see [[Meeting]]"));

  app_->renameNote(a, QStringLiteral("Meeting A renamed"));

  EXPECT_EQ(app_->noteBody(inB), QStringLiteral("see [[Meeting]] and [[Meeting#Risks]]"));
  EXPECT_EQ(app_->noteBody(inA), QStringLiteral("see [[Meeting A renamed]]"));
  EXPECT_EQ(heap::notes::backlinksTo(b, app_->notes()->items()).size(), 1);
}

// KNOW-6 (audit 2026-09-30): a rename to a title with '#' leaves links that
// still find the note, and a link to "C# basics" is a link to that note.
TEST_F(KnowAuditTest, Know6_HashAndBarTitlesLinkAndSurviveRename) {
  const QString target = app_->newNote(QStringLiteral("Plain target"));
  const QString csharp = app_->newNote(QStringLiteral("C# basics"));
  const QString other = app_->newNote(QStringLiteral("Linker"));
  app_->setNoteBody(other, QStringLiteral("[[Plain target]] and [[C\\# basics]]"));

  EXPECT_EQ(app_->resolveNoteLink(QStringLiteral("C\\# basics")).value("noteId").toString(), csharp);
  EXPECT_EQ(app_->resolveNoteLink(QStringLiteral("C# basics")).value("noteId").toString(), csharp);
  EXPECT_EQ(app_->backlinksToNote(csharp).size(), 1);

  app_->renameNote(target, QStringLiteral("Topic #1"));
  EXPECT_EQ(app_->noteBody(other), QStringLiteral("[[Topic \\#1]] and [[C\\# basics]]"));
  EXPECT_EQ(app_->resolveNoteLink(QStringLiteral("Topic \\#1")).value("noteId").toString(), target);
  EXPECT_EQ(app_->backlinksToNote(target).size(), 1);

  // A missing escaped link creates the note under its real name.
  const QString created = app_->createNoteForLink(QStringLiteral("A\\|B options"));
  const int row = app_->notes()->indexOfId(created);
  ASSERT_GE(row, 0);
  EXPECT_EQ(app_->notes()->items().at(row).title, QStringLiteral("A|B options"));
}

// KNOW-17 (audit 2026-09-30): quick capture while the editor still holds
// unflushed keystrokes keeps them.
TEST_F(KnowAuditTest, Know17_QuickCaptureFlushesTheEditorFirst) {
  const QString id = app_->newNote(QStringLiteral("QN target"));
  // The editor's pending text, written to notesState when asked to flush.
  QObject::connect(app_.get(), &AppController::aboutToChangeActiveNote, app_.get(), [this]() {
    if(!app_->notesState().contains(QStringLiteral("typing"))) {
      app_->setNotesState(QStringLiteral("# QN target\n\ntyping"));
    }
  });

  app_->appendNoteEntry(QStringLiteral("from quick capture"));

  // The typing is kept, and the entry goes on after it: the note was made a
  // moment ago, so the capture lands in it (APP-320).
  const QString body = app_->noteBody(id);
  EXPECT_TRUE(body.contains(QStringLiteral("typing"))) << body.toStdString();
  EXPECT_TRUE(body.contains(QStringLiteral("from quick capture"))) << body.toStdString();
  EXPECT_LT(body.indexOf(QStringLiteral("typing")), body.indexOf(QStringLiteral("from quick capture")));
}

TEST_F(KnowAuditTest, KnowC_TitleFollowsTheH1WhileTheyAgree) {
  const QString id = app_->newNote();
  app_->setNotesState(QStringLiteral("# Release checklist\n\nbody"));
  const int row = app_->notes()->indexOfId(id);
  EXPECT_EQ(app_->notes()->items().at(row).title, QStringLiteral("Release checklist"));
}

// APP-116: dropping one note on another merges them — the dropped note's text
// goes below the target's, its heading becomes a section, links to it follow
// it, and one undo puts both notes back.
TEST_F(KnowAuditTest, App116_MergeAppendsTheSourceBelowTheTarget) {
  const QString target = app_->newNote(QStringLiteral("Plan"));
  const QString source = app_->newNote(QStringLiteral("Ideas"));
  const QString other = app_->newNote(QStringLiteral("Diary"));
  app_->setNoteBody(target, QStringLiteral("# Plan\n\nship it\n\n"));
  app_->setNoteBody(source, QStringLiteral("# Ideas\n\ndark mode"));
  app_->setNoteBody(other, QStringLiteral("see [[Ideas]]"));
  app_->setActiveNoteId(source);

  ASSERT_TRUE(app_->mergeNotes(source, target));

  EXPECT_LT(app_->notes()->indexOfId(source), 0);
  EXPECT_EQ(app_->noteBody(target), QStringLiteral("# Plan\n\nship it\n\n## Ideas\n\ndark mode\n"));
  EXPECT_EQ(app_->noteBody(other), QStringLiteral("see [[Plan]]"));
  EXPECT_EQ(app_->activeNoteId(), target);
  EXPECT_EQ(app_->notesState(), app_->noteBody(target));

  app_->undo();
  ASSERT_GE(app_->notes()->indexOfId(source), 0);
  EXPECT_EQ(app_->noteBody(source), QStringLiteral("# Ideas\n\ndark mode"));
  EXPECT_EQ(app_->noteBody(target), QStringLiteral("# Plan\n\nship it\n\n"));
  EXPECT_EQ(app_->noteBody(other), QStringLiteral("see [[Ideas]]"));
}

TEST_F(KnowAuditTest, App116_ASourceWithoutHeadingGetsItsTitleAsOne) {
  const QString target = app_->newNote(QStringLiteral("Plan"));
  const QString source = app_->newNote(QStringLiteral("Loose"));
  app_->setNoteBody(source, QStringLiteral("just text"));
  ASSERT_TRUE(app_->mergeNotes(source, target));
  EXPECT_TRUE(app_->noteBody(target).endsWith(QStringLiteral("\n\n## Loose\n\njust text\n")));
}

TEST_F(KnowAuditTest, App116_MergeRefusesItselfAndUnknownNotes) {
  const QString a = app_->newNote(QStringLiteral("A"));
  EXPECT_FALSE(app_->mergeNotes(a, a));
  EXPECT_FALSE(app_->mergeNotes(a, QStringLiteral("note-missing")));
  EXPECT_FALSE(app_->mergeNotes(QStringLiteral("note-missing"), a));
  EXPECT_GE(app_->notes()->indexOfId(a), 0);
}

// APP-1: a note started by typing into the editor has no heading; it used
// to stay "Untitled note" for good. It takes its first line instead, follows
// it while typing, and stops once the user names it.
TEST_F(KnowAuditTest, App1_AnUntitledNoteIsNamedAfterItsFirstLine) {
  // Typing with no note open adopts the text into a new, unnamed note.
  app_->setActiveNoteId(QString());
  app_->setNotesState(QStringLiteral("S"));
  const QString id = app_->activeNoteId();
  ASSERT_FALSE(id.isEmpty());
  const auto title = [&] {
    return app_->notes()->items().at(app_->notes()->indexOfId(id)).title;
  };
  EXPECT_EQ(title(), QStringLiteral("S"));
  app_->setNotesState(QStringLiteral("Sprint review\nwhat went well"));
  EXPECT_EQ(title(), QStringLiteral("Sprint review"));
  app_->setNotesState(QStringLiteral("- [ ] Sprint review prep\nwhat went well"));
  EXPECT_EQ(title(), QStringLiteral("Sprint review prep"));

  app_->renameNote(id, QStringLiteral("Retro"));
  app_->setNotesState(QStringLiteral("# Retro\n\nAnother line"));
  app_->setNotesState(QStringLiteral("# Retro\n\nAnother line, edited"));
  EXPECT_EQ(title(), QStringLiteral("Retro"));
}

// PERA-6: retyping a new note's heading from scratch left the title at the
// last fragment saved before the heading was emptied ("Без н"), because the
// empty "# " in between broke the follow.
TEST_F(KnowAuditTest, Pera6_RetypingTheHeadingRenamesTheNote) {
  const QString id = app_->newNote();
  const auto title = [&] {
    return app_->notes()->items().at(app_->notes()->indexOfId(id)).title;
  };
  app_->setNotesState(QStringLiteral("# Untit\n\n"));
  EXPECT_EQ(title(), QStringLiteral("Untit"));
  app_->setNotesState(QStringLiteral("# \n\n"));
  EXPECT_EQ(title(), QStringLiteral("Untit")) << "an empty heading is not a name";
  app_->setNotesState(QStringLiteral("# S\n\n"));
  app_->setNotesState(QStringLiteral("# Standup 30.09\n\n"));
  EXPECT_EQ(title(), QStringLiteral("Standup 30.09"));
}

// Dogfood 2026-10-20: the first line that is text names the note — not a
// table row, a code fence or a rule — without its Markdown marks.
TEST_F(KnowAuditTest, Dogfood1020_TitleSkipsTablesCodeAndRules) {
  const QString id = app_->newNote();
  const auto title = [&] {
    return app_->notes()->items().at(app_->notes()->indexOfId(id)).title;
  };
  EXPECT_TRUE(app_->notesState().isEmpty()) << "a new note opens empty, the caret on its first line";
  app_->setNotesState(QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |\n\nAfter the table"));
  EXPECT_EQ(title(), QStringLiteral("After the table"));
  app_->setNotesState(QStringLiteral("```cpp\nint main();\n```\n---\n**Build** notes for `cmake`"));
  EXPECT_EQ(title(), QStringLiteral("Build notes for cmake"));
  app_->setNotesState(QStringLiteral("[ ] call Bob\n"));
  EXPECT_EQ(title(), QStringLiteral("call Bob"));
  // It follows the first line while nobody renamed it…
  app_->setNotesState(QStringLiteral("Retro 20.10\n"));
  EXPECT_EQ(title(), QStringLiteral("Retro 20.10"));
  // …and stops once somebody did.
  ASSERT_TRUE(app_->renameNote(id, QStringLiteral("Sprint 42 retro")));
  app_->setNotesState(QStringLiteral("Retro 20.10, edited\n"));
  EXPECT_EQ(title(), QStringLiteral("Sprint 42 retro"));
}

TEST_F(KnowAuditTest, Dogfood1020_ATableOnlyNoteKeepsThePlaceholder) {
  const QString id = app_->newNote();
  app_->setNotesState(QStringLiteral("| a | b |\n|---|---|\n"));
  const QString t = app_->notes()->items().at(app_->notes()->indexOfId(id)).title;
  EXPECT_TRUE(t == QStringLiteral("Untitled note") || t == QStringLiteral("Без названия")) << t.toStdString();
}

TEST_F(KnowAuditTest, App1_TitleIsCutToSixtyCharacters) {
  app_->setActiveNoteId(QString());
  app_->setNotesState(QStringLiteral("a"));
  const QString id = app_->activeNoteId();
  app_->setNotesState(QString(80, QLatin1Char('a')));
  EXPECT_EQ(app_->notes()->items().at(app_->notes()->indexOfId(id)).title.size(), 60);
}

TEST_F(KnowAuditTest, KnowC_PinAndMoveAreUndoable) {
  const QString id = app_->newNote(QStringLiteral("N"));
  app_->setNotePinned(id, true);
  app_->moveNoteToFolder(id, QStringLiteral("work"));
  app_->undo();
  EXPECT_TRUE(app_->notes()->items().at(app_->notes()->indexOfId(id)).folder.isEmpty());
  app_->undo();
  EXPECT_FALSE(app_->notes()->items().at(app_->notes()->indexOfId(id)).pinned);
}

TEST_F(KnowAuditTest, KnowC_FoldersCanBeRenamedAndRemovedKeepingNotes) {
  const QString a = app_->newNote(QStringLiteral("A"), QStringLiteral("meetings"));
  const QString b = app_->newNote(QStringLiteral("B"), QStringLiteral("meetings/2026"));
  EXPECT_EQ(app_->renameNoteFolder(QStringLiteral("meetings"), QStringLiteral("team")), 2);
  EXPECT_EQ(app_->noteFolders(), (QStringList{QStringLiteral("team"), QStringLiteral("team/2026")}));
  EXPECT_EQ(app_->removeNoteFolder(QStringLiteral("team/2026")), 1);
  EXPECT_EQ(app_->notes()->items().at(app_->notes()->indexOfId(b)).folder, QStringLiteral("team"));
  EXPECT_EQ(app_->notes()->rowCount(), 2);
  EXPECT_EQ(app_->notes()->items().at(app_->notes()->indexOfId(a)).folder, QStringLiteral("team"));
}

// ── KNOW-6 / TASKS-16: search reaches every note and every doc page ──

TEST_F(KnowAuditTest, Know6_PaletteFindsWordsInNotesThatAreNotOpenAndInDocPages) {
  const QString hidden = app_->newNote(QStringLiteral("Hidden"));
  app_->setNoteBody(hidden, QStringLiteral("# Hidden\n\n## Deploy\n\nthe zebracorn rollout"));
  app_->newNote(QStringLiteral("Open one"));  // now the open note
  const QString page = app_->newDocPage(QStringLiteral("Runbook"));
  app_->setDocPageBody(page, QStringLiteral("# Runbook\n\nrestart the quokkafleet"));

  const QVariantList notes = app_->searchFullText(QStringLiteral("zebracorn"));
  ASSERT_EQ(notes.size(), 1);
  EXPECT_EQ(notes.at(0).toMap().value("kind").toString(), QStringLiteral("note"));
  EXPECT_EQ(notes.at(0).toMap().value("noteId").toString(), hidden);
  EXPECT_EQ(notes.at(0).toMap().value("label").toString(), QStringLiteral("Hidden › Deploy"));

  const QVariantList pages = app_->searchFullText(QStringLiteral("quokkafleet"));
  ASSERT_EQ(pages.size(), 1);
  EXPECT_EQ(pages.at(0).toMap().value("kind").toString(), QStringLiteral("docPage"));
  EXPECT_EQ(pages.at(0).toMap().value("pageId").toString(), page);

  // And the palette's own list carries both, in the row shape it knows.
  bool sawNote = false;
  bool sawPage = false;
  for(const QVariant& v : app_->commandPaletteEntries()) {
    const QVariantMap m = v.toMap();
    sawNote = sawNote || (m.value("noteId").toString() == hidden && m.value("body").toString().contains(QStringLiteral("zebracorn")));
    sawPage = sawPage || (m.value("pageId").toString() == page && m.value("body").toString().contains(QStringLiteral("quokkafleet")));
  }
  EXPECT_TRUE(sawNote);
  EXPECT_TRUE(sawPage);
}

TEST_F(KnowAuditTest, Know19_TheNoteFilterMatchesTheWholeBody) {
  const QString a = app_->newNote(QStringLiteral("A"));
  app_->setNoteBody(a, QStringLiteral("# A\n\nline one\n\n") + QString(500, QLatin1Char('x')) + QStringLiteral("\n\nneedle deep inside"));
  app_->newNote(QStringLiteral("B"));
  EXPECT_EQ(app_->notesMatching(QStringLiteral("deep needle")), QStringList{a});
}

TEST_F(KnowAuditTest, Know12_MentionsResolveToPeopleInAnyScript) {
  app_->people()->reset({});
  Person p;
  p.id = QStringLiteral("p-oleg");
  p.name = QStringLiteral("Олег Т.");
  app_->people()->upsert(p);
  EXPECT_EQ(app_->personIdForHandle(QStringLiteral("@Олег_Т.")), QStringLiteral("p-oleg"));
  EXPECT_EQ(app_->personIdForHandle(QStringLiteral("олег")), QStringLiteral("p-oleg"));
}

// A login derived from the name ("@r.losev") names the one person it fits;
// an exact id always wins, and a login two people share names neither.
TEST_F(KnowAuditTest, MentionByLoginDerivedFromTheName) {
  app_->people()->reset({});
  Person roman;
  roman.id = QStringLiteral("p-roman");
  roman.name = QStringLiteral("Роман Лосев");
  app_->people()->upsert(roman);
  EXPECT_EQ(app_->personIdForHandle(QStringLiteral("@r.losev")), QStringLiteral("p-roman"));
  EXPECT_EQ(app_->personIdForHandle(QStringLiteral("@roman.losev")), QStringLiteral("p-roman"));
  EXPECT_EQ(app_->personIdForHandle(QStringLiteral("@r.lo")), QString());  // a prefix names no one

  Person ruslan;
  ruslan.id = QStringLiteral("p-ruslan");
  ruslan.name = QStringLiteral("Руслан Лосев");
  app_->people()->upsert(ruslan);
  EXPECT_EQ(app_->personIdForHandle(QStringLiteral("@r.losev")), QString());
  EXPECT_EQ(app_->personIdForHandle(QStringLiteral("@roman.losev")), QStringLiteral("p-roman"));

  Person holder;
  holder.id = QStringLiteral("r.losev");
  holder.name = QStringLiteral("Someone Else");
  app_->people()->upsert(holder);
  EXPECT_EQ(app_->personIdForHandle(QStringLiteral("@r.losev")), QStringLiteral("r.losev"));

  // matchPeople: both Losevs for the shared login, the id holder first.
  const QVariantList hits = app_->matchPeople(QStringLiteral("r.losev"), 8);
  ASSERT_EQ(hits.size(), 3);
  EXPECT_EQ(hits.first().toMap().value("id").toString(), QStringLiteral("r.losev"));
  const QVariantList romanOnly = app_->matchPeople(QStringLiteral("roman losev"), 8);
  ASSERT_EQ(romanOnly.size(), 1);
  EXPECT_EQ(romanOnly.first().toMap().value("name").toString(), QStringLiteral("Роман Лосев"));
}

// ── KNOW-20: copy as Markdown takes every note and every doc page ──

TEST_F(KnowAuditTest, Know20_CopyAsMarkdownHasAllNotesAndPages) {
  const QString a = app_->newNote(QStringLiteral("Alpha"));
  app_->setNoteBody(a, QStringLiteral("# Alpha\n\nalpha body"));
  const QString b = app_->newNote(QStringLiteral("Bravo"));
  app_->setNoteBody(b, QStringLiteral("bravo body"));
  const QString parent = app_->newDocPage(QStringLiteral("Runbook"));
  app_->setDocPageBody(parent, QStringLiteral("page body"));
  app_->newDocPage(QStringLiteral("Child"), parent);

  app_->copyActiveProfileMarkdownToClipboard();
  const QString md = QGuiApplication::clipboard()->text();

  EXPECT_TRUE(md.contains(QStringLiteral("### Alpha\n\nalpha body"))) << md.toStdString();
  EXPECT_TRUE(md.contains(QStringLiteral("bravo body")));
  EXPECT_TRUE(md.contains(QStringLiteral("## Docs")));
  EXPECT_TRUE(md.contains(QStringLiteral("### Runbook")));
  EXPECT_TRUE(md.contains(QStringLiteral("### › Child")));
}

// ── KNOW-16: deleting a page tree says so and can be undone ──

TEST_F(KnowAuditTest, Know16_DeletingAPageTreeOffersUndo) {
  const QString parent = app_->newDocPage(QStringLiteral("Parent"));
  app_->newDocPage(QStringLiteral("Kid"), parent);
  QSignalSpy toast(app_.get(), &AppController::undoableToast);

  app_->deleteDocPage(parent);

  ASSERT_EQ(toast.count(), 1);
  EXPECT_TRUE(toast.at(0).at(0).toString().contains(QStringLiteral("Parent")));
  EXPECT_EQ(app_->docPages()->rowCount(), 0);
  app_->undo();
  EXPECT_EQ(app_->docPages()->rowCount(), 2);
}

TEST_F(KnowAuditTest, KnowC_ExcerptsReadAsPlainText) {
  const QString id = app_->newNote(QStringLiteral("Plain"));
  app_->setNoteBody(id, QStringLiteral("# Plain\n\n- [ ] **Ship** the [build](http://x) and [[Other|the other]]"));
  const QModelIndex idx = app_->notes()->index(app_->notes()->indexOfId(id), 0);
  EXPECT_EQ(app_->notes()->data(idx, NoteModel::ExcerptRole).toString(), QStringLiteral("Ship the build and the other"));
}

// ── 2026-09-30 audit, KNOW-1: undoing a rename, pin or move keeps the typing ──
//
// Typing into a note is not an undo step, and the entry for a rename used to
// put the whole note back: everything typed after the rename went with it,
// and redo did not bring it back either.

TEST_F(KnowAuditTest, Audit0930Know1_UndoRenameKeepsTextTypedSince) {
  app_->clearPendingUndo();
  const QString id = app_->newNote(QStringLiteral("Draft"));
  const QString other = app_->newNote(QStringLiteral("Other"));
  app_->setActiveNoteId(id);
  app_->renameNote(id, QStringLiteral("Final"));
  app_->setNotesState(QStringLiteral("# Final\n\nimportant"));
  app_->setActiveNoteId(other);
  app_->setActiveNoteId(id);

  app_->undo();
  const Note* n = byTitle(QStringLiteral("Draft"));
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(n->body, QStringLiteral("# Draft\n\nimportant")) << "only the rename is taken back";
  EXPECT_EQ(app_->notesState(), n->body);

  app_->redo();
  n = byTitle(QStringLiteral("Final"));
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(n->body, QStringLiteral("# Final\n\nimportant")) << "and redo keeps it too";
  EXPECT_EQ(app_->notesState(), n->body);
}

TEST_F(KnowAuditTest, Audit0930Know1_UndoPinAndMoveKeepTextTypedSince) {
  app_->clearPendingUndo();
  const QString id = app_->newNote(QStringLiteral("Pinme"));
  app_->setNotePinned(id, true);
  app_->setNotesState(QStringLiteral("# Pinme\n\nafter the pin"));
  app_->moveNoteToFolder(id, QStringLiteral("work"));
  app_->setNotesState(QStringLiteral("# Pinme\n\nafter the pin\nafter the move"));
  const auto note = [&]() {
    return app_->notes()->items().at(app_->notes()->indexOfId(id));
  };

  app_->undo();
  EXPECT_TRUE(note().folder.isEmpty());
  EXPECT_EQ(note().body, QStringLiteral("# Pinme\n\nafter the pin\nafter the move"));
  app_->undo();
  EXPECT_FALSE(note().pinned);
  EXPECT_EQ(note().body, QStringLiteral("# Pinme\n\nafter the pin\nafter the move"));
  app_->redo();
  app_->redo();
  EXPECT_TRUE(note().pinned);
  EXPECT_EQ(note().folder, QStringLiteral("work"));
  EXPECT_EQ(note().body, QStringLiteral("# Pinme\n\nafter the pin\nafter the move"));
}

// The toast's Undo, out of order: typing since is no longer a reason to refuse,
// and is not lost either.
TEST_F(KnowAuditTest, Audit0930Know1_ToastUndoOfARenameKeepsTheTyping) {
  app_->clearPendingUndo();
  const QString id = app_->newNote(QStringLiteral("Draft"));
  const QString other = app_->newNote(QStringLiteral("Other"));
  app_->setActiveNoteId(id);
  app_->renameNote(id, QStringLiteral("Final"));
  const double serial = app_->undoSerialForToast();
  app_->setNotePinned(other, true);
  app_->setNotesState(QStringLiteral("# Final\n\nimportant"));

  EXPECT_TRUE(app_->undoEntry(serial));
  const Note* n = byTitle(QStringLiteral("Draft"));
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(n->body, QStringLiteral("# Draft\n\nimportant"));
  EXPECT_TRUE(app_->notes()->items().at(app_->notes()->indexOfId(other)).pinned) << "the later entry stays";
}

// Keystrokes still in the editor's debounce belong to the note before the undo
// reloads it.
TEST_F(KnowAuditTest, Audit0930Know1_UndoFlushesTheEditorFirst) {
  app_->clearPendingUndo();
  const QString id = app_->newNote(QStringLiteral("Draft"));
  app_->renameNote(id, QStringLiteral("Final"));
  const auto conn = QObject::connect(app_.get(), &AppController::aboutToChangeActiveNote, app_.get(), [this]() {
    app_->setNotesState(QStringLiteral("# Final\n\nstill in the editor"));
  });
  app_->undo();
  QObject::disconnect(conn);
  EXPECT_EQ(app_->noteBody(id), QStringLiteral("# Draft\n\nstill in the editor"));
}

// Delete, undo, type, redo, undo: the note comes back with what was typed into
// it while it was restored, not as it was when first deleted.
TEST_F(KnowAuditTest, Audit0930Know1_RedoneDeleteBringsBackTheLatestText) {
  app_->clearPendingUndo();
  const QString id = app_->newNote(QStringLiteral("Gone"));
  app_->deleteNote(id);
  app_->undo();
  app_->setNoteBody(id, QStringLiteral("# Gone\n\ntyped while restored"));
  app_->redo();
  EXPECT_LT(app_->notes()->indexOfId(id), 0);
  app_->undo();
  EXPECT_EQ(app_->noteBody(id), QStringLiteral("# Gone\n\ntyped while restored"));
}

// ── 2026-09-30 audit, KNOW-2: undoing a Docs deletion keeps later edits ──

namespace {

QJsonObject docEntry(const QString& id, const QString& title) {
  return QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("title"), title}};
}

QString docsBlob(const QJsonArray& items, const QJsonArray& snippets) {
  const QJsonObject section{{QStringLiteral("id"), QStringLiteral("web-standards")},
                            {QStringLiteral("title"), QStringLiteral("Web")},
                            {QStringLiteral("items"), items}};
  const QJsonObject root{{QStringLiteral("sections"), QJsonArray{section}},
                         {QStringLiteral("snippets"), snippets},
                         {QStringLiteral("contacts"), QJsonArray{}}};
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QJsonArray docItems(const QString& blob) {
  return QJsonDocument::fromJson(blob.toUtf8()).object().value("sections").toArray().at(0).toObject().value("items").toArray();
}

QJsonArray docSnippets(const QString& blob) {
  return QJsonDocument::fromJson(blob.toUtf8()).object().value("snippets").toArray();
}

}  // namespace

TEST_F(KnowAuditTest, Audit0930Know2_UndoDeleteKeepsAnotherEntrysLaterEdit) {
  app_->clearPendingUndo();
  const QJsonObject http = docEntry(QStringLiteral("e1"), QStringLiteral("HTTP Semantics"));
  const QJsonObject json = docEntry(QStringLiteral("e2"), QStringLiteral("The JSON Data Interchange Format"));
  const QJsonObject edited = docEntry(QStringLiteral("e2"), QStringLiteral("EDITED AFTER DELETE"));
  app_->setDocsState(docsBlob({http, json}, {}));
  app_->setDocsStateUndoable(docsBlob({json}, {}), QStringLiteral("Restored"));
  app_->setDocsState(docsBlob({edited}, {}));  // saveDoc: not an undo step

  app_->undo();
  EXPECT_EQ(docItems(app_->docsState()), (QJsonArray{http, edited})) << app_->docsState().toStdString();
  app_->redo();
  EXPECT_EQ(docItems(app_->docsState()), (QJsonArray{edited})) << app_->docsState().toStdString();
  app_->undo();
  EXPECT_EQ(docItems(app_->docsState()), (QJsonArray{http, edited}));
}

TEST_F(KnowAuditTest, Audit0930Know2_UndoSnippetDeleteKeepsTheOthersEdits) {
  app_->clearPendingUndo();
  const QJsonObject a{{QStringLiteral("title"), QStringLiteral("a")}};
  const QJsonObject b{{QStringLiteral("title"), QStringLiteral("b")}};
  const QJsonObject c{{QStringLiteral("title"), QStringLiteral("c")}};
  const QJsonObject c2{{QStringLiteral("title"), QStringLiteral("c, edited")}};
  const QJsonObject item = docEntry(QStringLiteral("e1"), QStringLiteral("x"));
  const QJsonObject item2 = docEntry(QStringLiteral("e1"), QStringLiteral("x, edited"));
  app_->setDocsState(docsBlob({item}, {a, b, c}));
  app_->setDocsStateUndoable(docsBlob({item}, {a, c}), QStringLiteral("Restored"));
  app_->setDocsState(docsBlob({item2}, {a, c2}));

  app_->undo();
  EXPECT_EQ(docSnippets(app_->docsState()), (QJsonArray{a, b, c2}));
  EXPECT_EQ(docItems(app_->docsState()), (QJsonArray{item2}));
}

// The toast's Undo, out of order, used to refuse because the blob had changed
// at all; an edit to another entry is no reason to.
TEST_F(KnowAuditTest, Audit0930Know2_ToastUndoAfterAnotherEditPutsTheEntryBack) {
  app_->clearPendingUndo();
  const QJsonObject http = docEntry(QStringLiteral("e1"), QStringLiteral("HTTP"));
  const QJsonObject json = docEntry(QStringLiteral("e2"), QStringLiteral("JSON"));
  const QJsonObject edited = docEntry(QStringLiteral("e2"), QStringLiteral("JSON, edited"));
  app_->setDocsState(docsBlob({http, json}, {}));
  app_->setDocsStateUndoable(docsBlob({json}, {}), QStringLiteral("Restored"));
  const double serial = app_->undoSerialForToast();
  const QString n = app_->newNote(QStringLiteral("Unrelated"));
  app_->setNotePinned(n, true);
  app_->setDocsState(docsBlob({edited}, {}));

  EXPECT_TRUE(app_->undoEntry(serial));
  EXPECT_EQ(docItems(app_->docsState()), (QJsonArray{http, edited}));
}

// ── C: code highlighting in the preview ──

namespace {

// The format the highlighter left at `pos` in the first block.
QTextCharFormat formatAt(QTextDocument& doc, int pos) {
  const QTextBlock block = doc.firstBlock();
  for(const QTextLayout::FormatRange& r : block.layout()->formats()) {
    if(pos >= r.start && pos < r.start + r.length) {
      return r.format;
    }
  }
  return {};
}

}  // namespace

TEST(KnowCodeHighlight, AHashInsideABashStringIsNotAComment) {
  QTextDocument doc;
  CodeHighlighter h;
  h.setLanguage(QStringLiteral("Bash"));  // any case
  h.setDocument(&doc);
  doc.setPlainText(QStringLiteral("echo \"#1 not a comment\" # a comment"));
  h.rehighlight();
  EXPECT_FALSE(formatAt(doc, 7).fontItalic()) << "inside the string";
  EXPECT_TRUE(formatAt(doc, 27).fontItalic()) << "the real comment";
}

TEST(KnowCodeHighlight, LanguageNamesAreCaseInsensitiveWithAliases) {
  EXPECT_EQ(CodeHighlighter::canonicalLanguage(QStringLiteral("C++")), QStringLiteral("cpp"));
  EXPECT_EQ(CodeHighlighter::canonicalLanguage(QStringLiteral("YML")), QStringLiteral("yaml"));
  EXPECT_EQ(CodeHighlighter::canonicalLanguage(QStringLiteral("Golang")), QStringLiteral("go"));
  EXPECT_EQ(CodeHighlighter::canonicalLanguage(QStringLiteral("zsh")), QStringLiteral("sh"));
  // And the languages beyond the original five get colour at all.
  for(const char* lang : {"go", "rust", "java", "sql", "json", "css", "xml", "powershell", "lua"}) {
    QTextDocument doc;
    CodeHighlighter h;
    h.setLanguage(QString::fromLatin1(lang));
    h.setDocument(&doc);
    doc.setPlainText(QStringLiteral("select \"s\" + 1 // x -- y # z <a b=\"c\"> +add"));
    h.rehighlight();
    EXPECT_FALSE(doc.firstBlock().layout()->formats().isEmpty()) << lang;
  }
}

// ── R2-070: open checklist items of new notes become tasks, on request ──

TEST(ChecklistItems, OpenItemsOnlyOutsideFences) {
  const QStringList items =
      heap::notes::openChecklistItems(QStringLiteral("# T\n- [ ] one\n- [x] done\n  * [ ]  two \n- [ ] \n```\n- [ ] code\n```\n"));
  EXPECT_EQ(items, (QStringList{QStringLiteral("one"), QStringLiteral("two")}));
}

TEST_F(KnowAuditTest, R2_070_ChecklistItemsBecomeTasksOnlyWhenAsked) {
  writeFile(QStringLiteral("vault/Plan.md"), "- [ ] write the parser\n- [ ] test it\n- [x] old\n");
  const QVariantMap preview = app_->previewNotesFolder(url(QStringLiteral("vault")));
  EXPECT_EQ(preview.value("checklists").toInt(), 1);
  EXPECT_EQ(preview.value("checklistItems").toInt(), 2);

  const int before = app_->tasks()->rowCount();
  const QVariantMap r = app_->importNotesFolder(url(QStringLiteral("vault")), true);
  EXPECT_EQ(r.value("tasksMade").toInt(), 2);
  EXPECT_EQ(app_->tasks()->rowCount(), before + 2);

  // A re-import touches no new notes, so it makes no tasks again.
  const QVariantMap again = app_->importNotesFolder(url(QStringLiteral("vault")), true);
  EXPECT_EQ(again.value("tasksMade").toInt(), 0);
  EXPECT_EQ(app_->tasks()->rowCount(), before + 2);
}

TEST_F(KnowAuditTest, R2_062_ExportOneNoteToAFile) {
  const QString id = app_->newNote(QStringLiteral("Rate limit"));
  app_->setNoteBody(id, QStringLiteral("Retry-After in seconds"));
  const QString path = dir_.path() + QStringLiteral("/one.md");
  ASSERT_TRUE(app_->exportNoteToFile(id, QUrl::fromLocalFile(path)));
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::ReadOnly));
  EXPECT_TRUE(QString::fromUtf8(f.readAll()).contains(QStringLiteral("Retry-After in seconds")));
  EXPECT_FALSE(app_->exportNoteToFile(QStringLiteral("nope"), QUrl::fromLocalFile(path)));
}

int main(int argc, char** argv) {
  QStandardPaths::setTestModeEnabled(true);
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
