// Hotline GUI: see big_knob.h.
#include "ui/big_knob.h"

#include "fairy_ui/rack_geometry.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicstransform.h"

namespace FairyTapes::Hotline::Ui {

using namespace VSTGUI;

CPoint BigRackKnob::center() const {
  const CRect& r = getViewSize();
  return CPoint(r.left + r.getWidth() / 2, r.top + knobCentreOffsetY(static_cast<float>(radius_)));
}

CRect BigRackKnob::valueArea() const {
  const CPoint c = center();
  const double baseline = FairyTapes::Ui::Rack::valueBaseline(c.y, radius_);
  return CRect(c.x - 34, baseline - 12, c.x + 34, baseline + 5);
}

void BigRackKnob::draw(CDrawContext* ctx) {
  // The base draws the cap kCenterOffsetY below the top: move it down by the rest of the offset.
  CGraphicsTransform shift;
  shift.translate(0, knobCentreOffsetY(static_cast<float>(radius_)) - AnalogKnob::kCenterOffsetY);
  CDrawContext::Transform transform(*ctx, shift);
  RackKnob::draw(ctx);
}

CMouseEventResult BigRackKnob::onMouseDown(CPoint& where, const CButtonState& buttons) {
  if (onValueClick && buttons.isLeftButton() && valueArea().pointInside(where)) {
    onValueClick();
    return kMouseDownEventHandledButDontNeedMovedOrUpEvents;
  }
  return AnalogKnob::onMouseDown(where, buttons);
}

} // namespace FairyTapes::Hotline::Ui
