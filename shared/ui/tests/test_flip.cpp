/** Fairy Tapes shared UI — the pose of the turning panel (the pure part of FlipView and PanelFlip). Ported from
 *  src/the-stripper/tests/test_flip.cpp, plus the progress from the elapsed time. */
#include "fairy_ui/flip_pose.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::Ui;

static int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

static void test_pose() {
  const FlipPose start = flipPose(0), mid = flipPose(0.5), end = flipPose(1);
  CHECK(start.widthScale == 1.0 && start.shade == 0.0 && !start.showsNewSide);   // face on, old side
  CHECK(end.widthScale > 0.999 && end.shade < 1e-6 && end.showsNewSide);         // face on, new side
  CHECK(mid.widthScale < 1e-9 && mid.shade > 0.5);                               // edge-on at the middle, darkest
  // the old side narrows, then the new side widens; the side changes exactly once, at the middle
  double previous = 1.0;
  bool switched = false;
  for (int i = 0; i <= 100; ++i) {
    const FlipPose p = flipPose(i / 100.0);
    CHECK(p.widthScale >= 0 && p.widthScale <= 1 && p.heightScale <= 1 && p.heightScale > 0.9 && p.shade >= 0 && p.shade <= 1);
    if (!p.showsNewSide) CHECK(p.widthScale <= previous + 1e-9);                  // narrowing
    if (p.showsNewSide) { if (!switched) { switched = true; previous = 0; } CHECK(p.widthScale >= previous - 1e-9); previous = p.widthScale; }
    else previous = p.widthScale;
  }
  CHECK(switched);
  // nonsense progress is safe
  CHECK(flipPose(-5).widthScale == 1.0 && !flipPose(-5).showsNewSide);
  CHECK(flipPose(7).showsNewSide);
  CHECK(flipPose(std::nan("")).widthScale == 1.0);
  CHECK(kFlipSeconds > 0.5 && kFlipSeconds < 1.2);                                // "about 0.8 s"
}

static void test_progress_from_elapsed_time() {
  CHECK(flipProgress(0.0) == 0.0);
  CHECK(std::fabs(flipProgress(kFlipSeconds / 2) - 0.5) < 1e-12);
  CHECK(flipProgress(kFlipSeconds) == 1.0 && flipProgress(10.0) == 1.0);
  CHECK(flipProgress(-1.0) == 0.0 && flipProgress(std::nan("")) == 0.0);
  CHECK(flipProgress(0.4, 0.0) == 1.0);                                          // no duration: done at once
  CHECK(flipProgress(0.2, 0.4) == 0.5);
  CHECK(flipDone(flipProgress(kFlipSeconds)) && !flipDone(flipProgress(kFlipSeconds * 0.99)));
}

int main() {
  test_pose();
  test_progress_from_elapsed_time();
  if (g_failed) return 1;
  std::puts("test_flip: ok");
  return 0;
}
