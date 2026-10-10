// Pillow Talk GUI: editor size math (no SDK or VSTGUI dependency, unit tested).
// The editor is laid out at a fixed logical size; the host's content scale factor
// (IPlugViewContentScaleSupport) zooms the frame and scales the window in pixels.
#pragma once

#include <cmath>

namespace FairyTapes::PillowTalk::Ui {

constexpr int kEditorWidth = 480;   // logical px
constexpr int kEditorHeight = 560;
constexpr double kMaxContentScale = 8.0;

// Non-finite or <= 0 -> 1; values above kMaxContentScale are clamped.
inline double sanitizeContentScale(double factor) {
  if (!std::isfinite(factor) || factor <= 0.0) return 1.0;
  return factor > kMaxContentScale ? kMaxContentScale : factor;
}

struct PixelSize {
  int width, height;
};

// Window size in pixels for a (sanitized) content scale factor.
inline PixelSize scaledEditorSize(double factor) {
  const double f = sanitizeContentScale(factor);
  return {static_cast<int>(std::lround(kEditorWidth * f)), static_cast<int>(std::lround(kEditorHeight * f))};
}

} // namespace FairyTapes::PillowTalk::Ui
