/**
 * Fairy Tapes · Pillow Talk — the writing on the paper tapes: font preference, text of each tape slot in the instance
 * decor, the seeded look of a tape and the ink fitted inside it. Header-only and free of VSTGUI so it is unit tested
 * (tests/test_tapes.cpp); TapeView (ui/tape_view.h) draws it with the shared tape kit (fairy_ui/tape_font.h,
 * fairy_ui/tape_paper.h, fairy_ui/tape_draw.h). A tape never written shows blank paper.
 */
#pragma once

#include "state.h"
#include "ui/layout.h"
#include "ui/prefs.h"

#include "fairy_ui/tape_font.h"
#include "fairy_ui/tape_label.h"
#include "fairy_ui/wear_rng.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace FairyTapes::PillowTalk::Ui {

constexpr double kTapeTextEdge = 6.0;   // blank paper kept at each end of the writing, px

inline FairyTapes::Ui::TapeFontId tapeFontIdOf(TapeFont f) {
  using FairyTapes::Ui::TapeFontId;
  return f == TapeFont::RockSalt ? TapeFontId::RockSalt : f == TapeFont::HomemadeApple ? TapeFontId::HomemadeApple : TapeFontId::Zeyada;
}

// The stored text of a tape slot; empty when never written or out of range.
inline std::string tapeTextFor(const Decor& d, int slot) {
  if (slot < 0 || static_cast<std::size_t>(slot) >= d.tapeTexts.size()) return {};
  return d.tapeTexts[static_cast<std::size_t>(slot)];
}

// The decor with `typed` written on `slot` (sanitised: printable ASCII, trimmed, at most 14 characters). A slot out of
// range leaves the decor as it is.
inline Decor withTapeText(Decor d, int slot, std::string_view typed) {
  if (slot < 0 || slot >= kNumTapeSlots) return d;
  if (d.tapeTexts.size() <= static_cast<std::size_t>(slot)) d.tapeTexts.resize(static_cast<std::size_t>(slot) + 1);
  d.tapeTexts[static_cast<std::size_t>(slot)] = sanitizeTapeText(typed);
  return d;
}

// The seeded look of the tape on `slot` (as in the preview: writing 11-14 px in standard-text size, tilted up to 3
// degrees on its tape). Same seed and slot, same look.
struct TapeLook {
  double textSize = 12;
  double textRotDeg = 0;
  std::uint32_t paperSeed = 0;
  std::uint32_t inkSeed = 0;
};

inline TapeLook tapeLook(std::uint32_t seed, int slot) {
  FairyTapes::Ui::WearRng R(seed ^ (0x7E11u + 0x9E3779B9u * static_cast<std::uint32_t>(slot + 1)));
  TapeLook l;
  l.textSize = R.range(11, 14);
  l.textRotDeg = R.range(-3, 3);
  l.paperSeed = static_cast<std::uint32_t>(R.next() * 4294967295.0);
  l.inkSeed = static_cast<std::uint32_t>(R.next() * 4294967295.0);
  return l;
}

struct TapeInk {
  std::string text;   // what to write (possibly cut, with an ellipsis); empty = blank paper
  double emPx = 0;
};

// The writing for a tape `tapeW` wide: nominal size from the look, shrunk (down to 60 %) and then cut so that the laid-out
// letters, with their seeded variation, stay within the tape minus kTapeTextEdge at each end.
inline TapeInk tapeInk(FairyTapes::Ui::TapeFontId font, const std::string& text, double tapeW, const TapeLook& look) {
  using namespace FairyTapes::Ui;
  const double avail = tapeW - 2 * kTapeTextEdge;
  if (text.empty() || !(avail > 0)) return {};
  const double em = normalizedFontSize(font, look.textSize);
  auto extent = [&](const FittedTapeText& f) {
    double lo = 0, hi = 0;
    for (const auto& p : layoutText(font, f.text, f.emPx, look.inkSeed, 1.0)) {
      lo = std::min(lo, p.x);
      hi = std::max(hi, p.x + p.glyph->advance / 1000.0 * f.emPx * p.scale);
    }
    return hi - lo;
  };
  double target = avail;
  for (int i = 0; i < 40; ++i, target *= 0.95) {   // the variation adds a few %: tighten until the real ink fits
    const FittedTapeText f = fitTapeLine(font, text, em, target, 0.6);
    if (f.text.empty()) return {};
    if (extent(f) <= avail) return {f.text, f.emPx};
  }
  return {};
}

} // namespace FairyTapes::PillowTalk::Ui
