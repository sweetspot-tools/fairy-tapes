// Fairy Tapes shared UI: rules for the text written on a piece of paper tape (header-only, no VSTGUI, unit tested in
// tests/test_tape.cpp). Kept apart from tape_font.h so plug-in state code can sanitise texts without the glyph data.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace FairyTapes::Ui {

constexpr std::size_t kTapeTextMaxChars = 14;

// Printable ASCII only (0x20-0x7E), spaces trimmed at both ends, at most `maxChars` characters (trimmed again after the
// cut). What the tape fonts can draw, and what a stored state may hold.
inline std::string sanitizeTapeText(std::string_view in, std::size_t maxChars = kTapeTextMaxChars) {
  std::string out;
  out.reserve(std::min<std::size_t>(in.size(), 64));
  for (char ch : in) {
    const auto c = static_cast<unsigned char>(ch);
    if (c >= 0x20 && c < 0x7f) out.push_back(ch);
  }
  const auto trim = [](std::string& s) {
    const std::size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) {
      s.clear();
      return;
    }
    s = s.substr(a, s.find_last_not_of(' ') - a + 1);
  };
  trim(out);
  if (out.size() > maxChars) {
    out.resize(maxChars);
    trim(out);
  }
  return out;
}

// Scale in [0, 1] that brings text `textWidth` wide within `tapeWidth`: never enlarges, never exceeds the tape
// (textWidth * scale <= tapeWidth). Nothing to draw -> 1; no room or a non-finite text width -> 0.
inline double fitScale(double textWidth, double tapeWidth) {
  if (std::isnan(textWidth) || std::isnan(tapeWidth) || tapeWidth <= 0.0 || std::isinf(textWidth)) return 0.0;
  if (textWidth <= tapeWidth) return 1.0;
  return std::clamp(tapeWidth / textWidth, 0.0, 1.0);
}

} // namespace FairyTapes::Ui
