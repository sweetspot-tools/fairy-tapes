/**
 * Fairy Tapes · Happy Ending — the texts and the LED of the LUFS screen, and the real RELEASE time of LIMIT.
 * Header-only, pure (no VSTGUI): tested in tests/test_ui.cpp. The values come from the display parameters
 * (display.h: lufsFromNorm gives -inf for "no measurement") and from tuning.h / params.h, never from copies.
 */

#pragma once

#include "params.h"
#include "tuning.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace FairyTapes::HappyEnding::Ui {

// UTF-8 of U+2212 MINUS SIGN and U+2014 EM DASH (escapes, so the source charset does not matter).
inline constexpr char kMinusSign[] = "\xE2\x88\x92";
inline constexpr char kEmDash[] = "\xE2\x80\x94";

// A target is met within this many LU.
inline constexpr double kTargetWindowLu = 0.5;

namespace detail {

// One decimal; a negative number takes the typographic minus, a value that rounds to zero prints plain "0.0";
// withPlus: a positive number is written "+0.4".
inline std::string oneDecimal(double v, bool withPlus) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f", v);
  std::string s = buf;
  if (s == "-0.0" || s == "0.0") return "0.0";
  if (s[0] == '-') return std::string(kMinusSign) + s.substr(1);
  return withPlus ? "+" + s : s;
}

// A whole number with the typographic minus ("-14" is U+2212 and "14").
inline std::string minusInt(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.0f", v);
  const std::string s = buf;
  return s[0] == '-' ? std::string(kMinusSign) + s.substr(1) : s;
}

inline double targetLufs(int targetIndex) {
  const int last = static_cast<int>(Tables::kTargets.size()) - 1;
  return Tables::kTargets[static_cast<std::size_t>(targetIndex < 0 ? 0 : (targetIndex > last ? last : targetIndex))];
}

// LIMIT RELEASE trim: index 4 is "as the style wants"; the limiter reads 2^((index - 4) / 3) around the base.
inline constexpr int kReleaseTrimCentre = 4;
inline constexpr int kReleaseTrimMax = 8;

// The release in ms of `style` (0..count-1, clamped) at trim `releaseIndex` (0..8, clamped), from the given bases.
inline std::string releaseTextFrom(const double* basesMs, int count, int style, int releaseIndex) {
  const int s = style < 0 ? 0 : (style > count - 1 ? count - 1 : style);
  const int r = releaseIndex < 0 ? 0 : (releaseIndex > kReleaseTrimMax ? kReleaseTrimMax : releaseIndex);
  const double ms = basesMs[s] * std::pow(2.0, (r - kReleaseTrimCentre) / 3.0);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.0f ms", ms);
  return buf;
}

} // namespace detail

// A loudness in LUFS with one decimal ("-14.2" with a real minus), the em dash while there is no measurement.
inline std::string lufsText(double lufs) {
  if (!std::isfinite(lufs)) return kEmDash;
  return detail::oneDecimal(lufs, false);
}

// TO TARGET: the signed gap of the short-term loudness to the chosen target, one decimal; the em dash without a measurement.
inline std::string toTargetText(double lufsShort, int targetIndex) {
  if (!std::isfinite(lufsShort)) return kEmDash;
  return detail::oneDecimal(lufsShort - detail::targetLufs(targetIndex), true);
}

// The green LED: lit within +-0.5 LU of the target (the limit included), dark without a measurement.
inline bool targetLedOn(double lufsShort, int targetIndex) {
  if (!std::isfinite(lufsShort)) return false;
  return std::fabs(lufsShort - detail::targetLufs(targetIndex)) <= kTargetWindowLu;
}

// The real RELEASE time of LIMIT under its knob, "50 ms": the base of the style (tuning.h) times the trim.
inline std::string releaseText(int style, int releaseIndex) {
  return detail::releaseTextFrom(Tuning::kLimitReleaseBaseMs,
      static_cast<int>(sizeof Tuning::kLimitReleaseBaseMs / sizeof Tuning::kLimitReleaseBaseMs[0]), style, releaseIndex);
}

} // namespace FairyTapes::HappyEnding::Ui
