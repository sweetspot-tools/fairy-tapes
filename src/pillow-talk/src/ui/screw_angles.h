/**
 * Fairy Tapes · Pillow Talk — the corner screws in the instance decor. The look (slot or cross, seeded angle, at most one
 * missing) comes from the shared generator (fairy_ui/screws.h, the same as The Stripper's for the same seed) at the
 * positions of ui/layout.h (kScrewPos); Decor::screwAngles keeps the user's rotation, added to the seeded angle, so a
 * fresh instance (rotation 0) shows the seeded screws. Header-only and free of VSTGUI so it is unit tested
 * (tests/test_screw_math.cpp); FairyTapes::Ui::ScrewView draws and turns them.
 */
#pragma once

#include "state.h"
#include "ui/layout.h"

#include "fairy_ui/screws.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace FairyTapes::PillowTalk::Ui {

static_assert(FairyTapes::Ui::kScrewCount == kNumScrews, "one stored rotation per screw");

// The four screws for a seed, at kScrewPos (editor units).
inline std::array<FairyTapes::Ui::ScrewSpec, FairyTapes::Ui::kScrewCount> screwSpecs(std::uint32_t seed) {
  FairyTapes::Ui::ScrewPositions pos;
  for (std::size_t i = 0; i < pos.size(); ++i) pos[i] = {kScrewPos[i][0], kScrewPos[i][1]};
  return FairyTapes::Ui::generateScrews(seed, pos);
}

// What the screw shows: the seeded angle plus the user's rotation (a bad rotation counts as none).
inline double shownScrewAngle(double initialDeg, float userDeg) {
  return FairyTapes::Ui::normalizeScrewAngle(initialDeg + sanitizeScrewAngle(userDeg));
}

// The user's rotation to store after a drag left the screw at `shownDeg`.
inline float userScrewAngle(double shownDeg, double initialDeg) {
  return sanitizeScrewAngle(static_cast<float>(FairyTapes::Ui::normalizeScrewAngle(shownDeg - initialDeg)));
}

// The decor with the rotation of screw `index` replaced (sanitized); an index out of range changes nothing.
inline Decor withScrewAngle(Decor d, int index, float userDeg) {
  if (index < 0 || index >= kNumScrews) return d;
  d.screwAngles[index] = sanitizeScrewAngle(userDeg);
  return d;
}

}  // namespace FairyTapes::PillowTalk::Ui
