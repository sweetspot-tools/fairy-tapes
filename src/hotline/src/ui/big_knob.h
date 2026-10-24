// Hotline GUI: a RackKnob for caps that do not fit the shared knob cell (the Morph cap, radius 34, skirt 37).
// RackKnob draws its cap kCenterOffsetY (30) below the top of its view; this view is kBigKnobExtra taller above and puts
// the cap, the centre (center()) and the value area (valueArea()) that much lower: all three come from
// knobCentreOffsetY(capRadius) in ui/layout.h. The base methods center() and valueArea() are not virtual, so they are
// hidden here (the knob is always used through this type) and onMouseDown, which is virtual, uses the shifted area.
#pragma once

#include "ui/layout.h"

#include "fairy_ui/rack_controls.h"

namespace FairyTapes::Hotline::Ui {

class BigRackKnob : public FairyTapes::Ui::RackKnob {
public:
  BigRackKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag, double capRadius,
              const FairyTapes::Ui::Skin* skin)
  : RackKnob(size, listener, tag, capRadius, skin), radius_(capRadius) {}

  VSTGUI::CPoint center() const;                // hides AnalogKnob::center
  VSTGUI::CRect valueArea() const;              // hides RackKnob::valueArea

  void draw(VSTGUI::CDrawContext* ctx) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;

private:
  double radius_;
};

} // namespace FairyTapes::Hotline::Ui
