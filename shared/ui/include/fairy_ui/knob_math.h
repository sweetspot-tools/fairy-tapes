// Fairy Tapes shared UI: pure control math, no VSTGUI dependency.
#pragma once


namespace FairyTapes::Ui {

constexpr double kDragPixelsFullRange = 200.0;
constexpr double kFineDragFactor = 0.1;

inline constexpr double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Knob arc: 225 deg (norm 0) to -45 deg (norm 1), clockwise.
inline constexpr double knobAngleFromNorm(double n) { return 225.0 - 270.0 * clamp01(n); }

// Vertical drag (dy > 0 = up): 200 px = full range, fine mode = 1/10.
inline constexpr double normFromDrag(double startNorm, double dyPixels, bool fine) {
  const double scale = fine ? kFineDragFactor : 1.0;
  return clamp01(startNorm + scale * dyPixels / kDragPixelsFullRange);
}

inline constexpr int choiceIndexFromNorm(double n, int count) {
  if (count <= 1) return 0;
  const int i = static_cast<int>(clamp01(n) * (count - 1) + 0.5);
  return i < 0 ? 0 : (i > count - 1 ? count - 1 : i);
}

inline constexpr double normFromChoiceIndex(int i, int count) {
  if (count <= 1) return 0.0;
  return clamp01(static_cast<double>(i) / (count - 1));
}

// Linear-in-dB meter mapping, clamped to 0..1.
inline constexpr double meterNormFromDb(double db, double minDb, double maxDb) {
  if (maxDb <= minDb) return 0.0;
  return clamp01((db - minDb) / (maxDb - minDb));
}

} // namespace FairyTapes::Ui
