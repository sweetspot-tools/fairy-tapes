/** Fairy Tapes · shared UI — wear generator: determinism, levels, bounds, panel size, pinned output (no VSTGUI). */

#include "fairy_ui/wear.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

bool same(const std::vector<WearMark>& a, const std::vector<WearMark>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].kind != b[i].kind || a[i].pts.size() != b[i].pts.size() || a[i].width != b[i].width || a[i].rgb != b[i].rgb ||
        a[i].alpha != b[i].alpha || a[i].shadowAlpha != b[i].shadowAlpha || a[i].roundCap != b[i].roundCap ||
        a[i].dashes != b[i].dashes)
      return false;
    for (std::size_t k = 0; k < a[i].pts.size(); ++k)
      if (a[i].pts[k].x != b[i].pts[k].x || a[i].pts[k].y != b[i].pts[k].y) return false;
  }
  return true;
}

void test_rng_matches_the_preview() {
  // golden values of mulberry32 (the algorithm of the preview and of its reference implementation)
  WearRng r(1);
  CHECK(std::fabs(r.next() - 0.6270739405881613) < 1e-15);
  CHECK(std::fabs(r.next() - 0.002735721180215478) < 1e-15);
  CHECK(std::fabs(r.next() - 0.5274470399599522) < 1e-15);
  WearRng r2(0x51);
  CHECK(std::fabs(r2.next() - 0.8377828947268426) < 1e-15);
  CHECK(std::fabs(r2.next() - 0.3127019423991442) < 1e-15);
}

void test_same_seed_same_wear() {
  CHECK(same(generateWear(1234, WearLevel::Light), generateWear(1234, WearLevel::Light)));
  CHECK(same(generateWear(1234, WearLevel::Heavy), generateWear(1234, WearLevel::Heavy)));
}

void test_different_seeds_differ() { CHECK(!same(generateWear(1, WearLevel::Light), generateWear(2, WearLevel::Light))); }

void test_off_gives_nothing_and_heavy_gives_more_than_light() {
  CHECK(generateWear(7, WearLevel::Off).empty());
  const auto light = generateWear(7, WearLevel::Light), heavy = generateWear(7, WearLevel::Heavy);
  CHECK(!light.empty());
  CHECK(heavy.size() > light.size() * 3 / 2);
}

void test_marks_are_well_formed_and_on_the_panel() {
  for (std::uint32_t seed : {0u, 1u, 0x51u, 0xFFFFFFFFu, 987654321u}) {
    for (WearLevel level : {WearLevel::Light, WearLevel::Heavy}) {
      int scratches = 0, specks = 0, bristles = 0, stains = 0;
      for (const auto& m : generateWear(seed, level)) {
        CHECK(m.alpha >= 0 && m.alpha <= 1 && m.width > 0 && m.rgb <= 0xFFFFFF);
        std::size_t expected = m.kind == WearMark::Kind::Speck ? 1 : m.kind == WearMark::Kind::Stain ? 9 : 3;
        CHECK(m.pts.size() == expected);
        for (const auto& p : m.pts) {
          CHECK(std::isfinite(p.x) && std::isfinite(p.y));
          CHECK(p.x > -300 && p.x < kWearWidth + 300 && p.y > -300 && p.y < kWearHeight + 300);   // long scratches may run off an edge
        }
        for (double d : m.dashes) CHECK(d > 0 && std::isfinite(d));
        scratches += m.kind == WearMark::Kind::Scratch;
        specks += m.kind == WearMark::Kind::Speck;
        bristles += m.kind == WearMark::Kind::Bristle;
        stains += m.kind == WearMark::Kind::Stain;
      }
      CHECK(scratches > 0 && specks > 0 && bristles > 0 && stains > 0);
    }
  }
}

void test_amounts() {
  CHECK(wearAmount(WearLevel::Off) == 0.0 && wearAmount(WearLevel::Light) == 1.0 && wearAmount(WearLevel::Heavy) == 2.2);
}

// The output of The Stripper's generator (src/the-stripper/src/ui/wear.h) for seed 0x51: the shared one must give the
// same marks so both plug-ins look alike.
void test_pinned_output_matches_the_stripper() {
  const auto light = generateWear(0x51, WearLevel::Light);
  CHECK(light.size() == 248);
  int counts[4] = {0, 0, 0, 0};
  for (const auto& m : light) ++counts[static_cast<int>(m.kind)];
  CHECK(counts[0] == 6 && counts[1] == 97 && counts[2] == 55 && counts[3] == 90);   // stains, bristles, scratches, specks
  if (light.size() == 248) {
    const WearMark& s = light[0];
    CHECK(s.kind == WearMark::Kind::Stain && s.pts.size() == 9 && s.rgb == 0xd8d8d2);
    CHECK(near(s.pts[0].x, 370.29187020676409) && near(s.pts[0].y, 285.86050206795335));
    CHECK(near(s.alpha, 0.030750145102851092));
    const WearMark& b = light[6];
    CHECK(b.kind == WearMark::Kind::Bristle && b.rgb == 0x8c8778 && b.dashes.size() == 6);
    CHECK(near(b.pts[0].x, 543.1996369053021) && near(b.pts[0].y, 485.18289549474855) && near(b.width, 0.71116745391860603));
    const WearMark& k = light[247];
    CHECK(k.kind == WearMark::Kind::Speck && k.rgb == 0xcfcfc8);
    CHECK(near(k.pts[0].x, 408.85196771007031) && near(k.pts[0].y, 77.057603947818279));
    CHECK(near(k.width, 0.7627449044957757) && near(k.alpha, 0.37258944269269711));
  }
  const auto heavy = generateWear(0x51, WearLevel::Heavy);
  CHECK(heavy.size() == 583);
  if (heavy.size() == 583) {
    CHECK(near(heavy[6].pts[0].x, 596.25959152975963) && near(heavy[6].pts[0].y, 485.31241228338331));
    CHECK(near(heavy[582].pts[0].x, 253.20822235196829) && near(heavy[582].pts[0].y, 498.52789349853992));
  }
}

void test_panel_size_scales_positions_only() {
  // the default size is the 1000 x 628 reference: passing it explicitly changes nothing
  CHECK(same(generateWear(99, WearLevel::Heavy), generateWear(99, WearLevel::Heavy, kWearWidth, kWearHeight)));
  // a panel twice as large: same marks (count, kinds, colours, widths), positions scaled
  const auto ref = generateWear(99, WearLevel::Light), big = generateWear(99, WearLevel::Light, 2000, 1256);
  CHECK(ref.size() == big.size());
  bool scaled = ref.size() == big.size();
  for (std::size_t i = 0; scaled && i < ref.size(); ++i) {
    scaled = ref[i].kind == big[i].kind && ref[i].rgb == big[i].rgb && ref[i].width == big[i].width;
    if (ref[i].kind == WearMark::Kind::Speck) scaled = scaled && near(big[i].pts[0].x, 2 * ref[i].pts[0].x, 1e-6) &&
                                                       near(big[i].pts[0].y, 2 * ref[i].pts[0].y, 1e-6);
  }
  CHECK(scaled);
  // every mark of a small panel starts on it (scratches and bristles may run off by their length)
  for (const auto& m : generateWear(5, WearLevel::Heavy, 480, 560)) {
    if (m.kind == WearMark::Kind::Speck) CHECK(m.pts[0].x >= 0 && m.pts[0].x <= 480 && m.pts[0].y >= 0 && m.pts[0].y <= 560);
    if (m.kind == WearMark::Kind::Scratch) CHECK(m.pts[0].x >= 0 && m.pts[0].x <= 480 && m.pts[0].y >= 0 && m.pts[0].y <= 560);
  }
}
} // namespace

int main() {
  test_rng_matches_the_preview();
  test_same_seed_same_wear();
  test_different_seeds_differ();
  test_off_gives_nothing_and_heavy_gives_more_than_light();
  test_marks_are_well_formed_and_on_the_panel();
  test_amounts();
  test_pinned_output_matches_the_stripper();
  test_panel_size_scales_positions_only();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("fairy_ui_wear_tests: ok");
  return 0;
}
