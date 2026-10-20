// The Stripper GUI: one screw of the panel. Drag it to turn it (right or up clockwise); a missing screw is a bare hole.
#pragma once

#include "ui/screws.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace FairyTapes::TheStripper::Ui {

class ScrewView : public VSTGUI::CView {
public:
  ScrewView(const ScrewSpec& spec, double angle);

  // A new screw (after RE-ROLL): its look and angle.
  void setScrew(const ScrewSpec& spec, double angle);
  double angle() const { return angle_; }
  const ScrewSpec& spec() const { return spec_; }
  std::function<void(double angle)> onTurned;   // while dragging and when the drag ends

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseCancel() override;
  CLASS_METHODS_NOCOPY(ScrewView, VSTGUI::CView)

private:
  ScrewSpec spec_;
  double angle_;
  bool dragging_ = false;
  VSTGUI::CPoint anchor_;
  double startAngle_ = 0;
};

} // namespace FairyTapes::TheStripper::Ui
