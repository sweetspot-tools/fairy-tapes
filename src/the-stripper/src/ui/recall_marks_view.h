// The Stripper GUI: the recall marks, pen marks across the scale dots of the knobs that have moved from their recalled value.
#pragma once

#include "params.h"

#include "vstgui/lib/cview.h"

#include <array>
#include <cstdint>

namespace FairyTapes::TheStripper::Ui {

class RecallMarksView : public VSTGUI::CView {
public:
  RecallMarksView(const VSTGUI::CRect& size, std::uint32_t seed);

  void setSeed(std::uint32_t seed);
  void setMarksExist(bool exist);                                         // false until the project was loaded or marked
  void setValues(Steinberg::Vst::ParamID id, double current, double recalled);   // normalised, for a knob's id

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(RecallMarksView, VSTGUI::CView)

private:
  std::uint32_t seed_;
  bool exist_ = false;
  std::array<double, kNumParams> current_{}, recalled_{};
};

} // namespace FairyTapes::TheStripper::Ui
