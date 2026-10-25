#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QNetworkProxyFactory>
#include <QPair>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>

// A local stand-in for a tracker / identity provider, shared by the network
// tests. The integration code owns its QNetworkAccessManager and has no
// transport seam, so the tests point it at a real socket instead: a minimal
// HTTP/1.1 server answering a fixed "METHOD /path" route table — just enough
// for QNetworkAccessManager. Header-only so a test target only has to include it.

namespace heap::testing {

class FakeHttpServer {
 public:
  struct Response {
    int status = 200;
    QByteArray body = "{}";
    QList<QPair<QByteArray, QByteArray>> headers;  // extra response headers (e.g. Mattermost's "Token")
  };

  // One request as the server received it. Header names are lower-cased.
  struct Request {
    QByteArray method;
    QByteArray path;   // without the query string
    QByteArray query;  // raw, without the '?'
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;

    QByteArray key() const {
      return method + " " + path;
    }

    // The same, with the query string kept. A paginating endpoint answers the
    // same path several times and has to be able to answer each page
    // differently, so a route may be registered either way.
    QByteArray keyWithQuery() const {
      return query.isEmpty() ? key() : key() + "?" + query;
    }
  };

  FakeHttpServer() {
    // Requests go to loopback: never ask the OS for a proxy. On the macOS
    // runners that lookup (CFNetwork) intermittently crashed the first request
    // of a suite (heap_jira_tests, heap_int_audit_tests: SegFault in ~0.05 s).
    // An application proxy a test sets explicitly is still honoured.
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    m_server.listen(QHostAddress::LocalHost, 0);
    QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
      while(m_server.hasPendingConnections()) {
        acceptOne();
      }
    });
  }

  QString base() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
  }

  // Register "METHOD /path", or "METHOD /path?query" to answer one page of a
  // walk. The more specific route wins.
  void route(const QByteArray& key, const Response& r) {
    m_routes.insert(key, r);
  }

  // Answer this route with each response in turn, the last one repeating. For
  // a retry test (429 then 200) or a walk whose pages differ but whose URL
  // the test would rather not spell out.
  void routeSequence(const QByteArray& key, const QList<Response>& responses) {
    m_sequences.insert(key, responses);
    m_sequenceAt.insert(key, 0);
  }

  // "METHOD /path" of every request, in arrival order.
  const QList<QByteArray>& seen() const {
    return m_seen;
  }

  const QList<Request>& requests() const {
    return m_requests;
  }

  // The most recent request for a route, or an empty Request when it never came.
  Request lastRequest(const QByteArray& key) const {
    for(auto it = m_requests.crbegin(); it != m_requests.crend(); ++it) {
      if(it->key() == key) {
        return *it;
      }
    }
    return {};
  }

  QByteArray lastBody() const {
    return m_requests.isEmpty() ? QByteArray() : m_requests.last().body;
  }

  // The body of the most recent request for a route. A pull queues follow-up
  // reads (statuses, time tracking) once its answer is out, and on a fast
  // machine they reach the server before the test looks: lastBody() is then
  // an empty GET's, not the search's.
  QByteArray lastBody(const QByteArray& key) const {
    return lastRequest(key).body;
  }

 private:
  static int contentLength(const QByteArray& headers) {
    const QList<QByteArray> lines = headers.split('\n');
    for(const QByteArray& line : lines) {
      const QByteArray trimmed = line.trimmed();
      if(trimmed.toLower().startsWith("content-length:")) {
        return trimmed.mid(trimmed.indexOf(':') + 1).trimmed().toInt();
      }
    }
    return 0;
  }

  static QByteArray reason(int status) {
    switch(status) {
      case 200:
        return "OK";
      case 201:
        return "Created";
      case 302:
        return "Found";
      case 400:
        return "Bad Request";
      case 401:
        return "Unauthorized";
      case 403:
        return "Forbidden";
      case 429:
        return "Too Many Requests";
      default:
        return "Not Found";
    }
  }

  void acceptOne() {
    QTcpSocket* sock = m_server.nextPendingConnection();
    QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock]() {
      m_buffers[sock] += sock->readAll();
      const QByteArray buf = m_buffers.value(sock);
      const int headerEnd = static_cast<int>(buf.indexOf("\r\n\r\n"));
      if(headerEnd < 0) {
        return;
      }
      if(buf.size() < headerEnd + 4 + contentLength(buf.left(headerEnd))) {
        return;  // body still arriving
      }
      m_buffers.remove(sock);
      respond(sock, buf, headerEnd);
    });
    QObject::connect(sock, &QTcpSocket::disconnected, sock, [this, sock]() {
      m_buffers.remove(sock);
      sock->deleteLater();
    });
  }

  void respond(QTcpSocket* sock, const QByteArray& raw, int headerEnd) {
    Request req;
    const QList<QByteArray> headerLines = raw.left(headerEnd).split('\n');
    const QList<QByteArray> parts = headerLines.value(0).trimmed().split(' ');
    req.method = parts.value(0);
    req.path = parts.value(1);
    const int q = static_cast<int>(req.path.indexOf('?'));
    if(q >= 0) {
      req.query = req.path.mid(q + 1);
      req.path = req.path.left(q);
    }
    for(int i = 1; i < headerLines.size(); ++i) {
      const QByteArray line = headerLines.at(i).trimmed();
      const int colon = static_cast<int>(line.indexOf(':'));
      if(colon > 0) {
        req.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
      }
    }
    req.body = raw.mid(headerEnd + 4);
    m_seen.append(req.key());
    m_requests.append(req);

    const Response r = responseFor(req);
    QByteArray out = "HTTP/1.1 " + QByteArray::number(r.status) + " " + reason(r.status) + "\r\n";
    out += "Content-Type: application/json\r\n";
    for(const auto& h : r.headers) {
      out += h.first + ": " + h.second + "\r\n";
    }
    out += "Content-Length: " + QByteArray::number(r.body.size()) + "\r\n";
    out += "Connection: close\r\n\r\n";
    out += r.body;
    sock->write(out);
    sock->flush();
    sock->disconnectFromHost();
  }

  // A query-qualified route beats the bare path, and a sequence beats a single
  // response, so a test can pin one page without unregistering the rest.
  Response responseFor(const Request& req) {
    for(const QByteArray& key : {req.keyWithQuery(), req.key()}) {
      if(m_sequences.contains(key)) {
        const QList<Response>& seq = m_sequences[key];
        if(!seq.isEmpty()) {
          int& at = m_sequenceAt[key];
          const Response r = seq.at(qMin(at, seq.size() - 1));
          ++at;
          return r;
        }
      }
      if(m_routes.contains(key)) {
        return m_routes.value(key);
      }
    }
    return Response{404, "{}", {}};
  }

  // The sockets are children of m_server, which goes last; they emit
  // disconnected while it deletes them, after m_buffers is already gone.
  struct SocketGuard {
    QTcpServer* server;

    ~SocketGuard() {
      for(QTcpSocket* s : server->findChildren<QTcpSocket*>()) {
        QObject::disconnect(s, nullptr, nullptr, nullptr);
      }
    }
  };

  QTcpServer m_server;
  QHash<QByteArray, Response> m_routes;
  QHash<QByteArray, QList<Response>> m_sequences;
  QHash<QByteArray, int> m_sequenceAt;
  QHash<QTcpSocket*, QByteArray> m_buffers;
  QList<QByteArray> m_seen;
  QList<Request> m_requests;
  SocketGuard m_guard{&m_server};  // last member: runs first on destruction
};

// The code under test answers from the event loop, so the test has to run one.
inline bool waitFor(const bool& done, int ms = 5000) {
  QElapsedTimer t;
  t.start();
  while(!done && t.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return done;
}

// Same, for a condition the test can only poll (e.g. "the server saw the call").
inline bool waitUntil(const std::function<bool()>& condition, int ms = 5000) {
  QElapsedTimer t;
  t.start();
  while(!condition() && t.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return condition();
}

}  // namespace heap::testing
