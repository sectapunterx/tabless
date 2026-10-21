// The wheel and middle-click autoscroll arithmetic (dogfood 2026-10-20:
// "scrolling is too fast; make it like other programs").
#include "ScrollMath.h"

#include <gtest/gtest.h>

using heap::scroll::autoscrollSpeed;
using heap::scroll::clampScroll;
using heap::scroll::wheelStep;

TEST(WheelStep, OneNotchIsTheSystemLinesOfOneTextLine) {
  // Windows' default: 3 lines a notch; a 20 px line at 100 %.
  const auto s = wheelStep(120, 0, 3, 20, 600);
  EXPECT_DOUBLE_EQ(s.pixels, 60);
  EXPECT_TRUE(s.animate);
  // Down is the other sign; two notches in one event, twice as far.
  EXPECT_DOUBLE_EQ(wheelStep(-120, 0, 3, 20, 600).pixels, -60);
  EXPECT_DOUBLE_EQ(wheelStep(-240, 0, 3, 20, 600).pixels, -120);
}

TEST(WheelStep, FollowsTheScaleAndTheSystemSetting) {
  EXPECT_DOUBLE_EQ(wheelStep(120, 0, 3, 25, 600).pixels, 75);  // 125 %
  EXPECT_DOUBLE_EQ(wheelStep(120, 0, 1, 20, 600).pixels, 20);
  EXPECT_DOUBLE_EQ(wheelStep(120, 0, 10, 20, 600).pixels, 200);
}

TEST(WheelStep, NeverTheOldTripleStep) {
  // The views multiplied the raw delta by 3: 360 px a notch.
  EXPECT_LT(wheelStep(120, 0, 3, 20, 600).pixels, 360);
}

TEST(WheelStep, OneScreenAtATimeSettingPagesWithOverlap) {
  // Windows "one screen at a time" reaches Qt as a negative or huge value.
  EXPECT_DOUBLE_EQ(wheelStep(120, 0, -1, 20, 800).pixels, 700);
  EXPECT_DOUBLE_EQ(wheelStep(120, 0, 0, 20, 800).pixels, 700);
  EXPECT_DOUBLE_EQ(wheelStep(-120, 0, 1000, 20, 800).pixels, -700);
  // A tiny view still moves at least a line.
  EXPECT_DOUBLE_EQ(wheelStep(120, 0, -1, 20, 10).pixels, 20);
}

TEST(WheelStep, TouchpadPixelsPassThroughOneToOne) {
  const auto s = wheelStep(120, 7, 3, 20, 600);
  EXPECT_DOUBLE_EQ(s.pixels, 7);
  EXPECT_FALSE(s.animate);
  EXPECT_DOUBLE_EQ(wheelStep(0, -3, 3, 20, 600).pixels, -3);
}

TEST(WheelStep, HighResolutionWheelIsProportionalNotAccelerated) {
  // A free-spinning or high-res wheel: 1/8 notch at a time.
  const auto s = wheelStep(15, 0, 3, 20, 600);
  EXPECT_DOUBLE_EQ(s.pixels, 7.5);
  EXPECT_FALSE(s.animate);
  // Eight of them add up to exactly one notch.
  double sum = 0;
  for(int i = 0; i < 8; ++i) {
    sum += wheelStep(15, 0, 3, 20, 600).pixels;
  }
  EXPECT_DOUBLE_EQ(sum, wheelStep(120, 0, 3, 20, 600).pixels);
}

TEST(WheelStep, NothingIsNothing) {
  const auto s = wheelStep(0, 0, 3, 20, 600);
  EXPECT_DOUBLE_EQ(s.pixels, 0);
  EXPECT_FALSE(s.animate);
}

TEST(AutoscrollSpeed, StillInsideTheDeadZone) {
  EXPECT_DOUBLE_EQ(autoscrollSpeed(0, 10, 1), 0);
  EXPECT_DOUBLE_EQ(autoscrollSpeed(10, 10, 1), 0);
  EXPECT_DOUBLE_EQ(autoscrollSpeed(-9.5, 10, 1), 0);
}

TEST(AutoscrollSpeed, GrowsWithDistanceAndKeepsTheSign) {
  const double near = autoscrollSpeed(20, 10, 1);
  const double mid = autoscrollSpeed(60, 10, 1);
  const double far = autoscrollSpeed(200, 10, 1);
  EXPECT_GT(near, 0);
  EXPECT_GT(mid, near);
  EXPECT_GT(far, mid);
  // Faster than linear far out: a long page crosses quickly.
  EXPECT_GT(far / 190, mid / 50);
  EXPECT_DOUBLE_EQ(autoscrollSpeed(-60, 10, 1), -mid);
  // Slow near the marker: under a line per frame at 60 fps 10 px out.
  EXPECT_LT(near / 60, 2);
}

TEST(AutoscrollSpeed, IsCapped) {
  EXPECT_DOUBLE_EQ(autoscrollSpeed(100000, 10, 1), 6000);
  EXPECT_DOUBLE_EQ(autoscrollSpeed(-100000, 10, 1), -6000);
}

TEST(AutoscrollSpeed, ScalesWithTheUi) {
  // The same hand movement at 150 % is 1.5x the pixels; it gives the same
  // feel: 1.5x the speed in pixels.
  EXPECT_DOUBLE_EQ(autoscrollSpeed(60 * 1.5, 10, 1.5), autoscrollSpeed(60, 10, 1) * 1.5);
  EXPECT_DOUBLE_EQ(autoscrollSpeed(14, 10, 1.5), 0);  // 15 px dead zone at 150 %
  EXPECT_DOUBLE_EQ(autoscrollSpeed(20, 10, 0), autoscrollSpeed(20, 10, 1));
}

TEST(ClampScroll, KeepsInsideAndPinsShortContent) {
  EXPECT_DOUBLE_EQ(clampScroll(50, 0, 100), 50);
  EXPECT_DOUBLE_EQ(clampScroll(-5, 0, 100), 0);
  EXPECT_DOUBLE_EQ(clampScroll(150, 0, 100), 100);
  EXPECT_DOUBLE_EQ(clampScroll(40, 10, 5), 10);
}
