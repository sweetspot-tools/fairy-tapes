/**
 * Fairy Tapes · Deep Throat — the text of the 12 knob values, shared by the controller (what the host shows,
 * getParamStringByValue / getParamValueByString) and the editor (the yellow value under each knob and the typed value):
 * one formatter, so the panel and the host always print the same string. The formats are the preview's
 * (docs/mockups/deep_throat_rack.html): dB with one decimal and "+" when above zero ("0.0 dB", "+12.0 dB"), Rider Target
 * in dBFS, Breath Amount 0..12 dB, Comp Amount as its threshold -6..-30 dB, De-ess Freq in kHz with one decimal, Tone
 * -100..+100 % and the plain percentages.
 * Parsing takes [spaces][sign]number[spaces][unit][spaces] (a comma works as the decimal point, case does not matter):
 * the knob's own unit or none; "%" on a knob that is not a percentage means that much of its travel (so "50 %" is the
 * middle of any knob); De-ess Freq takes kHz, k or Hz (no unit: kHz up to 100, Hz above). Anything else is refused.
 * Pure (params.h only): unit tested in tests/test_ui.cpp.
 */

#pragma once

#include "params.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace FairyTapes::DeepThroat {

constexpr bool isKnobParam(Steinberg::Vst::ParamID id) {
  switch (id) {
  case kInput: case kOutput: case kRiderTarget: case kRiderRange: case kBreathAmount: case kBreathSens:
  case kEqAmount: case kDeEssAmount: case kDeEssFreq: case kCompAmount: case kCompMix: case kTone:
    return true;
  default:
    return false;
  }
}

namespace value_format_detail {

constexpr double kBreathMaxDb = 12.0;              // Breath Amount: reduction 0..12 dB
constexpr double kCompThresholdTopDb = -6.0;       // Comp Amount 0 -> -6 dB threshold
constexpr double kCompThresholdSpanDb = 24.0;      // ... 1 -> -30 dB
constexpr double kDeEssLowKHz = 4.0, kDeEssRatio = 10.0 / 4.0;

inline double sane(double n) { return std::isfinite(n) ? std::clamp(n, 0.0, 1.0) : 0.0; }

// One decimal (printf rounding), never "-0.0"; "+" before a value above zero when `plus`.
inline std::string fixed1(double v, bool plus) {
  char b[32];
  std::snprintf(b, sizeof b, "%.1f", v);
  std::string s = b;
  if (s == "-0.0") s = "0.0";
  if (plus && s != "0.0" && s[0] != '-') s = "+" + s;
  return s;
}

// Math.round of the preview: nearest integer, halves up.
inline long long jsRound(double v) { return static_cast<long long>(std::floor(v + 0.5)); }

inline std::string percent(double p, bool plus) {
  const long long r = jsRound(p);
  return (plus && r > 0 ? "+" : "") + std::to_string(r) + " %";
}

// [spaces][sign]digits[.digits][e[sign]digits][spaces][unit][spaces], the whole text. The unit comes back lower-case.
inline bool split(const std::string& in, double& number, std::string& unit) {
  std::string s;
  for (char c : in) s.push_back(c == ',' ? '.' : c);
  std::size_t i = 0;
  auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
  auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
  while (i < s.size() && isSpace(s[i])) ++i;
  const std::size_t start = i;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
  std::size_t digits = 0;
  while (i < s.size() && isDigit(s[i])) { ++i; ++digits; }
  if (i < s.size() && s[i] == '.') {
    ++i;
    while (i < s.size() && isDigit(s[i])) { ++i; ++digits; }
  }
  if (digits == 0 || i - start > 40) return false;
  if (i + 1 < s.size() && (s[i] == 'e' || s[i] == 'E') &&
      (isDigit(s[i + 1]) || ((s[i + 1] == '+' || s[i + 1] == '-') && i + 2 < s.size() && isDigit(s[i + 2])))) {
    i += 2;
    while (i < s.size() && isDigit(s[i])) ++i;
  }
  number = std::strtod(s.substr(start, i - start).c_str(), nullptr);
  if (!std::isfinite(number)) return false;
  while (i < s.size() && isSpace(s[i])) ++i;
  unit.clear();
  while (i < s.size() && !isSpace(s[i])) unit.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i++]))));
  while (i < s.size() && isSpace(s[i])) ++i;
  return i == s.size();
}

} // namespace value_format_detail

// The value of a knob as the panel prints it and the host shows it; empty for a parameter that is not a knob.
inline std::string formatKnobValue(Steinberg::Vst::ParamID id, double norm) {
  namespace d = value_format_detail;
  const double n = d::sane(norm);
  switch (id) {
  case kInput: return d::fixed1(inputDbFromNorm(n), true) + " dB";
  case kOutput: return d::fixed1(outputDbFromNorm(n), true) + " dB";
  case kRiderTarget: return d::fixed1(riderTargetDbFromNorm(n), false) + " dBFS";
  case kBreathAmount: return d::fixed1(d::kBreathMaxDb * n, false) + " dB";
  case kCompAmount: return d::fixed1(d::kCompThresholdTopDb - d::kCompThresholdSpanDb * n, false) + " dB";
  case kDeEssFreq: return d::fixed1(deEssFreqHzFromNorm(n) / 1000.0, false) + " kHz";
  case kTone: return d::percent((n - 0.5) * 200.0, true);
  case kRiderRange: case kBreathSens: case kEqAmount: case kDeEssAmount: case kCompMix:
    return d::percent(n * 100.0, false);
  default: return {};
  }
}

// A typed or host text back to the knob's normalized value (clamped to 0..1); false for a text it does not understand
// or a parameter that is not a knob (then `norm` is left alone).
inline bool parseKnobValue(Steinberg::Vst::ParamID id, const std::string& text, double& norm) {
  namespace d = value_format_detail;
  if (!isKnobParam(id)) return false;
  double x = 0.0;
  std::string unit;
  if (!d::split(text, x, unit)) return false;
  double n = 0.0;
  const bool travel = unit == "%";   // on a knob whose own unit is not %: that much of the travel
  switch (id) {
  case kInput:
  case kOutput:
    if (travel) n = x / 100.0;
    else if (unit.empty() || unit == "db") n = (x + 12.0) / 24.0;
    else return false;
    break;
  case kRiderTarget:
    if (travel) n = x / 100.0;
    else if (unit.empty() || unit == "db" || unit == "dbfs") n = (x + 30.0) / 20.0;
    else return false;
    break;
  case kBreathAmount:
    if (travel) n = x / 100.0;
    else if (unit.empty() || unit == "db") n = x / d::kBreathMaxDb;
    else return false;
    break;
  case kCompAmount:
    if (travel) n = x / 100.0;
    else if (unit.empty() || unit == "db") n = (d::kCompThresholdTopDb - x) / d::kCompThresholdSpanDb;
    else return false;
    break;
  case kDeEssFreq: {
    double khz = 0.0;
    if (travel) { n = x / 100.0; break; }
    if (unit == "khz" || unit == "k") khz = x;
    else if (unit == "hz") khz = x / 1000.0;
    else if (unit.empty()) khz = x > 100.0 ? x / 1000.0 : x;
    else return false;
    if (khz <= 0.0) return false;
    n = std::log(khz / d::kDeEssLowKHz) / std::log(d::kDeEssRatio);
    break;
  }
  case kTone:   // the one knob whose % is its own signed scale (-100..+100), not a share of the travel
    if (!unit.empty() && unit != "%") return false;
    n = x / 200.0 + 0.5;
    break;
  default:   // the plain percentages
    if (!unit.empty() && unit != "%") return false;
    n = x / 100.0;
    break;
  }
  if (!std::isfinite(n)) return false;
  norm = std::clamp(n, 0.0, 1.0);
  return true;
}

} // namespace FairyTapes::DeepThroat
