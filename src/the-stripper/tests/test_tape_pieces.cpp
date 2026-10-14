/** Fairy Tapes · The Stripper — pieces of writable tape on the panel: seeded layout and default texts (no VSTGUI). */

#include "ui/tape_pieces.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::TheStripper;
using namespace FairyTapes::TheStripper::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

void test_pieces_are_deterministic_and_seeded() {
  const auto a = generateTapePieces(12345), b = generateTapePieces(12345), c = generateTapePieces(12346);
  CHECK(a.size() == static_cast<std::size_t>(kTapePieceCount));
  static_assert(kTapePieceCount <= kMaxTapePieces, "the state must be able to hold the writing of every piece");
  CHECK(a.size() == b.size());
  bool differs = false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    CHECK(a[i].x == b[i].x && a[i].y == b[i].y && a[i].rotationDeg == b[i].rotationDeg && a[i].defaultText == b[i].defaultText);
    differs |= a[i].x != c[i].x || a[i].y != c[i].y;
  }
  CHECK(differs);
}

void test_pieces_stay_on_the_panel_and_clear_of_the_header() {
  for (std::uint32_t seed : {0u, 1u, 0x51u, 0xFFFFFFFFu, 424242u}) {
    for (const auto& p : generateTapePieces(seed)) {
      CHECK(p.w >= 58 && p.w <= 96 && p.h >= 19 && p.h <= 26);
      CHECK(p.x >= 70 && p.x + p.w <= 1000 - 4 && p.y >= 110 && p.y + p.h <= 628);
      CHECK(std::fabs(p.rotationDeg) <= 9.0);
      CHECK(p.outline.size() == 5 + 6 + 5 + 4);   // a closed ragged outline: top 5, right 6, bottom 5, left 4
      CHECK(p.defaultText.size() <= static_cast<std::size_t>(kTapeTextCharacters));
      CHECK(p.textSize >= 11 && p.textSize <= 14);
    }
  }
}

void test_how_many_are_shown() {
  CHECK(piecesShown(WearLevel::Off, 99) == 0);
  for (std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}) {
    const int light = piecesShown(WearLevel::Light, seed), heavy = piecesShown(WearLevel::Heavy, seed);
    CHECK(light >= 1 && light <= 2);
    CHECK(heavy == kTapePieceCount);
    CHECK(heavy >= light);
  }
}

void test_default_texts_come_from_a_list_or_are_empty() {
  int withText = 0, empty = 0;
  for (std::uint32_t seed = 0; seed < 300; ++seed)
    for (const auto& p : generateTapePieces(seed)) (p.defaultText.empty() ? empty : withText)++;
  CHECK(withText > 0 && empty > 0);
}

void test_displayed_text_prefers_what_the_user_wrote() {
  TapePiece p; p.defaultText = "VOX";
  CHECK(displayedTapeText(p, TapeText{false, ""}) == "VOX");     // never written: the default
  CHECK(displayedTapeText(p, TapeText{true, "TAKE 3"}) == "TAKE 3");
  CHECK(displayedTapeText(p, TapeText{true, ""}).empty());       // erased on purpose: stays blank
}

void test_clamp_to_fourteen_characters() {
  CHECK(clampTapeText("0123456789ABCDEFGH") == "0123456789ABCD");
  CHECK(clampTapeText("short") == "short");
  CHECK(clampTapeText("").empty());
  // multi-byte characters count as one character each and are never cut in half
  const std::string e8 = "\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8";   // 16 x e-grave
  const std::string cut = clampTapeText(e8);
  CHECK(utf8Length(cut) == 14 && cut.size() == 28);
  CHECK(clampTapeText("a\nb\tc") == "a b c");   // control characters become spaces
}
} // namespace

int main() {
  test_pieces_are_deterministic_and_seeded();
  test_pieces_stay_on_the_panel_and_clear_of_the_header();
  test_how_many_are_shown();
  test_default_texts_come_from_a_list_or_are_empty();
  test_displayed_text_prefers_what_the_user_wrote();
  test_clamp_to_fourteen_characters();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_tape_pieces: ok");
  return 0;
}
