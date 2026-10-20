#include "markdown/JiraWiki.h"

#include <gtest/gtest.h>

using heap::md::jiraWikiToMarkdown;
using heap::md::looksLikeJiraWiki;

TEST(JiraWikiTest, TellsWikiFromMarkdown) {
  EXPECT_TRUE(looksLikeJiraWiki(QStringLiteral("h2. Steps\n* one")));
  EXPECT_TRUE(looksLikeJiraWiki(QStringLiteral("text\n{code:java}\nx();\n{code}")));
  EXPECT_TRUE(looksLikeJiraWiki(QStringLiteral("||a||b||\n|1|2|")));
  EXPECT_TRUE(looksLikeJiraWiki(QStringLiteral("see [docs|https://x.test]")));
  EXPECT_TRUE(looksLikeJiraWiki(QStringLiteral("run {{make}} first")));
  EXPECT_FALSE(looksLikeJiraWiki(QStringLiteral("## Steps\n- one\n\n```js\nx()\n```")));
  EXPECT_FALSE(looksLikeJiraWiki(QStringLiteral("plain words, a **bold** one and [a link](https://x.test)")));
}

TEST(JiraWikiTest, HeadingsListsAndRules) {
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("h1. Title\nh3. Sub")), QStringLiteral("# Title\n### Sub"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("* a\n** b\n# one\n## two\n*# mixed")),
            QStringLiteral("- a\n   - b\n1. one\n   1. two\n   2. mixed"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("# a\n# b\n# c\ntext\n# again")), QStringLiteral("1. a\n2. b\n3. c\ntext\n1. again"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("- dash\n----")), QStringLiteral("- dash\n---"));
}

TEST(JiraWikiTest, InlineMarkup) {
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("a *bold* and _it_ and -gone- and {{mono}}")),
            QStringLiteral("a **bold** and _it_ and ~~gone~~ and `mono`"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("2 * 3 * 4 and well-known x-y-z")), QStringLiteral("2 * 3 * 4 and well-known x-y-z"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("{{*not bold*}}")), QStringLiteral("`*not bold*`"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("{color:red}red{color} text")), QStringLiteral("red text"));
}

TEST(JiraWikiTest, LinksAndImages) {
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("see [the docs|https://x.test/a-b-c] now")),
            QStringLiteral("see [the docs](https://x.test/a-b-c) now"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("[https://x.test]")), QStringLiteral("<https://x.test>"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("ask [~jdoe]")), QStringLiteral("ask @jdoe"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("!screen.png|thumbnail!")), QStringLiteral("![screen.png](screen.png)"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("wow! really!")), QStringLiteral("wow! really!"));
}

TEST(JiraWikiTest, CodeKeepsItsText) {
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("{code:java}\nint *p = -1-;\nh1. no\n{code}")),
            QStringLiteral("```java\nint *p = -1-;\nh1. no\n```"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("{code:title=A.py|language=python}\nx\n{code}")), QStringLiteral("```python\nx\n```"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("{noformat}\n* raw\n{noformat}")), QStringLiteral("```\n* raw\n```"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("{code}one(){code}")), QStringLiteral("```\none()\n```"));
}

TEST(JiraWikiTest, TablesAndQuotes) {
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("||a||b||\n|1|*2*|")), QStringLiteral("| a | b |\n| --- | --- |\n| 1 | **2** |"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("bq. said")), QStringLiteral("> said"));
  EXPECT_EQ(jiraWikiToMarkdown(QStringLiteral("{quote}\nfirst\nsecond\n{quote}\nafter")), QStringLiteral("> first\n> second\nafter"));
}

// A tracker comment drawn by a Text in MarkdownText mode (dogfood
// 2026-10-19): no markup of its own, no image fetched, code left alone.
TEST(JiraWikiTest, CommentMarkdownIsMadeSafeForATextItem) {
  using heap::md::markdownForTextItem;
  EXPECT_EQ(markdownForTextItem(QStringLiteral("hi <img src=x onerror=y> **b**")), QStringLiteral("hi &lt;img src=x onerror=y> **b**"));
  EXPECT_EQ(markdownForTextItem(QStringLiteral("see ![shot](https://evil.test/p.png)")),
            QStringLiteral("see [shot](https://evil.test/p.png)"));
  EXPECT_EQ(markdownForTextItem(QStringLiteral("link <https://x.test/a> stays")), QStringLiteral("link <https://x.test/a> stays"));
  EXPECT_EQ(markdownForTextItem(QStringLiteral("code `a<b` and \\<c")), QStringLiteral("code `a<b` and \\<c"));
  EXPECT_EQ(markdownForTextItem(QStringLiteral("```\n<div>\n![x](y)\n```\n<b>")), QStringLiteral("```\n<div>\n![x](y)\n```\n&lt;b>"));
}

// Review 2026-10-19 (K): two ways a tag still reached the Text — a backtick
// that opens no code span, and indentation that continues a list item
// rather than starting code.
TEST(JiraWikiTest, CommentMarkdownLeavesNoTagThroughTicksOrIndentation) {
  using heap::md::markdownForTextItem;
  EXPECT_EQ(markdownForTextItem(QStringLiteral("a ` <img src=http://evil.test/x.png>")),
            QStringLiteral("a \\` &lt;img src=http://evil.test/x.png>"));
  EXPECT_EQ(markdownForTextItem(QStringLiteral("- item\n    <img src=http://evil.test/x.png>")),
            QStringLiteral("- item\n    &lt;img src=http://evil.test/x.png>"));
  EXPECT_EQ(markdownForTextItem(QStringLiteral("text\n    <b>continued</b>")), QStringLiteral("text\n    &lt;b>continued&lt;/b>"));
  // Real indented code is still left as it is.
  EXPECT_EQ(markdownForTextItem(QStringLiteral("code:\n\n    if(a < b) {}\n    x<y")),
            QStringLiteral("code:\n\n    if(a < b) {}\n    x<y"));
  // A matched span still is code.
  EXPECT_EQ(markdownForTextItem(QStringLiteral("``a<b`` and `c`")), QStringLiteral("``a<b`` and `c`"));
}
