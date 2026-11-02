// Fairy Tapes shared UI: the four screws of a rack panel (docs/DESIGN_SYSTEM.md, usura e viti): slot or cross, a seeded
// angle, and now and then one missing (a bare hole). The user turns a screw by dragging it (right or up clockwise, like a
// knob). Header-only and free of VSTGUI so it is unit tested; FairyTapes::Ui::ScrewView (fairy_ui/screw_view.h) draws
// and turns them. The seeded look is the same as The Stripper's (same generator, same order of draws), so the same seed
// gives the same screws in every plug-in of the suite. Where the angles are stored is up to the plug-in.
#pragma once

#include "fairy_ui/wear_rng.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace FairyTapes::Ui {

constexpr int kScrewCount = 4;
constexpr double kScrewHitRadius = 11;   // a click this close to the centre grabs the screw
constexpr double kScrewDegreesPerPixel = 1.6;
constexpr double kScrewMissingChance = 0.06;

struct ScrewPoint {
  double x = 0, y = 0;
};
using ScrewPositions = std::array<ScrewPoint, kScrewCount>;

constexpr double kScrewInset = 18;   // distance of the screw centres from the panel edges

// The corners of a rack panel of any size, 18 px in (top left, top right, bottom left, bottom right).
constexpr ScrewPositions screwPositions(double panelWidth, double panelHeight) {
  return {{{kScrewInset, kScrewInset},
           {panelWidth - kScrewInset, kScrewInset},
           {kScrewInset, panelHeight - kScrewInset},
           {panelWidth - kScrewInset, panelHeight - kScrewInset}}};
}

// The corners of the 1000 x 628 rack panel (the panel of the first plug-ins).
inline constexpr ScrewPositions kRackScrewPositions = screwPositions(1000, 628);

struct ScrewSpec {
  double x = 0, y = 0;       // centre, editor px
  bool cross = false;        // false = slot
  bool missing = false;      // a bare hole: nothing to turn
  double initialAngle = 0;   // degrees, whole, 0..180
};

// From the seed (the preview's rule): each screw slot or cross by a coin toss and a whole angle 0..180; then, with 6 %
// probability, one of the four missing. The look does not depend on the positions.
inline std::array<ScrewSpec, kScrewCount> generateScrews(std::uint32_t seed,
                                                        const ScrewPositions& positions = kRackScrewPositions) {
  std::array<ScrewSpec, kScrewCount> out;
  WearRng R(seed ^ 0xA5u);
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i].x = positions[i].x;
    out[i].y = positions[i].y;
    out[i].cross = !(R.next() < 0.5);
    out[i].initialAngle = std::round(R.range(0, 180));
  }
  if (R.next() < kScrewMissingChance) out[static_cast<std::size_t>(R.next() * kScrewCount) % kScrewCount].missing = true;
  return out;
}

// 0 <= angle < 360; anything that is not finite becomes 0.
inline double normalizeScrewAngle(double degrees) {
  if (!std::isfinite(degrees)) return 0.0;
  double a = std::fmod(degrees, 360.0);
  if (a < 0.0) a += 360.0;
  return a >= 360.0 ? 0.0 : a;
}

// The angle after dragging the mouse by (dx, dy) px from where the screw was grabbed: right or up turns it clockwise,
// left or down back, along either axis (they add up).
inline double screwAngleAfterDrag(double startDegrees, double dx, double dy) {
  return normalizeScrewAngle(normalizeScrewAngle(startDegrees) + (dx - dy) * kScrewDegreesPerPixel);
}

inline bool screwHit(double screwX, double screwY, double x, double y) {
  return std::hypot(x - screwX, y - screwY) <= kScrewHitRadius;
}

}  // namespace FairyTapes::Ui
