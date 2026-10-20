/**
 * Fairy Tapes · Pillow Talk — tests for the writing on the paper tapes (ui/tapes.h): font preference, the text of each
 * tape slot in the instance decor, editing it, and the ink that never leaves its tape. Pure, no VSTGUI.
 * Build target: pillow_talk_tapes_tests.
 */
#include "ui/tapes.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace FairyTapes::PillowTalk;
using namespace FairyTapes::PillowTalk::Ui;
using FairyTapes::Ui::TapeFontId;
using FairyTapes::Ui::WearLevel;

namespace {
int g_failed = 0, g_passed = 0;
#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; return; } \
} while (0)
#define RUN_TEST(fn) do { const int before_ = g_failed; fn(); if (g_failed == before_) ++g_passed; } while (0)

const TapeFontId kFonts[] = {TapeFontId::Zeyada, TapeFontId::RockSalt, TapeFontId::HomemadeApple};

void test_font_preference_maps_to_the_shared_font() {
  ASSERT_TRUE(tapeFontIdOf(TapeFont::Zeyada) == TapeFontId::Zeyada);
  ASSERT_TRUE(tapeFontIdOf(TapeFont::RockSalt) == TapeFontId::RockSalt);
  ASSERT_TRUE(tapeFontIdOf(TapeFont::HomemadeApple) == TapeFontId::HomemadeApple);
}

void test_text_of_a_slot() {
  Decor d;
  ASSERT_TRUE(tapeTextFor(d, 0).empty());          // never written: blank paper
  d.tapeTexts = {"VOX", "", "keep!"};
  ASSERT_TRUE(tapeTextFor(d, 0) == "VOX" && tapeTextFor(d, 1).empty() && tapeTextFor(d, 2) == "keep!");
  ASSERT_TRUE(tapeTextFor(d, 3).empty() && tapeTextFor(d, -1).empty() && tapeTextFor(d, 99).empty());
}

void test_writing_on_a_slot() {
  Decor d;
  d.seed = 77;
  Decor e = withTapeText(d, 4, "  TAKE 3  ");
  ASSERT_TRUE(e.seed == 77 && e.tapeTexts.size() == 5 && e.tapeTexts[4] == "TAKE 3" && e.tapeTexts[0].empty());
  e = withTapeText(e, 4, "a very long text indeed");
  ASSERT_TRUE(e.tapeTexts[4] == "a very long te");                      // at most 14 characters
  e = withTapeText(e, 1, "caf\xC3\xA9\t!");
  ASSERT_TRUE(e.tapeTexts[1] == "caf!");                                // printable ASCII only
  e = withTapeText(e, 4, "");
  ASSERT_TRUE(e.tapeTexts[4].empty());                                  // erased
  const Decor f = withTapeText(e, kNumTapeSlots, "x");                  // no such slot: unchanged
  ASSERT_TRUE(f.tapeTexts == e.tapeTexts);
  ASSERT_TRUE(withTapeText(e, -1, "x").tapeTexts == e.tapeTexts);
}

void test_look_is_seeded_per_slot() {
  const TapeLook a = tapeLook(5, 2), b = tapeLook(5, 2), c = tapeLook(5, 3), d = tapeLook(6, 2);
  ASSERT_TRUE(a.textSize == b.textSize && a.textRotDeg == b.textRotDeg && a.paperSeed == b.paperSeed && a.inkSeed == b.inkSeed);
  ASSERT_TRUE(a.paperSeed != c.paperSeed && a.paperSeed != d.paperSeed);
  for (uint32_t s = 0; s < 500; ++s)
    for (int slot = 0; slot < kNumTapeSlots; ++slot) {
      const TapeLook l = tapeLook(s, slot);
      ASSERT_TRUE(l.textSize >= 11 && l.textSize <= 14 && std::fabs(l.textRotDeg) <= 3);
    }
}

// The widest the laid-out writing really reaches (per-letter size and spacing variation included).
double inkExtent(TapeFontId f, const TapeInk& ink, uint32_t seed) {
  double lo = 0, hi = 0;
  for (const auto& p : FairyTapes::Ui::layoutText(f, ink.text, ink.emPx, seed, 1.0)) {
    lo = std::min(lo, p.x);
    hi = std::max(hi, p.x + p.glyph->advance / 1000.0 * ink.emPx * p.scale);
  }
  return hi - lo;
}

void test_ink_never_leaves_the_tape() {
  const char* texts[] = {"VOX", "TAKE 3", "WWWWWWWWWWWWWW", "mmmmmmmmmmmmmm", "@@@@@@@@@@@@@@", "a b c d e f g", "7/12 keep!"};
  for (TapeFontId f : kFonts)
    for (const char* t : texts)
      for (double w : {58.0, 70.0, 96.0})
        for (uint32_t seed = 0; seed < 40; ++seed) {
          const TapeLook look = tapeLook(seed, static_cast<int>(seed % kNumTapeSlots));
          const TapeInk ink = tapeInk(f, t, w, look);
          ASSERT_TRUE(ink.emPx > 0);
          ASSERT_TRUE(inkExtent(f, ink, look.inkSeed) <= w - 2 * kTapeTextEdge);
        }
  // a short word keeps its nominal size
  const TapeLook look = tapeLook(1, 0);
  const TapeInk vox = tapeInk(TapeFontId::Zeyada, "VOX", 96, look);
  ASSERT_TRUE(vox.text == "VOX" && vox.emPx == FairyTapes::Ui::normalizedFontSize(TapeFontId::Zeyada, look.textSize));
  // blank paper
  ASSERT_TRUE(tapeInk(TapeFontId::Zeyada, "", 96, look).text.empty());
}
}  // namespace

int main() {
  RUN_TEST(test_font_preference_maps_to_the_shared_font);
  RUN_TEST(test_text_of_a_slot);
  RUN_TEST(test_writing_on_a_slot);
  RUN_TEST(test_look_is_seeded_per_slot);
  RUN_TEST(test_ink_never_leaves_the_tape);
  std::printf("pillow_talk_tapes_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
