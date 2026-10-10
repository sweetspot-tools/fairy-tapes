// Fairy Tapes shared UI: editor window size math (no SDK or VSTGUI dependency, unit tested).
// An editor is laid out at a fixed logical size; the host's content scale factor
// (IPlugViewContentScaleSupport) zooms the frame and scales the window in pixels.
#pragma once

#include <cmath>

namespace FairyTapes::Ui {

constexpr double kMaxContentScale = 8.0;

// Non-finite or <= 0 -> 1; values above kMaxContentScale are clamped.
inline double sanitizeContentScale(double factor) {
  if (!std::isfinite(factor) || factor <= 0.0) return 1.0;
  return factor > kMaxContentScale ? kMaxContentScale : factor;
}

struct PixelSize {
  int width, height;
};

// Window size in pixels for a logical size and a (not yet sanitized) content scale factor.
inline PixelSize scaledSize(int logicalWidth, int logicalHeight, double factor) {
  const double f = sanitizeContentScale(factor);
  return {static_cast<int>(std::lround(logicalWidth * f)), static_cast<int>(std::lround(logicalHeight * f))};
}

} // namespace FairyTapes::Ui
