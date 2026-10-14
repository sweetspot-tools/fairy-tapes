// The Stripper GUI: what the shared silkscreen layer (fairy_ui/silkscreen_view.h) prints for this plug-in, taken from the
// layout table. Pure (no VSTGUI): unit tested.
#pragma once

#include "ui/layout.h"

#include "fairy_ui/rack_geometry.h"
#include "fairy_ui/silkscreen.h"

#include <vector>

namespace FairyTapes::TheStripper::Ui {

// Where the tag of a block is printed (baseline, left end), in editor coordinates; (0, 0) for blocks without a tag.
inline FairyTapes::Ui::BlockTag tagSlot(Module m) {
  const int key = static_cast<int>(m);
  switch (m) {
  case Module::Filters: return {key, 196, 114};
  case Module::Eq: return {key, 40, 302};
  case Module::Comp: return {key, 40, 474};
  case Module::Drive: return {key, 418, 474};
  case Module::Limiter: return {key, 724, 474};
  case Module::Gate: return {key, 862, 474};
  default: return {key, 0, 0};
  }
}

inline std::vector<FairyTapes::Ui::PrintedItem> silkscreenItems() {
  namespace Rack = FairyTapes::Ui::Rack;
  using FairyTapes::Ui::PrintedItem;
  using FairyTapes::Ui::PrintKind;
  std::vector<PrintedItem> out;
  for (const ControlSpec& c : kControls) {
    if (c.sharesView) continue;   // the Drive knob prints for both of its parameters
    PrintedItem it;
    it.id = static_cast<int>(c.id);
    it.cx = c.rect.x + c.rect.w / 2;
    if (c.kind == Kind::Knob) {
      const KnobLook* look = knobLookOf(c.id);
      it.kind = PrintKind::Knob;
      it.cy = c.rect.y + 30;   // cap centre (AnalogKnob::kCenterOffsetY)
      it.capRadius = look ? look->radius : Rack::kDefaultCapRadius;
      if (look) {
        it.lo = look->lo;
        it.hi = look->hi;
      }
      if (c.label) it.name = c.label;
    } else if (c.kind == Kind::Toggle) {
      it.kind = PrintKind::Button;
      it.cy = c.rect.y + Rack::kFaceHeight / 2;
      if (c.label && !isBellButton(c.id)) it.name = c.label;
    } else {
      it.kind = PrintKind::Choice;
      it.cy = c.rect.y + Rack::kFaceHeight / 2;
      it.width = c.rect.w;
      for (int k = 0; k < 3 && c.choices[k]; ++k) it.labels.push_back(c.choices[k]);
    }
    out.push_back(std::move(it));
  }
  return out;
}

inline std::vector<FairyTapes::Ui::BlockTag> silkscreenTags() {
  std::vector<FairyTapes::Ui::BlockTag> out;
  for (const Module m : {Module::Filters, Module::Eq, Module::Comp, Module::Drive, Module::Limiter, Module::Gate})
    out.push_back(tagSlot(m));
  return out;
}

} // namespace FairyTapes::TheStripper::Ui
