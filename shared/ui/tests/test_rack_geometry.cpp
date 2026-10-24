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

  // face-lit exclusive choice: a grid of cells, row-major, each face centred in its cell
  {   // VOICE / DRUMS / CLASSIC stacked: 26 px between the faces, as in the preview
    const Box c0 = litChoiceCell(44, 78, 3, 1, 0), c2 = litChoiceCell(44, 78, 3, 1, 2);
    CHECK(near(c0.top, 0) && near(c0.bottom, 26) && near(c2.top, 52) && near(c2.bottom, 78));
    CHECK(near(c0.left, 0) && near(c0.right, 44));
    const Box f = litFace(c2, 44, kLitFaceHeight);
    CHECK(near(f.top, 65 - kLitFaceHeight / 2) && near(f.bottom - f.top, kLitFaceHeight) && near(f.left, 0) && near(f.right, 44));
  }
  {   // NAM / 4K side by side: 40 px between the centres
    const Box a = litFace(litChoiceCell(80, 20, 2, 2, 0), 34, 20), b = litFace(litChoiceCell(80, 20, 2, 2, 1), 34, 20);
    CHECK(near((b.left + b.right) / 2 - (a.left + a.right) / 2, 40) && near(a.left, 3));
  }
  {   // 2 x 2 grid of notes: the last one is bottom right
    const Box c3 = litChoiceCell(80, 52, 4, 2, 3);
    CHECK(near(c3.left, 40) && near(c3.top, 26) && near(c3.right, 80) && near(c3.bottom, 52));
  }
  // hits: by cell; outside the view or on an empty cell of an incomplete grid there is none
  CHECK(litChoiceHit(44, 78, 3, 1, 10, 40) == 1);
  CHECK(litChoiceHit(80, 52, 4, 2, 79, 51) == 3);
  CHECK(litChoiceHit(80, 52, 3, 2, 60, 40) == -1);
  CHECK(litChoiceHit(44, 78, 3, 1, -1, 10) == -1 && litChoiceHit(44, 78, 3, 1, 10, 78) == -1);
  CHECK(litChoiceHit(44, 78, 0, 1, 10, 10) == -1 && litChoiceHit(44, 78, 3, 0, 10, 10) == -1);
  // cycling selector (M/S, Ratio, SC HPF, Ceil): the value is printed under the LED; a click steps to the next, wrapping
  CHECK(kCycleValueOffsetY > kLedOffsetY + kLedRadius && near(kCycleValueOffsetY, 39));
  CHECK(cycleNextIndex(0, 3) == 1 && cycleNextIndex(2, 3) == 0 && cycleNextIndex(-4, 3) == 1 && cycleNextIndex(7, 3) == 0);   // out of range: clamped first
  CHECK(cycleNextIndex(0, 1) == 0 && cycleNextIndex(5, 0) == 0);
  // the name sits on the face, centred
  CHECK(kLitTextBaseline > 0 && kLitTextBaseline < kLitFaceHeight / 2);
  if (g_failed) return 1;
  std::puts("test_rack_geometry: ok");
  return 0;
}
