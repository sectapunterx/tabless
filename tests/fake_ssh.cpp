// A stand-in for `ssh` in heap_git_remote_tests: no server, no network.
//
// It logs its arguments and the script it got on stdin to $FAKE_SSH_LOG, then
// either answers with canned output ($FAKE_SSH_MODE=canned: $FAKE_SSH_STDOUT
// is a file whose content goes to stdout, $FAKE_SSH_STDERR text to stderr,
// $FAKE_SSH_EXIT the exit code) or plays the server by running the script
// with the local `sh -s` in $FAKE_SSH_HOME ($FAKE_SSH_MODE=sh).

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

#include <cstdio>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QFile in;
  if(!in.open(stdin, QIODevice::ReadOnly)) {
    return 250;
  }
  const QByteArray script = in.readAll();
  const QByteArray log = qgetenv("FAKE_SSH_LOG");
  if(!log.isEmpty()) {
    QFile f(QString::fromLocal8Bit(log));
    if(f.open(QIODevice::Append)) {
      for(const QString& a : app.arguments().mid(1)) {
        f.write(a.toUtf8() + '\n');
      }
      f.write("---\n" + script + "===\n");
    }
  }
  const QByteArray mode = qgetenv("FAKE_SSH_MODE");
  if(mode == "sh") {
    const QString sh = QStandardPaths::findExecutable(QStringLiteral("sh"));
    if(sh.isEmpty()) {
      std::fputs("fake ssh: no sh\n", stderr);
      return 255;
    }
    QProcess p;
    p.setWorkingDirectory(QString::fromLocal8Bit(qgetenv("FAKE_SSH_HOME")));
    p.start(sh, {QStringLiteral("-s")});
    p.write(script);
    p.closeWriteChannel();
    p.waitForFinished(60000);
    const QByteArray out = p.readAllStandardOutput();
    const QByteArray err = p.readAllStandardError();
    std::fwrite(out.constData(), 1, static_cast<size_t>(out.size()), stdout);
    std::fwrite(err.constData(), 1, static_cast<size_t>(err.size()), stderr);
    return p.exitCode();
  }
  const QByteArray outFile = qgetenv("FAKE_SSH_STDOUT");
  if(!outFile.isEmpty()) {
    QFile f(QString::fromLocal8Bit(outFile));
    if(f.open(QIODevice::ReadOnly)) {
      const QByteArray out = f.readAll();
      std::fwrite(out.constData(), 1, static_cast<size_t>(out.size()), stdout);
    }
  }
  const QByteArray err = qgetenv("FAKE_SSH_STDERR");
  std::fwrite(err.constData(), 1, static_cast<size_t>(err.size()), stderr);
  return qEnvironmentVariableIntValue("FAKE_SSH_EXIT");
}
