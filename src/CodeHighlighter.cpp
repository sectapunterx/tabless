#include "CodeHighlighter.h"

#include "markdown/LogSyntax.h"

#include <QColor>
#include <QHash>
#include <QQuickTextDocument>
#include <QTextDocument>

CodeHighlighter::CodeHighlighter(QObject *parent)
    : QSyntaxHighlighter(parent)
{
  rebuildFormats();
}

void CodeHighlighter::setTarget(QQuickTextDocument *t) {
    if (m_target == t) return;
    m_target = t;
    setDocument(t ? t->textDocument() : nullptr);
    emit targetChanged();
    if (document()) rehighlight();
}

void CodeHighlighter::setLanguage(const QString &lang) {
    if (m_language == lang) return;
    m_language = lang;
    emit languageChanged();
    rebuildFormats();
    if (document()) rehighlight();
}

void CodeHighlighter::setPalette(const QVariantMap &p) {
    if (m_palette == p) return;
    m_palette = p;
    emit paletteChanged();
    rebuildFormats();
    if (document()) rehighlight();
}

QTextCharFormat CodeHighlighter::fmt(const char *key, bool bold, bool italic) const {
    QTextCharFormat f;
    const QVariant v = m_palette.value(QString::fromLatin1(key));
    QColor c = v.value<QColor>();
    if (!c.isValid()) {
        // Sensible dark-mode fallbacks if palette isn't supplied yet.
        if (QByteArray(key) == "keyword") c = QColor("#5cc2dd");
        else if (QByteArray(key) == "string") c = QColor("#d8c277");
        else if (QByteArray(key) == "comment") c = QColor("#5f6878");
        else if (QByteArray(key) == "number") c = QColor("#7cc492");
        else if (QByteArray(key) == "type") c = QColor("#6cc4b8");
        else if (QByteArray(key) == "builtin") c = QColor("#c07acf");
        else if(QByteArray(key) == "error") {
          c = QColor("#e5534b");
        } else if(QByteArray(key) == "warn") {
          c = QColor("#d8a73a");
        } else {
          c = QColor("#e5ecf3");
        }
    }
    f.setForeground(c);
    if (bold) f.setFontWeight(QFont::DemiBold);
    if (italic) f.setFontItalic(true);
    return f;
}

void CodeHighlighter::appendKeywordRule(QVector<Rule>& out, const QStringList& words, Kind kind) {
  if(words.isEmpty()) {
    return;
  }
  QString p = QStringLiteral("\\b(?:");
  for(int i = 0; i < words.size(); ++i) {
    if(i) {
      p += '|';
    }
    p += QRegularExpression::escape(words.at(i));
  }
  p += QStringLiteral(")\\b");
  Rule r;
  r.pattern = QRegularExpression(p);
  r.kind = kind;
  out.push_back(r);
}

QString CodeHighlighter::canonicalLanguage(const QString& lang) {
  // What people actually write after ```: case varies ("Bash", "C++",
  // "JSON") and so do the names. Matching them exactly left most fences
  // uncoloured.
  const QString l = lang.trimmed().toLower();
  static const QHash<QString, QString> aliases = {
      {"sh", "sh"},         {"bash", "sh"},       {"zsh", "sh"},        {"shell", "sh"},    {"console", "sh"},     {"shellsession", "sh"},
      {"fish", "sh"},       {"ksh", "sh"},        {"cpp", "cpp"},       {"c++", "cpp"},     {"cxx", "cpp"},        {"cc", "cpp"},
      {"hpp", "cpp"},       {"hxx", "cpp"},       {"h", "cpp"},         {"c", "cpp"},       {"objc", "cpp"},       {"cuda", "cpp"},
      {"qml", "js"},        {"py", "py"},         {"python", "py"},     {"python3", "py"},  {"py3", "py"},         {"js", "js"},
      {"javascript", "js"}, {"jsx", "js"},        {"mjs", "js"},        {"cjs", "js"},      {"ts", "js"},          {"typescript", "js"},
      {"tsx", "js"},        {"yaml", "yaml"},     {"yml", "yaml"},      {"json", "json"},   {"jsonc", "json"},     {"json5", "json"},
      {"go", "go"},         {"golang", "go"},     {"rust", "rust"},     {"rs", "rust"},     {"java", "java"},      {"kotlin", "java"},
      {"kt", "java"},       {"scala", "java"},    {"cs", "cs"},         {"csharp", "cs"},   {"c#", "cs"},          {"swift", "java"},
      {"dart", "java"},     {"sql", "sql"},       {"psql", "sql"},      {"mysql", "sql"},   {"sqlite", "sql"},     {"plsql", "sql"},
      {"toml", "ini"},      {"ini", "ini"},       {"cfg", "ini"},       {"conf", "ini"},    {"properties", "ini"}, {"dockerfile", "docker"},
      {"docker", "docker"}, {"makefile", "make"}, {"make", "make"},     {"cmake", "cmake"}, {"ps1", "ps"},         {"powershell", "ps"},
      {"pwsh", "ps"},       {"lua", "lua"},       {"ruby", "ruby"},     {"rb", "ruby"},     {"php", "php"},        {"html", "xml"},
      {"xml", "xml"},       {"svg", "xml"},       {"css", "css"},       {"scss", "css"},    {"less", "css"},       {"diff", "diff"},
      {"patch", "diff"},    {"proto", "java"},    {"protobuf", "java"}, {"graphql", "js"},  {"gql", "js"},         {"hcl", "ini"},
      {"terraform", "ini"}, {"tf", "ini"},        {"nginx", "ini"},     {"bat", "bat"},     {"cmd", "bat"},        {"batch", "bat"},
  };
  return aliases.value(l, l);
}

CodeHighlighter::RuleSet CodeHighlighter::buildRules(const QString& lang) {
  RuleSet out;

  constexpr Kind fComment = Kind::Comment;
  constexpr Kind fString = Kind::String;
  constexpr Kind fKeyword = Kind::Keyword;
  constexpr Kind fNumber = Kind::Number;
  constexpr Kind fType = Kind::Type;
  constexpr Kind fBuiltin = Kind::Builtin;

  Rule numRule;
  numRule.pattern = QRegularExpression(QStringLiteral("\\b(?:0x[0-9A-Fa-f]+|\\d+(?:\\.\\d+)?)\\b"));
  numRule.kind = fNumber;

  const auto comment = [&](const QString& pattern, bool multiline = false) {
    Rule r;
    r.pattern = QRegularExpression(pattern);
    if(multiline) {
      r.pattern.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);
    }
    r.kind = fComment;
    out.comments.push_back(r);
  };
  const auto string = [&](const QString& pattern, bool multiline = false) {
    Rule r;
    r.pattern = QRegularExpression(pattern);
    if(multiline) {
      r.pattern.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);
    }
    r.kind = fString;
    out.strings.push_back(r);
  };
  const auto other = [&](const QString& pattern, Kind f, int group = 0, bool caseInsensitive = false) {
    Rule r;
    r.pattern =
        QRegularExpression(pattern, caseInsensitive ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);
    r.kind = f;
    r.captureGroup = group;
    out.others.push_back(r);
  };
  const QString dq = QStringLiteral("\"(?:\\\\.|[^\"\\\\\\n])*\"");
  const QString sq = QStringLiteral("'(?:\\\\.|[^'\\\\\\n])*'");
  const QString cLineComment = QStringLiteral("//[^\\n]*");
  const QString cBlockComment = QStringLiteral("/\\*.*?\\*/");

  if(lang == "sh") {
    // A # starts a comment only as a word of its own: "${#arr}" and the #
    // inside "echo '#1'" are not comments (strings win, see highlightBlock).
    comment(QStringLiteral("(?:^|(?<=\\s))#[^\\n]*"));
    string(QStringLiteral("\"(?:\\\\.|[^\"\\\\\\n])*\""));
    string(QStringLiteral("'[^'\\n]*'"));
    appendKeywordRule(out.others,
                      {"if",    "then",     "else",   "elif",  "fi",       "case", "esac",  "for",    "in",       "while", "do",
                       "done",  "function", "return", "break", "continue", "exit", "local", "export", "readonly", "unset", "set",
                       "shift", "source",   "trap",   "echo",  "printf",   "read", "cd",    "pushd",  "popd"},
                      fKeyword);
    appendKeywordRule(
        out.others,
        {"bazel",   "cmake",     "make",       "gdb",   "tcpdump", "pgrep", "perf", "valgrind", "sudo", "git",  "grep", "awk",
         "sed",     "cat",       "ls",         "mkdir", "rm",      "mv",    "cp",   "ssh",      "scp",  "curl", "wget", "docker",
         "kubectl", "systemctl", "journalctl", "npm",   "npx",     "yarn",  "pip",  "cargo",    "go",   "ninja"},
        fBuiltin);
    other(QStringLiteral("\\$\\{?[A-Za-z_][A-Za-z0-9_]*\\}?"), fType);
    out.others.push_back(numRule);
  } else if(lang == "cpp" || lang == "cs" || lang == "java" || lang == "go" || lang == "rust" || lang == "js" || lang == "php") {
    comment(cLineComment);
    comment(cBlockComment, true);
    string(dq);
    if(lang == "js") {
      string(sq);
      string(QStringLiteral("`(?:\\\\.|[^`\\\\])*`"), true);
    } else if(lang == "rust" || lang == "cpp" || lang == "java" || lang == "cs") {
      string(QStringLiteral("'(?:\\\\.|[^'\\\\\\n])'"));
    } else {
      string(sq);
    }
    if(lang == "cpp") {
      string(QStringLiteral("R\"\\(.*?\\)\""), true);
      other(QStringLiteral("^\\s*#\\s*[a-z]+"), fKeyword);
    }
    if(lang == "go") {
      string(QStringLiteral("`[^`]*`"));
    }
    if(lang == "php") {
      comment(QStringLiteral("(?:^|(?<=\\s))#[^\\n]*"));
      other(QStringLiteral("\\$[A-Za-z_][A-Za-z0-9_]*"), fType);
    }
    static const QHash<QString, QStringList> keywords = {
        {"cpp",
         {"alignas",
          "alignof",
          "asm",
          "auto",
          "break",
          "case",
          "catch",
          "class",
          "co_await",
          "co_return",
          "co_yield",
          "concept",
          "const",
          "consteval",
          "constexpr",
          "constinit",
          "const_cast",
          "continue",
          "decltype",
          "default",
          "delete",
          "do",
          "dynamic_cast",
          "else",
          "enum",
          "explicit",
          "export",
          "extern",
          "false",
          "final",
          "for",
          "friend",
          "goto",
          "if",
          "inline",
          "mutable",
          "namespace",
          "new",
          "noexcept",
          "nullptr",
          "operator",
          "override",
          "private",
          "protected",
          "public",
          "register",
          "reinterpret_cast",
          "requires",
          "return",
          "sizeof",
          "static",
          "static_assert",
          "static_cast",
          "struct",
          "switch",
          "template",
          "this",
          "thread_local",
          "throw",
          "true",
          "try",
          "typedef",
          "typeid",
          "typename",
          "union",
          "using",
          "virtual",
          "void",
          "volatile",
          "while"}},
        {"cs", {"abstract", "as",        "async",    "await",     "base",     "break",     "case",   "catch", "checked",  "class",
                "const",    "continue",  "default",  "delegate",  "do",       "else",      "enum",   "event", "explicit", "extern",
                "false",    "finally",   "fixed",    "for",       "foreach",  "get",       "goto",   "if",    "implicit", "in",
                "init",     "interface", "internal", "is",        "lock",     "namespace", "new",    "null",  "operator", "out",
                "override", "params",    "private",  "protected", "public",   "readonly",  "record", "ref",   "return",   "sealed",
                "set",      "sizeof",    "static",   "struct",    "switch",   "this",      "throw",  "true",  "try",      "typeof",
                "using",    "var",       "virtual",  "void",      "volatile", "when",      "where",  "while", "yield"}},
        {"java",
         {"abstract",   "assert",   "break",   "case",         "catch",     "class",    "const",  "continue", "data",   "default", "do",
          "else",       "enum",     "extends", "false",        "final",     "finally",  "for",    "fun",      "func",   "guard",   "if",
          "implements", "import",   "in",      "instanceof",   "interface", "is",       "let",    "message",  "native", "new",     "null",
          "object",     "override", "package", "private",      "protected", "public",   "return", "rpc",      "sealed", "service", "static",
          "struct",     "super",    "switch",  "synchronized", "this",      "throw",    "throws", "true",     "try",    "val",     "var",
          "void",       "when",     "while",   "syntax",       "repeated",  "optional", "oneof"}},
        {"go", {"break",   "case",  "chan",   "const",  "continue", "default", "defer",     "else", "fallthrough", "false",
                "for",     "func",  "go",     "goto",   "if",       "import",  "interface", "iota", "map",         "nil",
                "package", "range", "return", "select", "struct",   "switch",  "true",      "type", "var"}},
        {"rust", {"as",   "async", "await",  "break",  "const", "continue", "crate", "dyn",  "else",   "enum", "extern", "false", "fn",
                  "for",  "if",    "impl",   "in",     "let",   "loop",     "match", "mod",  "move",   "mut",  "pub",    "ref",   "return",
                  "self", "Self",  "static", "struct", "super", "trait",    "true",  "type", "unsafe", "use",  "where",  "while"}},
        {"js", {"async",    "await",   "break",      "case",     "catch",     "class",      "const",     "continue", "debugger", "default",
                "delete",   "do",      "else",       "enum",     "export",    "extends",    "false",     "finally",  "for",      "from",
                "function", "if",      "implements", "import",   "in",        "instanceof", "interface", "let",      "new",      "null",
                "of",       "package", "private",    "property", "protected", "public",     "readonly",  "return",   "signal",   "static",
                "super",    "switch",  "this",       "throw",    "true",      "try",        "type",      "typeof",   "var",      "void",
                "while",    "with",    "yield",      "query",    "mutation",  "fragment"}},
        {"php", {"abstract", "and",       "array",  "as",      "break",    "case",    "catch",      "class",   "const",
                 "continue", "default",   "do",     "echo",    "else",     "elseif",  "extends",    "false",   "final",
                 "finally",  "fn",        "for",    "foreach", "function", "if",      "implements", "include", "interface",
                 "match",    "namespace", "new",    "null",    "or",       "private", "protected",  "public",  "require",
                 "return",   "static",    "switch", "throw",   "trait",    "true",    "try",        "use",     "while"}},
    };
    static const QHash<QString, QStringList> types = {
        {"cpp",
         {"bool",       "char",       "char8_t",       "char16_t", "char32_t",      "double",   "float",      "int",         "long",
          "short",      "signed",     "unsigned",      "wchar_t",  "int8_t",        "int16_t",  "int32_t",    "int64_t",     "uint8_t",
          "uint16_t",   "uint32_t",   "uint64_t",      "size_t",   "ptrdiff_t",     "std",      "string",     "string_view", "vector",
          "array",      "map",        "unordered_map", "set",      "unordered_set", "optional", "variant",    "tuple",       "pair",
          "shared_ptr", "unique_ptr", "weak_ptr",      "function", "span",          "chrono",   "filesystem", "atomic",      "mutex",
          "thread",     "QString",    "QStringList",   "QVariant", "QObject"}},
        {"cs", {"bool",  "byte",  "char",   "decimal", "double", "dynamic", "float", "int",        "long", "object",
                "sbyte", "short", "string", "uint",    "ulong",  "ushort",  "List",  "Dictionary", "Task", "IEnumerable"}},
        {"java",
         {"boolean",  "byte", "char", "double", "float",  "int",     "long",  "short", "String", "Integer", "List",  "Map",    "Set",
          "Optional", "Any",  "Unit", "Int",    "Double", "Boolean", "int32", "int64", "uint32", "uint64",  "bytes", "string", "bool"}},
        {"go", {"bool",  "byte", "complex64", "complex128", "error", "float32", "float64", "int",    "int8",    "int16", "int32",
                "int64", "rune", "string",    "uint",       "uint8", "uint16",  "uint32",  "uint64", "uintptr", "any"}},
        {"rust", {"bool", "char", "f32",  "f64",   "i8",     "i16", "i32",    "i64",    "i128", "isize", "str", "u8",     "u16",
                  "u32",  "u64",  "u128", "usize", "String", "Vec", "Option", "Result", "Box",  "Rc",    "Arc", "HashMap"}},
        {"js",
         {"string",
          "number",
          "boolean",
          "any",
          "unknown",
          "never",
          "object",
          "bigint",
          "symbol",
          "var",
          "int",
          "real",
          "bool",
          "list",
          "color",
          "url",
          "Item",
          "Rectangle",
          "Text"}},
    };
    static const QHash<QString, QStringList> builtins = {
        {"cpp",
         {"Q_OBJECT", "Q_PROPERTY", "Q_INVOKABLE", "QML_ELEMENT", "QML_SINGLETON", "emit", "slots", "signals", "Q_SLOTS", "Q_SIGNALS"}},
        {"cs", {"Console", "Math", "String", "Guid", "DateTime"}},
        {"java", {"System", "println", "Math", "Object", "Thread", "Exception"}},
        {"go", {"append", "cap", "close", "copy", "delete", "len", "make", "new", "panic", "print", "println", "recover", "fmt"}},
        {"rust", {"println", "format", "vec", "panic", "assert", "assert_eq", "Some", "None", "Ok", "Err"}},
        {"js",
         {"console",
          "window",
          "document",
          "Math",
          "JSON",
          "Object",
          "Array",
          "String",
          "Number",
          "Boolean",
          "Promise",
          "Map",
          "Set",
          "Symbol",
          "undefined",
          "NaN",
          "Infinity",
          "Qt",
          "parent"}},
        {"php", {"isset", "empty", "count", "strlen", "print", "var_dump", "die"}},
    };
    // A capitalised name is a type in these languages ("BigDecimal",
    // "Duration", "QString"): drawn as one, before the word lists, so a
    // builtin such as "System" keeps its own colour.
    if(lang != "go" && lang != "php") {
      other(QStringLiteral("\\b[A-Z][A-Za-z0-9_]*[a-z][A-Za-z0-9_]*\\b"), fType);
    }
    appendKeywordRule(out.others, keywords.value(lang), fKeyword);
    appendKeywordRule(out.others, types.value(lang), fType);
    appendKeywordRule(out.others, builtins.value(lang), fBuiltin);
    if(lang == "rust") {
      other(QStringLiteral("\\b[a-z_][a-z0-9_]*!"), fBuiltin);
    }
    out.others.push_back(numRule);
  } else if(lang == "py" || lang == "ruby") {
    comment(QStringLiteral("#[^\\n]*"));
    string(QStringLiteral("\"\"\".*?\"\"\""), true);
    string(QStringLiteral("'''.*?'''"), true);
    string(dq);
    string(sq);
    if(lang == "py") {
      appendKeywordRule(
          out.others,
          {"and",   "as",      "assert", "async",  "await",  "break", "class",  "continue", "def",   "del",    "elif", "else",     "except",
           "False", "finally", "for",    "from",   "global", "if",    "import", "in",       "is",    "lambda", "None", "nonlocal", "not",
           "or",    "pass",    "raise",  "return", "True",   "try",   "while",  "with",     "yield", "match",  "case"},
          fKeyword);
      appendKeywordRule(out.others,
                        {"print", "len", "range", "int",  "str",        "float",   "bool",      "list",    "dict", "set", "tuple",
                         "self",  "cls", "open",  "map",  "filter",     "zip",     "enumerate", "sorted",  "sum",  "min", "max",
                         "abs",   "any", "all",   "type", "isinstance", "getattr", "setattr",   "hasattr", "super"},
                        fBuiltin);
    } else {
      appendKeywordRule(
          out.others,
          {"alias", "and",   "begin", "break", "case",   "class",  "def",   "defined", "do",    "else",  "elsif",   "end",   "ensure",
           "false", "for",   "if",    "in",    "module", "next",   "nil",   "not",     "or",    "redo",  "rescue",  "retry", "return",
           "self",  "super", "then",  "true",  "undef",  "unless", "until", "when",    "while", "yield", "require", "puts"},
          fKeyword);
      other(QStringLiteral(":[A-Za-z_][A-Za-z0-9_]*"), fType);
    }
    out.others.push_back(numRule);
  } else if(lang == "lua") {
    comment(QStringLiteral("--[^\\n]*"));
    string(dq);
    string(sq);
    appendKeywordRule(out.others,
                      {"and", "break", "do",  "else", "elseif", "end",    "false",  "for",  "function", "goto",  "if",
                       "in",  "local", "nil", "not",  "or",     "repeat", "return", "then", "true",     "until", "while"},
                      fKeyword);
    out.others.push_back(numRule);
  } else if(lang == "sql") {
    comment(QStringLiteral("--[^\\n]*"));
    comment(cBlockComment, true);
    string(QStringLiteral("'(?:''|[^'\\n])*'"));
    string(dq);
    // SQL keywords are case-insensitive, and people write them both ways.
    other(QStringLiteral("\\b(?:select|from|where|and|or|not|insert|into|values|update|set|delete|create|table|index|"
                         "view|drop|alter|add|column|primary|key|foreign|references|join|inner|left|right|outer|full|on|"
                         "group|by|order|having|limit|offset|union|all|distinct|as|in|is|null|like|between|exists|case|"
                         "when|then|else|end|with|returning|begin|commit|rollback|transaction|default|unique|check|"
                         "cascade|asc|desc|count|sum|avg|min|max|coalesce|true|false)\\b"),
          fKeyword,
          0,
          true);
    other(QStringLiteral("\\b(?:int|integer|bigint|smallint|text|varchar|char|boolean|bool|date|timestamp|timestamptz|"
                         "numeric|decimal|real|float|double|serial|uuid|json|jsonb|blob)\\b"),
          fType,
          0,
          true);
    out.others.push_back(numRule);
  } else if(lang == "yaml") {
    comment(QStringLiteral("(?:^|(?<=\\s))#[^\\n]*"));
    string(QStringLiteral("\"[^\"\\n]*\""));
    string(QStringLiteral("'[^'\\n]*'"));
    other(QStringLiteral("^\\s*-?\\s*([A-Za-z_][A-Za-z0-9_.-]*)\\s*:"), fKeyword, 1);
    appendKeywordRule(out.others, {"true", "false", "null", "yes", "no", "on", "off"}, fBuiltin);
    out.others.push_back(numRule);
  } else if(lang == "json") {
    string(dq);
    other(QStringLiteral("(\"(?:\\\\.|[^\"\\\\\\n])*\")\\s*:"), fKeyword, 1);
    appendKeywordRule(out.others, {"true", "false", "null"}, fBuiltin);
    out.others.push_back(numRule);
  } else if(lang == "ini" || lang == "docker" || lang == "make" || lang == "cmake") {
    comment(QStringLiteral("(?:^|(?<=\\s))[#;][^\\n]*"));
    string(dq);
    string(sq);
    other(QStringLiteral("^\\s*\\[[^\\]]+\\]"), fType);
    other(QStringLiteral("^\\s*([A-Za-z_][A-Za-z0-9_.-]*)\\s*[=:]"), fKeyword, 1);
    if(lang == "docker") {
      other(QStringLiteral("^\\s*(?:FROM|RUN|CMD|LABEL|EXPOSE|ENV|ADD|COPY|ENTRYPOINT|VOLUME|USER|WORKDIR|ARG|ONBUILD|"
                           "STOPSIGNAL|HEALTHCHECK|SHELL|AS)\\b"),
            fKeyword,
            0,
            true);
    }
    if(lang == "cmake") {
      other(QStringLiteral("^\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\("), fBuiltin, 1);
      other(QStringLiteral("\\$\\{[^}]+\\}"), fType);
    }
    if(lang == "make") {
      other(QStringLiteral("\\$[({][^)}]+[)}]"), fType);
    }
    out.others.push_back(numRule);
  } else if(lang == "ps" || lang == "bat") {
    comment(lang == "ps" ? QStringLiteral("#[^\\n]*") : QStringLiteral("^\\s*(?:rem\\b|::)[^\\n]*"));
    if(lang == "ps") {
      comment(QStringLiteral("<#.*?#>"), true);
    }
    string(dq);
    string(sq);
    other(lang == "ps" ? QStringLiteral("\\$[A-Za-z_][A-Za-z0-9_:]*") : QStringLiteral("%[A-Za-z0-9_~]+%?"), fType);
    other(lang == "ps" ? QStringLiteral("\\b(?:if|else|elseif|foreach|for|while|do|function|param|return|try|catch|"
                                        "finally|throw|switch|break|continue|begin|process|end|in)\\b")
                       : QStringLiteral("\\b(?:echo|set|if|else|goto|call|exit|for|in|do|not|exist|errorlevel|setlocal|"
                                        "endlocal)\\b"),
          fKeyword,
          0,
          true);
    other(QStringLiteral("\\b[A-Z][a-z]+-[A-Z][A-Za-z]+\\b"), fBuiltin);
    out.others.push_back(numRule);
  } else if(lang == "xml") {
    comment(QStringLiteral("<!--.*?-->"), true);
    string(dq);
    string(sq);
    other(QStringLiteral("</?([A-Za-z][A-Za-z0-9:_-]*)"), fKeyword, 1);
    other(QStringLiteral("\\s([A-Za-z_:][A-Za-z0-9_:.-]*)\\s*="), fType, 1);
  } else if(lang == "css") {
    comment(cBlockComment, true);
    comment(cLineComment);
    string(dq);
    string(sq);
    other(QStringLiteral("([A-Za-z-]+)\\s*:"), fKeyword, 1);
    other(QStringLiteral("#[0-9A-Fa-f]{3,8}\\b"), fNumber);
    other(QStringLiteral("^\\s*[.#]?[A-Za-z][A-Za-z0-9_-]*"), fType);
    out.others.push_back(numRule);
  } else if(lang == "diff") {
    other(QStringLiteral("^\\+[^\\n]*"), fString);
    other(QStringLiteral("^-[^\\n]*"), fBuiltin);
    other(QStringLiteral("^@@[^\\n]*"), fKeyword);
  }
  // unknown language → leave all rule lists empty so the text renders plain
  return out;
}

const CodeHighlighter::RuleSet& CodeHighlighter::rulesFor(const QString& language) {
  // Built once per language and shared by every block of code on screen: the
  // docs viewer and the notes' editor ask for the same rules. GUI thread only.
  static QHash<QString, RuleSet> cache;
  const QString lang = canonicalLanguage(language);
  auto it = cache.find(lang);
  if(it == cache.end()) {
    it = cache.insert(lang, buildRules(lang));
  }
  return *it;
}

bool CodeHighlighter::knowsLanguage(const QString& language) {
  const RuleSet& set = rulesFor(language);
  return !set.comments.isEmpty() || !set.strings.isEmpty() || !set.others.isEmpty();
}

QVector<CodeHighlighter::Span> CodeHighlighter::spans(const QString& text, const QString& language) {
  QVector<Span> result;
  const RuleSet& set = rulesFor(language);
  // Words first, then strings and comments on top — but strings and comments
  // decided left to right, whichever starts first wins. Applying comments
  // last used to colour the # inside "echo '#1'" as the start of a comment.
  for(const Rule& r : set.others) {
    auto it = r.pattern.globalMatch(text);
    while(it.hasNext()) {
      const auto m = it.next();
      const int start = static_cast<int>(m.capturedStart(r.captureGroup));
      const int len = static_cast<int>(m.capturedLength(r.captureGroup));
      if(len > 0) {
        result.push_back({start, len, r.kind});
      }
    }
  }
  qsizetype pos = 0;
  while(pos < text.size()) {
    qsizetype bestStart = -1;
    qsizetype bestLen = 0;
    const Rule* best = nullptr;
    const auto consider = [&](const QVector<Rule>& rules) {
      for(const Rule& r : rules) {
        const auto m = r.pattern.match(text, pos);
        if(!m.hasMatch() || m.capturedLength() == 0) {
          continue;
        }
        if(bestStart < 0 || m.capturedStart() < bestStart) {
          bestStart = m.capturedStart();
          bestLen = m.capturedLength();
          best = &r;
        }
      }
    };
    consider(set.strings);
    consider(set.comments);
    if(best == nullptr) {
      break;
    }
    result.push_back({static_cast<int>(bestStart), static_cast<int>(bestLen), best->kind});
    pos = bestStart + bestLen;
  }
  return result;
}

void CodeHighlighter::rebuildFormats() {
  m_formats[static_cast<int>(Kind::Comment)] = fmt("comment", false, true);
  m_formats[static_cast<int>(Kind::String)] = fmt("string");
  m_formats[static_cast<int>(Kind::Keyword)] = fmt("keyword", true);
  m_formats[static_cast<int>(Kind::Number)] = fmt("number");
  m_formats[static_cast<int>(Kind::Type)] = fmt("type", true);
  m_formats[static_cast<int>(Kind::Builtin)] = fmt("builtin");
}

void CodeHighlighter::highlightBlock(const QString& text) {
  // A log (APP-315): its level, time, paths and quotes, one rule with the
  // note's editor (LogSyntax).
  if(heap::md::isLogLanguage(m_language)) {
    for(const heap::md::LogSpan& s : heap::md::logSpans(text)) {
      QTextCharFormat f;
      switch(s.kind) {
        case heap::md::LogKind::Error:
          f = fmt("error", true);
          break;
        case heap::md::LogKind::Warn:
          f = fmt("warn", true);
          break;
        case heap::md::LogKind::Quiet:
          f = fmt("comment");
          break;
        case heap::md::LogKind::Time:
          f = fmt("number");
          break;
        case heap::md::LogKind::Path:
          f = fmt("type");
          break;
        case heap::md::LogKind::Quoted:
          f = fmt("string");
          break;
      }
      setFormat(s.start, s.length, f);
    }
    return;
  }
  for(const Span& span : spans(text, m_language)) {
    setFormat(span.start, span.length, m_formats[static_cast<int>(span.kind)]);
  }
}
