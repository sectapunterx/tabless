#include "NotificationCenter.h"

#include "platform/BuildInfo.h"

#include <QAction>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QVariantMap>

namespace heap::notify {

namespace {

// The tray shows the glyph alone, in one colour (APP-280): the full app icon
// with its tile turns to mush at 16 px on a taskbar. macOS recolours a
// template image for the menu bar itself; elsewhere the glyph is white on a
// dark taskbar or panel and near-black on a light one.
QIcon trayIcon() {
#ifdef Q_OS_MACOS
  QIcon icon(QStringLiteral(":/brand/tabless/tabless-tray-template.svg"));
  icon.setIsMask(true);
#else
  // colorScheme() is Qt 6.5+; the clang-tidy job builds against the distro's
  // older Qt, where a dark panel is the safer guess.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  const bool dark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
#else
  const bool dark = true;
#endif
  QIcon icon(dark ? QStringLiteral(":/brand/tabless/tabless-tray-light.svg") : QStringLiteral(":/brand/tabless/tabless-tray-dark.svg"));
#endif
  return icon.isNull() ? QGuiApplication::windowIcon() : icon;
}

// Fallback backend: legacy QSystemTrayIcon::showMessage. No action buttons
// (the Shell_NotifyIcon balloon API does not support them). Clicking the
// message body translates to `activated(id)`.
class TrayBackend : public NotificationCenter {
  Q_OBJECT
 public:
  explicit TrayBackend(QObject* parent) : NotificationCenter(parent) {
    if(!QSystemTrayIcon::isSystemTrayAvailable()) {
      return;
    }
    m_tray = new QSystemTrayIcon(trayIcon(), this);
    m_tray->setToolTip(heap::build::productTitle());

    // Context menu so the app is controllable while running windowless (the
    // window hides to the tray on close). QMenu needs QtWidgets, which the app
    // already links. Parented to no widget — QSystemTrayIcon owns it via
    // setContextMenu. The items come from the app (setTrayMenu), refreshed
    // each time it opens so the timer and the next meeting are current.
    auto* menu = new QMenu();
    connect(menu, &QMenu::aboutToShow, this, &NotificationCenter::trayMenuAboutToShow, Qt::DirectConnection);
    m_tray->setContextMenu(menu);
    m_menu = menu;
    setTrayMenu(QStringLiteral("Tabless"),
                {{QStringLiteral("open"), QStringLiteral("Open Tabless"), {}, true},
                 {},
                 {QStringLiteral("quit"), QStringLiteral("Quit"), {}, true}});

    m_tray->show();
    // A left click / double click on the icon restores the window; a click on a
    // notification balloon still routes to the owning task via activated(id).
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
      if(reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        emit showWindowRequested();
      }
    });
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this]() {
      if(!m_lastId.isEmpty()) {
        emit activated(m_lastId);
      }
    });
  }

  ~TrayBackend() override {
    delete m_menu;
  }

  void setTrayMenu(const QString& header, const QVector<TrayItem>& items) override {
    if(!m_menu) {
      return;
    }
    m_menu->clear();
    QAction* head = m_menu->addAction(header);
    head->setEnabled(false);
    for(const TrayItem& item : items) {
      if(item.id.isEmpty() && item.text.isEmpty()) {
        m_menu->addSeparator();
        continue;
      }
      // "text<Tab>hint": QMenu puts the hint in the key column on the right.
      QAction* a = m_menu->addAction(item.hint.isEmpty() ? item.text : item.text + QLatin1Char('\t') + item.hint);
      a->setEnabled(item.enabled);
      const QString id = item.id;
      connect(a, &QAction::triggered, this, [this, id]() {
        if(id == QLatin1String("open")) {
          emit showWindowRequested();
        } else if(id == QLatin1String("quit")) {
          emit quitRequested();
        } else {
          emit trayItemTriggered(id);
        }
      });
    }
  }

  void setTrayToolTip(const QString& text) override {
    if(m_tray) {
      m_tray->setToolTip(text);
    }
  }

  void post(const Notification& n) override {
    if(!m_tray) {
      return;
    }
    m_lastId = n.id;
    const int ms = n.durationSec > 0 ? n.durationSec * 1000 : 5000;
    m_tray->showMessage(n.title, n.body, QSystemTrayIcon::Information, ms);
  }

  void dismiss(const QString& /*id*/) override {
    // Shell_NotifyIcon balloons time out on their own — no API to dismiss
    // a specific one. Intentionally no-op.
  }

  bool supportsActions() const override {
    return false;
  }

 private:
  QSystemTrayIcon* m_tray{};
  QMenu* m_menu{};
  QString m_lastId;
};

}  // namespace

std::unique_ptr<NotificationCenter> createTrayFallback(QObject* parent) {
  return std::make_unique<TrayBackend>(parent);
}

}  // namespace heap::notify

#include "NotificationCenter_tray.moc"
