/**
 * Fairy Tapes · Pillow Talk — the corner screws in the instance decor: shown angle = seeded initial angle + the user's
 * rotation (Decor::screwAngles), the rotation kept after a drag, the screw views' specs (pure, no VSTGUI).
 * Build target: pillow_talk_screw_math_tests.
 */
#include "ui/screw_angles.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace FairyTapes::PillowTalk;
using namespace FairyTapes::PillowTalk::Ui;

namespace {
int g_failed = 0, g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)
#define RUN_TEST(fn) do { fn(); ++g_passed; } while(0)

bool near(double a, double b) { return std::fabs(a - b) < 1e-4; }

void test_shown_angle_is_initial_plus_user_rotation() {
  ASSERT_TRUE(near(shownScrewAngle(30.f, 0.f), 30));
  ASSERT_TRUE(near(shownScrewAngle(30.f, 45.5f), 75.5));
  ASSERT_TRUE(near(shownScrewAngle(170.f, 300.f), 110));   // wraps
  ASSERT_TRUE(near(shownScrewAngle(30.f, std::numeric_limits<float>::quiet_NaN()), 30));   // bad rotation: none
  ASSERT_TRUE(near(shownScrewAngle(30.f, INFINITY), 30));
}

void test_user_rotation_after_a_drag() {
  ASSERT_TRUE(near(userScrewAngle(75.5, 30.f), 45.5));
  ASSERT_TRUE(near(userScrewAngle(10, 30.f), 340));   // turned back past the start
  ASSERT_TRUE(userScrewAngle(30, 30.f) == 0.f);
  ASSERT_TRUE(userScrewAngle(std::nan(""), 30.f) >= 0.f && userScrewAngle(std::nan(""), 30.f) < 360.f);
  // round trip: what is shown after saving is what the user left
  for (float initial : {0.f, 8.f, 91.f, 180.f})
    for (double shown : {0.0, 0.25, 33.3, 179.9, 359.75}) {
      const float user = userScrewAngle(shown, initial);
      ASSERT_TRUE(user >= 0.f && user < 360.f);
      const double back = shownScrewAngle(initial, user);
      ASSERT_TRUE(near(back, shown) || near(std::fabs(back - shown), 360));
    }
}

void test_with_screw_angle_changes_only_that_screw() {
  Decor d;
  d.seed = 77;
  d.screwAngles[1] = 12.f;
  d.tapeTexts = {"VOX"};
  const Decor e = withScrewAngle(d, 2, 400.f);
  ASSERT_TRUE(e.seed == 77 && e.tapeTexts == d.tapeTexts);
  ASSERT_TRUE(e.screwAngles[0] == 0.f && e.screwAngles[1] == 12.f && e.screwAngles[2] == 40.f && e.screwAngles[3] == 0.f);
  ASSERT_TRUE(withScrewAngle(d, -1, 5.f).screwAngles[0] == 0.f);   // out of range: unchanged
  ASSERT_TRUE(withScrewAngle(d, kNumScrews, 5.f).screwAngles[3] == 0.f);
  ASSERT_TRUE(withScrewAngle(d, 0, std::nanf("")).screwAngles[0] == 0.f);
}

void test_screw_specs_for_the_views() {
  for (uint32_t seed : {1u, 3u, 7u, 555u, 123456789u}) {
    const auto shared = FairyTapes::Ui::generateScrews(seed);
    const auto specs = screwSpecs(seed);
    for (int i = 0; i < kNumScrews; ++i) {
      ASSERT_TRUE(specs[i].x == kScrewPos[i][0] && specs[i].y == kScrewPos[i][1]);
      ASSERT_TRUE(specs[i].cross == shared[i].cross && specs[i].missing == shared[i].missing &&
                  specs[i].initialAngle == shared[i].initialAngle);
    }
  }
}
}  // namespace

int main() {
  RUN_TEST(test_shown_angle_is_initial_plus_user_rotation);
  RUN_TEST(test_user_rotation_after_a_drag);
  RUN_TEST(test_with_screw_angle_changes_only_that_screw);
  RUN_TEST(test_screw_specs_for_the_views);
  std::printf("%d passed, %d failed.\n", g_passed - g_failed, g_failed);
  return g_failed ? 1 : 0;
}
