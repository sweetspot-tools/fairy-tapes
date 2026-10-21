// Fairy Tapes shared UI: widgets of the back panel. Look from the approved previews (white outlines, grey push buttons).
#include "fairy_ui/back_widgets.h"

#include "fairy_ui/back_panel.h"
#include "fairy_ui/draw_util.h"
#include "fairy_ui/tape_draw.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::Ui {

using namespace VSTGUI;

namespace {
constexpr CColor kInk(0x11, 0x11, 0x11);
constexpr CColor kButtonFace(0x8e, 0x8e, 0x91);
constexpr CColor kButtonEdge(0xb8, 0xb8, 0xbb);
constexpr CColor kButtonInk(0x1a, 0x1a, 0x1c);
constexpr float kDisabledAlpha = 0.4f;

SharedPointer<CFontDesc> bold(CCoord size) { return makeOwned<CFontDesc>("Arial", size, kBoldFace); }

void outline(CDrawContext* ctx, const CRect& r, bool filled, const CColor& line, const CColor& fill, CCoord lineWidth = 1.5) {
  CRect inner(r);
  inner.inset(lineWidth / 2, lineWidth / 2);
  if (auto path = owned(ctx->createRoundRectGraphicsPath(inner, 2))) {
    if (filled) {
      ctx->setFillColor(fill);
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    }
    ctx->setFrameColor(line);
    ctx->setLineWidth(lineWidth);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
}

BackBox toBox(const CRect& r) { return {r.left, r.top, r.getWidth(), r.getHeight()}; }
}  // namespace

// ---- SegmentRow ----------------------------------------------------------------------------------------------------
SegmentRow::SegmentRow(const CRect& size, std::vector<std::string> options, int selected)
: CView(size), options_(std::move(options)), selected_(clampIndex(selected, static_cast<int>(options_.size()))) {}

void SegmentRow::setSelected(int index) {
  index = clampIndex(index, static_cast<int>(options_.size()));
  if (index == selected_) return;
  selected_ = index;
  invalid();
}

void SegmentRow::setEnabled(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  invalid();
}

void SegmentRow::draw(CDrawContext* ctx) {
  const int n = static_cast<int>(options_.size());
  if (n == 0) {
    setDirty(false);
    return;
  }
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  if (!enabled_) ctx->setGlobalAlpha(ctx->getGlobalAlpha() * kDisabledAlpha);
  const CRect& r = getViewSize();
  const CCoord w = r.getWidth() / n;
  ctx->setFont(bold(11));
  for (int i = 0; i < n; ++i) {
    const CRect cell(r.left + i * w, r.top, r.left + (i + 1) * w - 3, r.bottom);
    const bool on = i == selected_;
    outline(ctx, cell, on, kWhite, kWhite);
    ctx->setFontColor(on ? kInk : kWhite);
    ctx->drawString(options_[static_cast<std::size_t>(i)].c_str(), cell, kCenterText);
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult SegmentRow::onMouseDown(CPoint& where, const CButtonState& buttons) {
  if (!buttons.isLeftButton() || !enabled_ || options_.empty()) return kMouseEventNotHandled;
  const int i = segmentAt(toBox(getViewSize()), static_cast<int>(options_.size()), where.x);
  if (i >= 0 && i != selected_) {
    selected_ = i;
    invalid();
    if (onSelect) onSelect(i);
  }
  return kMouseEventHandled;
}

// ---- OptionStepper -------------------------------------------------------------------------------------------------
OptionStepper::OptionStepper(const CRect& size, std::vector<std::string> options, int selected, bool tapeFonts)
: CView(size), options_(std::move(options)), selected_(clampIndex(selected, static_cast<int>(options_.size()))), tapeFonts_(tapeFonts) {}

void OptionStepper::setSelected(int index) {
  index = clampIndex(index, static_cast<int>(options_.size()));
  if (index == selected_) return;
  selected_ = index;
  invalid();
}

void OptionStepper::setEnabled(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  invalid();
}

void OptionStepper::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  if (!enabled_) ctx->setGlobalAlpha(ctx->getGlobalAlpha() * kDisabledAlpha);
  const CRect& r = getViewSize();
  const CRect left(r.left, r.top, r.left + Back::kArrowWidth, r.bottom), right(r.right - Back::kArrowWidth, r.top, r.right, r.bottom);
  for (const CRect* a : {&left, &right}) {
    outline(ctx, *a, true, kButtonEdge, kButtonFace, 1.0);
    ctx->setFont(bold(14));
    ctx->setFontColor(kButtonInk);
    ctx->drawString(a == &left ? "<" : ">", *a, kCenterText);
  }
  const CRect mid(r.left + Back::kArrowWidth + Back::kArrowGap, r.top, r.right - Back::kArrowWidth - Back::kArrowGap, r.bottom);
  outline(ctx, mid, false, kWhite, kWhite);
  if (!options_.empty()) {
    const std::string& name = options_[static_cast<std::size_t>(selected_)];
    if (tapeFonts_) {
      // the name, written in its own font at the size of the standard 12 px text
      const auto id = static_cast<TapeFontId>(clampIndex(selected_, 3));
      drawTapeText(ctx, id, name, CPoint(mid.getCenter().x, mid.top + 17), normalizedFontSize(id, 12.0), kWhite, 0x7ea, 0.0,
                   TapeAlign::Center);
    } else {
      ctx->setFont(bold(12));
      ctx->setFontColor(kWhite);
      ctx->drawString(name.c_str(), mid, kCenterText);
    }
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult OptionStepper::onMouseDown(CPoint& where, const CButtonState& buttons) {
  if (!buttons.isLeftButton() || !enabled_) return kMouseEventNotHandled;
  const int d = stepperDirectionAt(toBox(getViewSize()), where.x);
  if (d != 0 && onStep) onStep(d);
  return kMouseEventHandled;
}

// ---- TextLink ------------------------------------------------------------------------------------------------------
TextLink::TextLink(const CRect& size, std::string text) : CView(size), text_(std::move(text)) {}

void TextLink::setActive(bool active) {
  if (active_ == active) return;
  active_ = active;
  invalid();
}

void TextLink::setText(std::string text) {
  if (text_ == text) return;
  text_ = std::move(text);
  invalid();
}

void TextLink::setLeftAligned(bool left) {
  if (left_ == left) return;
  left_ = left;
  invalid();
}

void TextLink::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CRect& r = getViewSize();
  ctx->setFont(bold(11));
  ctx->setFontColor(active_ ? kWhite : kDimWhite);
  const CCoord w = ctx->getStringWidth(text_.c_str());
  ctx->drawString(text_.c_str(), CPoint(left_ ? r.left + 2 : r.right - 2 - w, r.bottom - 6));
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult TextLink::onMouseDown(CPoint&, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  if (onClick) onClick();
  return kMouseEventHandled;
}

// ---- PushButton ----------------------------------------------------------------------------------------------------
PushButton::PushButton(const CRect& size, std::string text) : CView(size), text_(std::move(text)) {}

void PushButton::setText(std::string text) {
  if (text_ == text) return;
  text_ = std::move(text);
  invalid();
}

void PushButton::setEnabled(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  invalid();
}

void PushButton::draw(CDrawContext* ctx) {
  const CRect r = getViewSize();
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  if (!enabled_) ctx->setGlobalAlpha(ctx->getGlobalAlpha() * kDisabledAlpha);
  outline(ctx, r, true, kButtonEdge, kButtonFace, 1.0);
  ctx->setFont(bold(11));
  ctx->setFontColor(kButtonInk);
  ctx->drawString(text_.c_str(), r, kCenterText);
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult PushButton::onMouseDown(CPoint&, const CButtonState& buttons) {
  if (!buttons.isLeftButton() || !enabled_) return kMouseEventNotHandled;
  if (onClick) onClick();
  return kMouseEventHandled;
}

} // namespace FairyTapes::Ui
