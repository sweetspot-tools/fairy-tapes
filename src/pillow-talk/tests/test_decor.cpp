/**
 * Fairy Tapes · Pillow Talk — tests for the decor placement: paper tapes and corner screws from the instance seed
 * (pure, no VSTGUI). The wear marks themselves come from shared/ui (fairy_ui/wear.h) and are tested there.
 * Build target: pillow_talk_decor_tests.
 */
#include "ui/decor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace FairyTapes::PillowTalk::Ui;
using FairyTapes::Ui::WearLevel;

namespace {
int g_failed = 0, g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)
#define RUN_TEST(fn) do { fn(); ++g_passed; } while(0)

bool tapeInside(const TapePlacement& t, const Rect& p) {
  const float r = t.rotDeg * 3.14159265f / 180.f, c = std::cos(r), s = std::sin(r);
  for (int i = 0; i < 4; ++i) {
    const float lx = (i & 1 ? 0.5f : -0.5f) * t.w, ly = (i & 2 ? 0.5f : -0.5f) * t.h;
    const float x = t.cx + lx * c - ly * s, y = t.cy + lx * s + ly * c;
    if (x < p.x || x > p.x + p.w || y < p.y || y > p.y + p.h) return false;
  }
  return true;
}
void test_layout_inside_panel_and_one_screw_missing_at_most() {
  const Rect panels[] = {{0, 0, 1000, 628}, {0, 0, 2000, 1256}, {10, 20, 500, 314}};
  int sawMissing = 0;
  for (uint32_t seed = 0; seed < 3000; ++seed) {
    for (WearLevel lv : {WearLevel::Off, WearLevel::Light, WearLevel::Heavy}) {
      for (const Rect& p : panels) {
        WearLayout l = layoutFromSeed(seed, lv, p);
        ASSERT_TRUE(l.screws.size() == 4);
        int missing = 0;
        for (const auto& s : l.screws) {
          missing += s.missing;
          ASSERT_TRUE(s.x >= p.x && s.x <= p.x + p.w && s.y >= p.y && s.y <= p.y + p.h);
        }
        ASSERT_TRUE(missing <= 1);
        sawMissing += missing;
        for (const auto& t : l.tapes) ASSERT_TRUE(tapeInside(t, p));
        const size_t n = l.tapes.size();
        if (lv == WearLevel::Off) ASSERT_TRUE(n == 0);
        if (lv == WearLevel::Light) ASSERT_TRUE(n >= 1 && n <= 2);
        if (lv == WearLevel::Heavy) ASSERT_TRUE(n >= 2 && n <= 3);
      }
    }
  }
  ASSERT_TRUE(sawMissing > 0);  // the rare missing screw does occur
}
void test_layout_deterministic_and_seed_dependent() {
  const Rect p{0, 0, 1000, 628};
  WearLayout a = layoutFromSeed(5u, WearLevel::Heavy, p), b = layoutFromSeed(5u, WearLevel::Heavy, p);
  ASSERT_TRUE(a.tapes.size() == b.tapes.size());
  for (size_t i = 0; i < a.tapes.size(); ++i)
    ASSERT_TRUE(a.tapes[i].cx == b.tapes[i].cx && a.tapes[i].rotDeg == b.tapes[i].rotDeg);
  bool differs = false;
  for (uint32_t s = 6; s < 30 && !differs; ++s) {
    WearLayout c = layoutFromSeed(s, WearLevel::Heavy, p);
    differs = c.tapes.size() != a.tapes.size() || c.tapes[0].cx != a.tapes[0].cx ||
              c.screws[0].angleDeg != a.screws[0].angleDeg;
  }
  ASSERT_TRUE(differs);
}
void test_screws_independent_of_wear_level() {
  const Rect p{0, 0, 1000, 628};
  WearLayout a = layoutFromSeed(31337u, WearLevel::Light, p), b = layoutFromSeed(31337u, WearLevel::Heavy, p);
  for (int i = 0; i < 4; ++i)
    ASSERT_TRUE(a.screws[i].angleDeg == b.screws[i].angleDeg && a.screws[i].missing == b.screws[i].missing &&
                a.screws[i].cross == b.screws[i].cross);
}
// Placement pinned to the output of the former ui/wear.cpp (before the move to decor): the refactor must not move
// tapes or screws of existing instances.
void test_layout_pinned() {
  auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
  const WearLayout l = layoutFromSeed(5u, WearLevel::Heavy, {0, 0, 1000, 628});
  ASSERT_TRUE(l.tapes.size() == 2);
  ASSERT_TRUE(near(l.tapes[0].cx, 848.212524f) && near(l.tapes[0].cy, 536.068909f) && near(l.tapes[0].w, 65.1670074f) &&
              near(l.tapes[0].h, 22.1169682f) && near(l.tapes[0].rotDeg, -6.67677641f));
  ASSERT_TRUE(near(l.tapes[1].cx, 504.755157f) && near(l.tapes[1].rotDeg, -3.66987896f));
  ASSERT_TRUE(l.screws[0].angleDeg == 34.f && l.screws[0].cross && l.screws[3].angleDeg == 115.f && !l.screws[3].cross);
  const WearLayout m = layoutFromSeed(31337u, WearLevel::Heavy, {0, 0, 1000, 628});
  ASSERT_TRUE(m.tapes.size() == 3 && near(m.tapes[2].cx, 484.313538f) && near(m.tapes[2].rotDeg, 5.90488815f));
  ASSERT_TRUE(m.screws[0].angleDeg == 154.f && !m.screws[0].cross && m.screws[3].cross);
}
}  // namespace

int main() {
  RUN_TEST(test_layout_inside_panel_and_one_screw_missing_at_most);
  RUN_TEST(test_layout_deterministic_and_seed_dependent);
  RUN_TEST(test_screws_independent_of_wear_level);
  RUN_TEST(test_layout_pinned);
  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  return g_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
