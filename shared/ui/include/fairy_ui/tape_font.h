/**
 * Fairy Tapes shared UI — the tape fonts as vector paths. Header-only and free of VSTGUI so it is unit tested
 * (tests/test_tape_font.cpp); drawTapeText (fairy_ui/tape_draw.h) draws the layout. The glyph outlines are generated
 * (fairy_ui/tape_glyphs.h, shared/ui/tools/gen_tape_fonts.py). First written for The Stripper (src/ui/tape_font.h), without
 * its dependencies on the plug-in (preferences, model names): each plug-in maps its own font preference to TapeFontId.
 *
 * Every handwriting font draws its digits at a different size, so each font is normalised: the digit height is brought to
 * the digit height of the standard text (Arial bold, 0.716 em) at the size the standard text would have, times 1.3 for
 * legibility. Each letter gets a small variation of position, slant and size, derived from a seed, so the writing looks
 * hand made and is the same every time.
 */

#pragma once

#include "fairy_ui/tape_glyphs.h"
#include "fairy_ui/tape_label.h"
#include "fairy_ui/wear_rng.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace FairyTapes::Ui {

enum class TapeFontId { Zeyada, RockSalt, HomemadeApple };
using TapeGlyph = tape_glyphs::Glyph;
using TapeFontData = tape_glyphs::Font;

constexpr double kStandardDigitHeight = 0.716;   // digit height of the standard text font, in em
constexpr double kLegibilityFactor = 1.3;

inline const TapeFontData& tapeFontData(TapeFontId id) { return tape_glyphs::kFonts[static_cast<int>(id)]; }

inline const TapeGlyph* findGlyph(TapeFontId id, char32_t cp) {
  const TapeFontData& d = tapeFontData(id);
  for (std::size_t i = 0; i < d.glyphCount; ++i)   // 96 glyphs: a linear scan is fine
    if (d.glyphs[i].codepoint == cp) return &d.glyphs[i];
  return nullptr;
}

// UTF-8 to code points; a malformed or truncated sequence gives one replacement character (never reads past the end).
inline std::vector<char32_t> decodeUtf8(const std::string& s) {
  std::vector<char32_t> out;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    if (len == 0 || i + static_cast<std::size_t>(len) > s.size()) {
      out.push_back(U'?');
      ++i;
      continue;
    }
    char32_t cp = len == 1 ? c : static_cast<char32_t>(c & (0xFF >> (len + 1)));
    bool ok = true;
    for (int k = 1; k < len; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
      if ((cc & 0xC0) != 0x80) { ok = false; break; }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!ok) {
      out.push_back(U'?');
      ++i;
      continue;
    }
    out.push_back(cp);
    i += static_cast<std::size_t>(len);
  }
  return out;
}

// Font size (em, px) at which the digits are `digitHeightPx` tall.
inline double fontSizeForDigitHeight(TapeFontId id, double digitHeightPx) { return digitHeightPx * 1000.0 / tapeFontData(id).digitHeight; }

// Font size for text that would be `standardSizePx` in the standard font: same digit height, times 1.3.
inline double normalizedFontSize(TapeFontId id, double standardSizePx) {
  return fontSizeForDigitHeight(id, kStandardDigitHeight * standardSizePx * kLegibilityFactor);
}

inline const TapeGlyph* glyphOrFallback(TapeFontId id, char32_t cp) {
  const TapeGlyph* g = findGlyph(id, cp);
  return g ? g : findGlyph(id, U' ');   // an unknown character leaves a blank
}

inline double textWidth(TapeFontId id, const std::string& utf8, double emPx) {
  double w = 0;
  for (char32_t cp : decodeUtf8(utf8)) w += glyphOrFallback(id, cp)->advance / 1000.0 * emPx;
  return w;
}

struct PlacedGlyph {
  const TapeGlyph* glyph;
  double x;          // pen position, px from the start of the text
  double y;          // offset from the baseline, px (positive = down)
  double angleDeg;   // slant of this letter, degrees (positive = clockwise)
  double scale;      // size factor of this letter
};

// Lays the text out on a baseline. `variation` 0 = a straight line, 1 = the normal hand-made variation.
inline std::vector<PlacedGlyph> layoutText(TapeFontId id, const std::string& utf8, double emPx, std::uint32_t seed, double variation) {
  std::vector<PlacedGlyph> out;
  WearRng R(seed);
  double pen = 0;
  for (char32_t cp : decodeUtf8(utf8)) {
    const TapeGlyph* g = glyphOrFallback(id, cp);
    PlacedGlyph p{g, pen, 0.0, 0.0, 1.0};
    if (variation > 0) {
      p.y = R.range(-0.05, 0.05) * emPx * variation;
      p.angleDeg = R.range(-4.0, 4.0) * variation;
      p.scale = 1.0 + R.range(-0.06, 0.06) * variation;
      pen += R.range(-0.015, 0.015) * emPx * variation;   // a little jitter in the spacing
    }
    p.x = pen;
    out.push_back(p);
    pen += g->advance / 1000.0 * emPx;
  }
  return out;
}

// `text` cut at the end until `measure` says it fits in `maxWidth`, ending with an ellipsis (never in the middle of a
// UTF-8 sequence, no blank or separator before the ellipsis); empty if not even the ellipsis fits. From The Stripper's
// ui/model_source.h.
inline std::string truncateToWidth(const std::string& text, double maxWidth,
                                   const std::function<double(const std::string&)>& measure) {
  if (text.empty() || measure(text) <= maxWidth) return text;
  static const std::string kEllipsis = "\xE2\x80\xA6";
  std::size_t end = text.size();
  while (end > 0) {
    // step back to the start of the previous UTF-8 character
    do {
      --end;
    } while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80);
    std::string head = text.substr(0, end);
    while (!head.empty() && (head.back() == ' ' || head.back() == '_' || head.back() == '-' || head.back() == '.'))
      head.pop_back();
    const std::string candidate = head + kEllipsis;
    if (measure(candidate) <= maxWidth) return candidate;
  }
  return {};
}

// The text cut by its measured width, ending with an ellipsis (never by character count, never in the middle of a UTF-8 sequence).
inline std::string fitTapeText(TapeFontId id, const std::string& utf8, double emPx, double maxWidthPx) {
  return truncateToWidth(utf8, maxWidthPx, [&](const std::string& s) { return textWidth(id, s, emPx); });
}

struct FittedTapeText {
  std::string text;   // what to draw (possibly cut, ending with an ellipsis); empty = nothing
  double emPx = 0;    // the font size to draw it at
};

// One line of writing on a tape `maxWidth` wide: the nominal size `emPx`, shrunk to fit (fitScale, down to `minScale`),
// then cut by its measured width. The result never measures more than `maxWidth` (textWidth at the returned size).
inline FittedTapeText fitTapeLine(TapeFontId id, const std::string& utf8, double emPx, double maxWidth, double minScale = 0.7) {
  if (utf8.empty() || !std::isfinite(emPx) || emPx <= 0.0 || !std::isfinite(maxWidth) || maxWidth <= 0.0) return {};
  const double natural = textWidth(id, utf8, emPx);
  const double scale = std::max(std::clamp(minScale, 0.0, 1.0), fitScale(natural, maxWidth));
  FittedTapeText out;
  out.emPx = emPx * scale;
  out.text = fitTapeText(id, utf8, out.emPx, maxWidth);
  return out;
}

} // namespace FairyTapes::Ui
