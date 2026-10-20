/** Fairy Tapes · shared UI — the four panel screws: seeded look, missing screw, turning by dragging (no VSTGUI). */

#include "fairy_ui/screws.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

void test_screws_are_deterministic_and_at_the_given_positions() {
  const auto a = generateScrews(555), b = generateScrews(555), c = generateScrews(556);
  bool differs = false;
  for (int i = 0; i < kScrewCount; ++i) {
    CHECK(a[i].cross == b[i].cross && a[i].missing == b[i].missing && a[i].initialAngle == b[i].initialAngle);
    differs |= a[i].initialAngle != c[i].initialAngle || a[i].cross != c[i].cross;
    CHECK(a[i].initialAngle >= 0 && a[i].initialAngle <= 180 && a[i].initialAngle == std::round(a[i].initialAngle));
  }
  CHECK(differs);
  // default: the four corners of the 1000 x 628 rack panel, 18 px in
  CHECK(a[0].x == 18 && a[0].y == 18 && a[1].x == 982 && a[1].y == 18 && a[2].x == 18 && a[2].y == 610 && a[3].x == 982 && a[3].y == 610);
  // other positions: same look, only the centres change
  const ScrewPositions pos{{{1, 2}, {3, 4}, {5, 6}, {7, 8}}};
  const auto d = generateScrews(555, pos);
  for (int i = 0; i < kScrewCount; ++i) {
    CHECK(d[i].x == pos[i].x && d[i].y == pos[i].y);
    CHECK(d[i].cross == a[i].cross && d[i].missing == a[i].missing && d[i].initialAngle == a[i].initialAngle);
  }
}

// The same screws as The Stripper's generateScrews (src/the-stripper/src/ui/screws.h) for the same seed, so the two
// plug-ins look alike. Values taken from that function; a change here changes the screws of existing instances.
void test_screws_pinned_to_the_stripper() {
  struct Pin { std::uint32_t seed; bool cross[4]; bool missing[4]; double angle[4]; };
  const Pin pins[] = {
    {1u, {false, false, true, true}, {false, false, false, false}, {176, 108, 173, 28}},
    {555u, {false, true, true, false}, {false, false, false, false}, {48, 125, 91, 104}},
    {123456789u, {false, false, false, false}, {false, false, false, false}, {113, 112, 115, 6}},
    {4000000000u, {true, false, false, false}, {false, false, false, false}, {68, 142, 57, 162}},
    {3u, {true, false, true, false}, {true, false, false, false}, {8, 16, 157, 116}},
    {7u, {true, false, false, true}, {false, true, false, false}, {164, 87, 57, 83}},
  };
  for (const Pin& p : pins) {
    const auto s = generateScrews(p.seed);
    for (int i = 0; i < kScrewCount; ++i)
      CHECK(s[i].cross == p.cross[i] && s[i].missing == p.missing[i] && s[i].initialAngle == p.angle[i]);
  }
}

void test_slot_and_cross_both_occur_and_at_most_one_screw_goes_missing() {
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
  CHECK(moreThanOne == 0);
}

void test_dragging_turns_the_screw_like_a_knob() {
  CHECK(screwAngleAfterDrag(10, 10, 0) > 10);    // right: clockwise
  CHECK(screwAngleAfterDrag(10, 0, -10) > 10);   // up: clockwise
  CHECK(screwAngleAfterDrag(10, -5, 0) < 10);
  CHECK(screwAngleAfterDrag(10, 0, 5) < 10);
  CHECK(std::fabs(screwAngleAfterDrag(0, 10, 0) - 16.0) < 1e-9);     // 1.6 degrees per pixel
  CHECK(std::fabs(screwAngleAfterDrag(0, 10, -10) - 32.0) < 1e-9);   // the two axes add up
  CHECK(std::fabs(screwAngleAfterDrag(0, 5, 5)) < 1e-9);             // and cancel out
  CHECK(std::fabs(screwAngleAfterDrag(350, 10, 0) - 6.0) < 1e-9);    // wraps
}

void test_angles_are_normalised() {
  const double values[] = {0, 359.999, 360, 720.5, -0.5, -90, -720, 1e300, -1e300};
  for (double a : values) {
    const double n = normalizeScrewAngle(a);
    CHECK(n >= 0 && n < 360);
  }
  CHECK(std::fabs(normalizeScrewAngle(-90) - 270) < 1e-9 && std::fabs(normalizeScrewAngle(370) - 10) < 1e-9);
  CHECK(normalizeScrewAngle(360) == 0);
  CHECK(normalizeScrewAngle(std::nan("")) == 0 && normalizeScrewAngle(INFINITY) == 0 && normalizeScrewAngle(-INFINITY) == 0);
  CHECK(screwAngleAfterDrag(std::nan(""), 0, 0) == 0);
  CHECK(screwAngleAfterDrag(0, std::nan(""), 0) == 0);
}

void test_hit_radius() {
  CHECK(screwHit(100, 100, 100, 100));
  CHECK(screwHit(100, 100, 100 + kScrewHitRadius, 100));
  CHECK(!screwHit(100, 100, 100 + kScrewHitRadius + 0.5, 100));
  CHECK(!screwHit(100, 100, 108, 108));   // the corner of the view box is not the screw
}
}  // namespace

int main() {
  test_screws_are_deterministic_and_at_the_given_positions();
  test_screws_pinned_to_the_stripper();
  test_slot_and_cross_both_occur_and_at_most_one_screw_goes_missing();
  test_dragging_turns_the_screw_like_a_knob();
  test_angles_are_normalised();
  test_hit_radius();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("fairy_ui_screw_tests: ok");
  return 0;
}
