/** Fairy Tapes · shared UI — unit tests for the control wear generator (no VSTGUI). */
#include "fairy_ui/control_wear.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::Ui;

static int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

static bool same(const std::vector<ControlWearMark>& a, const std::vector<ControlWearMark>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].kind != b[i].kind || a[i].pts != b[i].pts || a[i].size != b[i].size || a[i].rgb != b[i].rgb ||
        a[i].alpha != b[i].alpha || a[i].length != b[i].length || a[i].underCap != b[i].underCap)
      return false;
  }
  return true;
}

int main() {
  // the shared generator is the preview's mulberry32
  WearRng r(1);
  CHECK(std::fabs(r.next() - 0.6270739405881613) < 1e-15);

  // determinism and levels
  CHECK(same(knobWear(42, 1.0, 19), knobWear(42, 1.0, 19)));
  CHECK(!same(knobWear(42, 1.0, 19), knobWear(43, 1.0, 19)));
  CHECK(knobWear(42, 0.0, 19).empty() && buttonWear(42, 0.0, 24, 22).empty());
  CHECK(knobWear(42, 2.2, 19).size() > knobWear(42, 1.0, 19).size());
  CHECK(buttonWear(42, 2.2, 24, 22).size() > buttonWear(42, 1.0, 24, 22).size());
  CHECK(knobWear(1, 1.0, 0).empty());   // nonsense sizes give nothing, no crash

  // geometry: knob marks stay close to the cap, button marks close to the face
  for (std::uint32_t seed : {0u, 1u, 7u, 0xFFFFFFFFu, 123456u}) {
    for (double R : {19.0, 21.0, 22.0}) {
      bool under = false, over = false;
      for (const auto& m : knobWear(seed, 2.2, R)) {
        under |= m.underCap;
        over |= !m.underCap;
        if (m.kind == ControlWearMark::Kind::Pointer) {
          CHECK(m.size >= 0 && m.size <= 1 && m.length > 0 && m.size + m.length <= 1 + 1e-9);
          continue;
        }
        CHECK(!m.pts.empty());
        for (const auto& p : m.pts) {
          CHECK(std::isfinite(p.first) && std::isfinite(p.second));
          CHECK(std::hypot(p.first, p.second) < R * 1.6);
        }
        CHECK(m.alpha >= 0 && m.alpha <= 1);
        if (m.underCap) for (const auto& p : m.pts) CHECK(std::hypot(p.first, p.second) > R * 0.9);   // dirt lies around the base
      }
      CHECK(under && over);
    }
    for (const auto& m : buttonWear(seed, 2.2, 24, 22)) {
      CHECK(!m.pts.empty() && m.alpha >= 0 && m.alpha <= 1 && m.size > 0);
      for (const auto& p : m.pts) CHECK(p.first > -6 && p.first < 30 && p.second > -6 && p.second < 28);
    }
  }
  // the chips sit on the finger side (lower right) more than on the opposite side, over many seeds
  int lowerRight = 0, upperLeft = 0;
  for (std::uint32_t seed = 0; seed < 200; ++seed)
    for (const auto& m : knobWear(seed, 1.0, 19))
      if (m.kind == ControlWearMark::Kind::Chip) {
        const auto& p = m.pts[0];
        if (p.first > 0 && p.second > 0) ++lowerRight;
        if (p.first < 0 && p.second < 0) ++upperLeft;
      }
  CHECK(lowerRight > upperLeft * 2);

  if (g_failed) return 1;
  std::puts("test_control_wear: ok");
  return 0;
}
