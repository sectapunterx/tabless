#include "git/RemoteRepo.h"

#include <QRegularExpression>

namespace heap::git {

QString RemoteSpec::destination() const {
  return user.isEmpty() ? host : user + QLatin1Char('@') + host;
}

QString RemoteSpec::canonical() const {
  if(port > 0) {
    QString p = path;
    if(!p.startsWith(QLatin1Char('/'))) {
      p.prepend(QLatin1Char('/'));
    }
    return QStringLiteral("ssh://") + destination() + QLatin1Char(':') + QString::number(port) + p;
  }
  return destination() + QLatin1Char(':') + path;
}

QString RemoteSpec::shortName() const {
  QString p = path;
  while(p.endsWith(QLatin1Char('/')) && p.size() > 1) {
    p.chop(1);
  }
  return host + QLatin1Char(':') + p.section(QLatin1Char('/'), -1);
}

namespace {

bool validHost(const QString& h) {
  static const QRegularExpression rx(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
  // Two characters at least: "C:" is a drive, not a host.
  return h.size() >= 2 && rx.match(h).hasMatch();
}

bool validUser(const QString& u) {
  static const QRegularExpression rx(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9._-]*$"));
  return u.isEmpty() || rx.match(u).hasMatch();
}

}  // namespace

std::optional<RemoteSpec> parseRemoteSpec(const QString& text) {
  const QString t = text.trimmed();
  if(t.isEmpty()) {
    return std::nullopt;
  }
  RemoteSpec spec;
  if(t.startsWith(QStringLiteral("ssh://"), Qt::CaseInsensitive)) {
    const QString rest = t.mid(6);
    const qsizetype slash = rest.indexOf(QLatin1Char('/'));
    if(slash <= 0) {
      return std::nullopt;
    }
    QString auth = rest.left(slash);
    QString path = rest.mid(slash);
    if(path.startsWith(QStringLiteral("/~"))) {
      path = path.mid(1);  // ssh://host/~/src: relative to the home directory
    }
    const qsizetype at = auth.lastIndexOf(QLatin1Char('@'));
    if(at >= 0) {
      spec.user = auth.left(at);
      auth = auth.mid(at + 1);
    }
    const qsizetype colon = auth.indexOf(QLatin1Char(':'));
    if(colon >= 0) {
      bool ok = false;
      const int port = auth.mid(colon + 1).toInt(&ok);
      if(!ok || port <= 0 || port > 65535) {
        return std::nullopt;
      }
      spec.port = port;
      auth = auth.left(colon);
    }
    spec.host = auth;
    spec.path = path;
  } else {
    if(t.startsWith(QStringLiteral("\\\\")) || t.startsWith(QStringLiteral("//"))) {
      return std::nullopt;  // a UNC path
    }
    const qsizetype colon = t.indexOf(QLatin1Char(':'));
    if(colon <= 0) {
      return std::nullopt;
    }
    const QString before = t.left(colon);
    if(before.contains(QLatin1Char('/')) || before.contains(QLatin1Char('\\'))) {
      return std::nullopt;
    }
    const qsizetype at = before.lastIndexOf(QLatin1Char('@'));
    spec.user = at >= 0 ? before.left(at) : QString();
    spec.host = before.mid(at + 1);
    spec.path = t.mid(colon + 1);
  }
  if(!validHost(spec.host) || !validUser(spec.user) || spec.path.isEmpty() || spec.path.startsWith(QLatin1Char('\\'))) {
    return std::nullopt;
  }
  return spec;
}

bool isRemoteRepoSpec(const QString& text) {
  return parseRemoteSpec(text).has_value();
}

QStringList parseSshConfigHosts(const QString& text, QStringList* includes) {
  QStringList out;
  static const QRegularExpression kv(QStringLiteral(R"(^(\S+?)\s*(?:=\s*|\s+)(.*)$)"));
  static const QRegularExpression ws(QStringLiteral("\\s+"));
  for(const QString& raw : text.split(QLatin1Char('\n'))) {
    const QString line = raw.trimmed();
    if(line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
      continue;
    }
    const auto m = kv.match(line);
    if(!m.hasMatch()) {
      continue;
    }
    const QString key = m.captured(1).toLower();
    const QStringList values = m.captured(2).split(ws, Qt::SkipEmptyParts);
    if(key == QLatin1String("host")) {
      for(QString v : values) {
        if(v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')) && v.size() >= 2) {
          v = v.mid(1, v.size() - 2);
        }
        if(v.isEmpty() || v.contains(QLatin1Char('*')) || v.contains(QLatin1Char('?')) || v.startsWith(QLatin1Char('!'))) {
          continue;
        }
        if(!out.contains(v)) {
          out << v;
        }
      }
    } else if(key == QLatin1String("include") && includes) {
      *includes << values;
    }
  }
  return out;
}

bool sshSupportsControlMaster() {
#ifdef Q_OS_WIN
  return false;
#else
  return true;
#endif
}

SshOptions defaultSshOptions() {
  SshOptions o;
  o.controlMaster = sshSupportsControlMaster();
  o.controlPath = QStringLiteral("~/.ssh/tabless-%C");
  return o;
}

QStringList sshArguments(const RemoteSpec& spec, const SshOptions& options) {
  QStringList a{QStringLiteral("-o"),
                QStringLiteral("BatchMode=yes"),
                QStringLiteral("-o"),
                QStringLiteral("ConnectTimeout=%1").arg(options.connectTimeoutSec),
                QStringLiteral("-o"),
                QStringLiteral("ServerAliveInterval=15"),
                QStringLiteral("-o"),
                QStringLiteral("ServerAliveCountMax=2"),
                QStringLiteral("-T")};
  if(options.controlMaster && !options.controlPath.isEmpty()) {
    a << QStringLiteral("-o") << QStringLiteral("ControlMaster=auto") << QStringLiteral("-o")
      << QStringLiteral("ControlPath=") + options.controlPath << QStringLiteral("-o")
      << QStringLiteral("ControlPersist=%1").arg(options.controlPersistSec);
  }
  if(spec.port > 0) {
    a << QStringLiteral("-p") << QString::number(spec.port);
  }
  a << spec.destination() << QStringLiteral("sh") << QStringLiteral("-s");
  return a;
}

QString shellQuote(const QString& s) {
  QString out = s;
  out.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
  return QLatin1Char('\'') + out + QLatin1Char('\'');
}

namespace {

const QByteArray kMarker = QByteArrayLiteral("\n@@tabless-end:");
// Printed before the first command: whatever the remote shell's startup
// files print (.zshenv, BASH_ENV, an echo in them) lands before it.
const QByteArray kBegin = QByteArrayLiteral("@@tabless-begin@@");

QString quotePath(const QString& path) {
  if(path == QLatin1String("~")) {
    return QStringLiteral("\"$HOME\"");
  }
  if(path.startsWith(QStringLiteral("~/"))) {
    return QStringLiteral("\"$HOME\"/") + shellQuote(path.mid(2));
  }
  return shellQuote(path);
}

}  // namespace

QByteArray remoteGitScript(const QString& path, const QList<QStringList>& gitCommands) {
  QString s;
  s += QStringLiteral("export GIT_TERMINAL_PROMPT=0\n");
  s += QStringLiteral("cd -- %1 2>/dev/null || { echo 'tabless: no such directory' >&2; exit %2; }\n")
           .arg(quotePath(path))
           .arg(kRemoteNoPath);
  s += QStringLiteral("command -v git >/dev/null 2>&1 || { echo 'tabless: git not found' >&2; exit %1; }\n").arg(kRemoteNoGit);
  s += QStringLiteral("git rev-parse --is-inside-work-tree >/dev/null 2>&1 || { echo 'tabless: not a git repository' >&2; exit %1; }\n")
           .arg(kRemoteNotRepo);
  s += QStringLiteral("printf '@@tabless-begin@@\\n'\n");
  for(const QStringList& cmd : gitCommands) {
    QString line = QStringLiteral("git");
    for(const QString& arg : cmd) {
      line += QLatin1Char(' ') + shellQuote(arg);
    }
    line += QStringLiteral("; printf '\\n@@tabless-end:%s@@\\n' \"$?\"\n");
    s += line;
  }
  return s.toUtf8();
}

QList<GitResult> parseRemoteBatchOutput(const QByteArray& out, qsizetype commands) {
  QList<GitResult> results;
  // From the begin marker on. An answer without one (the script stopped
  // before it, a canned test answer) is read from the start.
  const qsizetype begin = out.indexOf(kBegin);
  qsizetype pos = 0;
  if(begin >= 0) {
    pos = begin + kBegin.size();
    if(pos < out.size() && out.at(pos) == '\r') {
      ++pos;
    }
    if(pos < out.size() && out.at(pos) == '\n') {
      ++pos;
    }
  }
  for(qsizetype i = 0; i < commands; ++i) {
    GitResult r;
    const qsizetype idx = out.indexOf(kMarker, pos);
    if(idx < 0) {
      results.append(r);
      pos = out.size();
      continue;
    }
    r.out = out.mid(pos, idx - pos);
    const qsizetype after = idx + kMarker.size();
    const qsizetype end = out.indexOf("@@", after);
    if(end < 0) {
      results.append(r);
      pos = out.size();
      continue;
    }
    bool ok = false;
    const int code = out.mid(after, end - after).toInt(&ok);
    r.code = ok ? code : -1;
    results.append(r);
    pos = end + 2;
    if(pos < out.size() && out.at(pos) == '\n') {
      ++pos;
    }
  }
  return results;
}

RemoteFailure classifyRemoteFailure(int exitCode, const QByteArray& stderrText) {
  if(exitCode == 0) {
    return RemoteFailure::None;
  }
  if(exitCode == kRemoteNoPath) {
    return RemoteFailure::NoPath;
  }
  if(exitCode == kRemoteNoGit) {
    return RemoteFailure::NoGit;
  }
  if(exitCode == kRemoteNotRepo) {
    return RemoteFailure::NotRepo;
  }
  const QString e = QString::fromUtf8(stderrText).toLower();
  const auto has = [&e](const char* needle) {
    return e.contains(QLatin1String(needle));
  };
  // GitRunner's own notes, when the process never ran or was killed.
  if(has("tabless: timed out")) {
    return RemoteFailure::Timeout;
  }
  if(has("tabless: did not start") || has("tabless: no ssh")) {
    return RemoteFailure::NoSsh;
  }
  if(has("host key verification failed") || has("remote host identification has changed") || has("no matching host key")) {
    return RemoteFailure::HostKey;
  }
  if(has("permission denied") || has("too many authentication failures") || has("no supported authentication methods") ||
     has("authentication failed")) {
    return RemoteFailure::Auth;
  }
  if(has("could not resolve hostname") || has("name or service not known") || has("nodename nor servname") || has("no route to host") ||
     has("connection refused") || has("connection timed out") || has("operation timed out") || has("network is unreachable") ||
     has("connection closed by") || has("no address associated")) {
    return RemoteFailure::Unreachable;
  }
  if(exitCode == 127 && has("git")) {
    return RemoteFailure::NoGit;
  }
  return RemoteFailure::Other;
}

QString remoteFailureKey(RemoteFailure f) {
  switch(f) {
    case RemoteFailure::None:
      return {};
    case RemoteFailure::NoSsh:
      return QStringLiteral("noSsh");
    case RemoteFailure::Unreachable:
      return QStringLiteral("unreachable");
    case RemoteFailure::Auth:
      return QStringLiteral("auth");
    case RemoteFailure::HostKey:
      return QStringLiteral("hostKey");
    case RemoteFailure::NoPath:
      return QStringLiteral("noPath");
    case RemoteFailure::NoGit:
      return QStringLiteral("noGit");
    case RemoteFailure::NotRepo:
      return QStringLiteral("notRepo");
    case RemoteFailure::Timeout:
      return QStringLiteral("timeout");
    case RemoteFailure::Other:
      break;
  }
  return QStringLiteral("other");
}

ForgeRepo forgeRepoFromRemoteUrl(const QString& url) {
  ForgeRepo r;
  QString u = url.trimmed();
  if(u.isEmpty()) {
    return r;
  }
  QString hostPart;
  QString path;
  static const QRegularExpression schemeRx(QStringLiteral("^([a-z][a-z0-9+.-]*)://([^/]+)(/.*)?$"),
                                           QRegularExpression::CaseInsensitiveOption);
  const auto m = schemeRx.match(u);
  if(m.hasMatch()) {
    hostPart = m.captured(2);
    path = m.captured(3);
    const qsizetype at = hostPart.lastIndexOf(QLatin1Char('@'));
    if(at >= 0) {
      hostPart = hostPart.mid(at + 1);
    }
    const qsizetype colon = hostPart.indexOf(QLatin1Char(':'));
    if(colon >= 0) {
      hostPart = hostPart.left(colon);
    }
  } else {
    // scp-like: git@github.com:owner/name.git
    const qsizetype colon = u.indexOf(QLatin1Char(':'));
    if(colon <= 0) {
      return r;
    }
    hostPart = u.left(colon);
    const qsizetype at = hostPart.lastIndexOf(QLatin1Char('@'));
    if(at >= 0) {
      hostPart = hostPart.mid(at + 1);
    }
    path = u.mid(colon + 1);
  }
  while(path.startsWith(QLatin1Char('/'))) {
    path = path.mid(1);
  }
  while(path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  if(path.endsWith(QStringLiteral(".git"))) {
    path.chop(4);
  }
  r.host = hostPart.toLower();
  r.path = path;
  return r;
}

QString ghRepoArg(const ForgeRepo& repo) {
  if(!repo.valid()) {
    return {};
  }
  return repo.host == QLatin1String("github.com") ? repo.path : repo.host + QLatin1Char('/') + repo.path;
}

QString glabRepoArg(const ForgeRepo& repo) {
  if(!repo.valid()) {
    return {};
  }
  return QStringLiteral("https://") + repo.host + QLatin1Char('/') + repo.path;
}

QList<QStringList> remotePollCommands() {
  return {
      {QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("--short"), QStringLiteral("HEAD")},
      {QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"), QStringLiteral("HEAD")},
      {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("--symbolic-full-name"), QStringLiteral("@{u}")},
      {QStringLiteral("rev-list"), QStringLiteral("--left-right"), QStringLiteral("--count"), QStringLiteral("HEAD...@{u}")},
      {QStringLiteral("config"), QStringLiteral("--get"), QStringLiteral("remote.origin.url")},
  };
}

RemoteHead parseRemotePoll(const QList<GitResult>& results) {
  RemoteHead h;
  if(results.size() < 5 || results.at(0).code < 0 || results.at(1).code < 0) {
    return h;
  }
  h.ok = true;
  if(results.at(1).code == 0) {
    h.sha = QString::fromUtf8(results.at(1).out).trimmed();
  }
  if(results.at(0).code == 0) {
    h.branch = QString::fromUtf8(results.at(0).out).trimmed();
  } else if(!h.sha.isEmpty()) {
    h.branch = QStringLiteral("(detached HEAD)");
  }
  if(results.at(2).code == 0) {
    h.upstream = QString::fromUtf8(results.at(2).out).trimmed();
  }
  if(results.at(3).code == 0) {
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const QStringList parts = QString::fromUtf8(results.at(3).out).trimmed().split(ws, Qt::SkipEmptyParts);
    if(parts.size() == 2) {
      h.ahead = parts.at(0).toInt();
      h.behind = parts.at(1).toInt();
    }
  }
  if(results.at(4).code == 0) {
    h.originUrl = QString::fromUtf8(results.at(4).out).trimmed();
  }
  return h;
}

}  // namespace heap::git
