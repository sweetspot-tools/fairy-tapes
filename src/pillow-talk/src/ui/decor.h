// Pillow Talk GUI: where the paper tapes and the corner screws go, from the instance seed.
// Pure C++ (no VSTGUI; SDK headers only through params.h), deterministic for (seed, level, panel). The wear marks (scratches, stains, residue,
// specks) come from shared/ui (fairy_ui/wear.h, drawn by FairyTapes::Ui::WearView).
#pragma once

#include "ui/layout.h"

#include "fairy_ui/wear.h"

#include <cstdint>
#include <vector>

namespace FairyTapes::PillowTalk::Ui {

using FairyTapes::Ui::WearLevel;

// Reference panel: 1000 x 470 units (since 2026-10-23, was 628); placements scale to the panel passed to layoutFromSeed.
constexpr float kDecorDesignW = 1000.f;
constexpr float kDecorDesignH = 470.f;

// A strip of paper tape: centre, size and rotation (degrees, clockwise), in panel units. `slot`: its base position in
// kTapeSlots, which is also the index of its text in the instance decor (Decor::tapeTexts).
struct TapePlacement {
  float cx = 0.f, cy = 0.f, w = 0.f, h = 0.f, rotDeg = 0.f;
  int slot = 0;
};

// A corner screw. `angleDeg`: the seeded initial angle (whole degrees, 0..180); the user's rotation (Decor::screwAngles)
// is added on top. `missing`: only the empty hole is drawn.
struct ScrewPlacement {
  float x = 0.f, y = 0.f, angleDeg = 0.f;
  bool cross = false;
  bool missing = false;
};

struct WearLayout {
  std::vector<TapePlacement> tapes;
  std::vector<ScrewPlacement> screws;
};

// Base placement: kTapeSlots and kScrewPos in ui/layout.h (reference 1000 x 470 space); Rect comes from there too.

// Pure and deterministic. Tapes: 0 (Off), 1-2 (Light), 2-3 (Heavy), always fully inside `panel`.
// Screws: always 4 entries at the corners (scaled to `panel`), at most one missing; kind, angle and the missing one from
// FairyTapes::Ui::generateScrews (fairy_ui/screws.h), so they look as in The Stripper for the same seed.
// The screw layout does not depend on `w` (changing Wear never moves screws).
WearLayout layoutFromSeed(uint32_t seed, WearLevel w, const Rect& panel);

} // namespace FairyTapes::PillowTalk::Ui
