/** Fairy Tapes · The Stripper — the four screws of the panel: seeded layout, missing screw, turning by dragging (no VSTGUI). */

#include "ui/screws.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::TheStripper::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

void test_screws_are_deterministic_and_at_the_corners() {
  const auto a = generateScrews(555), b = generateScrews(555), c = generateScrews(556);
  bool differs = false;
  for (int i = 0; i < kScrewCount; ++i) {
    CHECK(a[i].cross == b[i].cross && a[i].missing == b[i].missing && a[i].initialAngle == b[i].initialAngle);
    differs |= a[i].initialAngle != c[i].initialAngle || a[i].cross != c[i].cross;
    CHECK(a[i].initialAngle >= 0 && a[i].initialAngle <= 180);
  }
  CHECK(differs);
  // the four corners of the 1000 x 628 panel, 18 px in
  CHECK(a[0].x == 18 && a[0].y == 18 && a[1].x == 982 && a[1].y == 18 && a[2].x == 18 && a[2].y == 610 && a[3].x == 982 && a[3].y == 610);
}

void test_slot_and_cross_both_occur_and_a_screw_rarely_goes_missing() {
  int slots = 0, crosses = 0, instancesWithMissing = 0, moreThanOne = 0;
  const int N = 5000;
  for (std::uint32_t seed = 0; seed < static_cast<std::uint32_t>(N); ++seed) {
    int missing = 0;
    for (const auto& s : generateScrews(seed * 2654435761u)) {
      (s.cross ? crosses : slots)++;
      missing += s.missing;
    }
    instancesWithMissing += missing > 0;
    moreThanOne += missing > 1;
  }
  CHECK(slots > 0.4 * 4 * N && crosses > 0.4 * 4 * N);                          // about half and half
  CHECK(instancesWithMissing > 0.03 * N && instancesWithMissing < 0.10 * N);   // about 6 % of the panels lack a screw
  CHECK(moreThanOne == 0);                                                      // never two
}

void test_dragging_turns_the_screw_like_a_knob() {
  // right or up turns clockwise (the angle grows), left or down turns it back, along either axis
  CHECK(screwAngleAfterDrag(10, 10, 0) > 10);
  CHECK(screwAngleAfterDrag(10, 0, -10) > 10);
  CHECK(screwAngleAfterDrag(10, -5, 0) < 10);
  CHECK(screwAngleAfterDrag(10, 0, 5) < 10);
  CHECK(std::fabs(screwAngleAfterDrag(0, 10, 0) - 16.0) < 1e-9);               // 1.6 degrees per pixel
  CHECK(std::fabs(screwAngleAfterDrag(0, 10, -10) - 32.0) < 1e-9);             // both axes add up
  CHECK(std::fabs(screwAngleAfterDrag(0, 5, 5)) < 1e-9);                       // right and down cancel
}

void test_angles_are_normalised_and_nan_safe() {
  for (double a : {-1000.0, -90.0, 0.0, 359.9, 360.0, 725.0, 1e9}) {
    const double n = normalizeScrewAngle(a);
    CHECK(n >= 0.0 && n < 360.0);
  }
  CHECK(std::fabs(normalizeScrewAngle(-90) - 270) < 1e-9 && std::fabs(normalizeScrewAngle(450) - 90) < 1e-9);
  CHECK(normalizeScrewAngle(std::nan("")) == 0.0 && normalizeScrewAngle(INFINITY) == 0.0);
  CHECK(screwAngleAfterDrag(std::nan(""), 3, 3) >= 0.0);
  const double big = screwAngleAfterDrag(350, 1000, 0);
  CHECK(big >= 0.0 && big < 360.0);
}

void test_hit_test() {
  CHECK(screwHit(18, 18, 18, 18) && screwHit(18, 18, 24, 18) && screwHit(18, 18, 18, 28));
  CHECK(!screwHit(18, 18, 40, 40) && !screwHit(18, 18, 18, 31));
}
} // namespace

int main() {
  test_screws_are_deterministic_and_at_the_corners();
  test_slot_and_cross_both_occur_and_a_screw_rarely_goes_missing();
  test_dragging_turns_the_screw_like_a_knob();
  test_angles_are_normalised_and_nan_safe();
  test_hit_test();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_screws: ok");
  return 0;
}
