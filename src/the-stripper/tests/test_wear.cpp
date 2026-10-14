/** Fairy Tapes · The Stripper — wear generator: determinism, levels, bounds (header-only, no VSTGUI). */

#include "ui/wear.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::TheStripper;
using namespace FairyTapes::TheStripper::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

bool same(const std::vector<WearMark>& a, const std::vector<WearMark>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].kind != b[i].kind || a[i].pts.size() != b[i].pts.size() || a[i].width != b[i].width || a[i].rgb != b[i].rgb ||
        a[i].alpha != b[i].alpha || a[i].dashes != b[i].dashes)
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

void test_different_seeds_differ() {
  CHECK(!same(generateWear(1, WearLevel::Light), generateWear(2, WearLevel::Light)));
}

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
} // namespace

int main() {
  test_rng_matches_the_preview();
  test_same_seed_same_wear();
  test_different_seeds_differ();
  test_off_gives_nothing_and_heavy_gives_more_than_light();
  test_marks_are_well_formed_and_on_the_panel();
  test_amounts();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_wear: ok");
  return 0;
}
