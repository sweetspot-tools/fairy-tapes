/** Fairy Tapes · shared UI — unit tests for the rack look geometry. */
#include "fairy_ui/rack_geometry.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::Ui::Rack;

static int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

int main() {
  const double cx = 100, cy = 200, r = 19;
  // first dot lower left, last lower right, middle straight up
  const Pt first = scaleDot(cx, cy, r, 0), last = scaleDot(cx, cy, r, kScaleDots - 1), mid = scaleDot(cx, cy, r, 5);
  CHECK(first.x < cx && first.y > cy);
  CHECK(last.x > cx && last.y > cy);
  CHECK(near(mid.x, cx) && near(mid.y, cy - (r + kScaleGap)));
  CHECK(near(first.x - cx, -(last.x - cx)) && near(first.y, last.y));   // symmetric
  for (int i = 0; i < kScaleDots; ++i) {   // every dot at the same distance from the centre
    const Pt p = scaleDot(cx, cy, r, i);
    CHECK(near(std::hypot(p.x - cx, p.y - cy), r + kScaleGap));
  }
  CHECK(scaleDotRadius(0) == 1.9 && scaleDotRadius(5) == 1.9 && scaleDotRadius(10) == 1.9 && scaleDotRadius(3) == 1.2);
  // the pointer follows the dots: value 0 at the first, 1 at the last, 0.5 straight up
  CHECK(near(pointerDegrees(0.0), 225.0) && near(pointerDegrees(1.0), -45.0) && near(pointerDegrees(0.5), 90.0));
  // end labels on either side of the lower dots, same baseline
  const Pt lo = endLabelLow(cx, cy, r), hi = endLabelHigh(cx, cy, r);
  CHECK(lo.x < first.x && hi.x > last.x && near(lo.y, hi.y) && lo.y > cy);
  // text under the knob: name above value, both below the cap
  CHECK(nameBaseline(cy, r) > cy + r);
  CHECK(valueBaseline(cy, r) > nameBaseline(cy, r));
  CHECK(near(valueBaseline(cy, 22) - nameBaseline(cy, 22), 15.5));
  // button: LED under the face, label above it
  CHECK(kLedOffsetY > kFaceHeight / 2 + kLedRadius);
  CHECK(kLabelOffsetY < -kFaceHeight / 2);
  if (g_failed) return 1;
  std::puts("test_rack_geometry: ok");
  return 0;
}
