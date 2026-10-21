#pragma once

#include <algorithm>
#include <cmath>

// The arithmetic of scrolling (dogfood 2026-10-20: "the wheel is too fast,
// make it like other programs; middle click to scroll"), without the event
// plumbing, so it can be tested on its own. ScrollFeel applies it.
namespace heap::scroll {

// One wheel event, turned into a distance.
struct WheelStep {
  // How far the content travels, in pixels, in the wheel's own sign
  // (positive = towards the start: up, or left).
  double pixels = 0;
  // A whole notch of a classic wheel glides there; a touchpad or a
  // high-resolution wheel already arrives in small steps and follows 1:1.
  bool animate = false;
};

// A notch of a classic wheel is 120 units of angleDelta.
inline constexpr int kNotch = 120;

// `lines`: QStyleHints::wheelScrollLines (3 by default on Windows; zero,
// negative or absurd means "one screen at a time"). `line_height`: one line
// of text at the UI scale. `page`: the viewport's length along the axis.
inline WheelStep wheelStep(int angle_delta, int pixel_delta, int lines, double line_height, double page) {
  if(pixel_delta != 0) {
    return {static_cast<double>(pixel_delta), false};
  }
  if(angle_delta == 0) {
    return {};
  }
  const bool pageMode = lines <= 0 || lines > 100;
  const double perNotch = pageMode ? std::max(line_height, page * 0.875) : lines * line_height;
  const bool notched = angle_delta % kNotch == 0;
  return {angle_delta / static_cast<double>(kNotch) * perNotch, notched};
}

// Middle-click autoscroll: the speed in px/s for the pointer's distance
// from where the button went down (signed: the sign of `distance`). Still
// within `dead_zone`, then growing with the distance - slowly near the
// marker, for reading, fast far from it. Distances are measured at the UI
// `scale`, so the same hand movement gives the same feel at 150 %.
inline double autoscrollSpeed(double distance, double dead_zone, double scale) {
  const double s = scale > 0 ? scale : 1.0;
  const double beyond = std::abs(distance) - dead_zone * s;
  if(beyond <= 0) {
    return 0;
  }
  const double d = beyond / s;
  const double v = std::min(d * 6.0 + d * d * 0.04, 6000.0) * s;
  return distance < 0 ? -v : v;
}

// A scroll position kept inside [lo, hi]; a range that is empty or upside
// down (content shorter than the view) pins it to `lo`.
inline double clampScroll(double value, double lo, double hi) {
  if(hi < lo) {
    return lo;
  }
  return std::clamp(value, lo, hi);
}

}  // namespace heap::scroll
