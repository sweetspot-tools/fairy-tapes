// The Stripper GUI: a small graphics-chip icon on the NAM button, shown only while the model runs on the GPU.
#pragma once

#include "vstgui/lib/cview.h"

namespace FairyTapes::TheStripper::Ui {

class GpuChipView : public VSTGUI::CView {
public:
  // `size` is the square the chip is drawn in (the centre of the NAM button face).
  explicit GpuChipView(const VSTGUI::CRect& size);

  void setDimmed(bool dimmed);

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(GpuChipView, VSTGUI::CView)

private:
  bool dimmed_ = false;
};

} // namespace FairyTapes::TheStripper::Ui
