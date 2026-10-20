// Fairy Tapes shared UI: the pose of the turning panel for a progress 0..1 (pure math, no VSTGUI: unit tested in
// tests/test_flip.cpp). First written for The Stripper (src/the-stripper/src/ui/flip_pose.h); same numbers.
#pragma once

#include <algorithm>
#include <cmath>

namespace FairyTapes::Ui {

constexpr double kFlipSeconds = 0.8;

// Horizontal scale of the visible picture and which side shows, for progress 0..1.
struct FlipPose {
  double widthScale;    // 1 = face on, 0 = edge-on
  double heightScale;   // a little smaller toward the edge (perspective)
  double shade;         // 0..1, how dark the picture is (0 = face on)
  bool showsNewSide;
};
inline FlipPose flipPose(double progress) {
  const double p = (progress == progress) ? std::clamp(progress, 0.0, 1.0) : 0.0;
  // ease in and out so the turn starts and ends gently
  const double e = p * p * (3.0 - 2.0 * p);
  const double c = std::fabs(std::cos(3.14159265358979323846 * e));   // 1 -> 0 -> 1
  return {c, 1.0 - 0.06 * (1.0 - c), 0.55 * (1.0 - c), e >= 0.5};
}

// Progress 0..1 after `elapsedSeconds` of a turn lasting `durationSeconds` (a duration of 0 or less ends at once).
inline double flipProgress(double elapsedSeconds, double durationSeconds = kFlipSeconds) {
  if (!(durationSeconds > 0.0)) return 1.0;
  if (!(elapsedSeconds == elapsedSeconds)) return 0.0;
  return std::clamp(elapsedSeconds / durationSeconds, 0.0, 1.0);
}
inline bool flipDone(double progress) { return progress >= 1.0; }

} // namespace FairyTapes::Ui
