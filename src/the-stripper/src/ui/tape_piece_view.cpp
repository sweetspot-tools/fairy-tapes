// The Stripper GUI: a piece of writable tape. Colours as in the approved preview (tape about 80 % opaque).
#include "ui/tape_piece_view.h"

#include "ui/tape_kit.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgraphicstransform.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;

namespace {
constexpr double kPad = 6;   // room around the rotated tape for the shadow and the torn edges

CRect boundsOf(const TapePiece& p) {
  const auto b = FairyTapes::Ui::tapeBounds(p.x + p.w / 2, p.y + p.h / 2, p.w, p.h, p.rotationDeg, kPad);
  return CRect(b.left, b.top, b.right, b.bottom);
}
}  // namespace

TapePieceView::TapePieceView(const TapePiece& piece, int index, TapeFontId font, std::uint32_t seed)
: CView(boundsOf(piece)), piece_(piece), index_(index), font_(font), seed_(seed), text_(piece.defaultText) {}

void TapePieceView::setText(const std::string& displayed) {
  if (text_ == displayed) return;
  text_ = displayed;
  invalid();
}

void TapePieceView::setFont(TapeFontId font) {
  if (font_ == font) return;
  font_ = font;
  invalid();
}

CRect TapePieceView::tapeRect() const { return CRect(piece_.x, piece_.y, piece_.x + piece_.w, piece_.y + piece_.h); }

void TapePieceView::draw(CDrawContext* ctx) {
  const CRect t = tapeRect();
  drawTapePaper(ctx, piece_.paper, t, piece_.rotationDeg);   // shadow, paper and fibres (shared/ui); it turns the paper itself

  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  CGraphicsTransform tilt;
  tilt.rotate(piece_.rotationDeg, t.getCenter());
  CDrawContext::Transform transform(*ctx, tilt);

  if (!text_.empty()) {
    // The writing: nominal size, shrunk to fit the tape (down to 70 %), then cut by its measured width.
    const double maxWidth = piece_.w - 12;
    double em = normalizedFontSize(font_, piece_.textSize);
    const double natural = textWidth(font_, text_, em);
    if (natural > maxWidth) em *= std::max(0.7, maxWidth / natural);
    const std::string shown = fitTapeText(font_, text_, em, maxWidth);
    CGraphicsTransform slant;
    slant.rotate(piece_.textRotationDeg, t.getCenter());
    CDrawContext::Transform textTransform(*ctx, slant);
    drawTapeText(ctx, font_, shown, CPoint(t.getCenter().x, t.top + piece_.h * 0.68), em, FairyTapes::Ui::kTapeInk,
                 seed_ + static_cast<std::uint32_t>(index_) * 977u, 1.0, TapeAlign::Center);
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult TapePieceView::onMouseDown(CPoint&, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  if (onClick) onClick(this);
  return kMouseEventHandled;
}

} // namespace FairyTapes::TheStripper::Ui
