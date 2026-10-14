// The Stripper GUI: the turn of the panel (front -> back and back). A flat imitation of a 3D flip: the picture of the old
// side narrows around the vertical axis until it is edge-on, then the picture of the new side widens, with a little shade
// and perspective. About 0.8 s. The editor snapshots both sides into bitmaps, hides the real views while this one runs.
#pragma once

#include "ui/flip_pose.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cview.h"

namespace FairyTapes::TheStripper::Ui {

class FlipView : public VSTGUI::CView {
public:
  FlipView(const VSTGUI::CRect& size, VSTGUI::SharedPointer<VSTGUI::CBitmap> from, VSTGUI::SharedPointer<VSTGUI::CBitmap> to);
  void setProgress(double progress);   // 0..1
  double progress() const { return progress_; }

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&, const VSTGUI::CButtonState&) override { return VSTGUI::kMouseEventHandled; }
  CLASS_METHODS_NOCOPY(FlipView, VSTGUI::CView)

private:
  VSTGUI::SharedPointer<VSTGUI::CBitmap> from_, to_;
  double progress_ = 0;
};

} // namespace FairyTapes::TheStripper::Ui
