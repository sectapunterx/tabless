#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

// A watched repository that lives on an SSH dev server (dogfood 2026-10-19).
//
// The pure half: reading `ssh://[user@]host[:port]/path` and `host:/path`,
// building the `ssh` command line and the small POSIX script that runs git
// there, splitting the script's answer back into one result per git command,
// and telling why a connection failed. No process here, so every rule is
// testable without a server.
namespace heap::git {

struct RemoteSpec {
  QString user;  // empty: whatever ~/.ssh/config or the local user says
  QString host;  // a name, an address, or a Host alias from ~/.ssh/config
  int port = 0;  // 0: not given
  QString path;  // on the server; "~/src/app" is relative to the home dir

  // "user@host" or "host": what ssh connects to.
  QString destination() const;
  // The one spelling settings keep: "host:/path", "user@host:/path", or
  // "ssh://user@host:2222/path" when a port is given.
  QString canonical() const;
  // "host:app" for a short label.
  QString shortName() const;
};

// `ssh://[user@]host[:port]/path` or scp-like `[user@]host:path`. A Windows
// drive ("C:/src", "C:\src") and a UNC path are not remote. nullopt when the
// text is not a remote repository.
std::optional<RemoteSpec> parseRemoteSpec(const QString& text);
bool isRemoteRepoSpec(const QString& text);

// `Host` names from an ssh_config, without patterns (`*`, `?`, `!`) — the ones
// a person can pick. `includes` receives `Include` arguments, as written.
QStringList parseSshConfigHosts(const QString& text, QStringList* includes = nullptr);

struct SshOptions {
  int connectTimeoutSec = 10;
  // OpenSSH's connection sharing. Off on Windows: its OpenSSH port has no
  // ControlMaster, so there every poll is one fresh connection carrying all
  // its git commands in one script.
  bool controlMaster = false;
  QString controlPath;  // "~/.ssh/tabless-%C"
  int controlPersistSec = 120;
};

// Whether this platform's OpenSSH can share a connection.
bool sshSupportsControlMaster();
SshOptions defaultSshOptions();

// The arguments after `ssh`: never asks for a password (BatchMode), gives up
// on a dead host quickly, and runs `sh -s`, which reads the script from
// standard input — so the remote login shell, whatever it is, only ever sees
// two plain words.
QStringList sshArguments(const RemoteSpec& spec, const SshOptions& options);

// POSIX sh single quotes.
QString shellQuote(const QString& s);

// Exit codes of the script's own checks, before any git command runs.
inline constexpr int kRemoteNoPath = 96;
inline constexpr int kRemoteNoGit = 97;
inline constexpr int kRemoteNotRepo = 98;

// The script for `sh -s`: change to the repository (failing with the codes
// above), then each git command, each followed by a marker line carrying its
// exit code, so one connection answers many questions.
QByteArray remoteGitScript(const QString& path, const QList<QStringList>& gitCommands);

struct GitResult {
  int code = -1;  // -1: did not run, or did not finish
  QByteArray out;
  QByteArray err;
};

// Splits the script's stdout into one result per command. Commands the
// output does not reach (the connection dropped) come back with code -1.
QList<GitResult> parseRemoteBatchOutput(const QByteArray& out, qsizetype commands);

enum class RemoteFailure : std::uint8_t {
  None,
  NoSsh,        // no ssh binary on this machine
  Unreachable,  // name does not resolve, refused, timed out, no route
  Auth,         // needs an agent or a key (BatchMode never asks)
  HostKey,      // unknown or changed host key
  NoPath,       // the directory does not exist
  NoGit,        // git is not installed there
  NotRepo,      // the directory is not a git work tree
  Timeout,      // Tabless gave up waiting
  Other,
};

// From ssh's exit code and stderr (255 is ssh's own failure; the script's
// checks have their own codes).
RemoteFailure classifyRemoteFailure(int exitCode, const QByteArray& stderrText);
// "unreachable", "auth", … — an I18n key suffix; "" for None.
QString remoteFailureKey(RemoteFailure f);

// "owner/name" (or "group/sub/name") and the host from an origin URL:
// https://github.com/o/n.git, git@github.com:o/n.git, ssh://git@host:22/o/n.
struct ForgeRepo {
  QString host;
  QString path;

  bool valid() const {
    return !host.isEmpty() && path.contains(QLatin1Char('/'));
  }
};

ForgeRepo forgeRepoFromRemoteUrl(const QString& url);
// What `gh -R` takes: "owner/name" on github.com, "host/owner/name" elsewhere.
QString ghRepoArg(const ForgeRepo& repo);
// What `glab -R` takes: the full https URL works with any instance.
QString glabRepoArg(const ForgeRepo& repo);

// What one poll of a remote repository learns, from the batch in
// remotePollCommands().
struct RemoteHead {
  QString branch;  // "(detached HEAD)" when detached, "" when unknown
  QString sha;
  QString upstream;  // "origin/main", "" with none
  int ahead = 0;
  int behind = 0;
  QString originUrl;
  bool ok = false;
};

QList<QStringList> remotePollCommands();
RemoteHead parseRemotePoll(const QList<GitResult>& results);

}  // namespace heap::git
