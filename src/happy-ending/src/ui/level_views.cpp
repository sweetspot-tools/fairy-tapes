// Happy Ending GUI: the LUFS screen, the GR row tags and the ACT LEDs (ui/level_views.h).
#include "ui/level_views.h"

#include "ui/readouts.h"

#include "fairy_ui/draw_util.h"
#include "fairy_ui/sanitize.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <iterator>

namespace FairyTapes::HappyEnding::Ui {

using namespace VSTGUI;

namespace {

constexpr CColor kLedOff(0x2a, 0x2a, 0x2c);
constexpr CColor kLedBorder(0x00, 0x00, 0x00);
constexpr CColor kLedGreen(0x37, 0xd0, 0x4a);
constexpr CColor kActOff(0x33, 0x26, 0x0a);   // the preview's ACTOFF
constexpr float kDimmedAlpha = 0.35f;
constexpr CCoord kGrTagSize = 10;
static_assert(GrTags::kNumRows == static_cast<int>(std::size(kGrRows)), "one tag per GR row");

// A path, not drawEllipse (the Cairo backend of drawEllipse scales the unit circle wrongly).
void fillLed(CDrawContext* ctx, const CPoint& c, CCoord radius, const CColor& fill) {
  auto path = owned(ctx->createGraphicsPath());
  if (!path) return;
  path->addEllipse(CRect(c.x - radius, c.y - radius, c.x + radius, c.y + radius));
  ctx->setFillColor(fill);
  ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
  ctx->setLineWidth(0.6);
  ctx->setFrameColor(kLedBorder);
  ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
}

// `from` towards `to` by t (0..1), per channel.
CColor mix(const CColor& from, const CColor& to, double t) {
  auto ch = [t](uint8_t a, uint8_t b) { return static_cast<uint8_t>(a + (b - a) * t + 0.5); };
  return CColor(ch(from.red, to.red), ch(from.green, to.green), ch(from.blue, to.blue));
}

CRect toCRect(const Rect& r) { return CRect(r.x, r.y, r.x + r.w, r.y + r.h); }

// Layout coordinates (the panel's) to this view's: the view sits at `view` in its parent, the layout rect at `layout`.
CPoint shifted(const CRect& view, const Rect& layout, double x, double y) {
  return CPoint(view.left + (x - layout.x), view.top + (y - layout.y));
}

void drawRight(CDrawContext* ctx, const CPoint& baselineRight, const std::string& text) {
  ctx->drawString(text.c_str(), CPoint(baselineRight.x - ctx->getStringWidth(text.c_str()), baselineRight.y));
}

} // namespace

// ---- LufsView -------------------------------------------------------------------------------------------------------
LufsView::LufsView(const CRect& size) : CView(size), short_(kEmDash), integrated_(kEmDash), toTarget_(kEmDash) {}

void LufsView::setReadout(double lufsShort, double lufsIntegrated, int targetIndex) {
  const std::string s = lufsText(lufsShort), i = lufsText(lufsIntegrated), t = toTargetText(lufsShort, targetIndex);
  const bool led = targetLedOn(lufsShort, targetIndex);
  if (s == short_ && i == integrated_ && t == toTarget_ && led == targetLed_) return;
  short_ = s;
  integrated_ = i;
  toTarget_ = t;
  targetLed_ = led;
  invalid();
}

void LufsView::draw(CDrawContext* ctx) {
  static const SharedPointer<CFontDesc> shortFont = makeOwned<CFontDesc>("Arial", kLufsShortValue.size, kBoldFace);
  static const SharedPointer<CFontDesc> integratedFont = makeOwned<CFontDesc>("Arial", kLufsIntegratedValue.size, kBoldFace);
  static const SharedPointer<CFontDesc> toTargetFont = makeOwned<CFontDesc>("Arial", kLufsToTargetValue.size, kBoldFace);
  const CRect& r = getViewSize();
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  ctx->setFont(shortFont);
  ctx->setFontColor(FairyTapes::Ui::kAmber);
  drawRight(ctx, shifted(r, kLufsScreenRect, kLufsShortValue.x, kLufsShortValue.y), short_);
  ctx->setFont(integratedFont);
  drawRight(ctx, shifted(r, kLufsScreenRect, kLufsIntegratedValue.x, kLufsIntegratedValue.y), integrated_);
  ctx->setFont(toTargetFont);
  ctx->setFontColor(FairyTapes::Ui::kWhite);
  drawRight(ctx, shifted(r, kLufsScreenRect, kLufsToTargetValue.x, kLufsToTargetValue.y), toTarget_);
  fillLed(ctx, shifted(r, kLufsScreenRect, kTargetLed.cx, kTargetLed.cy), kTargetLed.r, targetLed_ ? kLedGreen : kLedOff);
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult LufsView::onMouseDown(CPoint& where, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  const CRect& r = getViewSize();
  const double x = where.x - r.left + kLufsScreenRect.x, y = where.y - r.top + kLufsScreenRect.y;   // layout coordinates
  if (!toCRect(kLufsResetRect).pointInside(CPoint(x, y))) return kMouseEventNotHandled;
  if (onReset) onReset();
  return kMouseEventHandled;
}

// ---- GrTags ---------------------------------------------------------------------------------------------------------
namespace {
// The column of the tags: right-aligned at kRowTagX, from the first GR row to the last.
constexpr Rect kGrTagsRect{kRowTagX - 16, kGrRows[0].cy - 8, 20, kGrRows[4].cy - kGrRows[0].cy + 16};
} // namespace

GrTags::GrTags() : CView(toCRect(kGrTagsRect)) { setMouseEnabled(false); }

void GrTags::setDimmed(int row, bool dimmed) {
  if (row < 0 || row >= kNumRows || dimmed_[static_cast<std::size_t>(row)] == dimmed) return;
  dimmed_[static_cast<std::size_t>(row)] = dimmed;
  invalid();
}

void GrTags::draw(CDrawContext* ctx) {
  static const SharedPointer<CFontDesc> font = makeOwned<CFontDesc>("Arial", kGrTagSize, kBoldFace);
  const CRect& r = getViewSize();
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  ctx->setFont(font);
  ctx->setFontColor(FairyTapes::Ui::kWhite);
  for (int i = 0; i < kNumRows; ++i) {
    ctx->setGlobalAlpha(dimmed_[static_cast<std::size_t>(i)] ? kDimmedAlpha : 1.f);
    drawRight(ctx, shifted(r, kGrTagsRect, kRowTagX, kGrRows[i].cy + kRowTagBaselineBelow), kGrRows[i].label);
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

// ---- ActLed ---------------------------------------------------------------------------------------------------------
ActLed::ActLed(const Led& at)
: CView(CRect(at.cx - at.r - 1, at.cy - at.r - 1, at.cx + at.r + 1, at.cy + at.r + 1)), led_(at) {
  setMouseEnabled(false);
}

void ActLed::setLevel(double norm) {
  const double v = FairyTapes::Ui::sanitizeNorm(norm, 0.0);
  if (v == level_) return;
  level_ = v;
  invalid();
}

void ActLed::setDimmed(bool dimmed) {
  if (dimmed_ == dimmed) return;
  dimmed_ = dimmed;
  invalid();
}

void ActLed::draw(CDrawContext* ctx) {
  const CRect& r = getViewSize();
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  if (dimmed_) ctx->setGlobalAlpha(kDimmedAlpha);
  fillLed(ctx, CPoint(r.left + r.getWidth() / 2, r.top + r.getHeight() / 2), led_.r,
          mix(kActOff, FairyTapes::Ui::kAmber, std::min(1.0, level_ * 1.4)));
  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::HappyEnding::Ui
