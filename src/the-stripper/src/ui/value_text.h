/**
 * Fairy Tapes · The Stripper — typed values (spec 4, valori digitabili): a click on the yellow value under a knob opens a
 * text field that accepts "3", "+3.5 dB", "1.2k", "45 %"; the number is brought back into the range by inverting the
 * display of the parameter (the controller's own text: no second copy of the mappings). Header-only and free of VSTGUI so
 * it is unit tested.
 */

#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>

namespace FairyTapes::TheStripper::Ui {

namespace value_text_detail {
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Parses [sign] number [spaces] [unit] from the whole string; the unit is k (x1000), kHz, Hz, dB or %.
// A display (allowAnyUnit) may also say "-inf dB" (the bottom of the gate threshold): minus infinity, the low end.
inline std::optional<double> parse(const std::string& in, bool allowAnyUnit) {
  std::string s;
  for (char c : in) s.push_back(c == ',' ? '.' : c);
  std::size_t i = 0;
  while (i < s.size() && s[i] == ' ') ++i;
  const std::size_t start = i;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
  if (allowAnyUnit && s.compare(i, 3, "inf") == 0 && (i + 3 == s.size() || s[i + 3] == ' ')) {
    std::size_t j = i + 3;
    while (j < s.size() && s[j] == ' ') ++j;
    std::string unit;
    while (j < s.size() && s[j] != ' ') unit.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[j++]))));
    while (j < s.size() && s[j] == ' ') ++j;
    if (j != s.size() || !(unit.empty() || unit == "db")) return std::nullopt;
    return (start < s.size() && s[start] == '-') ? -INFINITY : INFINITY;
  }
  std::size_t digits = 0;
  while (i < s.size() && isDigit(s[i])) { ++i; ++digits; }
  if (i < s.size() && s[i] == '.') {
    ++i;
    while (i < s.size() && isDigit(s[i])) { ++i; ++digits; }
  }
  if (digits == 0 || i - start > 40) return std::nullopt;
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E') && i + 1 < s.size() && (isDigit(s[i + 1]) || ((s[i + 1] == '+' || s[i + 1] == '-') && i + 2 < s.size() && isDigit(s[i + 2])))) {
    ++i;
    if (s[i] == '+' || s[i] == '-') ++i;
    while (i < s.size() && isDigit(s[i])) ++i;
  }
  const double number = std::strtod(s.substr(start, i - start).c_str(), nullptr);
  while (i < s.size() && s[i] == ' ') ++i;
  std::string unit;
  while (i < s.size() && s[i] != ' ') unit.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i++]))));
  while (i < s.size() && s[i] == ' ') ++i;
  if (i != s.size()) return std::nullopt;   // something after the unit
  double scale = 1.0;
  if (unit.empty() || unit == "hz" || unit == "db" || unit == "%") scale = 1.0;
  else if (unit == "k" || unit == "khz") scale = 1000.0;
  else if (!allowAnyUnit) return std::nullopt;
  const double value = number * scale;
  if (!std::isfinite(value)) return std::nullopt;
  return value;
}
}  // namespace value_text_detail

// What the user typed, as a number in the units of the display (kHz and k mean thousands).
inline std::optional<double> parseTypedNumber(const std::string& typed) { return value_text_detail::parse(typed, false); }

// The number behind a text the controller formats ("+3.5 dB", "2.5 kHz", "45%", "0.70", "-inf dB"); nothing for text such as "Bypass".
inline std::optional<double> numberOfDisplay(const std::string& display) { return value_text_detail::parse(display, true); }

// The text the edit field starts with: the number alone (no unit), without trailing zeros; other text as it is.
inline std::string editFieldText(const std::string& display) {
  const auto n = numberOfDisplay(display);
  if (!n) return display;
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.2f", *n);
  std::string out = buf;
  if (out.find('.') != std::string::npos) {
    while (!out.empty() && out.back() == '0') out.pop_back();
    if (!out.empty() && out.back() == '.') out.pop_back();
  }
  return out == "-0" ? "0" : out;
}

// The normalised value (0..1) whose display shows `target`: bisection over the display (which must be monotonic, rising or
// falling). A target beyond the ends gives the end; a display that is not a number gives 0.5.
inline double solveNormalized(const std::function<std::string(double)>& display, double target) {
  auto number = [&](double v) -> std::optional<double> { return numberOfDisplay(display(v)); };
  const auto n0 = number(0.0), n1 = number(1.0);
  if (!n0 || !n1 || !std::isfinite(target)) return 0.5;
  const bool rising = *n1 >= *n0;
  const double lowEnd = rising ? *n0 : *n1, highEnd = rising ? *n1 : *n0;
  if (target <= lowEnd) return rising ? 0.0 : 1.0;
  if (target >= highEnd) return rising ? 1.0 : 0.0;
  double lo = 0.0, hi = 1.0;
  for (int i = 0; i < 40; ++i) {
    const double mid = 0.5 * (lo + hi);
    const auto m = number(mid);
    if (!m) return 0.5;
    const bool below = rising ? *m < target : *m > target;   // the display of mid is on the near end
    (below ? lo : hi) = mid;
  }
  // pick the side whose display is nearer to what was typed
  const auto a = number(lo), b = number(hi);
  if (!a || !b) return 0.5;
  return std::fabs(*a - target) <= std::fabs(*b - target) ? lo : hi;
}

} // namespace FairyTapes::TheStripper::Ui
