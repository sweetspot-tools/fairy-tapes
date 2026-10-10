/**
 * Fairy Tapes · Pillow Talk — tests for runtime wear generation (pure, no VSTGUI).
 * Build target: pillow_talk_wear_tests.
 */
#include "ui/wear.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

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

std::vector<uint8_t> render(int w, int h, uint32_t seed, Wear lv, bool tapes = false) {
  std::vector<uint8_t> b(static_cast<size_t>(w) * h * 4, 0xAB);  // garbage: renderer must clear
  if (tapes) renderTapes(b.data(), w, h, 1, seed, lv);
  else renderWear(b.data(), w, h, 1, seed, lv);
  return b;
}
size_t covered(const std::vector<uint8_t>& b) {
  size_t n = 0;
  for (size_t i = 3; i < b.size(); i += 4) n += b[i] > 0;
  return n;
}
double alphaSum(const std::vector<uint8_t>& b) {
  double s = 0;
  for (size_t i = 3; i < b.size(); i += 4) s += b[i];
  return s;
}

void test_same_seed_same_bytes() {
  ASSERT_TRUE(render(500, 314, 12345u, Wear::Heavy) == render(500, 314, 12345u, Wear::Heavy));
  ASSERT_TRUE(render(500, 314, 7u, Wear::Light) == render(500, 314, 7u, Wear::Light));
  ASSERT_TRUE(render(500, 314, 7u, Wear::Light, true) == render(500, 314, 7u, Wear::Light, true));
}
void test_different_seed_different_bytes() {
  ASSERT_TRUE(render(500, 314, 1u, Wear::Light) != render(500, 314, 2u, Wear::Light));
  ASSERT_TRUE(render(500, 314, 1u, Wear::Heavy) != render(500, 314, 2u, Wear::Heavy));
}
void test_off_is_transparent() {
  auto b = render(200, 126, 99u, Wear::Off);
  ASSERT_TRUE(covered(b) == 0);
  for (uint8_t v : b) ASSERT_TRUE(v == 0);
  auto t = render(200, 126, 99u, Wear::Off, true);
  ASSERT_TRUE(covered(t) == 0);
}
void test_heavy_covers_more_than_light() {
  for (uint32_t seed : {1u, 2u, 3u, 4u, 5u, 77u, 4000000000u}) {
    auto l = render(500, 314, seed, Wear::Light), hv = render(500, 314, seed, Wear::Heavy);
    ASSERT_TRUE(covered(l) > 0);
    ASSERT_TRUE(covered(hv) > covered(l));
    ASSERT_TRUE(alphaSum(hv) > alphaSum(l));
  }
}
void test_stays_in_buffer_odd_sizes() {
  // Guard bytes around a buffer of odd size must be untouched.
  for (auto sz : {std::pair<int, int>{1, 1}, {7, 3}, {333, 211}, {1001, 629}}) {
    const size_t n = static_cast<size_t>(sz.first) * sz.second * 4;
    std::vector<uint8_t> mem(n + 64, 0x5C);
    renderWear(mem.data() + 32, sz.first, sz.second, 1, 42u, Wear::Heavy);
    for (int i = 0; i < 32; ++i) ASSERT_TRUE(mem[i] == 0x5C);
    for (int i = 0; i < 32; ++i) ASSERT_TRUE(mem[32 + n + i] == 0x5C);
    renderTapes(mem.data() + 32, sz.first, sz.second, 2, 42u, Wear::Heavy);
    for (int i = 0; i < 32; ++i) ASSERT_TRUE(mem[i] == 0x5C);
    for (int i = 0; i < 32; ++i) ASSERT_TRUE(mem[32 + n + i] == 0x5C);
  }
}
void test_degenerate_args_are_safe() {
  uint8_t px[4] = {1, 2, 3, 4};
  renderWear(nullptr, 10, 10, 1, 1u, Wear::Heavy);
  renderWear(px, 0, 10, 1, 1u, Wear::Heavy);
  renderWear(px, 10, -1, 1, 1u, Wear::Heavy);
  renderWear(px, 1, 1, 0, 1u, Wear::Heavy);
  renderTapes(nullptr, 10, 10, 1, 1u, Wear::Heavy);
  ASSERT_TRUE(true);
}
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
    for (Wear lv : {Wear::Off, Wear::Light, Wear::Heavy}) {
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
        if (lv == Wear::Off) ASSERT_TRUE(n == 0);
        if (lv == Wear::Light) ASSERT_TRUE(n >= 1 && n <= 2);
        if (lv == Wear::Heavy) ASSERT_TRUE(n >= 2 && n <= 3);
      }
    }
  }
  ASSERT_TRUE(sawMissing > 0);  // the rare missing screw does occur
}
void test_layout_deterministic_and_seed_dependent() {
  const Rect p{0, 0, 1000, 628};
  WearLayout a = layoutFromSeed(5u, Wear::Heavy, p), b = layoutFromSeed(5u, Wear::Heavy, p);
  ASSERT_TRUE(a.tapes.size() == b.tapes.size());
  for (size_t i = 0; i < a.tapes.size(); ++i)
    ASSERT_TRUE(a.tapes[i].cx == b.tapes[i].cx && a.tapes[i].rotDeg == b.tapes[i].rotDeg);
  bool differs = false;
  for (uint32_t s = 6; s < 30 && !differs; ++s) {
    WearLayout c = layoutFromSeed(s, Wear::Heavy, p);
    differs = c.tapes.size() != a.tapes.size() || c.tapes[0].cx != a.tapes[0].cx ||
              c.screws[0].angleDeg != a.screws[0].angleDeg;
  }
  ASSERT_TRUE(differs);
}
void test_screws_independent_of_wear_level() {
  const Rect p{0, 0, 1000, 628};
  WearLayout a = layoutFromSeed(31337u, Wear::Light, p), b = layoutFromSeed(31337u, Wear::Heavy, p);
  for (int i = 0; i < 4; ++i)
    ASSERT_TRUE(a.screws[i].angleDeg == b.screws[i].angleDeg && a.screws[i].missing == b.screws[i].missing &&
                a.screws[i].cross == b.screws[i].cross);
}
void test_tapes_render_only_where_placed() {
  auto b = render(500, 314, 9u, Wear::Heavy, true);
  ASSERT_TRUE(covered(b) > 500);
  auto l = render(500, 314, 9u, Wear::Light, true);
  ASSERT_TRUE(covered(l) > 0 && covered(l) < covered(b) + 1);
}
void test_render_time_sanity() {
  const int w = 2000, h = 1256;
  std::vector<uint8_t> b(static_cast<size_t>(w) * h * 4);
  auto t0 = std::chrono::steady_clock::now();
  renderWear(b.data(), w, h, 2, 123456u, Wear::Heavy);
  auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::fprintf(stdout, "  renderWear Heavy 2000x1256: %.1f ms\n", ms);
  ASSERT_TRUE(ms < 400.0);
}
}  // namespace

int main() {
  RUN_TEST(test_same_seed_same_bytes);
  RUN_TEST(test_different_seed_different_bytes);
  RUN_TEST(test_off_is_transparent);
  RUN_TEST(test_heavy_covers_more_than_light);
  RUN_TEST(test_stays_in_buffer_odd_sizes);
  RUN_TEST(test_degenerate_args_are_safe);
  RUN_TEST(test_layout_inside_panel_and_one_screw_missing_at_most);
  RUN_TEST(test_layout_deterministic_and_seed_dependent);
  RUN_TEST(test_screws_independent_of_wear_level);
  RUN_TEST(test_tapes_render_only_where_placed);
  RUN_TEST(test_render_time_sanity);
  std::fprintf(stdout, "\n%d passed, %d failed.\n", g_passed, g_failed);
  return g_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
