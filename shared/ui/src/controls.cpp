// Fairy Tapes shared UI: code-drawn VSTGUI controls.
// Look: the approved Pillow Talk mockup (dark body, amber value arc, light pointer).
#include "fairy_ui/controls.h"

#include "fairy_ui/knob_math.h"
#include "fairy_ui/sanitize.h"
#include "fairy_ui/skin.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"

#include <cmath>
#include <utility>

namespace FairyTapes::Ui {

using namespace VSTGUI;

namespace {

constexpr CColor kInk(0xe8, 0xe2, 0xd4);
constexpr CColor kDimText(0x9a, 0x93, 0x84);
constexpr CColor kAmber(0xd9, 0xa4, 0x41);
constexpr CColor kHairline(0x4a, 0x46, 0x3e);
constexpr CColor kBody(0x17, 0x16, 0x14);
constexpr CColor kBorder(0x6b, 0x67, 0x5e);
constexpr CColor kOnText(0x2a, 0x1d, 0x05);

constexpr float kDimmedAlpha = 0.4f;

// Knob geometry (px, relative to the knob center), from the mockup.
constexpr CCoord kKnobCenterY = 30;
constexpr CCoord kArcRadius = 24;
constexpr CCoord kArcWidth = 3;
constexpr CCoord kBodyRadius = 17;
constexpr CCoord kPointerInner = 5;
constexpr CCoord kPointerOuter = 15;
constexpr CCoord kPointerWidth = 2;
constexpr CCoord kLabelCenterY = 38;  // mockup baselines cy+42 / cy+54, 11 px text
constexpr CCoord kValueCenterY = 50;
constexpr CCoord kTextHeight = 14;
constexpr double kMinVisibleArc = 0.005;

// Button geometry.
constexpr CCoord kButtonRadius = 4;
constexpr CCoord kChoiceGap = 6;

// VSTGUI arcs: 0 deg = 3 o'clock, angles grow clockwise (y down).
// Knob angle 225 deg (counter-clockwise, y up) is 135 deg here; the sweep is 270 deg.
constexpr double kArcStartDeg = 135.0;
constexpr double kArcSweepDeg = 270.0;

CRect circleRect(const CPoint& c, CCoord r) { return CRect(c.x - r, c.y - r, c.x + r, c.y + r); }

void strokeArc(CDrawContext* ctx, const CPoint& c, double fromDeg, double toDeg, const CColor& color) {
  auto path = owned(ctx->createGraphicsPath());
  if (!path) return;
  path->addArc(circleRect(c, kArcRadius), fromDeg, toDeg, true);
  ctx->setFrameColor(color);
  ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
}

// Flat button: body or amber fill, border, centered label. A skin bitmap replaces fill and border.
void drawButton(CDrawContext* ctx, const CRect& r, const std::string& label, bool on, const Skin* skin) {
  CBitmap* bmp = skin ? skin->bitmap(on ? SkinAsset::kButtonOn : SkinAsset::kButtonOff) : nullptr;
  if (bmp) {
    ctx->drawBitmap(bmp, r);
  } else {
    // Inset by half a pixel so the 1 px border stays inside the view.
    CRect inner(r);
    inner.inset(0.5, 0.5);
    if (auto path = owned(ctx->createRoundRectGraphicsPath(inner, kButtonRadius))) {
      ctx->setFillColor(on ? kAmber : kBody);
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
      ctx->setFrameColor(kBorder);
      ctx->setLineWidth(1);
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
  }
  ctx->setFont(kNormalFontSmall);
  ctx->setFontColor(on ? kOnText : kInk);
  ctx->drawString(label.c_str(), r, kCenterText);
}

void beginDrawing(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
}

} // namespace

// ---------------------------------------------------------------------------
// NormalizedControl

NormalizedControl::NormalizedControl(const CRect& size, IControlListener* listener, int32_t tag,
                                     const Skin* skin)
    : CControl(size, listener, tag), skin_(skin) {
  setMin(0.f);
  setMax(1.f);
}

void NormalizedControl::setValue(float val) {
  CControl::setValue(static_cast<float>(quantize(val, getDefaultValue())));
}

void NormalizedControl::setDefaultValue(float val) {
  CControl::setDefaultValue(static_cast<float>(quantize(val, 0.0)));
}

double NormalizedControl::quantize(double v, double fallback) const { return sanitizeNorm(v, fallback); }

void NormalizedControl::editTo(float newValue) {
  beginEdit();
  setValue(newValue);
  if (isDirty()) {
    valueChanged();
    invalid();
  }
  endEdit();
}

// ---------------------------------------------------------------------------
// AnalogKnob

AnalogKnob::AnalogKnob(const CRect& size, IControlListener* listener, int32_t tag, std::string label,
                       const Skin* skin)
    : NormalizedControl(size, listener, tag, skin), label_(std::move(label)) {}

void AnalogKnob::setLabel(std::string label) {
  label_ = std::move(label);
  invalid();
}

void AnalogKnob::setValueFormatter(ValueFormatter formatter) {
  formatter_ = std::move(formatter);
  valueTextFor_ = -1.f;
  invalid();
}

void AnalogKnob::setDimmed(bool dimmed) {
  if (dimmed_ == dimmed) return;
  dimmed_ = dimmed;
  invalid();
}

CPoint AnalogKnob::center() const {
  const CRect& r = getViewSize();
  return CPoint(r.left + r.getWidth() / 2, r.top + kKnobCenterY);
}

const std::string& AnalogKnob::valueText() {
  const float v = getValue();
  if (v != valueTextFor_) {
    valueText_ = formatter_ ? formatter_(v) : std::string();
    valueTextFor_ = v;
  }
  return valueText_;
}

void AnalogKnob::draw(CDrawContext* ctx) {
  beginDrawing(ctx);
  if (dimmed_) ctx->setGlobalAlpha(ctx->getGlobalAlpha() * kDimmedAlpha);

  const double v = sanitizeNorm(getValue(), getDefaultValue());
  const CPoint c = center();

  ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapRound));
  ctx->setLineWidth(kArcWidth);
  strokeArc(ctx, c, kArcStartDeg, kArcStartDeg + kArcSweepDeg, kHairline);
  if (v >= kMinVisibleArc) strokeArc(ctx, c, kArcStartDeg, kArcStartDeg + kArcSweepDeg * v, kAmber);

  const CRect body = circleRect(c, kBodyRadius);
  if (CBitmap* bmp = skin_ ? skin_->bitmap(SkinAsset::kKnobBody) : nullptr) {
    ctx->drawBitmap(bmp, body);
  } else if (auto path = owned(ctx->createGraphicsPath())) {
    // A path, not drawEllipse: the Cairo backend of drawEllipse scales the unit circle wrongly.
    path->addEllipse(body);
    ctx->setFillColor(kBody);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    ctx->setLineWidth(1);
    ctx->setFrameColor(kBorder);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }

  const double rad = knobAngleFromNorm(v) * Constants::pi / 180.0;
  const CCoord dx = std::cos(rad);
  const CCoord dy = -std::sin(rad);
  ctx->setLineWidth(kPointerWidth);
  ctx->setFrameColor(kInk);
  ctx->drawLine(CPoint(c.x + kPointerInner * dx, c.y + kPointerInner * dy),
                CPoint(c.x + kPointerOuter * dx, c.y + kPointerOuter * dy));

  const CRect& r = getViewSize();
  ctx->setFont(kNormalFontSmall);
  ctx->setFontColor(kInk);
  ctx->drawString(label_.c_str(),
                  CRect(r.left, c.y + kLabelCenterY - kTextHeight / 2, r.right, c.y + kLabelCenterY + kTextHeight / 2),
                  kCenterText);
  ctx->setFontColor(kDimText);
  ctx->drawString(valueText().c_str(),
                  CRect(r.left, c.y + kValueCenterY - kTextHeight / 2, r.right, c.y + kValueCenterY + kTextHeight / 2),
                  kCenterText);

  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult AnalogKnob::onMouseDown(CPoint& where, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  if (buttons.isDoubleClick()) {
    if (dragging_) {  // a double click may arrive while the first click's gesture is still open
      dragging_ = false;
      endEdit();
    }
    editTo(getDefaultValue());
    return kMouseDownEventHandledButDontNeedMovedOrUpEvents;
  }
  beginEdit();
  dragging_ = true;
  dragFine_ = (buttons.getModifierState() & kShift) != 0;
  dragAnchorY_ = where.y;
  dragAnchorValue_ = sanitizeNorm(getValue(), getDefaultValue());
  gestureStartValue_ = static_cast<float>(dragAnchorValue_);
  return kMouseEventHandled;
}

CMouseEventResult AnalogKnob::onMouseMoved(CPoint& where, const CButtonState& buttons) {
  if (!dragging_) return kMouseEventNotHandled;
  const bool fine = (buttons.getModifierState() & kShift) != 0;
  if (fine != dragFine_) {  // re-anchor so switching precision does not jump
    dragFine_ = fine;
    dragAnchorY_ = where.y;
    dragAnchorValue_ = getValue();
  }
  const double dyUp = dragAnchorY_ - where.y;
  setValue(static_cast<float>(normFromDrag(dragAnchorValue_, dyUp, dragFine_)));
  if (isDirty()) {
    valueChanged();
    invalid();
  }
  return kMouseEventHandled;
}

CMouseEventResult AnalogKnob::onMouseUp(CPoint&, const CButtonState&) {
  if (!dragging_) return kMouseEventNotHandled;
  dragging_ = false;
  endEdit();
  return kMouseEventHandled;
}

bool AnalogKnob::removed(CView* parent) {
  if (dragging_) {
    dragging_ = false;
    endEdit();
  }
  return NormalizedControl::removed(parent);
}

CMouseEventResult AnalogKnob::onMouseCancel() {
  if (!dragging_) return kMouseEventNotHandled;
  dragging_ = false;
  setValue(gestureStartValue_);
  if (isDirty()) {
    valueChanged();
    invalid();
  }
  endEdit();
  return kMouseEventHandled;
}

// ---------------------------------------------------------------------------
// ToggleButton

ToggleButton::ToggleButton(const CRect& size, IControlListener* listener, int32_t tag, std::string label,
                           const Skin* skin)
    : NormalizedControl(size, listener, tag, skin), label_(std::move(label)) {
  // CControl's default is 0.5, which VSTGUI's built-in Ctrl/Cmd-click reset would send.
  setDefaultValue(0.f);
  setValue(0.f);
}

double ToggleButton::quantize(double v, double fallback) const { return snapToggle(v, fallback); }

void ToggleButton::draw(CDrawContext* ctx) {
  beginDrawing(ctx);
  drawButton(ctx, getViewSize(), label_, getValue() >= 0.5f, skin_);
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult ToggleButton::onMouseDown(CPoint&, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  editTo(getValue() >= 0.5f ? 0.f : 1.f);
  return kMouseDownEventHandledButDontNeedMovedOrUpEvents;
}

// ---------------------------------------------------------------------------
// ChoiceButtons

ChoiceButtons::ChoiceButtons(const CRect& size, IControlListener* listener, int32_t tag,
                             std::vector<std::string> labels, const Skin* skin)
    : NormalizedControl(size, listener, tag, skin), labels_(std::move(labels)) {
  setDefaultValue(0.f);  // see ToggleButton
  setValue(0.f);
}

double ChoiceButtons::quantize(double v, double fallback) const { return snapChoice(v, count(), fallback); }

int ChoiceButtons::selectedIndex() const {
  return choiceIndexFromNorm(sanitizeNorm(getValue(), getDefaultValue()), count());
}

CRect ChoiceButtons::buttonRect(int index) const {
  const CRect& r = getViewSize();
  const int n = count();
  const CCoord w = n > 0 ? (r.getWidth() - kChoiceGap * (n - 1)) / n : r.getWidth();
  const CCoord left = r.left + index * (w + kChoiceGap);
  return CRect(left, r.top, left + w, r.bottom);
}

void ChoiceButtons::draw(CDrawContext* ctx) {
  beginDrawing(ctx);
  const int selected = selectedIndex();
  for (int i = 0; i < count(); ++i)
    drawButton(ctx, buttonRect(i), labels_[static_cast<size_t>(i)], i == selected, skin_);
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult ChoiceButtons::onMouseDown(CPoint& where, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  for (int i = 0; i < count(); ++i) {
    if (buttonRect(i).pointInside(where)) {
      if (i != selectedIndex()) editTo(static_cast<float>(normFromChoiceIndex(i, count())));
      break;
    }
  }
  return kMouseDownEventHandledButDontNeedMovedOrUpEvents;
}

} // namespace FairyTapes::Ui
