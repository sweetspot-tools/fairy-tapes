// The Stripper GUI: a piece of writable tape. Colours as in the approved preview (tape about 80 % opaque).
#include "ui/tape_piece_view.h"

#include "ui/tape_text.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgraphicstransform.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;

namespace {
constexpr CColor kTape(0xd9, 0xcf, 0xa6, 0xcc);
constexpr CColor kShadow(0, 0, 0, 128);
constexpr CColor kFibre(0x8f, 0x85, 0x60);
constexpr CColor kInk(0x2b, 0x26, 0x20, 0xf2);
constexpr double kPad = 6;   // room around the rotated tape for the shadow and the torn edges

CRect boundsOf(const TapePiece& p) {
  const double a = p.rotationDeg * 3.14159265358979323846 / 180.0, c = std::fabs(std::cos(a)), s = std::fabs(std::sin(a));
  const double bw = p.w * c + p.h * s, bh = p.w * s + p.h * c, cx = p.x + p.w / 2, cy = p.y + p.h / 2;
  return CRect(cx - bw / 2 - kPad, cy - bh / 2 - kPad, cx + bw / 2 + kPad, cy + bh / 2 + kPad);
}
}  // namespace

TapePieceView::TapePieceView(const TapePiece& piece, int index, TapeFontId font, std::uint32_t seed)
: CView(boundsOf(piece)), piece_(piece), index_(index), font_(font), seed_(seed), text_(piece.defaultText) {}

void TapePieceView::setText(const std::string& displayed) {
  if (text_ == displayed) return;
  text_ = displayed;
  invalid();
}

CRect TapePieceView::tapeRect() const { return CRect(piece_.x, piece_.y, piece_.x + piece_.w, piece_.y + piece_.h); }

void TapePieceView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CRect t = tapeRect();
  CGraphicsTransform tilt;
  tilt.rotate(piece_.rotationDeg, t.getCenter());
  CDrawContext::Transform transform(*ctx, tilt);

  if (auto outline = owned(ctx->createGraphicsPath())) {
    outline->beginSubpath(CPoint(t.left + piece_.outline[0].x, t.top + piece_.outline[0].y));
    for (std::size_t i = 1; i < piece_.outline.size(); ++i) outline->addLine(CPoint(t.left + piece_.outline[i].x, t.top + piece_.outline[i].y));
    outline->closeSubpath();
    CGraphicsTransform shift;
    shift.translate(0.8, 1.6);
    ctx->setFillColor(kShadow);
    ctx->drawGraphicsPath(outline, CDrawContext::kPathFilled, &shift);
    ctx->setFillColor(kTape);
    ctx->drawGraphicsPath(outline, CDrawContext::kPathFilled);
  }
  ctx->setLineWidth(0.5);
  for (const auto& f : piece_.fibres) {
    ctx->setFrameColor(CColor(kFibre.red, kFibre.green, kFibre.blue, static_cast<uint8_t>(f.alpha * 255)));
    ctx->drawLine(CPoint(t.left + f.x1, t.top + f.y1), CPoint(t.left + f.x2, t.top + f.y2));
  }

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
    drawTapeText(ctx, font_, shown, CPoint(t.getCenter().x, t.top + piece_.h * 0.68), em, kInk,
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
