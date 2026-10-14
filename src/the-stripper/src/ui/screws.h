/**
 * Fairy Tapes · The Stripper — the four screws of the panel (spec 4, viti): slot or cross, a random angle, and now and then
 * one missing (a bare hole). Seeded; the user can turn a screw by dragging it (right or up clockwise, like a knob) and the
 * angles are saved with the project. Header-only and free of VSTGUI so it is unit tested; ScrewView draws and turns them.
 */

#pragma once

#include "fairy_ui/wear.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace FairyTapes::TheStripper::Ui {

using FairyTapes::Ui::WearRng;

constexpr int kScrewCount = 4;
constexpr double kScrewHitRadius = 11;       // a click this close to the centre grabs the screw
constexpr double kScrewDegreesPerPixel = 1.6;

struct ScrewSpec {
  double x = 0, y = 0;           // centre, editor px
  bool cross = false;            // false = slot
  bool missing = false;          // a bare hole: nothing to turn
  double initialAngle = 0;       // degrees, 0..180
};

// From the seed (the preview's rule): each screw slot or cross by a coin toss, an angle 0..180 and, with 6 % probability,
// one of the four missing.
inline std::array<ScrewSpec, kScrewCount> generateScrews(std::uint32_t seed) {
  static const double kPos[kScrewCount][2] = {{18, 18}, {982, 18}, {18, 610}, {982, 610}};
  std::array<ScrewSpec, kScrewCount> out;
  WearRng R(seed ^ 0xA5u);
  for (int i = 0; i < kScrewCount; ++i) {
    out[i].x = kPos[i][0];
    out[i].y = kPos[i][1];
    out[i].cross = !(R.next() < 0.5);
    out[i].initialAngle = std::round(R.range(0, 180));
  }
  if (R.next() < 0.06) out[static_cast<std::size_t>(R.next() * kScrewCount) % kScrewCount].missing = true;
  return out;
}

// 0 <= angle < 360; anything that is not finite becomes 0.
inline double normalizeScrewAngle(double degrees) {
  if (!std::isfinite(degrees)) return 0.0;
  double a = std::fmod(degrees, 360.0);
  if (a < 0.0) a += 360.0;
  return a >= 360.0 ? 0.0 : a;
}

// The angle after dragging the mouse by (dx, dy) px from where the screw was grabbed: right or up turns it clockwise, left
// or down back, along either axis (they add up).
inline double screwAngleAfterDrag(double startDegrees, double dx, double dy) {
  return normalizeScrewAngle(normalizeScrewAngle(startDegrees) + (dx - dy) * kScrewDegreesPerPixel);
}

inline bool screwHit(double screwX, double screwY, double x, double y) {
  return std::hypot(x - screwX, y - screwY) <= kScrewHitRadius;
}

} // namespace FairyTapes::TheStripper::Ui
