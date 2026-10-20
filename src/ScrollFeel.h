#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;
class QQuickItem;
class QQuickWindow;
class QVariantAnimation;
class QWheelEvent;

// How every scrollable view in the app answers the mouse (dogfood
// 2026-10-20: "scrolling is too fast; make it like other programs; and the
// middle button should scroll").
//
// Installed once on the application, it stands in for Flickable's own wheel
// handling (a velocity flick that sped up the harder you turned) in every
// Flickable, ListView, ScrollView and TableView:
//   - a notch of a classic wheel moves the system's lines per notch
//     (QStyleHints::wheelScrollLines, 3 by default) of one text line at the
//     UI scale, gliding there in Theme.durScroll (none with Reduce motion);
//   - a touchpad or a high-resolution wheel follows the fingers 1:1;
//   - the innermost view that can still move takes the wheel; one that runs
//     out mid-gesture keeps it (no sudden slide of the view around it), and
//     a gesture that began on the outer view stays with it;
//   - Ctrl+wheel is left alone (zoom where a view has it).
// Middle-click autoscroll, as in a browser: a middle press over a view that
// can scroll drops a marker; the view then moves with a speed that grows
// with the pointer's distance from it. A second click, any key, Esc or
// any other button stops it; held and dragged, it stops on release.
//
// An item lying over a view without being inside it (the note's source
// field over its drawn blocks) names the view in a `wheelScrollTarget`
// property and gets the same wheel.
class ScrollFeel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  // One line of text at the UI scale, in pixels (Theme sets it).
  Q_PROPERTY(double lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged)
  // The glide of a wheel notch, ms; 0 = jump (Theme.durScroll).
  Q_PROPERTY(int duration READ duration WRITE setDuration NOTIFY durationChanged)
  Q_PROPERTY(bool autoscrolling READ autoscrolling NOTIFY autoscrollingChanged)

 public:
  static ScrollFeel* instance();
  static ScrollFeel* create(QQmlEngine* qml, QJSEngine* js);

  double lineHeight() const {
    return line_height_;
  }

  void setLineHeight(double v);

  int duration() const {
    return duration_;
  }

  void setDuration(int v);

  bool autoscrolling() const {
    return !auto_.isNull();
  }

  // The pixels one notch of the wheel moves now.
  Q_INVOKABLE double notchPixels() const;
  Q_INVOKABLE void stopAutoscroll();

 signals:
  void lineHeightChanged();
  void durationChanged();
  void autoscrollingChanged();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  explicit ScrollFeel(QObject* parent = nullptr);

  bool wheel(QQuickItem* flick, QWheelEvent* event);
  void scrollTo(QQuickItem* flick, bool horizontal, double target, bool animate);
  double scrollBase(QQuickItem* flick, bool horizontal) const;
  QVariantAnimation* animation(QQuickItem* flick, bool horizontal);

  bool startAutoscroll(QQuickWindow* window, QPointF scene_pos);
  void tick();
  void updateCursor(double vx, double vy);

  double line_height_ = 20;
  int duration_ = 120;

  // The view a wheel gesture is latched to, and when it last moved.
  QPointer<QQuickItem> latched_;
  QElapsedTimer latch_clock_;
  // A wheel event an inner view at its edge let through: the view around
  // it must not turn it sideways.
  bool edge_valid_ = false;
  QHash<QQuickItem*, QVariantAnimation*> anim_y_;
  QHash<QQuickItem*, QVariantAnimation*> anim_x_;

  // Autoscroll.
  QPointer<QQuickItem> auto_;
  QPointer<QQuickWindow> auto_window_;
  QPointer<QQuickItem> marker_;
  QPointF origin_;
  QPointF pointer_;
  bool auto_x_ = false;
  bool auto_y_ = false;
  bool button_down_ = false;  // the middle button still held since the start
  bool dragged_ = false;      // ...and moved out of the dead zone meanwhile
  Qt::MouseButton eat_release_ = Qt::NoButton;
  bool eat_key_ = false;
  bool cursor_set_ = false;
  Qt::CursorShape cursor_shape_ = Qt::ArrowCursor;
  QTimer tick_;
  QElapsedTimer frame_clock_;
};
