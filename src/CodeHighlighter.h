#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVariantMap>
#include <QVector>
#include <QRegularExpression>
#include <QQuickTextDocument>
#include <qqmlregistration.h>

class CodeHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickTextDocument* target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged)

public:
    explicit CodeHighlighter(QObject *parent = nullptr);

    QQuickTextDocument* target() const { return m_target; }
    void setTarget(QQuickTextDocument *t);

    QString language() const { return m_language; }
    void    setLanguage(const QString &lang);

    QVariantMap palette() const { return m_palette; }
    void        setPalette(const QVariantMap &p);

    // The rule set a fence's language name selects: case-insensitive, with
    // the usual aliases ("Bash", "zsh", "C++", "yml", "golang", …).
    static QString canonicalLanguage(const QString& lang);

    // What a line of code is made of, by its language's rules: one rule set
    // for the docs viewer and the notes' editor (MdHighlighter), so a snippet
    // is coloured the same wherever it is read. Spans come words first, then
    // strings and comments; a later span wins where they overlap. A log is
    // not covered here (LogSyntax).
    enum class Kind { Comment, String, Keyword, Number, Type, Builtin };

    struct Span {
      int start = 0;
      int length = 0;
      Kind kind = Kind::Keyword;
    };

    static QVector<Span> spans(const QString& text, const QString& language);
    // Whether the language has rules at all ("text", "" and unknown names do not).
    static bool knowsLanguage(const QString& language);

   signals:
    void targetChanged();
    void languageChanged();
    void paletteChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        Kind kind = Kind::Keyword;
        int                captureGroup = 0;
    };

    struct RuleSet {
      QVector<Rule> comments;
      QVector<Rule> strings;
      QVector<Rule> others;
    };

    QQuickTextDocument *m_target = nullptr;
    QString             m_language = "text";
    QVariantMap         m_palette;
    QTextCharFormat m_formats[6];

    static RuleSet buildRules(const QString& lang);
    static const RuleSet& rulesFor(const QString& language);
    void rebuildFormats();
    QTextCharFormat fmt(const char *key, bool bold = false, bool italic = false) const;
    static void appendKeywordRule(QVector<Rule>& out, const QStringList& words, Kind kind);
};
