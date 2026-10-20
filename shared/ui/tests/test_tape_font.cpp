/** Fairy Tapes · shared UI — tape fonts as vector paths: glyph data, measuring, layout with variation, fitting (no VSTGUI).
 * Ported from The Stripper (src/the-stripper/tests/test_tape_font.cpp). Build target: fairy_ui_tape_font_tests. */

#include "fairy_ui/tape_font.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace FairyTapes::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

const TapeFontId kFonts[] = {TapeFontId::Zeyada, TapeFontId::RockSalt, TapeFontId::HomemadeApple};

void test_fonts_have_the_needed_glyphs() {
  for (TapeFontId f : kFonts) {
    const TapeFontData& d = tapeFontData(f);
    CHECK(d.glyphCount >= 90 && d.unitsPerEm == 1000 && d.digitHeight > 300 && d.digitHeight < 1500);
    for (char32_t cp : {U' ', U'A', U'Z', U'a', U'z', U'0', U'9', U'.', U'-', U'+', U'%', U'!', U'?', U'…'})
      CHECK(findGlyph(f, cp) != nullptr);
    CHECK(findGlyph(f, U'è') == nullptr);   // not embedded: ASCII and the ellipsis only
    CHECK(findGlyph(f, U'A')->advance > 0);
    CHECK(findGlyph(f, U' ')->count == 0 || findGlyph(f, U' ')->advance > 0);
  }
}

void test_glyph_streams_are_well_formed() {
  for (TapeFontId f : kFonts) {
    const TapeFontData& d = tapeFontData(f);
    for (std::size_t g = 0; g < d.glyphCount; ++g) {
      const TapeGlyph& gl = d.glyphs[g];
      std::size_t i = gl.offset;
      const std::size_t end = gl.offset + gl.count;
      bool open = false;
      while (i < end) {
        const int op = d.stream[i++];
        const int args = op == 0 || op == 1 ? 2 : op == 2 ? 4 : op == 3 ? 0 : -1;
        CHECK(args >= 0);
        if (args < 0) break;
        CHECK(i + args <= end);
        for (int k = 0; k < args; ++k) CHECK(std::abs(d.stream[i + k]) < 3000);
        i += args;
        if (op == 0) open = true;
        if (op == 3) { CHECK(open); open = false; }
        if ((op == 1 || op == 2)) CHECK(open);
      }
      CHECK(i == end && !open);
    }
  }
}

void test_digit_height_normalisation() {
  // every font gets its digits to the same height: the standard text digit height times 1.3 (spec 4)
  for (TapeFontId f : kFonts) {
    const double em = normalizedFontSize(f, 10.0);
    const double digitPx = tapeFontData(f).digitHeight / 1000.0 * em;
    CHECK(std::fabs(digitPx - 0.716 * 10.0 * 1.3) < 1e-9);
  }
  CHECK(std::fabs(fontSizeForDigitHeight(TapeFontId::Zeyada, 6.2) - 6.2 * 1000.0 / tapeFontData(TapeFontId::Zeyada).digitHeight) < 1e-9);
  // the fonts really differ in digit size: without normalisation Rock Salt digits are much taller than Zeyada's
  CHECK(tapeFontData(TapeFontId::RockSalt).digitHeight > tapeFontData(TapeFontId::Zeyada).digitHeight * 1.3);
}

void test_utf8_decoding_is_safe() {
  CHECK(decodeUtf8("Ab1").size() == 3);
  CHECK(decodeUtf8("\xC3\xA8").size() == 1);
  CHECK(decodeUtf8("\xE2\x80\xA6")[0] == U'…');
  CHECK(decodeUtf8("\xC3").size() == 1);           // truncated sequence: one replacement, no crash
  CHECK(decodeUtf8("\xFF\xFE").size() == 2);
  CHECK(decodeUtf8("").empty());
}

void test_text_width() {
  for (TapeFontId f : kFonts) {
    CHECK(textWidth(f, "", 20) == 0);
    const double w1 = textWidth(f, "AB", 20), w2 = textWidth(f, "ABAB", 20);
    CHECK(w1 > 0 && w2 > w1 * 1.8 && w2 < w1 * 2.2);
    CHECK(std::fabs(textWidth(f, "AB", 40) - 2 * w1) < 1e-9);   // proportional to the size
    CHECK(textWidth(f, "\xC3\xA8", 20) >= 0);                   // an unknown character is not an error
  }
}

void test_layout_variation_is_seeded_and_bounded() {
  const std::string text = "Take 3 keep!";
  for (TapeFontId f : kFonts) {
    const auto a = layoutText(f, text, 20, 777, 1.0), b = layoutText(f, text, 20, 777, 1.0), c = layoutText(f, text, 20, 778, 1.0);
    CHECK(a.size() == b.size() && a.size() == c.size() && !a.empty());
    bool differs = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      CHECK(a[i].x == b[i].x && a[i].y == b[i].y && a[i].angleDeg == b[i].angleDeg && a[i].scale == b[i].scale);   // deterministic
      differs |= a[i].y != c[i].y || a[i].angleDeg != c[i].angleDeg;
      CHECK(std::fabs(a[i].y) <= 0.05 * 20 + 1e-9);          // at most 5 % of the size up or down
      CHECK(std::fabs(a[i].angleDeg) <= 4.0 + 1e-9);
      CHECK(a[i].scale >= 0.94 && a[i].scale <= 1.06);
      if (i > 0) CHECK(a[i].x > a[i - 1].x - 2.0);            // the letters advance left to right (small jitter allowed)
    }
    CHECK(differs);
    // no variation: straight, level, exact advances
    for (const auto& p : layoutText(f, text, 20, 777, 0.0)) CHECK(p.y == 0 && p.angleDeg == 0 && p.scale == 1.0);
    const auto flat = layoutText(f, "AB", 20, 1, 0.0);
    CHECK(std::fabs(flat[1].x - findGlyph(f, U'A')->advance / 1000.0 * 20) < 1e-9);
  }
}

void test_fit_truncates_by_measured_width() {
  for (TapeFontId f : kFonts) {
    const std::string name = "Plexi_crunch_with_a_very_long_name_indeed";
    const double full = textWidth(f, name, 14);
    CHECK(fitTapeText(f, name, 14, full + 1) == name);                   // fits: untouched
    const std::string cut = fitTapeText(f, name, 14, full / 2);
    CHECK(cut != name && cut.size() < name.size());
    CHECK(cut.size() >= 3 && cut.compare(cut.size() - 3, 3, "\xE2\x80\xA6") == 0);   // ends with the ellipsis
    CHECK(textWidth(f, cut, 14) <= full / 2 + 1e-9);                     // and really fits
    CHECK(fitTapeText(f, name, 14, 0.5).empty() || textWidth(f, fitTapeText(f, name, 14, 0.5), 14) <= 0.5);   // nothing fits: never loops
    CHECK(fitTapeText(f, "", 14, 100).empty());
  }
}

void test_truncate_to_width() {
  auto chars = [](const std::string& s) { return static_cast<double>(s.size()); };   // 1 px per byte
  CHECK(truncateToWidth("abc", 10, chars) == "abc");
  CHECK(truncateToWidth("", 0, chars).empty());
  const std::string cut = truncateToWidth("abcdefghij", 7, chars);   // the ellipsis is 3 bytes here
  CHECK(cut == "abcd\xE2\x80\xA6");
  CHECK(truncateToWidth("ab  cdef", 7, chars) == "ab\xE2\x80\xA6");   // no blank before the ellipsis
  CHECK(truncateToWidth("abcdef", 1, chars).empty());                // nothing fits
}

void test_fit_tape_line_never_exceeds_the_width() {
  for (TapeFontId f : kFonts) {
    const double em = normalizedFontSize(f, 12.0);
    // short text: untouched, full size
    const FittedTapeText a = fitTapeLine(f, "VOX", em, 200.0);
    CHECK(a.text == "VOX" && a.emPx == em);
    // a little too wide: shrunk, not cut
    const double w = textWidth(f, "TAKE 3", em);
    const FittedTapeText b = fitTapeLine(f, "TAKE 3", em, w * 0.85);
    CHECK(b.text == "TAKE 3" && b.emPx < em && b.emPx >= em * 0.7 - 1e-9);
    CHECK(textWidth(f, b.text, b.emPx) <= w * 0.85 + 1e-9);
    // far too wide (14 wide letters on a tiny tape): shrunk to the floor and cut with an ellipsis
    const FittedTapeText c = fitTapeLine(f, "WWWWWWWWWWWWWW", em, 40.0);
    CHECK(c.emPx == em * 0.7);
    CHECK(textWidth(f, c.text, c.emPx) <= 40.0 + 1e-9);
    CHECK(c.text != "WWWWWWWWWWWWWW");
    // degenerate widths and sizes: nothing drawn, no loop
    CHECK(fitTapeLine(f, "abc", em, 0.0).text.empty());
    CHECK(fitTapeLine(f, "abc", em, -5.0).text.empty());
    CHECK(fitTapeLine(f, "abc", std::nan(""), 50.0).text.empty());
    CHECK(fitTapeLine(f, "", em, 50.0).text.empty());
  }
}
} // namespace

int main() {
  test_fonts_have_the_needed_glyphs();
  test_glyph_streams_are_well_formed();
  test_digit_height_normalisation();
  test_utf8_decoding_is_safe();
  test_text_width();
  test_layout_variation_is_seeded_and_bounded();
  test_fit_truncates_by_measured_width();
  test_truncate_to_width();
  test_fit_tape_line_never_exceeds_the_width();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("fairy_ui_tape_font_tests: ok");
  return 0;
}
