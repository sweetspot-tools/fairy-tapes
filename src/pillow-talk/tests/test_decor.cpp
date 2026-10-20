/**
 * Fairy Tapes · Pillow Talk — tests for the decor placement: paper tapes and corner screws from the instance seed
 * (pure, no VSTGUI). The wear marks themselves come from shared/ui (fairy_ui/wear.h) and are tested there.
 * Build target: pillow_talk_decor_tests.
 */
#include "ui/decor.h"

#include "fairy_ui/screws.h"
#include "fairy_ui/tape_paper.h"

#include <set>
#include <vector>
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
// The screws are the shared ones (fairy_ui/screws.h, the same as The Stripper's for the same seed), at kScrewPos scaled
// to the panel. 4203449416, 1134715807 and 2027020070 are seeds where the former float version differed.
void test_screws_are_the_shared_ones() {
  const Rect panel{10, 20, 500, 314};
  std::vector<uint32_t> seeds = {4203449416u, 1134715807u, 2027020070u, 1u, 3u, 7u, 555u};
  for (uint32_t k = 0; k < 20000; ++k) seeds.push_back(k * 2654435761u);
  for (uint32_t seed : seeds) {
    const auto shared = FairyTapes::Ui::generateScrews(seed);
    const WearLayout l = layoutFromSeed(seed, WearLevel::Light, panel);
    ASSERT_TRUE(l.screws.size() == shared.size());
    for (std::size_t i = 0; i < shared.size(); ++i) {
      ASSERT_TRUE(l.screws[i].cross == shared[i].cross && l.screws[i].missing == shared[i].missing &&
                  l.screws[i].angleDeg == static_cast<float>(shared[i].initialAngle));
      ASSERT_TRUE(std::fabs(l.screws[i].x - (panel.x + kScrewPos[i][0] * 0.5f)) < 1e-3f &&
                  std::fabs(l.screws[i].y - (panel.y + kScrewPos[i][1] * 0.5f)) < 1e-3f);
    }
  }
}
// Placement pinned (screws: the output of the former ui/wear.cpp, unchanged; tapes: re-pinned on 2026-10-21 when the slots
// moved off the controls, Task 6, before any tape was drawn): a refactor must not move tapes or screws of existing instances.
void test_layout_pinned() {
  auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
  const WearLayout l = layoutFromSeed(5u, WearLevel::Heavy, {0, 0, 1000, 628});
  ASSERT_TRUE(l.tapes.size() == 2);
  ASSERT_TRUE(near(l.tapes[0].cx, 736.425049f) && near(l.tapes[0].cy, 580.610779f) && near(l.tapes[0].w, 65.1670074f) &&
              near(l.tapes[0].h, 22.1169682f) && near(l.tapes[0].rotDeg, -6.67677641f));
  ASSERT_TRUE(near(l.tapes[1].cx, 135.434479f) && near(l.tapes[1].rotDeg, -3.66987896f) &&
              l.tapes[0].slot == 5 && l.tapes[1].slot == 1);
  ASSERT_TRUE(l.screws[0].angleDeg == 34.f && l.screws[0].cross && l.screws[3].angleDeg == 115.f && !l.screws[3].cross);
  const WearLayout m = layoutFromSeed(31337u, WearLevel::Heavy, {0, 0, 1000, 628});
  ASSERT_TRUE(m.tapes.size() == 3 && near(m.tapes[2].cx, 112.072601f) && near(m.tapes[2].rotDeg, 5.90488815f));
  ASSERT_TRUE(m.screws[0].angleDeg == 154.f && !m.screws[0].cross && m.screws[3].cross);
}
// What a tape must never cover: every control view, the scope, the LEVEL rows and the clip LEDs.
std::vector<Rect> tapeObstacles() {
  std::vector<Rect> out;
  for (const ControlSpec& c : kControls) out.push_back(viewRect(c));
  out.push_back(kScopeRect);
  for (const MeterRow& r : kMeterRows) out.push_back(meterBarRect(r));
  for (const ClipSpec& c : kClips) out.push_back(clipRect(c));
  return out;
}
bool overlaps(const FairyTapes::Ui::TapeBox& b, const Rect& r) {
  return b.left < r.x + r.w && b.right > r.x && b.top < r.y + r.h && b.bottom > r.y;
}
// The drawn tape: rotated rectangle plus its ragged ends and shadow (1 px more for safety).
FairyTapes::Ui::TapeBox drawnBox(const TapePlacement& t) {
  return FairyTapes::Ui::tapeBounds(t.cx, t.cy, t.w, t.h, t.rotDeg, FairyTapes::Ui::kTapeRaggedX + 1.f);
}
void test_tapes_never_cover_controls_or_each_other() {
  const Rect panel{0, 0, 1000, 628};
  const std::vector<Rect> obstacles = tapeObstacles();
  for (uint32_t seed = 0; seed < 5000; ++seed) {
    for (WearLevel lv : {WearLevel::Light, WearLevel::Heavy}) {
      const WearLayout l = layoutFromSeed(seed, lv, panel);
      for (size_t i = 0; i < l.tapes.size(); ++i) {
        const auto b = drawnBox(l.tapes[i]);
        for (const Rect& r : obstacles) ASSERT_TRUE(!overlaps(b, r));
        for (size_t j = i + 1; j < l.tapes.size(); ++j) {
          const auto o = drawnBox(l.tapes[j]);
          ASSERT_TRUE(!overlaps(b, Rect{static_cast<float>(o.left), static_cast<float>(o.top),
                                        static_cast<float>(o.right - o.left), static_cast<float>(o.bottom - o.top)}));
        }
      }
    }
  }
}
// Each tape names its slot (the index of its stored text): valid, distinct, and the same tape keeps its slot when the
// wear goes from Light to Heavy (Light is a prefix of Heavy).
void test_tape_slots_are_distinct_and_stable() {
  const Rect panel{0, 0, 1000, 628};
  for (uint32_t seed = 0; seed < 2000; ++seed) {
    const WearLayout light = layoutFromSeed(seed, WearLevel::Light, panel), heavy = layoutFromSeed(seed, WearLevel::Heavy, panel);
    std::set<int> seen;
    for (const auto& t : heavy.tapes) {
      ASSERT_TRUE(t.slot >= 0 && t.slot < kNumTapeSlots);
      ASSERT_TRUE(seen.insert(t.slot).second);
    }
    ASSERT_TRUE(light.tapes.size() <= heavy.tapes.size());
    for (size_t i = 0; i < light.tapes.size(); ++i)
      ASSERT_TRUE(light.tapes[i].slot == heavy.tapes[i].slot && light.tapes[i].cx == heavy.tapes[i].cx);
  }
}
}  // namespace

int main() {
  RUN_TEST(test_tapes_never_cover_controls_or_each_other);
  RUN_TEST(test_tape_slots_are_distinct_and_stable);
  RUN_TEST(test_layout_inside_panel_and_one_screw_missing_at_most);
  RUN_TEST(test_layout_deterministic_and_seed_dependent);
  RUN_TEST(test_screws_independent_of_wear_level);
  RUN_TEST(test_screws_are_the_shared_ones);
  RUN_TEST(test_layout_pinned);
  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  return g_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
