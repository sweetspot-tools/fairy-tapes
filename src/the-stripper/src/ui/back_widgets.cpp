// The Stripper GUI: widgets of the back panel. Look from the approved preview (white outlines, grey push buttons).
#include "ui/back_widgets.h"

#include "ui/tape_text.h"

#include "fairy_ui/draw_util.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kWhite;

namespace {
constexpr CColor kInk(0x11, 0x11, 0x11);
constexpr CColor kButtonFace(0x8e, 0x8e, 0x91);
constexpr CColor kButtonEdge(0xb8, 0xb8, 0xbb);
constexpr CColor kButtonInk(0x1a, 0x1a, 0x1c);

SharedPointer<CFontDesc> bold(CCoord size) { return makeOwned<CFontDesc>("Arial", size, kBoldFace); }

void outline(CDrawContext* ctx, const CRect& r, bool filled, const CColor& line, const CColor& fill) {
  CRect inner(r);
  inner.inset(0.75, 0.75);
  if (auto path = owned(ctx->createRoundRectGraphicsPath(inner, 2))) {
    if (filled) {
      ctx->setFillColor(fill);
      ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    }
    ctx->setFrameColor(line);
    ctx->setLineWidth(1.5);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
}
}  // namespace

// ---- SegmentRow ----------------------------------------------------------------------------------------------------
SegmentRow::SegmentRow(const CRect& size, std::vector<std::string> options, int selected)
: CView(size), options_(std::move(options)), selected_(selected) {}

void SegmentRow::setSelected(int index) {
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
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  if (!enabled_) ctx->setGlobalAlpha(0.4f);
  const CRect& r = getViewSize();
  const int n = static_cast<int>(options_.size());
  if (n == 0) { ctx->restoreGlobalState(); setDirty(false); return; }
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
  const CRect& r = getViewSize();
  const int i = static_cast<int>((where.x - r.left) / (r.getWidth() / static_cast<double>(options_.size())));
  if (i >= 0 && i < static_cast<int>(options_.size()) && i != selected_) {
    selected_ = i;
    invalid();
    if (onSelect) onSelect(i);
  }
  return kMouseEventHandled;
}

// ---- FontStepper ---------------------------------------------------------------------------------------------------
namespace {
constexpr CCoord kArrow = 24;
const char* const kFontNames[] = {"Zeyada", "Rock Salt", "Homemade Apple"};
}  // namespace

FontStepper::FontStepper(const CRect& size, int selected) : CView(size), selected_(selected) {}

void FontStepper::setSelected(int index) {
  if (index == selected_) return;
  selected_ = index;
  invalid();
}

void FontStepper::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CRect& r = getViewSize();
  const CRect left(r.left, r.top, r.left + kArrow, r.bottom), right(r.right - kArrow, r.top, r.right, r.bottom);
  for (const CRect* a : {&left, &right}) {
    outline(ctx, *a, true, kButtonEdge, kButtonFace);
    ctx->setFont(bold(14));
    ctx->setFontColor(kButtonInk);
    ctx->drawString(a == &left ? "<" : ">", *a, kCenterText);
  }
  const CRect mid(r.left + kArrow + 2, r.top, r.right - kArrow - 2, r.bottom);
  outline(ctx, mid, false, kWhite, kWhite);
  const int i = selected_ < 0 ? 0 : selected_ > 2 ? 2 : selected_;
  const TapeFontId id = static_cast<TapeFontId>(i);
  // the name, written in its own font at the size of the standard 12 px text
  drawTapeText(ctx, id, kFontNames[i], CPoint(mid.getCenter().x, mid.top + 17), normalizedFontSize(id, 12.0), kWhite, 0x7ea, 0.0,
               TapeAlign::Center);
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult FontStepper::onMouseDown(CPoint& where, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  const CRect& r = getViewSize();
  if (where.x < r.left + kArrow) { if (onStep) onStep(-1); }
  else if (where.x > r.right - kArrow) { if (onStep) onStep(+1); }
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

void TextLink::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CRect& r = getViewSize();
  ctx->setFont(bold(11));
  ctx->setFontColor(active_ ? kWhite : kDimWhite);
  ctx->drawString(text_.c_str(), CRect(r.left, r.top, r.right, r.bottom - 2), kRightText);
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult TextLink::onMouseDown(CPoint&, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  if (onClick) onClick();
  return kMouseEventHandled;
}

} // namespace FairyTapes::TheStripper::Ui
