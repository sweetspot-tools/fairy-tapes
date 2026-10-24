/** Fairy Tapes · The Stripper — recall marks (the pen marks on the scale): when they show and where they are drawn (no VSTGUI). */

#include "ui/recall_marks.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::TheStripper;
using namespace FairyTapes::TheStripper::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

void test_a_mark_shows_only_where_the_value_moved() {
  CHECK(!recallMarkVisible(0.50, 0.50, true));                    // unchanged
  CHECK(!recallMarkVisible(0.50, 0.505, true));                   // a hair away: not worth a mark
  CHECK(recallMarkVisible(0.50, 0.60, true) && recallMarkVisible(0.60, 0.50, true));   // moved: both directions
  CHECK(recallMarkVisible(0.0, 1.0, true));
  CHECK(!recallMarkVisible(0.20, 0.90, false));                   // no marks yet (nothing was loaded or marked)
  CHECK(!recallMarkVisible(std::nan(""), 0.5, true) && !recallMarkVisible(0.5, std::nan(""), true));
  CHECK(recallMarkVisible(0.5, 0.5 + kRecallThreshold * 1.01, true));
  CHECK(!recallMarkVisible(0.5, 0.5 + kRecallThreshold * 0.99, true));
}

void test_the_mark_sits_on_the_scale_at_the_recalled_value() {
  const double cx = 100, cy = 200, R = 19;
  for (double v : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    const auto pts = recallMarkStroke(cx, cy, R, v, 4242);
    CHECK(pts.size() >= 3);
    // the stroke crosses the ring of scale dots: it starts inside it and ends outside, along the radius of the value
    const double a = (225.0 - 270.0 * v) * 3.14159265358979323846 / 180.0, dx = std::cos(a), dy = -std::sin(a);
    double rMin = 1e9, rMax = 0;
    for (const auto& p : pts) {
      const double r = std::hypot(p.x - cx, p.y - cy);
      rMin = std::min(rMin, r);
      rMax = std::max(rMax, r);
      CHECK(std::isfinite(p.x) && std::isfinite(p.y));
      // close to the radius line (a pen is not a ruler)
      const double along = (p.x - cx) * dx + (p.y - cy) * dy, across = std::fabs((p.x - cx) * dy - (p.y - cy) * dx);
      CHECK(along > 0 && across < 3.5);
    }
    CHECK(rMin < R + 9 && rMax > R + 9);   // crosses the dots at R + 9
    CHECK(rMax < R + 20);
  }
}

void test_marks_are_hand_made_but_stable() {
  const auto a = recallMarkStroke(100, 200, 19, 0.3, 1), b = recallMarkStroke(100, 200, 19, 0.3, 1), c = recallMarkStroke(100, 200, 19, 0.3, 2);
  bool same = a.size() == b.size(), differs = false;
  for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i].x == b[i].x && a[i].y == b[i].y;
  for (std::size_t i = 0; i < a.size() && i < c.size(); ++i) differs |= a[i].x != c[i].x || a[i].y != c[i].y;
  CHECK(same && differs);
}

void test_only_knobs_get_marks() {
  CHECK(hasRecallMark(kInputGain) && hasRecallMark(kEqLFFreq) && hasRecallMark(kDriveAmount) && hasRecallMark(kGateThreshold));
  CHECK(!hasRecallMark(kBypass) && !hasRecallMark(kStripMode) && !hasRecallMark(kEqLFBell) && !hasRecallMark(kCompressorMode) && !hasRecallMark(kDriveNamOn));
  CHECK(hasRecallMark(kEqSaturation) && hasRecallMark(kDriveTapeAmount));   // SATURATE (EQ) and TAPE are knobs of their own
  CHECK(!hasRecallMark(kDriveTapeOn));
}
} // namespace

int main() {
  test_a_mark_shows_only_where_the_value_moved();
  test_the_mark_sits_on_the_scale_at_the_recalled_value();
  test_marks_are_hand_made_but_stable();
  test_only_knobs_get_marks();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_recall_marks: ok");
  return 0;
}
