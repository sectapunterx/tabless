#include "git/CodeOwners.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QVariantList>

#include <algorithm>

namespace heap::git {

QStringList codeOwnersLocations(OwnersFlavor flavor) {
  if(flavor == OwnersFlavor::GitLab) {
    return {QStringLiteral("CODEOWNERS"), QStringLiteral("docs/CODEOWNERS"), QStringLiteral(".gitlab/CODEOWNERS")};
  }
  return {QStringLiteral(".github/CODEOWNERS"), QStringLiteral("CODEOWNERS"), QStringLiteral("docs/CODEOWNERS")};
}

namespace {

// Characters that need a backslash inside a regex character class.
QString escapeClassBody(const QString& body) {
  QString out;
  for(const QChar c : body) {
    if(c == QLatin1Char('\\') || c == QLatin1Char('[') || c == QLatin1Char(']') || c == QLatin1Char('^')) {
      out += QLatin1Char('\\');
    }
    out += c;
  }
  return out;
}

QString globToRegex(const QString& glob, bool classes) {
  QString rx;
  const qsizetype n = glob.size();
  for(qsizetype i = 0; i < n; ++i) {
    const QChar c = glob.at(i);
    if(c == QLatin1Char('\\') && i + 1 < n) {
      rx += QRegularExpression::escape(QString(glob.at(++i)));
      continue;
    }
    if(c == QLatin1Char('*')) {
      if(i + 1 < n && glob.at(i + 1) == QLatin1Char('*')) {
        qsizetype j = i + 2;
        while(j < n && glob.at(j) == QLatin1Char('*')) {
          ++j;
        }
        const bool atStart = i == 0 || glob.at(i - 1) == QLatin1Char('/');
        if(atStart && j < n && glob.at(j) == QLatin1Char('/')) {
          rx += QStringLiteral("(?:.*/)?");  // "**/": any leading directories, none too
          i = j;
          continue;
        }
        if(atStart && j == n) {
          rx += QStringLiteral(".*");  // "dir/**": everything inside
          i = j - 1;
          continue;
        }
        // Any other run of stars is an ordinary star (gitignore).
        rx += QStringLiteral("[^/]*");
        i = j - 1;
        continue;
      }
      rx += QStringLiteral("[^/]*");
      continue;
    }
    if(c == QLatin1Char('?')) {
      rx += QStringLiteral("[^/]");
      continue;
    }
    if(c == QLatin1Char('[') && classes) {
      qsizetype j = i + 1;
      if(j < n && (glob.at(j) == QLatin1Char('!') || glob.at(j) == QLatin1Char('^'))) {
        ++j;
      }
      if(j < n && glob.at(j) == QLatin1Char(']')) {
        ++j;
      }
      while(j < n && glob.at(j) != QLatin1Char(']')) {
        ++j;
      }
      if(j < n) {
        QString body = glob.mid(i + 1, j - i - 1);
        const bool negate = body.startsWith(QLatin1Char('!')) || body.startsWith(QLatin1Char('^'));
        if(negate) {
          body = body.mid(1);
        }
        rx += QLatin1Char('[') + (negate ? QStringLiteral("^/") : QString()) + escapeClassBody(body) + QLatin1Char(']');
        i = j;
        continue;
      }
    }
    rx += QRegularExpression::escape(QString(c));
  }
  return rx;
}

struct Compiled {
  QRegularExpression rx;
  bool dirOnly = false;
  bool filesOnly = false;  // "dir/*": the files right in dir, not deeper
  bool everything = false;
};

Compiled compile(const QString& pattern, OwnersFlavor flavor) {
  Compiled out;
  QString p = pattern;
  while(p.endsWith(QLatin1Char('/')) && !p.endsWith(QStringLiteral("\\/"))) {
    out.dirOnly = true;
    p.chop(1);
  }
  bool anchored = false;
  if(p.startsWith(QLatin1Char('/'))) {
    anchored = true;
    p = p.mid(1);
  } else if(flavor == OwnersFlavor::GitHub && p.contains(QLatin1Char('/'))) {
    // gitignore: a slash anywhere but the end anchors the pattern.
    anchored = true;
  }
  if(p.isEmpty()) {
    out.everything = true;  // "/" — the whole repository
    return out;
  }
  // Both forges read "docs/*" as the files directly in docs/; gitignore would
  // also take a directory docs/x, and so everything under it.
  out.filesOnly = !out.dirOnly && p.endsWith(QStringLiteral("/*")) && !p.endsWith(QStringLiteral("**/*"));
  const QString body = globToRegex(p, flavor == OwnersFlavor::GitLab);
  out.rx =
      QRegularExpression(anchored ? QStringLiteral("^") + body + QLatin1Char('$') : QStringLiteral("^(?:.*/)?") + body + QLatin1Char('$'));
  return out;
}

// The path and every directory above it: "a", "a/b", "a/b/c.txt". A pattern
// that matches a directory owns what is inside it, so each is a candidate.
bool matchCompiled(const Compiled& c, const QString& path) {
  if(c.everything) {
    return true;
  }
  if(!c.rx.isValid()) {
    return false;
  }
  qsizetype from = 0;
  while(true) {
    const qsizetype slash = path.indexOf(QLatin1Char('/'), from);
    const bool last = slash < 0;
    if(last && c.dirOnly) {
      return false;  // the file itself is not a directory
    }
    const QString candidate = last ? path : path.left(slash);
    if((last || !c.filesOnly) && c.rx.match(candidate).hasMatch()) {
      return true;
    }
    if(last) {
      return false;
    }
    from = slash + 1;
  }
}

QStringList splitOwners(const QString& rest) {
  QStringList out;
  static const QRegularExpression ws(QStringLiteral("\\s+"));
  for(const QString& tok : rest.split(ws, Qt::SkipEmptyParts)) {
    if(tok.startsWith(QLatin1Char('#'))) {
      break;  // a comment after the owners
    }
    out << tok;
  }
  return out;
}

QString bare(const QString& handle) {
  QString h = handle.trimmed();
  while(h.startsWith(QLatin1Char('@'))) {
    h = h.mid(1);
  }
  return h;
}

// A team, a group or a GitLab role: someone Tabless cannot list.
bool isGroupHandle(const QString& handle) {
  return handle.startsWith(QStringLiteral("@@")) || (handle.startsWith(QLatin1Char('@')) && handle.contains(QLatin1Char('/')));
}

bool handleMatches(const QString& handle, const QString& login, bool teamSlug) {
  const QString h = bare(handle);
  const QString l = bare(login);
  if(h.isEmpty() || l.isEmpty()) {
    return false;
  }
  if(h.compare(l, Qt::CaseInsensitive) == 0) {
    return true;
  }
  // A requested team comes back as its slug alone.
  return teamSlug && h.contains(QLatin1Char('/')) && !l.contains(QLatin1Char('/')) &&
         h.section(QLatin1Char('/'), -1).compare(l, Qt::CaseInsensitive) == 0;
}

bool containsCi(const QStringList& list, const QString& s) {
  return std::ranges::any_of(list, [&s](const QString& x) {
    return x.compare(s, Qt::CaseInsensitive) == 0;
  });
}

}  // namespace

bool ownersPatternMatches(const QString& pattern, const QString& path, OwnersFlavor flavor) {
  return matchCompiled(compile(pattern, flavor), path);
}

bool ownerHandleIs(const QString& handle, const QString& login) {
  return handleMatches(handle, login, /*teamSlug=*/true);
}

CodeOwnersFile parseCodeOwners(const QString& text, OwnersFlavor flavor) {
  CodeOwnersFile f;
  OwnersSection head;
  if(flavor == OwnersFlavor::GitLab) {
    head.name = QStringLiteral("codeowners");
  }
  f.sections.append(head);
  qsizetype current = 0;
  static const QRegularExpression sectionRx(QStringLiteral(R"(^(\^)?\[([^\]]+)\](?:\[(\d+)\])?(?:\s+(.*))?$)"));
  const QStringList lines = text.split(QLatin1Char('\n'));
  for(qsizetype i = 0; i < lines.size(); ++i) {
    const QString line = lines.at(i).trimmed();
    const int lineNo = static_cast<int>(i) + 1;
    if(line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
      continue;
    }
    if(flavor == OwnersFlavor::GitLab) {
      const auto m = sectionRx.match(line);
      if(m.hasMatch()) {
        const QString name = m.captured(2).trimmed();
        qsizetype found = -1;
        for(qsizetype s = 0; s < f.sections.size(); ++s) {
          if(f.sections.at(s).name.compare(name, Qt::CaseInsensitive) == 0) {
            found = s;
            break;
          }
        }
        if(found < 0) {
          OwnersSection sec;
          sec.name = name;
          sec.optional = !m.captured(1).isEmpty();
          sec.approvals = m.captured(3).isEmpty() ? 1 : std::max(1, m.captured(3).toInt());
          sec.defaultOwners = splitOwners(m.captured(4));
          f.sections.append(sec);
          found = f.sections.size() - 1;
        }
        current = found;
        continue;
      }
    }
    // The pattern runs to the first whitespace that is not escaped.
    QString pat;
    qsizetype j = 0;
    while(j < line.size()) {
      const QChar ch = line.at(j);
      if(ch == QLatin1Char('\\') && j + 1 < line.size()) {
        pat += ch;
        pat += line.at(j + 1);
        j += 2;
        continue;
      }
      if(ch.isSpace()) {
        break;
      }
      pat += ch;
      ++j;
    }
    OwnersRule rule;
    rule.line = lineNo;
    rule.owners = splitOwners(line.mid(j));
    if(pat.startsWith(QLatin1Char('!'))) {
      if(flavor == OwnersFlavor::GitHub) {
        // GitHub has no negation; the line does nothing there.
        f.warnings << QStringLiteral("line %1: negation is not supported").arg(lineNo);
        continue;
      }
      rule.exclusion = true;
      pat = pat.mid(1);
    }
    if(pat.isEmpty()) {
      continue;
    }
    rule.pattern = pat;
    f.sections[current].rules.append(rule);
  }
  return f;
}

QList<OwnersNeed> ownersForFiles(const CodeOwnersFile& file, const QStringList& changedFiles, OwnersFlavor flavor) {
  QList<OwnersNeed> out;
  for(const OwnersSection& sec : file.sections) {
    QList<Compiled> compiled;
    compiled.reserve(sec.rules.size());
    for(const OwnersRule& r : sec.rules) {
      compiled.append(compile(r.pattern, flavor));
    }
    QHash<QString, qsizetype> byOwners;  // owner set → index in out
    for(const QString& rawPath : changedFiles) {
      QString path = rawPath.trimmed();
      while(path.startsWith(QLatin1Char('/'))) {
        path = path.mid(1);
      }
      if(path.isEmpty()) {
        continue;
      }
      const OwnersRule* winner = nullptr;
      bool excluded = false;
      for(qsizetype r = 0; r < sec.rules.size(); ++r) {
        if(!matchCompiled(compiled.at(r), path)) {
          continue;
        }
        if(sec.rules.at(r).exclusion) {
          excluded = true;
          break;
        }
        winner = &sec.rules.at(r);  // the last match wins
      }
      if(excluded || winner == nullptr) {
        continue;
      }
      const QStringList owners = winner->owners.isEmpty() && flavor == OwnersFlavor::GitLab ? sec.defaultOwners : winner->owners;
      if(owners.isEmpty()) {
        continue;  // owned by nobody: anyone with write access may approve
      }
      const QString key = owners.join(QLatin1Char(' '));
      auto it = byOwners.constFind(key);
      if(it == byOwners.constEnd()) {
        OwnersNeed need;
        need.section = sec.name;
        need.optional = sec.optional;
        need.approvals = sec.approvals;
        need.owners = owners;
        out.append(need);
        it = byOwners.insert(key, out.size() - 1);
      }
      QStringList& files = out[*it].files;
      if(!files.contains(path)) {
        files << path;
      }
    }
  }
  return out;
}

CodeOwnersReport buildOwnersReport(const QString& location,
                                   const QString& text,
                                   OwnersFlavor flavor,
                                   const QStringList& changedFiles,
                                   const QStringList& approvedBy,
                                   const QStringList& requested,
                                   const QString& author) {
  CodeOwnersReport rep;
  if(location.isEmpty()) {
    return rep;
  }
  rep.found = true;
  rep.location = location;
  const CodeOwnersFile file = parseCodeOwners(text, flavor);
  rep.warnings = file.warnings;
  for(const OwnersNeed& need : ownersForFiles(file, changedFiles, flavor)) {
    OwnersRequirement req;
    req.section = need.section;
    req.optional = need.optional;
    req.approvals = need.approvals;
    req.files = need.files;
    bool hasGroup = false;
    for(const QString& handle : need.owners) {
      OwnerState st;
      st.handle = handle;
      hasGroup = hasGroup || isGroupHandle(handle);
      const auto is = [&handle](const QString& who) {
        return handleMatches(handle, who, false);
      };
      const auto asked = [&handle](const QString& who) {
        return handleMatches(handle, who, true);
      };
      if(!author.isEmpty() && is(author)) {
        st.state = QStringLiteral("author");
      } else if(std::ranges::any_of(approvedBy, is)) {
        st.state = QStringLiteral("approved");
        ++req.approvedCount;
      } else if(std::ranges::any_of(requested, asked)) {
        st.state = QStringLiteral("requested");
      } else {
        st.state = QStringLiteral("missing");
      }
      req.owners.append(st);
    }
    // Approvals from people the file does not name: on a team or group
    // they may well count, Tabless cannot tell.
    const bool unnamedApproval = std::ranges::any_of(approvedBy, [&](const QString& who) {
      if(!author.isEmpty() && bare(who).compare(bare(author), Qt::CaseInsensitive) == 0) {
        return false;
      }
      return std::ranges::none_of(need.owners, [&who](const QString& h) {
        return handleMatches(h, who, false);
      });
    });
    if(req.approvedCount >= req.approvals) {
      req.state = QStringLiteral("approved");
    } else if(hasGroup && unnamedApproval) {
      req.state = QStringLiteral("maybe");
    } else {
      req.state = QStringLiteral("pending");
    }
    if(req.state == QLatin1String("pending") && !req.optional) {
      for(const OwnerState& o : req.owners) {
        if(o.state == QLatin1String("missing") && !containsCi(rep.toPing, o.handle)) {
          rep.toPing << o.handle;
        }
      }
    }
    rep.requirements.append(req);
  }
  return rep;
}

QVariantMap CodeOwnersReport::toVariant() const {
  QVariantList reqs;
  for(const OwnersRequirement& r : requirements) {
    QVariantList owners;
    for(const OwnerState& o : r.owners) {
      owners.append(QVariantMap{{QStringLiteral("handle"), o.handle}, {QStringLiteral("state"), o.state}});
    }
    reqs.append(QVariantMap{{QStringLiteral("section"), r.section},
                            {QStringLiteral("optional"), r.optional},
                            {QStringLiteral("approvals"), r.approvals},
                            {QStringLiteral("approvedCount"), r.approvedCount},
                            {QStringLiteral("state"), r.state},
                            {QStringLiteral("files"), r.files},
                            {QStringLiteral("owners"), owners}});
  }
  return {{QStringLiteral("found"), found},
          {QStringLiteral("location"), location},
          {QStringLiteral("requirements"), reqs},
          {QStringLiteral("toPing"), toPing},
          {QStringLiteral("warnings"), warnings}};
}

QString ghOwnersJsonFields() {
  return QStringLiteral("files,baseRefName,latestReviews,reviewRequests,author");
}

OwnersFacts parseGhOwnersFacts(const QByteArray& raw) {
  OwnersFacts f;
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
  if(err.error != QJsonParseError::NoError || !doc.isObject()) {
    return f;
  }
  const QJsonObject o = doc.object();
  f.ok = true;
  f.baseRef = o.value(QStringLiteral("baseRefName")).toString();
  f.author = o.value(QStringLiteral("author")).toObject().value(QStringLiteral("login")).toString();
  for(const auto& v : o.value(QStringLiteral("files")).toArray()) {
    const QString p = v.toObject().value(QStringLiteral("path")).toString();
    if(!p.isEmpty()) {
      f.files << p;
    }
  }
  for(const auto& v : o.value(QStringLiteral("latestReviews")).toArray()) {
    const QJsonObject r = v.toObject();
    if(r.value(QStringLiteral("state")).toString().toUpper() == QLatin1String("APPROVED")) {
      const QString who = r.value(QStringLiteral("author")).toObject().value(QStringLiteral("login")).toString();
      if(!who.isEmpty()) {
        f.approvedBy << who;
      }
    }
  }
  for(const auto& v : o.value(QStringLiteral("reviewRequests")).toArray()) {
    const QJsonObject r = v.toObject();
    QString who = r.value(QStringLiteral("login")).toString();
    if(who.isEmpty()) {
      who = r.value(QStringLiteral("slug")).toString();
    }
    if(who.isEmpty()) {
      who = r.value(QStringLiteral("name")).toString();
    }
    if(!who.isEmpty()) {
      f.requested << who;
    }
  }
  return f;
}

QStringList parseGlabApprovedBy(const QByteArray& raw) {
  QStringList out;
  const QJsonDocument doc = QJsonDocument::fromJson(raw);
  for(const auto& v : doc.object().value(QStringLiteral("approved_by")).toArray()) {
    const QString who = v.toObject().value(QStringLiteral("user")).toObject().value(QStringLiteral("username")).toString();
    if(!who.isEmpty()) {
      out << who;
    }
  }
  return out;
}

QStringList parseGlabChangedFiles(const QByteArray& raw, bool* ok) {
  QStringList out;
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
  const bool parsed = err.error == QJsonParseError::NoError && (doc.isArray() || doc.isObject());
  if(ok) {
    *ok = parsed;
  }
  const QJsonArray arr = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("changes")).toArray();
  QSet<QString> seen;
  for(const auto& v : arr) {
    const QJsonObject o = v.toObject();
    for(const QString& key : {QStringLiteral("old_path"), QStringLiteral("new_path")}) {
      const QString p = o.value(key).toString();
      if(!p.isEmpty() && !seen.contains(p)) {
        seen.insert(p);
        out << p;
      }
    }
  }
  return out;
}

}  // namespace heap::git
