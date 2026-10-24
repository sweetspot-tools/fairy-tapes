/**
 * Fairy Tapes · The Stripper — the recall marks (spec 4, segni di richiamo): where a knob was when the project was loaded (or
 * at the last "Mark now") is shown by a pen mark across the scale dots, only while the knob has moved away from it. The
 * marks belong to the notes layer (a preference can hide them). Header-only and free of VSTGUI so it is unit tested;
 * RecallMarksView draws them.
 *
 * Provisional choices, to be confirmed (spec section 11): the threshold of "moved", the look of the mark. (Alt/Ctrl + double
 * click on a knob returns it to the recalled value while the mark shows; see Editor and AnalogKnob::altDoubleClickTarget.)
 */

#pragma once

#include "params.h"
#include "ui/layout.h"
#include "fairy_ui/wear.h"

#include "fairy_ui/rack_geometry.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace FairyTapes::TheStripper::Ui {

constexpr double kRecallThreshold = 0.02;   // a knob counts as moved after 2 % of its travel

inline bool recallMarkVisible(double current, double recall, bool marksExist) {
  return marksExist && std::isfinite(current) && std::isfinite(recall) && std::fabs(current - recall) >= kRecallThreshold;
}

// Knobs get marks; buttons and selectors do not.
inline bool hasRecallMark(Steinberg::Vst::ParamID id) {
  for (const auto& c : kControls)
    if (c.id == id && c.kind == Kind::Knob && !c.sharesView) return true;
  return false;
}

// The pen stroke across the scale dots at the angle of the recalled value: from just inside the ring of dots (radius R + 9)
// to just outside it, a little wobbly like a hand drawn line. Points in editor coordinates; `seed` varies the wobble.
inline std::vector<FairyTapes::Ui::WearPt> recallMarkStroke(double cx, double cy, double capRadius, double recalled, std::uint32_t seed) {
  using FairyTapes::Ui::WearPt;
  using FairyTapes::Ui::WearRng;
  namespace Rack = FairyTapes::Ui::Rack;
  const double a = Rack::pointerDegrees(recalled) * Rack::kPi / 180.0, dx = std::cos(a), dy = -std::sin(a);
  const double nx = -dy, ny = dx;   // across the stroke
  WearRng R(seed);
  std::vector<WearPt> pts;
  const double r0 = capRadius + 3.5, r1 = capRadius + 14.5;
  constexpr int kSteps = 4;
  for (int i = 0; i <= kSteps; ++i) {
    const double r = r0 + (r1 - r0) * i / kSteps, wobble = (i == 0 || i == kSteps) ? R.range(-0.4, 0.4) : R.range(-0.9, 0.9);
    pts.push_back({cx + dx * r + nx * wobble, cy + dy * r + ny * wobble});
  }
  return pts;
}

} // namespace FairyTapes::TheStripper::Ui
