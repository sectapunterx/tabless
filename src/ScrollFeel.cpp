#include "ScrollFeel.h"
#include "ScrollMath.h"

#include <QCoreApplication>
#include <QCursor>
#include <QEasingCurve>
#include <QGuiApplication>
#include <QJSEngine>
#include <QKeyEvent>
#include <QLineF>
#include <QList>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

// A wheel gesture stays with the view it started on while its notches come
// closer together than this.
constexpr int kLatchMs = 400;
// Autoscroll: the pointer may wander this far (at 100 %) before anything moves.
constexpr double kDeadZone = 10;
constexpr int kFrameMs = 16;
// The text line the wheel counts in, at 100 %; Theme sets the scaled one.
constexpr double kBaseLine = 20;
// A wheel gesture past this many of its view's screens is a long one.
constexpr double kLongWheelScreens = 3;

bool isFlickable(const QObject* o) {
  return o != nullptr && o->inherits("QQuickFlickable");
}

double num(const QObject* o, const char* name) {
  return o->property(name).toDouble();
}

struct Span {
  double lo = 0;
  double hi = 0;
};

// Where contentX/contentY may go, margins and origin included.
Span span(const QQuickItem* f, bool horizontal) {
  if(horizontal) {
    const double lo = num(f, "originX") - num(f, "leftMargin");
    const double hi = num(f, "originX") + num(f, "contentWidth") + num(f, "rightMargin") - f->width();
    return {lo, std::max(lo, hi)};
  }
  const double lo = num(f, "originY") - num(f, "topMargin");
  const double hi = num(f, "originY") + num(f, "contentHeight") + num(f, "bottomMargin") - f->height();
  return {lo, std::max(lo, hi)};
}

// Flickable.flickableDirection: AutoFlickDirection 0, HorizontalFlick 1,
// VerticalFlick 2, HorizontalAndVerticalFlick 3.
bool allows(const QQuickItem* f, bool horizontal) {
  const int dir = f->property("flickableDirection").toInt();
  if(dir == 0) {
    return horizontal ? std::abs(num(f, "contentWidth") - f->width()) > 0.5 : std::abs(num(f, "contentHeight") - f->height()) > 0.5;
  }
  return horizontal ? (dir & 1) != 0 : (dir & 2) != 0;
}

bool canScroll(const QQuickItem* f, bool horizontal) {
  if(!allows(f, horizontal)) {
    return false;
  }
  const Span s = span(f, horizontal);
  return s.hi - s.lo > 0.5;
}

// A view around this one that scrolls up and down.
bool verticalAncestor(const QQuickItem* f) {
  for(const QQuickItem* p = f->parentItem(); p != nullptr; p = p->parentItem()) {
    if(isFlickable(p) && p->property("interactive").toBool() && canScroll(p, false)) {
      return true;
    }
  }
  return false;
}

// Every item under a scene point, topmost first, the way Qt Quick paints
// them: children above their parent (by z, then order), negative z below.
void collect(QQuickItem* item, QPointF scene_pos, QList<QQuickItem*>& out) {
  if(!item->isVisible() || !item->isEnabled()) {
    return;
  }
  const bool inside = item->contains(item->mapFromScene(scene_pos));
  if(item->clip() && !inside) {
    return;
  }
  QList<QQuickItem*> kids = item->childItems();
  std::stable_sort(kids.begin(), kids.end(), [](const QQuickItem* a, const QQuickItem* b) {
    return a->z() < b->z();
  });
  for(auto it = kids.crbegin(); it != kids.crend(); ++it) {
    if((*it)->z() >= 0) {
      collect(*it, scene_pos, out);
    }
  }
  if(inside) {
    out.append(item);
  }
  for(auto it = kids.crbegin(); it != kids.crend(); ++it) {
    if((*it)->z() < 0) {
      collect(*it, scene_pos, out);
    }
  }
}

// A modal popup lays a dimmer over the page in the overlay: nothing under
// it is live.
bool overlayIsModal(const QQuickItem* overlay) {
  const auto kids = overlay->childItems();
  return std::any_of(kids.begin(), kids.end(), [](const QQuickItem* c) {
    return c->isVisible() && !c->inherits("QQuickPopupItem");
  });
}

// Buttons asked for by name, the middle one among them. Qt.AllButtons is a
// catch-all, not a use: a pane eating every click (and Qt itself, which
// widens an item to all buttons the moment a TapHandler or HoverHandler goes
// on it) has nothing to do with the middle button.
bool namesMiddle(Qt::MouseButtons buttons) {
  return (buttons & Qt::MiddleButton) != 0 && buttons != Qt::AllButtons;
}

// An item with its own use for the middle button: a link that opens in the
// background, a tab that closes, a scrim that keeps the page under it still.
// A card, a row or a line of text that merely takes clicks is not one - a
// card's hover handler made the whole card "take" the middle button, and
// autoscroll never started over a board (dogfood 2026-10-20).
bool usesMiddle(const QQuickItem* item) {
  const QVariant own = item->property("usesMiddleButton");
  if(own.isValid()) {
    return own.toBool();
  }
  if(namesMiddle(item->acceptedMouseButtons())) {
    return true;
  }
  const auto kids = item->children();
  return std::any_of(kids.begin(), kids.end(), [](const QObject* o) {
    if(!o->inherits("QQuickPointerHandler") || o->inherits("QQuickHoverHandler") || !o->property("enabled").toBool()) {
      return false;
    }
    const QVariant b = o->property("acceptedButtons");
    return b.isValid() && namesMiddle(b.value<Qt::MouseButtons>());
  });
}

}  // namespace

ScrollFeel::ScrollFeel(QObject* parent) : QObject(parent) {
  tick_.setInterval(kFrameMs);
  tick_.setTimerType(Qt::PreciseTimer);
  connect(&tick_, &QTimer::timeout, this, &ScrollFeel::tick);
}

ScrollFeel* ScrollFeel::instance() {
  static ScrollFeel* self = new ScrollFeel(QCoreApplication::instance());
  return self;
}

ScrollFeel* ScrollFeel::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/) {
  ScrollFeel* self = instance();
  QJSEngine::setObjectOwnership(self, QJSEngine::CppOwnership);
  return self;
}

void ScrollFeel::setLineHeight(double v) {
  if(v <= 0 || qFuzzyCompare(v, line_height_)) {
    return;
  }
  line_height_ = v;
  emit lineHeightChanged();
}

void ScrollFeel::setDuration(int v) {
  v = std::max(0, v);
  if(v == duration_) {
    return;
  }
  duration_ = v;
  emit durationChanged();
}

double ScrollFeel::notchPixels() const {
  return heap::scroll::wheelStep(heap::scroll::kNotch, 0, QGuiApplication::styleHints()->wheelScrollLines(), line_height_, 0).pixels;
}

bool ScrollFeel::eventFilter(QObject* watched, QEvent* event) {
  switch(event->type()) {
    case QEvent::Wheel: {
      if(qobject_cast<QQuickWindow*>(watched) != nullptr) {
        // A new wheel event starts its way down from the window.
        edge_valid_ = false;
        if(!auto_.isNull()) {
          stopAutoscroll();
        }
        return false;
      }
      auto* wheelEvent = static_cast<QWheelEvent*>(event);
      if(isFlickable(watched)) {
        return wheel(static_cast<QQuickItem*>(watched), wheelEvent);
      }
      if(auto* item = qobject_cast<QQuickItem*>(watched)) {
        const QVariant target = item->property("wheelScrollTarget");
        if(target.isValid()) {
          auto* flick = qobject_cast<QQuickItem*>(target.value<QObject*>());
          if(isFlickable(flick)) {
            return wheel(flick, wheelEvent);
          }
        }
      }
      return false;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
      auto* window = qobject_cast<QQuickWindow*>(watched);
      if(window == nullptr) {
        return false;
      }
      const auto* me = static_cast<QMouseEvent*>(event);
      if(event->type() == QEvent::MouseButtonDblClick && me->button() == eat_release_) {
        return true;
      }
      if(!auto_.isNull()) {
        // Any button ends it, and only ends it: the click does nothing else.
        stopAutoscroll();
        eat_release_ = me->button();
        return true;
      }
      if(event->type() == QEvent::MouseButtonPress && me->button() == Qt::MiddleButton && startAutoscroll(window, me->scenePosition())) {
        button_down_ = true;
        dragged_ = false;
        eat_release_ = Qt::NoButton;
        return true;
      }
      return false;
    }
    case QEvent::MouseButtonRelease: {
      if(qobject_cast<QQuickWindow*>(watched) == nullptr) {
        return false;
      }
      const auto* me = static_cast<QMouseEvent*>(event);
      if(eat_release_ != Qt::NoButton && me->button() == eat_release_) {
        eat_release_ = Qt::NoButton;
        return true;
      }
      if(!auto_.isNull() && me->button() == Qt::MiddleButton) {
        const bool heldAndMoved = button_down_ && dragged_;
        button_down_ = false;
        // Held, moved and let go: done. A plain click leaves it running
        // until the next click or key.
        if(heldAndMoved) {
          stopAutoscroll();
        }
        return true;
      }
      return false;
    }
    case QEvent::MouseMove: {
      if(auto_.isNull() || qobject_cast<QQuickWindow*>(watched) == nullptr) {
        return false;
      }
      pointer_ = static_cast<QMouseEvent*>(event)->scenePosition();
      if(button_down_ && QLineF(pointer_, origin_).length() > kDeadZone * line_height_ / kBaseLine) {
        dragged_ = true;
      }
      // The page under the marker is not live: no hover, no drag.
      return true;
    }
    case QEvent::ShortcutOverride:
      if(auto_.isNull()) {
        return false;
      }
      // Claimed, so no shortcut fires; the key press that follows is eaten.
      stopAutoscroll();
      eat_key_ = true;
      event->accept();
      return true;
    case QEvent::KeyPress:
      if(auto_.isNull() && !eat_key_) {
        return false;
      }
      stopAutoscroll();
      eat_key_ = false;
      return true;
    case QEvent::KeyRelease:
      eat_key_ = false;
      return false;
    case QEvent::WindowDeactivate:
    case QEvent::Hide:
    case QEvent::Close:
      if(!auto_.isNull() && watched == auto_window_.data()) {
        stopAutoscroll();
      }
      return false;
    default:
      return false;
  }
}

bool ScrollFeel::wheel(QQuickItem* flick, QWheelEvent* event) {
  // Ctrl+wheel is a zoom where a view has one: left as it was.
  if((event->modifiers() & Qt::ControlModifier) != 0 || !flick->property("interactive").toBool()) {
    return false;
  }
  const QPoint angle = event->angleDelta();
  const QPoint pixel = event->pixelDelta();
  bool sideways = std::abs(angle.x()) > std::abs(angle.y()) || std::abs(pixel.x()) > std::abs(pixel.y());
  const int a = sideways ? angle.x() : angle.y();
  const int p = sideways ? pixel.x() : pixel.y();
  // Shift turns a plain wheel sideways.
  if((event->modifiers() & Qt::ShiftModifier) != 0) {
    sideways = true;
  }
  const bool canX = canScroll(flick, true);
  const bool canY = canScroll(flick, false);
  // A view that only goes sideways (the board) takes the plain wheel
  // sideways - unless a view inside it ran out and let it by, or a view
  // around it scrolls up and down (a row of chips in a list).
  const bool horizontal = sideways || (!canY && canX && !edge_valid_ && !verticalAncestor(flick));
  if((horizontal ? !canX : !canY) || (a == 0 && p == 0)) {
    event->ignore();
    return true;
  }
  const bool latched = !latched_.isNull() && latch_clock_.isValid() && latch_clock_.elapsed() < kLatchMs;
  // The gesture began on the view around this one: it stays there.
  if(latched && latched_ != flick && latched_->isAncestorOf(flick)) {
    event->ignore();
    return true;
  }
  const auto step = heap::scroll::wheelStep(
      a, p, QGuiApplication::styleHints()->wheelScrollLines(), line_height_, horizontal ? flick->width() : flick->height());
  const Span s = span(flick, horizontal);
  const double base = scrollBase(flick, horizontal);
  const double target = heap::scroll::clampScroll(base - step.pixels, s.lo, s.hi);
  if(std::abs(target - base) < 0.5) {
    if(latched && latched_ == flick) {
      // Ran out mid-gesture: the wheel stops here, the outer view stays.
      latch_clock_.restart();
      event->accept();
      return true;
    }
    edge_valid_ = true;
    event->ignore();
    return true;
  }
  if(!latched || latched_ != flick) {
    gesture_pixels_ = 0;
    gesture_long_ = false;
  }
  latched_ = flick;
  latch_clock_.restart();
  gesture_pixels_ += std::abs(target - base);
  const double extent = horizontal ? flick->width() : flick->height();
  if(!gesture_long_ && extent > 0 && gesture_pixels_ > kLongWheelScreens * extent) {
    gesture_long_ = true;
    emit longWheelScroll(flick);
  }
  scrollTo(flick, horizontal, target, step.animate && duration_ > 0);
  event->accept();
  return true;
}

double ScrollFeel::scrollBase(QQuickItem* flick, bool horizontal) const {
  const auto& anims = horizontal ? anim_x_ : anim_y_;
  const auto it = anims.constFind(flick);
  if(it != anims.cend() && (*it)->state() == QAbstractAnimation::Running) {
    return (*it)->endValue().toDouble();
  }
  return num(flick, horizontal ? "contentX" : "contentY");
}

QVariantAnimation* ScrollFeel::animation(QQuickItem* flick, bool horizontal) {
  auto& anims = horizontal ? anim_x_ : anim_y_;
  if(auto* existing = anims.value(flick)) {
    return existing;
  }
  auto* anim = new QVariantAnimation(flick);
  anim->setEasingCurve(QEasingCurve::OutQuad);
  const char* prop = horizontal ? "contentX" : "contentY";
  QPointer<QQuickItem> guard(flick);
  connect(anim, &QVariantAnimation::valueChanged, this, [guard, prop](const QVariant& v) {
    if(!guard.isNull()) {
      guard->setProperty(prop, v);
    }
  });
  connect(anim, &QObject::destroyed, this, [this, flick, horizontal]() {
    (horizontal ? anim_x_ : anim_y_).remove(flick);
  });
  anims.insert(flick, anim);
  return anim;
}

void ScrollFeel::scrollTo(QQuickItem* flick, bool horizontal, double target, bool animate) {
  QVariantAnimation* anim = animation(flick, horizontal);
  const char* prop = horizontal ? "contentX" : "contentY";
  anim->stop();
  if(!animate) {
    flick->setProperty(prop, target);
    return;
  }
  anim->setStartValue(num(flick, prop));
  anim->setEndValue(target);
  anim->setDuration(duration_);
  anim->start();
}

bool ScrollFeel::startAutoscroll(QQuickWindow* window, QPointF scene_pos) {
  QList<QQuickItem*> hits;
  collect(window->contentItem(), scene_pos, hits);
  QQuickItem* target = nullptr;
  for(QQuickItem* item : std::as_const(hits)) {
    if(item->inherits("QQuickOverlay")) {
      if(overlayIsModal(item)) {
        return false;
      }
      continue;
    }
    if(isFlickable(item)) {
      if(item->property("interactive").toBool() && (canScroll(item, true) || canScroll(item, false))) {
        target = item;
        break;
      }
      continue;
    }
    // A link, a tab or a scrim with its own use for the middle button.
    if(usesMiddle(item)) {
      return false;
    }
  }
  if(target == nullptr) {
    return false;
  }
  auto_ = target;
  auto_window_ = window;
  origin_ = scene_pos;
  pointer_ = scene_pos;
  auto_x_ = canScroll(target, true);
  auto_y_ = canScroll(target, false);
  if(QQmlEngine* engine = qmlEngine(target)) {
    QQmlComponent component(engine, QStringLiteral("TodoCpp"), QStringLiteral("AutoScrollMarker"));
    QObject* made = component.createWithInitialProperties({{QStringLiteral("canX"), auto_x_}, {QStringLiteral("canY"), auto_y_}});
    if(auto* marker = qobject_cast<QQuickItem*>(made)) {
      QQmlEngine::setObjectOwnership(marker, QQmlEngine::CppOwnership);
      marker->setParent(window);
      marker->setParentItem(window->contentItem());
      marker->setZ(1e9);
      marker->setPosition(scene_pos - QPointF(marker->width() / 2, marker->height() / 2));
      marker_ = marker;
    } else {
      delete made;
      if(component.isError()) {
        qWarning() << "AutoScrollMarker:" << component.errors();
      }
    }
  }
  updateCursor(0, 0);
  frame_clock_.start();
  tick_.start();
  emit autoscrollingChanged();
  return true;
}

void ScrollFeel::tick() {
  if(auto_.isNull() || auto_window_.isNull() || !auto_->isVisible()) {
    stopAutoscroll();
    return;
  }
  // Outside the window no move events arrive; the cursor itself is asked
  // (not under the offscreen test platform, where it never moves).
  const QString platform = QGuiApplication::platformName();
  if(platform != QLatin1String("offscreen") && platform != QLatin1String("minimal")) {
    pointer_ = auto_window_->mapFromGlobal(QCursor::pos());
  }
  const double dt = std::min(0.05, static_cast<double>(frame_clock_.restart()) / 1000.0);
  const double scale = line_height_ / kBaseLine;
  const QPointF d = pointer_ - origin_;
  const double vx = auto_x_ ? heap::scroll::autoscrollSpeed(d.x(), kDeadZone, scale) : 0;
  const double vy = auto_y_ ? heap::scroll::autoscrollSpeed(d.y(), kDeadZone, scale) : 0;
  updateCursor(vx, vy);
  if(vx != 0) {
    const Span s = span(auto_, true);
    auto_->setProperty("contentX", heap::scroll::clampScroll(num(auto_, "contentX") + vx * dt, s.lo, s.hi));
  }
  if(vy != 0) {
    const Span s = span(auto_, false);
    auto_->setProperty("contentY", heap::scroll::clampScroll(num(auto_, "contentY") + vy * dt, s.lo, s.hi));
  }
}

void ScrollFeel::updateCursor(double vx, double vy) {
  Qt::CursorShape shape = Qt::SizeAllCursor;
  if(vx != 0 && vy != 0) {
    shape = (vx > 0) == (vy > 0) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
  } else if(vy != 0 || (vx == 0 && auto_y_ && !auto_x_)) {
    shape = Qt::SizeVerCursor;
  } else if(vx != 0 || (auto_x_ && !auto_y_)) {
    shape = Qt::SizeHorCursor;
  }
  if(!cursor_set_) {
    QGuiApplication::setOverrideCursor(QCursor(shape));
    cursor_set_ = true;
  } else if(shape != cursor_shape_) {
    QGuiApplication::changeOverrideCursor(QCursor(shape));
  }
  cursor_shape_ = shape;
}

void ScrollFeel::stopAutoscroll() {
  if(auto_.isNull() && marker_.isNull() && !tick_.isActive()) {
    return;
  }
  tick_.stop();
  if(!marker_.isNull()) {
    marker_->setParentItem(nullptr);
    marker_->deleteLater();
  }
  marker_ = nullptr;
  if(cursor_set_) {
    QGuiApplication::restoreOverrideCursor();
    cursor_set_ = false;
  }
  // Still held: its release is not a click on whatever lies under it.
  if(button_down_) {
    eat_release_ = Qt::MiddleButton;
  }
  button_down_ = false;
  dragged_ = false;
  auto_ = nullptr;
  auto_window_ = nullptr;
  emit autoscrollingChanged();
}
