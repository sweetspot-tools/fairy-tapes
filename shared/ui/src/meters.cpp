// Fairy Tapes shared UI: code-drawn level meters. Colours from the approved rack preview.
#include "fairy_ui/meters.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <utility>

namespace FairyTapes::Ui {

using namespace VSTGUI;

namespace {

constexpr CColor kOff(0x2a, 0x2a, 0x2c);
constexpr CColor kLedBorder(0x00, 0x00, 0x00);
constexpr CColor kWhite(0xf4, 0xf4, 0xf0);
constexpr float kDimmedAlpha = 0.35f;

CColor ledRgb(LedColor c) {
  switch (c) {
    case LedColor::Green: return CColor(0x37, 0xd0, 0x4a);
    case LedColor::Lime: return CColor(0x9f, 0xe0, 0x3a);
    case LedColor::Yellow: return CColor(0xf2, 0xc2, 0x1b);
    case LedColor::Red: return CColor(0xe8, 0x39, 0x2b);
    case LedColor::Amber: return CColor(0xf0, 0xb4, 0x29);
    case LedColor::Off: break;
  }
  return kOff;
}

// A path, not drawEllipse: the Cairo backend of drawEllipse scales the unit circle wrongly.
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

} // namespace

// ---------------------------------------------------------------------------
// MeterBar

MeterBar::MeterBar(const CRect& size, MeterKind kind, int ledCount)
    : CView(size), kind_(kind), count_(ledCount < 1 ? 1 : ledCount) {}

void MeterBar::setLevel(double norm, double dtSeconds) {
  const double v = sanitizeNorm(norm, 0.0);
  const double beforeHold = peak_.hold();
  const bool changed = v != level_;
  level_ = v;
  peak_.update(v, dtSeconds);
  if (changed || peak_.hold() != beforeHold) invalid();
}

void MeterBar::setHoldSettings(double holdSeconds, double fallDbPerSecond) {
  peak_.configure(holdSeconds, fallDbPerSecond);
  invalid();
}

void MeterBar::setDimmed(bool dimmed) {
  if (dimmed_ == dimmed) return;
  dimmed_ = dimmed;
  invalid();
}

void MeterBar::draw(CDrawContext* ctx) {
  const CRect r = getViewSize();
  const int lit = ledsLit(level_, count_);
  const int holdIndex = ledsLit(peak_.hold(), count_) - 1;     // -1 = nothing held
  const CCoord pitch = r.getWidth() / count_;
  const CCoord radius = std::max<CCoord>(1.0, std::min(pitch / 2 - 1.0, r.getHeight() / 2 - 0.5));
  const CCoord cy = r.top + r.getHeight() / 2;

  ctx->saveGlobalState();
  if (dimmed_) ctx->setGlobalAlpha(kDimmedAlpha);
  for (int i = 0; i < count_; ++i) {
    const CPoint c(r.left + pitch * (i + 0.5), cy);
    const bool on = i < lit;
    const bool held = kind_ == MeterKind::Level && peak_.holdEnabled() && i == holdIndex && !on;   // held peak above the live level
    fillLed(ctx, c, radius, (on || held) ? ledRgb(ledColor(i, count_, kind_)) : kOff);
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

// ---------------------------------------------------------------------------
// ClipLed

ClipLed::ClipLed(const CRect& size, std::string label) : CView(size), label_(std::move(label)) {}

void ClipLed::setLit(bool lit) {
  if (lit_ == lit) return;
  lit_ = lit;
  invalid();
}

void ClipLed::setDimmed(bool dimmed) {
  if (dimmed_ == dimmed) return;
  dimmed_ = dimmed;
  invalid();
}

void ClipLed::draw(CDrawContext* ctx) {
  const CRect r = getViewSize();
  constexpr CCoord kRadius = 3.4;
  ctx->saveGlobalState();
  if (dimmed_) ctx->setGlobalAlpha(kDimmedAlpha);
  fillLed(ctx, CPoint(r.left + kRadius + 1, r.top + r.getHeight() / 2), kRadius,
          lit_ ? ledRgb(LedColor::Red) : kOff);
  ctx->setFont(kNormalFontVerySmall);
  ctx->setFontColor(kWhite);
  ctx->drawString(label_.c_str(),
                  CRect(r.left + 2 * kRadius + 5, r.top, r.right, r.bottom), kLeftText);
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult ClipLed::onMouseDown(CPoint&, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  if (onClick) onClick();
  return kMouseEventHandled;
}

} // namespace FairyTapes::Ui
