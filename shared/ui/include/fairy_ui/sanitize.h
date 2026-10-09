// Fairy Tapes shared UI: sanitizing and snapping of normalized values, no VSTGUI dependency.
// knob_math.h assumes finite input; everything coming from the host or the user
// goes through sanitizeNorm first.
#pragma once

#include "fairy_ui/knob_math.h"

#include <limits>

namespace FairyTapes::Ui {

inline constexpr bool isFiniteValue(double v) {
  return v == v && v <= std::numeric_limits<double>::max() && v >= -std::numeric_limits<double>::max();
}

// Finite values are clamped to 0..1; NaN and +-inf become the (sanitized) fallback.
inline constexpr double sanitizeNorm(double v, double fallback = 0.0) {
  if (isFiniteValue(v)) return clamp01(v);
  return isFiniteValue(fallback) ? clamp01(fallback) : 0.0;
}

// On/off value: sanitized, then >= 0.5 -> 1, else 0.
inline constexpr double snapToggle(double v, double fallback = 0.0) {
  return sanitizeNorm(v, fallback) >= 0.5 ? 1.0 : 0.0;
}

// Nearest value on the normFromChoiceIndex grid of `count` choices (sanitized first).
inline constexpr double snapChoice(double v, int count, double fallback = 0.0) {
  return normFromChoiceIndex(choiceIndexFromNorm(sanitizeNorm(v, fallback), count), count);
}

} // namespace FairyTapes::Ui
