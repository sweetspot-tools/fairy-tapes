// Hotline GUI: where the paper tapes and the corner screws go, from the instance seed, and the seeds of the wear.
// Pure C++ (no VSTGUI; SDK headers only through params.h), deterministic for (seed, level, panel). The wear marks
// (scratches, stains, residue, specks) come from shared/ui (fairy_ui/wear.h, drawn by FairyTapes::Ui::WearView); the
// control wear (caps and faces) from fairy_ui/control_wear.h through the rack controls' setWear. Pattern copied from
// Pillow Talk's ui/decor.h (copy, not include).
#pragma once

#include "ui/layout.h"

#include "fairy_ui/wear.h"

#include <cstdint>
#include <vector>

namespace FairyTapes::Hotline::Ui {

using FairyTapes::Ui::WearLevel;

// Reference panel: 1000 x 470 units; placements scale to the panel passed to layoutFromSeed.
constexpr float kDecorDesignW = 1000.f;
constexpr float kDecorDesignH = 470.f;
static_assert(kDecorDesignW == kEditorWidth && kDecorDesignH == kEditorHeight, "decor placed on the editor panel");

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

// Pure and deterministic. Tapes: 0 (Off), 1-2 (Light), 2-3 (Heavy) as in the preview (n(1) + a coin toss), always fully
// inside `panel`, on the slots of kTapeSlots (ui/layout.h). Screws: always 4 entries at kScrewPos (scaled to `panel`),
// at most one missing; kind, angle and the missing one from FairyTapes::Ui::generateScrews (fairy_ui/screws.h), so they
// look as in the other plug-ins for the same seed. The screw layout does not depend on `w` (changing Wear never moves
// screws).
WearLayout layoutFromSeed(uint32_t seed, WearLevel w, const Rect& panel);

// The front panel's wear marks: the shared generator on the 1000 x 470 panel (positions scaled from its 628 reference).
inline std::vector<FairyTapes::Ui::WearMark> panelWear(uint32_t seed, WearLevel level) {
  return FairyTapes::Ui::generateWear(seed, level, kEditorWidth, kEditorHeight);
}

// The seed of one control's wear (cap or faces): the preview's ctrlSeed, seed ^ (index * 2654435761), with the ParamID
// as the index (as The Stripper).
constexpr uint32_t controlWearSeed(uint32_t seed, ParamID id) {
  return seed ^ (static_cast<uint32_t>(id) * 2654435761u);
}

} // namespace FairyTapes::Hotline::Ui
