// Fairy Tapes shared UI: sanitizing of normalized values, no VSTGUI dependency.
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

} // namespace FairyTapes::Ui
