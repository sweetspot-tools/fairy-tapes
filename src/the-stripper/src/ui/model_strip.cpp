// The Stripper GUI: the paper-tape strip with the model name. Tape colours from the approved preview.
#include "ui/model_strip.h"

#include "ui/model_source.h"
#include "ui/tape_kit.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"

#include <cmath>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;

namespace {

constexpr CColor kTape(0xd9, 0xcf, 0xa6, 0xe6);
constexpr CColor kTapeShadow(0, 0, 0, 120);
constexpr CColor kInk(0x2b, 0x26, 0x20);
constexpr CColor kErrorInk(0x9c, 0x24, 0x18);
constexpr CCoord kTextInset = 12;
constexpr double kTilt = -1.2;   // degrees, as in the preview

// Slightly irregular torn ends: fixed offsets (the seeded variation arrives with the wear layers).
constexpr CPoint kEdgeJitterLeft[] = {{1.2, 0}, {-1.5, 7}, {1.6, 14}, {-1.0, 21}, {1.4, 28}};
constexpr CPoint kEdgeJitterRight[] = {{-1.4, 0}, {1.7, 7}, {-1.1, 14}, {1.5, 21}, {-1.6, 28}};

} // namespace

ModelStrip::ModelStrip(const CRect& size) : CView(size) {}

void ModelStrip::setModel(Kind kind, std::string text) {
  if (kind_ == kind && text_ == text) return;
  kind_ = kind;
  text_ = std::move(text);
  invalid();
}

void ModelStrip::setFont(TapeFontId font) {
  if (font_ == font) return;
  font_ = font;
  invalid();
}

void ModelStrip::setDimmed(bool dimmed) {
  if (dimmed_ == dimmed) return;
  dimmed_ = dimmed;
  invalid();
}

void ModelStrip::draw(CDrawContext* ctx) {
  const CRect r = getViewSize();
  const CCoord w = r.getWidth(), h = r.getHeight();
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  if (dimmed_) ctx->setGlobalAlpha(0.35f);

  // The tape and its text are drawn tilted together around the centre.
  CGraphicsTransform tilt;
  tilt.rotate(kTilt, r.getCenter());
  CDrawContext::Transform transform(*ctx, tilt);

  if (auto tape = owned(ctx->createGraphicsPath())) {
    tape->beginSubpath(CPoint(r.left + kEdgeJitterLeft[0].x, r.top));
    tape->addLine(CPoint(r.right + kEdgeJitterRight[0].x, r.top));
    for (const CPoint& j : kEdgeJitterRight) tape->addLine(CPoint(r.right + j.x, r.top + j.y * h / 28));
    tape->addLine(CPoint(r.right, r.bottom));
    tape->addLine(CPoint(r.left, r.bottom));
    for (int i = 4; i >= 0; --i) tape->addLine(CPoint(r.left + kEdgeJitterLeft[i].x, r.top + kEdgeJitterLeft[i].y * h / 28));
    tape->closeSubpath();
    CGraphicsTransform shadowShift;
    shadowShift.translate(0.8, 1.8);
    ctx->setFillColor(kTapeShadow);
    ctx->drawGraphicsPath(tape, CDrawContext::kPathFilled, &shadowShift);
    ctx->setFillColor(kTape);
    ctx->drawGraphicsPath(tape, CDrawContext::kPathFilled);
  }

  // The writing: the tape font, sized so its digits match the standard text (x1.3), cut by its measured width.
  const double em = normalizedFontSize(font_, 13.0);
  std::string text = kind_ == Kind::NoModel ? "No model" : kind_ == Kind::Loading ? "Loading..." : text_;
  text = fitTapeText(font_, text, em, w - 2 * kTextInset);
  drawTapeText(ctx, font_, text, CPoint(r.left + kTextInset, r.top + h * 0.68), em,
               kind_ == Kind::Error ? kErrorInk : kInk, 0x5eed, 1.0);
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult ModelStrip::onMouseDown(CPoint&, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  if (onClick) onClick();
  return kMouseEventHandled;
}

} // namespace FairyTapes::TheStripper::Ui
