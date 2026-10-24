// Hotline GUI: a strip of writable paper tape (look of docs/mockups/hotline_rack.html).
#include "ui/tape_view.h"

#include "fairy_ui/tape_draw.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicstransform.h"

namespace FairyTapes::Hotline::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::TapeFontId;

namespace {
constexpr double kPad = 6;   // room around the turned tape for the shadow and the torn ends

CRect boundsOf(const TapePlacement& p) {
  const auto b = FairyTapes::Ui::tapeBounds(p.cx, p.cy, p.w, p.h, p.rotDeg, kPad);
  return CRect(b.left, b.top, b.right, b.bottom);
}
} // namespace

TapeView::TapeView(const TapePlacement& placement, TapeFontId font, std::uint32_t seed)
: CView(boundsOf(placement)), placement_(placement), font_(font), look_(tapeLook(seed, placement.slot)),
  paper_(FairyTapes::Ui::makeTapePaper(placement.w, placement.h, look_.paperSeed)) {}

void TapeView::setText(const std::string& text) {
  if (text == text_) return;
  text_ = text;
  refit();
  invalid();
}

void TapeView::setFont(TapeFontId font) {
  if (font == font_) return;
  font_ = font;
  refit();
  invalid();
}

void TapeView::refit() { ink_ = tapeInk(font_, text_, placement_.w, look_); }

CRect TapeView::tapeRect() const {
  const TapePlacement& p = placement_;
  return CRect(p.cx - p.w / 2, p.cy - p.h / 2, p.cx + p.w / 2, p.cy + p.h / 2);
}

void TapeView::draw(CDrawContext* ctx) {
  const CRect t = tapeRect();
  FairyTapes::Ui::drawTapePaper(ctx, paper_, t, placement_.rotDeg);
  if (!ink_.text.empty()) {
    ctx->saveGlobalState();
    CGraphicsTransform tilt;
    tilt.rotate(placement_.rotDeg + look_.textRotDeg, t.getCenter());
    CDrawContext::Transform transform(*ctx, tilt);
    FairyTapes::Ui::drawTapeText(ctx, font_, ink_.text, CPoint(t.getCenter().x, t.top + placement_.h * 0.68), ink_.emPx,
                                 FairyTapes::Ui::kTapeInk, look_.inkSeed, 1.0, FairyTapes::Ui::TapeAlign::Center);
    ctx->restoreGlobalState();
  }
  setDirty(false);
}

CMouseEventResult TapeView::onMouseDown(CPoint&, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  if (onClick) onClick(this);
  return kMouseEventHandled;
}

} // namespace FairyTapes::Hotline::Ui
