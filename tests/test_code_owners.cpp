// CODEOWNERS parsing, matching and the "who to ping" report (dogfood
// 2026-10-19). Pure: no process, no forge.

#include "git/CodeOwners.h"

#include <QVariantList>

#include <gtest/gtest.h>

using heap::git::buildOwnersReport;
using heap::git::codeOwnersLocations;
using heap::git::ownerHandleIs;
using heap::git::OwnersFlavor;
using heap::git::ownersForFiles;
using heap::git::ownersPatternMatches;
using heap::git::parseCodeOwners;

namespace {

bool gh(const char* pattern, const char* path) {
  return ownersPatternMatches(QString::fromUtf8(pattern), QString::fromUtf8(path), OwnersFlavor::GitHub);
}

bool gl(const char* pattern, const char* path) {
  return ownersPatternMatches(QString::fromUtf8(pattern), QString::fromUtf8(path), OwnersFlavor::GitLab);
}

}  // namespace

// ---- locations ----

TEST(CodeOwnersLocations, GitHubLooksInDotGithubThenRootThenDocs) {
  EXPECT_EQ(codeOwnersLocations(OwnersFlavor::GitHub), (QStringList{".github/CODEOWNERS", "CODEOWNERS", "docs/CODEOWNERS"}));
}

TEST(CodeOwnersLocations, GitLabLooksInRootThenDocsThenDotGitlab) {
  EXPECT_EQ(codeOwnersLocations(OwnersFlavor::GitLab), (QStringList{"CODEOWNERS", "docs/CODEOWNERS", ".gitlab/CODEOWNERS"}));
}

// ---- patterns ----

TEST(CodeOwnersPattern, StarAloneMatchesEverything) {
  EXPECT_TRUE(gh("*", "README.md"));
  EXPECT_TRUE(gh("*", "src/deep/a.cpp"));
  EXPECT_TRUE(gl("*", ".hidden/x"));
}

TEST(CodeOwnersPattern, ExtensionMatchesAtAnyDepth) {
  EXPECT_TRUE(gh("*.js", "app.js"));
  EXPECT_TRUE(gh("*.js", "web/src/app.js"));
  EXPECT_FALSE(gh("*.js", "web/src/app.jsx"));
  EXPECT_FALSE(gh("*.js", "web/app.js.map"));
}

TEST(CodeOwnersPattern, CaseSensitive) {
  EXPECT_FALSE(gh("*.JS", "app.js"));
  EXPECT_FALSE(gh("/Docs/", "docs/a.md"));
}

TEST(CodeOwnersPattern, LeadingSlashAnchorsAtRoot) {
  EXPECT_TRUE(gh("/build/logs/", "build/logs/a.log"));
  EXPECT_TRUE(gh("/build/logs/", "build/logs/deep/b.log"));
  EXPECT_FALSE(gh("/build/logs/", "x/build/logs/a.log"));
}

TEST(CodeOwnersPattern, TrailingSlashIsADirectoryAnywhere) {
  EXPECT_TRUE(gh("apps/", "apps/a.txt"));
  EXPECT_TRUE(gh("apps/", "x/y/apps/z/a.txt"));
  // A file named "apps" is not the directory.
  EXPECT_FALSE(gh("apps/", "apps"));
  EXPECT_FALSE(gh("apps/", "x/apps"));
}

TEST(CodeOwnersPattern, DirStarIsOnlyTheDirectFiles) {
  EXPECT_TRUE(gh("docs/*", "docs/getting-started.md"));
  EXPECT_FALSE(gh("docs/*", "docs/build-app/troubleshooting.md"));
  EXPECT_TRUE(gl("/docs/*", "docs/index.md"));
  EXPECT_FALSE(gl("/docs/*", "docs/sub/index.md"));
}

TEST(CodeOwnersPattern, GitHubAnchorsAMiddleSlashGitLabDoesNot) {
  // gitignore rules on GitHub: "docs/*" is the root docs.
  EXPECT_FALSE(gh("docs/*", "x/docs/a.md"));
  EXPECT_FALSE(gh("internal/README.md", "x/internal/README.md"));
  EXPECT_TRUE(gh("internal/README.md", "internal/README.md"));
  // GitLab: an unanchored path is /**/path.
  EXPECT_TRUE(gl("internal/README.md", "x/internal/README.md"));
  EXPECT_TRUE(gl("internal/README.md", "internal/README.md"));
  EXPECT_FALSE(gl("/internal/README.md", "x/internal/README.md"));
}

TEST(CodeOwnersPattern, DoubleStarCrossesDirectories) {
  EXPECT_TRUE(gh("**/logs", "logs/a"));
  EXPECT_TRUE(gh("**/logs", "build/logs/a"));
  EXPECT_TRUE(gh("**/logs", "deeply/nested/logs/x/y"));
  EXPECT_TRUE(gh("/src/**/test.cpp", "src/test.cpp"));
  EXPECT_TRUE(gh("/src/**/test.cpp", "src/a/b/test.cpp"));
  EXPECT_FALSE(gh("/src/**/test.cpp", "lib/a/test.cpp"));
  EXPECT_TRUE(gh("/vendor/**", "vendor/a/b.c"));
  EXPECT_FALSE(gh("/vendor/**", "vendors/a"));
  EXPECT_TRUE(gl("/docs/**/*", "docs/a/b/c.md"));
  EXPECT_TRUE(gl("/docs/**/*", "docs/c.md"));
}

TEST(CodeOwnersPattern, OtherStarRunsAreOrdinaryStars) {
  EXPECT_TRUE(gh("/a**b.txt", "axyzb.txt"));
  EXPECT_FALSE(gh("/a**b.txt", "ax/yzb.txt"));
}

TEST(CodeOwnersPattern, QuestionMarkIsOneCharacterNotASlash) {
  EXPECT_TRUE(gh("/file?.txt", "file1.txt"));
  EXPECT_FALSE(gh("/file?.txt", "file12.txt"));
  EXPECT_FALSE(gh("/a?b", "a/b"));
}

TEST(CodeOwnersPattern, AFileOrDirectoryNameOwnsWhatIsInside) {
  EXPECT_TRUE(gh("/config", "config"));
  EXPECT_TRUE(gh("/config", "config/settings.yml"));
  EXPECT_TRUE(gh("Makefile", "sub/Makefile"));
}

TEST(CodeOwnersPattern, EscapedCharactersAreLiteral) {
  EXPECT_TRUE(gl("/path\\ with\\ spaces/", "path with spaces/a.txt"));
  EXPECT_TRUE(gl("\\#notes.md", "#notes.md"));
  EXPECT_TRUE(gh("/a\\*b", "a*b"));
  EXPECT_FALSE(gh("/a\\*b", "axb"));
  // Regex metacharacters in names stay literal.
  EXPECT_TRUE(gh("/lib/c++/x.h", "lib/c++/x.h"));
  EXPECT_FALSE(gh("/a.b", "axb"));
}

TEST(CodeOwnersPattern, CharacterClassesOnGitLabOnly) {
  EXPECT_TRUE(gl("/file[0-9].txt", "file3.txt"));
  EXPECT_FALSE(gl("/file[0-9].txt", "filex.txt"));
  EXPECT_TRUE(gl("/file[!0-9].txt", "filex.txt"));
  // GitHub does not support ranges: the brackets are literal there.
  EXPECT_FALSE(gh("/file[0-9].txt", "file3.txt"));
  EXPECT_TRUE(gh("/file[0-9].txt", "file[0-9].txt"));
}

TEST(CodeOwnersPattern, RootSlashIsTheWholeRepository) {
  EXPECT_TRUE(gh("/", "anything/at/all"));
}

// ---- parsing ----

TEST(CodeOwnersParse, CommentsBlankLinesAndInlineComments) {
  const auto f =
      parseCodeOwners(QStringLiteral("# top\n\n*.js @web  # the web team\r\n  # indented comment\n/docs/ @docs\n"), OwnersFlavor::GitHub);
  ASSERT_EQ(f.sections.size(), 1);
  ASSERT_EQ(f.sections[0].rules.size(), 2);
  EXPECT_EQ(f.sections[0].rules[0].pattern, QStringLiteral("*.js"));
  EXPECT_EQ(f.sections[0].rules[0].owners, (QStringList{"@web"}));
  EXPECT_EQ(f.sections[0].rules[0].line, 3);
  EXPECT_EQ(f.sections[0].rules[1].owners, (QStringList{"@docs"}));
}

TEST(CodeOwnersParse, EscapedSpaceStaysInThePattern) {
  const auto f = parseCodeOwners(QStringLiteral("/my\\ dir/ @a @b\n"), OwnersFlavor::GitLab);
  ASSERT_EQ(f.sections[0].rules.size(), 1);
  EXPECT_EQ(f.sections[0].rules[0].pattern, QStringLiteral("/my\\ dir/"));
  EXPECT_EQ(f.sections[0].rules[0].owners, (QStringList{"@a", "@b"}));
}

TEST(CodeOwnersParse, GitHubHasNoNegation) {
  const auto f = parseCodeOwners(QStringLiteral("* @all\n!/docs/ @nobody\n"), OwnersFlavor::GitHub);
  ASSERT_EQ(f.sections[0].rules.size(), 1);  // the ! line does nothing
  EXPECT_EQ(f.warnings.size(), 1);
  // And so docs still belong to the catch-all owner.
  const auto needs = ownersForFiles(f, {QStringLiteral("docs/a.md")}, OwnersFlavor::GitHub);
  ASSERT_EQ(needs.size(), 1);
  EXPECT_EQ(needs[0].owners, (QStringList{"@all"}));
}

TEST(CodeOwnersParse, GitHubBracketLineIsAPathNotASection) {
  const auto f = parseCodeOwners(QStringLiteral("[Docs] @x\n"), OwnersFlavor::GitHub);
  ASSERT_EQ(f.sections.size(), 1);
  EXPECT_EQ(f.sections[0].rules.size(), 1);
}

TEST(CodeOwnersParse, GitLabSectionsOptionalApprovalsAndDefaults) {
  const auto f = parseCodeOwners(QStringLiteral("* @root\n"
                                                "[Docs] @docs-team\n"
                                                "/docs/\n"
                                                "README.md @writer\n"
                                                "^[Style][2] @a @b\n"
                                                "*.css\n"
                                                "[Database][3]\n"
                                                "/db/ @dba\n"),
                                 OwnersFlavor::GitLab);
  ASSERT_EQ(f.sections.size(), 4);
  EXPECT_EQ(f.sections[0].name, QStringLiteral("codeowners"));
  EXPECT_EQ(f.sections[1].name, QStringLiteral("Docs"));
  EXPECT_FALSE(f.sections[1].optional);
  EXPECT_EQ(f.sections[1].approvals, 1);
  EXPECT_EQ(f.sections[1].defaultOwners, (QStringList{"@docs-team"}));
  EXPECT_EQ(f.sections[2].name, QStringLiteral("Style"));
  EXPECT_TRUE(f.sections[2].optional);
  EXPECT_EQ(f.sections[2].approvals, 2);
  EXPECT_EQ(f.sections[3].approvals, 3);
  EXPECT_TRUE(f.sections[3].defaultOwners.isEmpty());
}

TEST(CodeOwnersParse, GitLabSameSectionNameIsOneSection) {
  const auto f = parseCodeOwners(QStringLiteral("[Docs]\n/a/ @x\n[docs]\n/b/ @y\n"), OwnersFlavor::GitLab);
  ASSERT_EQ(f.sections.size(), 2);  // head + Docs
  EXPECT_EQ(f.sections[1].rules.size(), 2);
}

// ---- owners for files ----

TEST(CodeOwnersMatch, GitHubLastMatchWins) {
  const auto f = parseCodeOwners(QStringLiteral("* @everyone\n*.js @web\n/apps/ @octocat\n/apps/github\n"), OwnersFlavor::GitHub);
  const auto needs = ownersForFiles(
      f,
      {QStringLiteral("README.md"), QStringLiteral("src/a.js"), QStringLiteral("apps/x.js"), QStringLiteral("apps/github/y.js")},
      OwnersFlavor::GitHub);
  ASSERT_EQ(needs.size(), 3);
  EXPECT_EQ(needs[0].owners, (QStringList{"@everyone"}));
  EXPECT_EQ(needs[0].files, (QStringList{"README.md"}));
  EXPECT_EQ(needs[1].owners, (QStringList{"@web"}));
  EXPECT_EQ(needs[2].owners, (QStringList{"@octocat"}));
  EXPECT_EQ(needs[2].files, (QStringList{"apps/x.js"}));
  // apps/github/y.js: the last rule names nobody, so nobody is needed.
}

TEST(CodeOwnersMatch, FilesWithTheSameOwnersAreOneNeed) {
  const auto f = parseCodeOwners(QStringLiteral("/src/ @core @lead\n"), OwnersFlavor::GitHub);
  const auto needs =
      ownersForFiles(f, {QStringLiteral("src/a.cpp"), QStringLiteral("src/b.cpp"), QStringLiteral("x.md")}, OwnersFlavor::GitHub);
  ASSERT_EQ(needs.size(), 1);
  EXPECT_EQ(needs[0].files.size(), 2);
}

TEST(CodeOwnersMatch, GitLabEachSectionCountsOnItsOwn) {
  const auto f = parseCodeOwners(QStringLiteral("* @root\n"
                                                "[Docs] @docs-team\n"
                                                "*.md\n"
                                                "[Backend]\n"
                                                "/src/ @backend\n"
                                                "/src/legacy/ @old\n"),
                                 OwnersFlavor::GitLab);
  const auto needs = ownersForFiles(f, {QStringLiteral("src/legacy/README.md")}, OwnersFlavor::GitLab);
  ASSERT_EQ(needs.size(), 3);
  EXPECT_EQ(needs[0].section, QStringLiteral("codeowners"));
  EXPECT_EQ(needs[0].owners, (QStringList{"@root"}));
  EXPECT_EQ(needs[1].section, QStringLiteral("Docs"));
  EXPECT_EQ(needs[1].owners, (QStringList{"@docs-team"}));  // the default owners
  EXPECT_EQ(needs[2].section, QStringLiteral("Backend"));
  EXPECT_EQ(needs[2].owners, (QStringList{"@old"}));  // last match in the section
}

TEST(CodeOwnersMatch, GitLabExclusionTakesAFileOutOfItsSection) {
  const auto f = parseCodeOwners(QStringLiteral("[Ruby]\n*.rb @ruby\n!/config/routes.rb\n"), OwnersFlavor::GitLab);
  EXPECT_TRUE(ownersForFiles(f, {QStringLiteral("config/routes.rb")}, OwnersFlavor::GitLab).isEmpty());
  EXPECT_EQ(ownersForFiles(f, {QStringLiteral("app/x.rb")}, OwnersFlavor::GitLab).size(), 1);
}

// GitLab's reference: "After a pattern is excluded, it cannot be included
// again in the same section" — the docs' own example: routes.rb stays out.
// Another section still owns it.
TEST(CodeOwnersMatch, GitLabExcludedFileIsNotIncludedAgainInItsSection) {
  const auto f = parseCodeOwners(QStringLiteral("[Ruby]\n*.rb @ruby\n!/config/**/*.rb\n/config/routes.rb @ops\n[Ops]\n/config/ @sre\n"),
                                 OwnersFlavor::GitLab);
  const auto needs = ownersForFiles(f, {QStringLiteral("config/routes.rb")}, OwnersFlavor::GitLab);
  ASSERT_EQ(needs.size(), 1);
  EXPECT_EQ(needs[0].section, QStringLiteral("Ops"));
  EXPECT_EQ(needs[0].owners, (QStringList{"@sre"}));
}

TEST(CodeOwnersParse, AByteOrderMarkDoesNotEatTheFirstPattern) {
  const QString text = QString(QChar(0xFEFF)) + QStringLiteral("* @team\n");
  for(const auto flavor : {OwnersFlavor::GitHub, OwnersFlavor::GitLab}) {
    const auto f = parseCodeOwners(text, flavor);
    const auto needs = ownersForFiles(f, {QStringLiteral("src/a.cpp")}, flavor);
    ASSERT_EQ(needs.size(), 1);
    EXPECT_EQ(needs[0].owners, (QStringList{"@team"}));
  }
}

TEST(CodeOwnersMatch, LeadingSlashInChangedPathIsIgnored) {
  const auto f = parseCodeOwners(QStringLiteral("/src/ @core\n"), OwnersFlavor::GitHub);
  EXPECT_EQ(ownersForFiles(f, {QStringLiteral("/src/a")}, OwnersFlavor::GitHub).size(), 1);
}

// ---- handles ----

TEST(CodeOwnersHandle, LoginsAndTeams) {
  EXPECT_TRUE(ownerHandleIs(QStringLiteral("@Alice"), QStringLiteral("alice")));
  EXPECT_TRUE(ownerHandleIs(QStringLiteral("@org/core"), QStringLiteral("core")));
  EXPECT_TRUE(ownerHandleIs(QStringLiteral("@org/core"), QStringLiteral("org/core")));
  EXPECT_FALSE(ownerHandleIs(QStringLiteral("@alice"), QStringLiteral("alicia")));
  EXPECT_FALSE(ownerHandleIs(QStringLiteral("a@b.c"), QStringLiteral("a")));
}

// ---- the report ----

TEST(CodeOwnersReport, NoFileNoReport) {
  const auto rep = buildOwnersReport({}, {}, OwnersFlavor::GitHub, {QStringLiteral("a")}, {}, {}, {});
  EXPECT_FALSE(rep.found);
  EXPECT_TRUE(rep.toPing.isEmpty());
}

TEST(CodeOwnersReport, ApprovedRequestedMissingAndTheAuthor) {
  const QString text = QStringLiteral("/src/ @alice @bob\n/docs/ @carol\n/ci/ @me @dave\n/web/ @erin\n");
  const auto rep =
      buildOwnersReport(QStringLiteral(".github/CODEOWNERS"),
                        text,
                        OwnersFlavor::GitHub,
                        {QStringLiteral("src/a.cpp"), QStringLiteral("docs/x.md"), QStringLiteral("ci/y.yml"), QStringLiteral("web/z.ts")},
                        /*approvedBy=*/{QStringLiteral("alice")},
                        /*requested=*/{QStringLiteral("carol")},
                        /*author=*/QStringLiteral("me"));
  ASSERT_TRUE(rep.found);
  ASSERT_EQ(rep.requirements.size(), 4);
  // /src/: alice approved — satisfied, bob is not needed.
  EXPECT_EQ(rep.requirements[0].state, QStringLiteral("approved"));
  EXPECT_EQ(rep.requirements[0].owners[0].state, QStringLiteral("approved"));
  EXPECT_EQ(rep.requirements[0].owners[1].state, QStringLiteral("missing"));
  // /docs/: carol is asked already.
  EXPECT_EQ(rep.requirements[1].state, QStringLiteral("pending"));
  EXPECT_EQ(rep.requirements[1].owners[0].state, QStringLiteral("requested"));
  // /ci/: I own it but cannot approve my own PR; dave is the one to ask.
  EXPECT_EQ(rep.requirements[2].owners[0].state, QStringLiteral("author"));
  EXPECT_EQ(rep.requirements[2].owners[1].state, QStringLiteral("missing"));
  EXPECT_EQ(rep.toPing, (QStringList{"@dave", "@erin"}));
}

TEST(CodeOwnersReport, ARequestedTeamIsNotPinged) {
  const auto rep = buildOwnersReport(QStringLiteral("CODEOWNERS"),
                                     QStringLiteral("* @org/core\n"),
                                     OwnersFlavor::GitHub,
                                     {QStringLiteral("a")},
                                     {},
                                     {QStringLiteral("core")},
                                     QStringLiteral("me"));
  EXPECT_EQ(rep.requirements[0].owners[0].state, QStringLiteral("requested"));
  EXPECT_TRUE(rep.toPing.isEmpty());
}

TEST(CodeOwnersReport, AnUnnamedApprovalOnATeamIsAMaybe) {
  const auto rep = buildOwnersReport(QStringLiteral("CODEOWNERS"),
                                     QStringLiteral("* @org/core\n"),
                                     OwnersFlavor::GitHub,
                                     {QStringLiteral("a")},
                                     {QStringLiteral("someone")},
                                     {},
                                     QStringLiteral("me"));
  EXPECT_EQ(rep.requirements[0].state, QStringLiteral("maybe"));
  EXPECT_TRUE(rep.toPing.isEmpty());
}

TEST(CodeOwnersReport, MyOwnApprovalDoesNotCount) {
  const auto rep = buildOwnersReport(QStringLiteral("CODEOWNERS"),
                                     QStringLiteral("* @org/core\n"),
                                     OwnersFlavor::GitHub,
                                     {QStringLiteral("a")},
                                     {QStringLiteral("me")},
                                     {},
                                     QStringLiteral("me"));
  EXPECT_EQ(rep.requirements[0].state, QStringLiteral("pending"));
  EXPECT_EQ(rep.toPing, (QStringList{"@org/core"}));
}

TEST(CodeOwnersReport, GitLabApprovalCountAndOptionalSections) {
  const QString text = QStringLiteral("[Backend][2] @a @b @c\n/src/\n^[Style] @stylist\n*.css\n");
  const auto rep = buildOwnersReport(QStringLiteral("CODEOWNERS"),
                                     text,
                                     OwnersFlavor::GitLab,
                                     {QStringLiteral("src/x.css")},
                                     {QStringLiteral("a")},
                                     {},
                                     QStringLiteral("me"));
  ASSERT_EQ(rep.requirements.size(), 2);
  EXPECT_EQ(rep.requirements[0].section, QStringLiteral("Backend"));
  EXPECT_EQ(rep.requirements[0].approvals, 2);
  EXPECT_EQ(rep.requirements[0].approvedCount, 1);
  EXPECT_EQ(rep.requirements[0].state, QStringLiteral("pending"));
  EXPECT_TRUE(rep.requirements[1].optional);
  // b and c are still needed; the optional Style section asks for nobody.
  EXPECT_EQ(rep.toPing, (QStringList{"@b", "@c"}));
}

TEST(CodeOwnersReport, EachHandlePingedOnce) {
  const auto rep = buildOwnersReport(QStringLiteral("CODEOWNERS"),
                                     QStringLiteral("/a/ @x @y\n/b/ @X\n"),
                                     OwnersFlavor::GitHub,
                                     {QStringLiteral("a/1"), QStringLiteral("b/2")},
                                     {},
                                     {},
                                     {});
  EXPECT_EQ(rep.toPing, (QStringList{"@x", "@y"}));
}

TEST(CodeOwnersReport, ToVariantCarriesTheShape) {
  const auto rep =
      buildOwnersReport(QStringLiteral("CODEOWNERS"), QStringLiteral("* @x\n"), OwnersFlavor::GitHub, {QStringLiteral("a")}, {}, {}, {});
  const QVariantMap v = rep.toVariant();
  EXPECT_TRUE(v.value("found").toBool());
  EXPECT_EQ(v.value("toPing").toStringList(), (QStringList{"@x"}));
  const QVariantList reqs = v.value("requirements").toList();
  ASSERT_EQ(reqs.size(), 1);
  EXPECT_EQ(reqs[0].toMap().value("owners").toList()[0].toMap().value("state").toString(), QStringLiteral("missing"));
}

// ---- GitLab's documented rules, one by one (dogfood 2026-10-20, item 18) ----
//
// GitLab normalises a pattern (Gitlab::CodeOwners::File#normalize_pattern)
// and then runs Ruby's File.fnmatch? with FNM_PATHNAME | FNM_DOTMATCH on
// "/" + the path. These pin each consequence.

TEST(CodeOwnersGitLab, RelativePathIsAGlobstarPath) {
  // "README.md is treated the same way as /**/README.md".
  EXPECT_TRUE(gl("README.md", "README.md"));
  EXPECT_TRUE(gl("README.md", "a/b/README.md"));
  EXPECT_TRUE(gl("internal/README.md", "docs/internal/README.md"));
  EXPECT_FALSE(gl("internal/README.md", "internal/sub/README.md"));
}

TEST(CodeOwnersGitLab, LeadingSlashIsTheRoot) {
  EXPECT_TRUE(gl("/README.md", "README.md"));
  EXPECT_FALSE(gl("/README.md", "docs/README.md"));
  EXPECT_TRUE(gl("/docs/README.md", "docs/README.md"));
}

TEST(CodeOwnersGitLab, TrailingSlashIsEverythingBelowAtAnyDepth) {
  EXPECT_TRUE(gl("/docs/", "docs/a.md"));
  EXPECT_TRUE(gl("/docs/", "docs/deep/er/a.md"));
  EXPECT_FALSE(gl("/docs/", "x/docs/a.md"));
  EXPECT_TRUE(gl("docs/", "x/docs/a.md"));
  EXPECT_FALSE(gl("docs/", "docs"));  // a file named docs is not below docs/
}

TEST(CodeOwnersGitLab, ANameWithoutTrailingSlashIsAFileNotADirectory) {
  // Unlike GitHub (gitignore), fnmatch on the whole path: /config owns a
  // file named config, not what is inside a directory of that name.
  EXPECT_TRUE(gl("/config", "config"));
  EXPECT_FALSE(gl("/config", "config/settings.yml"));
  EXPECT_FALSE(gl("lib", "lib/a.rb"));
  EXPECT_TRUE(gh("/config", "config/settings.yml"));
}

TEST(CodeOwnersGitLab, StarStopsAtSlashButMatchesDotFiles) {
  EXPECT_TRUE(gl("/docs/*.md", "docs/a.md"));
  EXPECT_FALSE(gl("/docs/*.md", "docs/sub/a.md"));
  EXPECT_TRUE(gl("*.md", ".github/.hidden.md"));
  EXPECT_TRUE(gl("*", "a/.b/.c"));
}

TEST(CodeOwnersGitLab, GlobstarOnlyAsAWholeSegment) {
  EXPECT_TRUE(gl("/docs/**/index.md", "docs/index.md"));
  EXPECT_TRUE(gl("/docs/**/index.md", "docs/a/b/index.md"));
  EXPECT_FALSE(gl("/docs/**/index.md", "src/docs/index.md"));
  // "/docs/**" with no slash after: two stars are one star there.
  EXPECT_TRUE(gl("/docs/**", "docs/a.md"));
  EXPECT_FALSE(gl("/docs/**", "docs/a/b.md"));
  EXPECT_TRUE(gl("/a**b", "axyb"));
  EXPECT_FALSE(gl("/a**b", "ax/yb"));
}

TEST(CodeOwnersGitLab, NoBraceExpansionAndCaseSensitive) {
  EXPECT_FALSE(gl("*.{js,ts}", "a.js"));
  EXPECT_TRUE(gl("/a.{js,ts}", "a.{js,ts}"));
  EXPECT_FALSE(gl("*.MD", "a.md"));
}

TEST(CodeOwnersGitLab, EscapedSpacesAndPound) {
  const auto f = parseCodeOwners(QStringLiteral("/my\\ docs/ @writer\n\\#notes.md @scribe\n"), OwnersFlavor::GitLab);
  ASSERT_EQ(f.sections[0].rules.size(), 2);
  EXPECT_EQ(ownersForFiles(f, {QStringLiteral("my docs/a.md")}, OwnersFlavor::GitLab).value(0).owners, (QStringList{"@writer"}));
  EXPECT_EQ(ownersForFiles(f, {QStringLiteral("x/#notes.md")}, OwnersFlavor::GitLab).value(0).owners, (QStringList{"@scribe"}));
}

TEST(CodeOwnersGitLab, OwnersAreUsersGroupsSubgroupsRolesAndEmails) {
  const auto f =
      parseCodeOwners(QStringLiteral("* @dev @tel/bts/l2 @@maintainer lead@corp.ru plainword # tail @nobody\n"), OwnersFlavor::GitLab);
  ASSERT_EQ(f.sections[0].rules.size(), 1);
  // A plain word names nobody on GitLab; the comment is not read.
  EXPECT_EQ(f.sections[0].rules[0].owners, (QStringList{"@dev", "@tel/bts/l2", "@@maintainer", "lead@corp.ru"}));
}

TEST(CodeOwnersGitLab, AnEmailOwnerIsOpaqueSoAnUnnamedApprovalIsAMaybe) {
  const auto rep = buildOwnersReport(QStringLiteral("CODEOWNERS"),
                                     QStringLiteral("* lead@corp.ru\n"),
                                     OwnersFlavor::GitLab,
                                     {QStringLiteral("a")},
                                     {QStringLiteral("lead")},
                                     {},
                                     QStringLiteral("me"));
  EXPECT_EQ(rep.requirements[0].state, QStringLiteral("maybe"));
}

TEST(CodeOwnersGitLab, DefaultOwnersBelongToTheirHeader) {
  // Section written twice with different defaults: each entry takes those
  // of the header it stands under; owners written beside a path win.
  const auto f = parseCodeOwners(QStringLiteral("[Docs] @first\n/a/\n/b/ @own\n[docs] @second\n/c/\n"), OwnersFlavor::GitLab);
  ASSERT_EQ(f.sections.size(), 2);
  const auto& rules = f.sections[1].rules;
  ASSERT_EQ(rules.size(), 3);
  EXPECT_EQ(rules[0].owners, (QStringList{"@first"}));
  EXPECT_TRUE(rules[0].defaulted);
  EXPECT_EQ(rules[1].owners, (QStringList{"@own"}));
  EXPECT_FALSE(rules[1].defaulted);
  EXPECT_EQ(rules[2].owners, (QStringList{"@second"}));
}

TEST(CodeOwnersGitLab, AnEntryWithNoOwnersAndNoDefaultsNeedsNobody) {
  const auto f = parseCodeOwners(QStringLiteral("* @root\n/vendor/\n"), OwnersFlavor::GitLab);
  // The last match in the section wins and names nobody.
  EXPECT_TRUE(ownersForFiles(f, {QStringLiteral("vendor/x.c")}, OwnersFlavor::GitLab).isEmpty());
}

TEST(CodeOwnersGitLab, DuplicatedSectionIsRequiredIfEitherIs) {
  auto f = parseCodeOwners(QStringLiteral("^[Go]\n*.go @a\n[go][2]\n/cmd/ @b\n"), OwnersFlavor::GitLab);
  ASSERT_EQ(f.sections.size(), 2);
  EXPECT_FALSE(f.sections[1].optional);
  EXPECT_EQ(f.sections[1].approvals, 2);
  EXPECT_EQ(f.sections[1].name, QStringLiteral("Go"));  // the first spelling
  f = parseCodeOwners(QStringLiteral("^[Go]\n*.go @a\n^[GO]\n/cmd/ @b\n"), OwnersFlavor::GitLab);
  EXPECT_TRUE(f.sections[1].optional);
}

TEST(CodeOwnersGitLab, LastMatchWithinASectionEverySectionApplies) {
  // The docs' own example: internal/README.md is @admin's in the unnamed
  // section and @user4's in [README Owners].
  const auto f = parseCodeOwners(QStringLiteral("* @admin\n\n[README Owners]\nREADME.md @user1 @user2\ninternal/README.md @user4\n"),
                                 OwnersFlavor::GitLab);
  const auto needs = ownersForFiles(f, {QStringLiteral("internal/README.md")}, OwnersFlavor::GitLab);
  ASSERT_EQ(needs.size(), 2);
  EXPECT_EQ(needs[0].owners, (QStringList{"@admin"}));
  EXPECT_EQ(needs[1].section, QStringLiteral("README Owners"));
  EXPECT_EQ(needs[1].owners, (QStringList{"@user4"}));
}

TEST(CodeOwnersGitLab, TheSamePatternAgainKeepsItsFirstPlace) {
  // GitLab keeps a section's entries in a hash by pattern: /src/ written
  // again changes its owners but not its place, so the later /src/x/ still
  // wins for src/x/.
  const auto f = parseCodeOwners(QStringLiteral("/src/ @a\n/src/x/ @b\n/src/ @c\n"), OwnersFlavor::GitLab);
  ASSERT_EQ(f.sections[0].rules.size(), 2);
  EXPECT_EQ(ownersForFiles(f, {QStringLiteral("src/x/1.c")}, OwnersFlavor::GitLab).value(0).owners, (QStringList{"@b"}));
  EXPECT_EQ(ownersForFiles(f, {QStringLiteral("src/1.c")}, OwnersFlavor::GitLab).value(0).owners, (QStringList{"@c"}));
}

TEST(CodeOwnersGitLab, NestedGroupProjectFileCountsLikeAnyOther) {
  // The owner's real layout: a deep group path and an MR on it.
  const QString text = QStringLiteral(
      "[L2 model][2] @tel/bts/l2-maintainers\n"
      "/src/\n"
      "/docs/ @tel/bts/writers\n"
      "^[CI]\n"
      ".gitlab-ci.yml @devops\n");
  const auto rep = buildOwnersReport(QStringLiteral(".gitlab/CODEOWNERS"),
                                     text,
                                     OwnersFlavor::GitLab,
                                     {QStringLiteral("src/l2/mac.cpp"), QStringLiteral(".gitlab-ci.yml")},
                                     {},
                                     {},
                                     QStringLiteral("fin"));
  ASSERT_EQ(rep.requirements.size(), 2);
  EXPECT_EQ(rep.requirements[0].section, QStringLiteral("L2 model"));
  EXPECT_EQ(rep.requirements[0].approvals, 2);
  EXPECT_EQ(rep.requirements[0].owners.value(0).handle, QStringLiteral("@tel/bts/l2-maintainers"));
  EXPECT_TRUE(rep.requirements[1].optional);
  EXPECT_EQ(rep.toPing, (QStringList{"@tel/bts/l2-maintainers"}));
}

// ---- the forge's answers ----

TEST(CodeOwnersFacts, GhPrView) {
  const QByteArray raw = R"({"author":{"login":"me"},"baseRefName":"main",
    "files":[{"path":"src/a.cpp","additions":1},{"path":"README.md"}],
    "latestReviews":[{"author":{"login":"alice"},"state":"APPROVED"},{"author":{"login":"bob"},"state":"CHANGES_REQUESTED"}],
    "reviewRequests":[{"login":"carol"},{"__typename":"Team","slug":"core","name":"Core"}]})";
  const auto f = heap::git::parseGhOwnersFacts(raw);
  ASSERT_TRUE(f.ok);
  EXPECT_EQ(f.baseRef, QStringLiteral("main"));
  EXPECT_EQ(f.author, QStringLiteral("me"));
  EXPECT_EQ(f.files, (QStringList{"src/a.cpp", "README.md"}));
  EXPECT_EQ(f.approvedBy, (QStringList{"alice"}));
  EXPECT_EQ(f.requested, (QStringList{"carol", "core"}));
}

TEST(CodeOwnersFacts, GhGarbageIsNotOk) {
  EXPECT_FALSE(heap::git::parseGhOwnersFacts("no pull requests found").ok);
}

TEST(CodeOwnersFacts, GlabApprovalsAndDiffs) {
  EXPECT_EQ(heap::git::parseGlabApprovedBy(R"({"approved_by":[{"user":{"username":"ann"}},{"user":{"username":"ben"}}]})"),
            (QStringList{"ann", "ben"}));
  bool ok = false;
  EXPECT_EQ(heap::git::parseGlabChangedFiles(R"([{"old_path":"a/old.rb","new_path":"a/new.rb"},{"old_path":"b","new_path":"b"}])", &ok),
            (QStringList{"a/old.rb", "a/new.rb", "b"}));
  EXPECT_TRUE(ok);
  EXPECT_EQ(heap::git::parseGlabChangedFiles(R"({"changes":[{"old_path":"x","new_path":"x"}]})"), (QStringList{"x"}));
  heap::git::parseGlabChangedFiles("<html>", &ok);
  EXPECT_FALSE(ok);
}
