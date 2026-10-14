/**
 * Fairy Tapes · The Stripper — pieces of writable paper tape stuck on the panel (spec 4, nastro scrivibile). Seeded and
 * deterministic; header-only and free of VSTGUI so it is unit tested. A click on a piece lets the user write on it (up to 14
 * characters, saved in the controller state); a piece that was never written shows a default text or stays blank.
 */

#pragma once

#include "state.h"
#include "ui/model_source.h"
#include "fairy_ui/wear.h"

#include <cmath>
#include <string>
#include <vector>

namespace FairyTapes::TheStripper::Ui {

using FairyTapes::Ui::WearLevel;
using FairyTapes::Ui::WearPt;
using FairyTapes::Ui::WearRng;

constexpr int kTapePieceCount = 3;          // pieces of tape the seed gives the panel (a level may show fewer)
constexpr int kTapeTextCharacters = 14;

struct TapePiece {
  double x = 0, y = 0, w = 0, h = 0;      // the unrotated tape, top left corner in editor px
  double rotationDeg = 0;                 // around the centre
  std::vector<WearPt> outline;            // ragged torn outline, relative to the top left corner (unrotated)
  struct Fibre { double x1, y1, x2, y2, alpha; };
  std::vector<Fibre> fibres;              // faint paper fibres, relative to the top left corner
  std::string defaultText;                // shown until the user writes; may be empty
  double textSize = 12;                   // size of the writing in standard-text px (normalised per font when drawn)
  double textRotationDeg = 0;
};

inline std::vector<TapePiece> generateTapePieces(std::uint32_t seed) {
  static const char* const kDefaults[] = {"VOX", "TAKE 3", "DRUMS?", "BUS", "GTR", "OLD", "7/12", "keep!"};
  WearRng R(seed ^ 0x7A9E5u);
  std::vector<TapePiece> out;
  for (int i = 0; i < kTapePieceCount; ++i) {
    TapePiece p;
    p.x = R.range(70, 900);
    p.y = R.range(110, 560);
    p.w = R.range(58, 96);
    p.h = R.range(19, 26);
    p.rotationDeg = R.range(-9, 9);
    p.x = std::min(p.x, 1000.0 - 4.0 - p.w);   // stays on the panel
    p.y = std::min(p.y, 628.0 - p.h - 4.0);
    // torn ends: a jittered top edge, jittered sides, a jittered bottom edge
    p.outline.push_back({0, R.range(0, 2.5)});
    for (int k = 1; k <= 4; ++k) p.outline.push_back({p.w * k / 4, R.range(-1.2, 1.2)});
    for (int q = 0; q <= 5; ++q) p.outline.push_back({p.w + R.range(-3, 3), p.h * q / 5});
    for (int k = 4; k >= 0; --k) p.outline.push_back({p.w * k / 4, p.h + R.range(-1.2, 1.2)});
    for (int q = 4; q >= 1; --q) p.outline.push_back({R.range(-3, 3), p.h * q / 5});
    for (int f = 0; f < 5; ++f)
      p.fibres.push_back({R.range(0, p.w * 0.3), R.range(2, p.h - 2), R.range(p.w * 0.6, p.w), R.range(2, p.h - 2), R.range(0.08, 0.2)});
    p.defaultText = R.next() < 0.45 ? kDefaults[R.pick(8)] : "";
    p.textSize = R.range(11, 14);
    p.textRotationDeg = R.range(-3, 3);
    out.push_back(std::move(p));
  }
  return out;
}

// How many of the pieces a wear level shows: none, one or two (from the seed), or all.
inline int piecesShown(WearLevel level, std::uint32_t seed) {
  if (level == WearLevel::Off) return 0;
  if (level == WearLevel::Heavy) return kTapePieceCount;
  WearRng R(seed ^ 0x51ABu);
  return 1 + (R.next() < 0.5 ? 1 : 0);
}

inline std::string displayedTapeText(const TapePiece& piece, const TapeText& saved) {
  return saved.set ? saved.text : piece.defaultText;
}

// What the user typed, limited to 14 characters (never in the middle of a UTF-8 sequence); control characters become spaces.
inline std::string clampTapeText(const std::string& typed) {
  std::string cleaned;
  for (char c : typed) cleaned.push_back(static_cast<unsigned char>(c) < 0x20 ? ' ' : c);
  std::string out;
  int chars = 0;
  for (std::size_t i = 0; i < cleaned.size();) {
    const unsigned char c = static_cast<unsigned char>(cleaned[i]);
    const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (chars == kTapeTextCharacters) break;
    out.append(cleaned, i, std::min(len, cleaned.size() - i));
    i += len;
    ++chars;
  }
  return out;
}

} // namespace FairyTapes::TheStripper::Ui
