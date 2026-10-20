/** Fairy Tapes · shared UI — writable paper tape: text rules (sanitising, fitting scale) and the seeded paper body
 * (ragged outline, fibres, bounds). No VSTGUI. Build target: fairy_ui_tape_tests. */

#include "fairy_ui/tape_label.h"
#include "fairy_ui/tape_paper.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

using namespace FairyTapes::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

void test_sanitize_keeps_printable_ascii_trimmed_and_short() {
  CHECK(kTapeTextMaxChars == 14);
  CHECK(sanitizeTapeText("VOX") == "VOX");
  CHECK(sanitizeTapeText("  take 3  ") == "take 3");
  CHECK(sanitizeTapeText("a\tb\nc\x7f" "d") == "abcd");                // control characters and DEL removed
  CHECK(sanitizeTapeText("caf\xC3\xA9") == "caf");                      // non-ASCII bytes removed
  CHECK(sanitizeTapeText("abcdefghijklmnopqrstuvwxyz") == "abcdefghijklmn");
  CHECK(sanitizeTapeText("abcdefghijklm   xyz") == "abcdefghijklm");   // cut, then trimmed again
  CHECK(sanitizeTapeText("").empty());
  CHECK(sanitizeTapeText("    ").empty());
  CHECK(sanitizeTapeText(std::string("ab\0cd", 5)) == "abcd");          // embedded NUL
  CHECK(sanitizeTapeText("abcdef", 3) == "abc");
  CHECK(sanitizeTapeText(std::string(10000, 'x')).size() == 14);
  for (char c : sanitizeTapeText("~!@#$%^&*()_+{}|:\"<>?")) CHECK(c >= 0x20 && c < 0x7f);
}

void test_fit_scale_never_exceeds_the_tape() {
  CHECK(fitScale(50, 100) == 1.0);       // fits: never enlarged
  CHECK(fitScale(100, 100) == 1.0);
  CHECK(std::fabs(fitScale(200, 100) - 0.5) < 1e-12);
  CHECK(fitScale(0, 100) == 1.0);        // nothing to draw
  CHECK(fitScale(100, 0) == 0.0);        // no room
  CHECK(fitScale(100, -3) == 0.0);
  const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
  CHECK(fitScale(nan, 100) == 0.0);
  CHECK(fitScale(100, nan) == 0.0);
  CHECK(fitScale(inf, 100) == 0.0);
  CHECK(fitScale(100, inf) == 1.0);
  for (double text = 0.5; text < 2000; text *= 1.37)
    for (double tape = 1; tape < 500; tape *= 1.61) {
      const double s = fitScale(text, tape);
      CHECK(s >= 0.0 && s <= 1.0);
      CHECK(text * s <= tape * (1 + 1e-12));
    }
}

// The paper of The Stripper's tape pieces (src/the-stripper/src/ui/tape_pieces.h), written out: the shared generator must
// consume the generator in the same order and give the same numbers, so the Stripper can switch without a visual change.
TapePaper stripperPaper(double w, double h, WearRng& R) {
  TapePaper p;
  p.w = w;
  p.h = h;
  p.outline.push_back({0, R.range(0, 2.5)});
  for (int k = 1; k <= 4; ++k) p.outline.push_back({w * k / 4, R.range(-1.2, 1.2)});
  for (int q = 0; q <= 5; ++q) p.outline.push_back({w + R.range(-3, 3), h * q / 5});
  for (int k = 4; k >= 0; --k) p.outline.push_back({w * k / 4, h + R.range(-1.2, 1.2)});
  for (int q = 4; q >= 1; --q) p.outline.push_back({R.range(-3, 3), h * q / 5});
  for (int f = 0; f < 5; ++f)
    p.fibres.push_back({R.range(0, w * 0.3), R.range(2, h - 2), R.range(w * 0.6, w), R.range(2, h - 2), R.range(0.08, 0.2)});
  return p;
}

void test_paper_is_seeded_and_matches_the_stripper() {
  for (std::uint32_t seed : {1u, 2u, 0x51u, 123456789u}) {
    WearRng a(seed), b(seed);
    const TapePaper p = makeTapePaper(80, 22, a), q = stripperPaper(80, 22, b);
    CHECK(p.outline.size() == q.outline.size() && p.fibres.size() == q.fibres.size());
    for (std::size_t i = 0; i < p.outline.size() && i < q.outline.size(); ++i)
      CHECK(p.outline[i].x == q.outline[i].x && p.outline[i].y == q.outline[i].y);
    for (std::size_t i = 0; i < p.fibres.size() && i < q.fibres.size(); ++i)
      CHECK(p.fibres[i].x1 == q.fibres[i].x1 && p.fibres[i].y2 == q.fibres[i].y2 && p.fibres[i].alpha == q.fibres[i].alpha);
    CHECK(a.next() == b.next());   // the generator is left in the same place
  }
  const TapePaper x = makeTapePaper(80, 22, 7u), y = makeTapePaper(80, 22, 7u), z = makeTapePaper(80, 22, 8u);
  bool same = true, differs = false;
  for (std::size_t i = 0; i < x.outline.size(); ++i) {
    same &= x.outline[i].x == y.outline[i].x && x.outline[i].y == y.outline[i].y;
    differs |= x.outline[i].x != z.outline[i].x || x.outline[i].y != z.outline[i].y;
  }
  CHECK(same && differs);
}

void test_paper_stays_near_its_rectangle() {
  for (std::uint32_t seed = 0; seed < 500; ++seed) {
    const double w = 58 + seed % 40, h = 19 + seed % 8;
    const TapePaper p = makeTapePaper(w, h, seed);
    CHECK(p.outline.size() == 20 && p.fibres.size() == 5);
    for (const WearPt& pt : p.outline) {
      CHECK(pt.x >= -kTapeRaggedX && pt.x <= w + kTapeRaggedX);
      CHECK(pt.y >= -kTapeRaggedY && pt.y <= h + kTapeRaggedY);
    }
    for (const auto& f : p.fibres) {
      CHECK(f.x1 >= 0 && f.x2 <= w && f.y1 >= 2 && f.y1 <= h - 2 && f.y2 >= 2 && f.y2 <= h - 2);
      CHECK(f.alpha >= 0.08 && f.alpha <= 0.2);
    }
  }
  // degenerate sizes give an empty paper, never NaN
  CHECK(makeTapePaper(0, 20, 1u).outline.empty());
  CHECK(makeTapePaper(80, std::nan(""), 1u).outline.empty());
}

void test_bounds_cover_the_rotated_tape() {
  const TapeBox flat = tapeBounds(100, 50, 80, 20, 0, 0);
  CHECK(flat.left == 60 && flat.right == 140 && flat.top == 40 && flat.bottom == 60);
  const TapeBox padded = tapeBounds(100, 50, 80, 20, 0, 5);
  CHECK(padded.left == 55 && padded.bottom == 65);
  for (double rot = -9; rot <= 9; rot += 1.5) {
    const TapeBox b = tapeBounds(300, 200, 90, 24, rot, 0);
    const double r = rot * 3.14159265358979323846 / 180, c = std::cos(r), s = std::sin(r);
    for (int i = 0; i < 4; ++i) {   // every corner of the rotated tape is inside
      const double lx = (i & 1 ? 0.5 : -0.5) * 90, ly = (i & 2 ? 0.5 : -0.5) * 24;
      const double x = 300 + lx * c - ly * s, y = 200 + lx * s + ly * c;
      CHECK(x >= b.left - 1e-9 && x <= b.right + 1e-9 && y >= b.top - 1e-9 && y <= b.bottom + 1e-9);
    }
    CHECK(std::fabs((b.left + b.right) / 2 - 300) < 1e-9 && std::fabs((b.top + b.bottom) / 2 - 200) < 1e-9);
  }
}
} // namespace

int main() {
  test_sanitize_keeps_printable_ascii_trimmed_and_short();
  test_fit_scale_never_exceeds_the_tape();
  test_paper_is_seeded_and_matches_the_stripper();
  test_paper_stays_near_its_rectangle();
  test_bounds_cover_the_rotated_tape();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("fairy_ui_tape_tests: ok");
  return 0;
}
