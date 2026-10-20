/** Fairy Tapes · The Stripper — the link between the preference and the common tape kit (fonts, text and paper are tested in shared/ui). */

#include "ui/tape_font_id.h"

#include <cstdio>

using namespace FairyTapes::TheStripper;
using namespace FairyTapes::TheStripper::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

void test_preference_mapping() {
  CHECK(tapeFontIdOf(TapeFont::Zeyada) == TapeFontId::Zeyada);
  CHECK(tapeFontIdOf(TapeFont::RockSalt) == TapeFontId::RockSalt);
  CHECK(tapeFontIdOf(TapeFont::HomemadeApple) == TapeFontId::HomemadeApple);
}

void test_the_editor_names_reach_the_common_kit() {
  // the common glyph data is what the editor draws: the three fonts exist and measure text
  for (TapeFontId f : {TapeFontId::Zeyada, TapeFontId::RockSalt, TapeFontId::HomemadeApple})
    CHECK(textWidth(f, "VOX 3", normalizedFontSize(f, 12.0)) > 0.0);
  CHECK(truncateToWidth("abc", 100, [](const std::string& s) { return 10.0 * s.size(); }) == "abc");
}
} // namespace

int main() {
  test_preference_mapping();
  test_the_editor_names_reach_the_common_kit();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_tape_kit: ok");
  return 0;
}
