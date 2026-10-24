/**
 * Fairy Tapes · Happy Ending — LOUDNESS push curve and MATCH (pure functions, spec §6).
 *
 * LOUDNESS adds drive on top of the stage settings; the stage knobs themselves never move.
 * The push is spent in order: CLIP first, then the LIMIT threshold, then FINAL. MATCH moves the
 * LOUDNESS knob once, to bring the short-term loudness to the TARGET.
 */

#pragma once

#include "tuning.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::HappyEnding {

struct LoudPush {
  float clipDb;   // extra CLIP drive, dB (>= 0)
  float limitDb;  // how far the LIMIT threshold is lowered, dB (>= 0: the engine subtracts it)
  float final01;  // extra FINAL EFFECT / DRIVE, 0..1 of its range
};

inline constexpr int kLoudnessMaxIndex = 20;  // LOUDNESS has 21 values: 0..10 in 0.5 steps

// Each stage ramps linearly over its own third of the 0..10 knob and saturates at its maximum.
inline LoudPush loudPush(float loudness) {
  const double x = std::clamp(static_cast<double>(loudness), 0.0, 10.0);
  const double third = 10.0 / 3.0;
  const auto ramp = [&](int stage) { return std::clamp((x - stage * third) / third, 0.0, 1.0); };
  return {static_cast<float>(Tuning::kLoudCurve.clipDb * ramp(0)),
          static_cast<float>(-Tuning::kLoudCurve.limitDb * ramp(1)),
          static_cast<float>(Tuning::kLoudCurve.finalAmount * ramp(2))};
}

// New LOUDNESS index centring the target: one step of the knob absorbs kMatchDbPerStep of error.
// Unchanged when there is no measurement (short-term is -inf, or not a finite number).
inline int matchLoudnessIndex(int currentIndex, double shortTermLufs, double targetLufs) {
  if (!std::isfinite(shortTermLufs) || !std::isfinite(targetLufs))
    return std::clamp(currentIndex, 0, kLoudnessMaxIndex);
  const double steps = std::round((targetLufs - shortTermLufs) / Tuning::kMatchDbPerStep);
  const double idx = static_cast<double>(currentIndex) + std::clamp(steps, -100.0, 100.0);
  return std::clamp(static_cast<int>(idx), 0, kLoudnessMaxIndex);
}

} // namespace FairyTapes::HappyEnding
