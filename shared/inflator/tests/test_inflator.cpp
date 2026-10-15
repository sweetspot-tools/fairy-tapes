/** Fairy Tapes · shared Inflator — waveshaper maths: fixed points, mixer, wet/dry (header-only, no JUCE). */

#include "inflator.h"

#include <cmath>
#include <cstdio>

using namespace inflator;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

constexpr double eps = 1e-9;

void test_identity_is_identity() {
  Shaper<double> s;
  s.setParameters(Shape::identity, 0.25, false);
  for (double x : {0.0, 0.3, 0.7, 1.0}) CHECK(std::fabs(s(x) - x) < eps);
}

void test_quadratic_values() {
  Shaper<double> s;
  s.setParameters(Shape::quadratic, 0.25, false);
  CHECK(std::fabs(s(0.5) - 0.75) < eps);  // 0.5 * (2 - 0.5)
  CHECK(std::fabs(s(1.0) - 1.0) < eps);
  CHECK(std::fabs(s(0.0)) < eps);
}

void test_cubic_values() {
  Shaper<double> s;
  s.setParameters(Shape::cubic, 0.25, false);
  CHECK(std::fabs(s(0.5) - 0.625) < eps);  // 0.5 * (1 + 0.5 * 0.5)
  CHECK(std::fabs(s(1.0) - 1.0) < eps);
}

void test_quartic_and_sin_hit_one_at_full_scale() {
  // every curve passes through (0,0) and (1,1) when compensation is off
  for (double curve : {0.0, 0.25, 0.5, 1.0}) {
    Shaper<double> q;
    q.setParameters(Shape::quartic, curve, false);
    CHECK(std::fabs(q(1.0) - 1.0) < 1e-7);
    CHECK(std::fabs(q(0.0)) < eps);

    Shaper<double> s;
    s.setParameters(Shape::sin, curve, false);
    CHECK(std::fabs(s(1.0) - 1.0) < 1e-9);
    CHECK(std::fabs(s(0.0)) < eps);
  }
}

void test_compensation_scales_quadratic() {
  Shaper<double> s;
  s.setParameters(Shape::quadratic, 0.25, true);
  CHECK(std::fabs(s(1.0) - 1.0 / 1.7619606880293588) < eps);
}

void test_mixer_weight_selects_shaper() {
  ShaperMixer<double> m;
  m.setTypes(static_cast<std::size_t>(Shape::quadratic), static_cast<std::size_t>(Shape::cubic));
  m.setShapes(0.25, 0.25, 0.0, false);  // weight 0: only shaper 1
  CHECK(std::fabs(m(0.5) - 0.75) < eps);
  m.setShapes(0.25, 0.25, 1.0, false);  // weight 1: only shaper 2
  CHECK(std::fabs(m(0.5) - 0.625) < eps);
}

void test_inflate_wet_dry_and_sign() {
  Inflator<double> inf;
  inf.setTypes(static_cast<std::size_t>(Shape::quadratic), static_cast<std::size_t>(Shape::quadratic));
  inf.setShapes(0.25, 0.25, 0.0, false);

  inf.setWet(0.0);  // fully dry: bypass
  CHECK(std::fabs(inf(0.4) - 0.4) < eps);
  CHECK(std::fabs(inf(-0.4) + 0.4) < eps);

  inf.setWet(1.0);  // fully wet: symmetric shaping around zero
  CHECK(std::fabs(inf(0.5) - 0.75) < eps);
  CHECK(std::fabs(inf(-0.5) + 0.75) < eps);

  inf.setWet(0.5);
  CHECK(std::fabs(inf(0.5) - (0.5 * 0.75 + 0.5 * 0.5)) < eps);
}

void test_inflate_clamps_shaper_input() {
  // the shaper only sees |x| clamped to 1; the dry part keeps the raw value
  Inflator<double> inf;
  inf.setTypes(static_cast<std::size_t>(Shape::quadratic), static_cast<std::size_t>(Shape::quadratic));
  inf.setShapes(0.25, 0.25, 0.0, false);
  inf.setWet(1.0);
  CHECK(std::fabs(inf(3.0) - 1.0) < eps);
}
}  // namespace

int main() {
  test_identity_is_identity();
  test_quadratic_values();
  test_cubic_values();
  test_quartic_and_sin_hit_one_at_full_scale();
  test_compensation_scales_quadratic();
  test_mixer_weight_selects_shaper();
  test_inflate_wet_dry_and_sign();
  test_inflate_clamps_shaper_input();
  if (g_failed == 0) std::printf("inflator tests: OK\n");
  return g_failed == 0 ? 0 : 1;
}
