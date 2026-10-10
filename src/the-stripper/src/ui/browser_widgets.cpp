// The Stripper GUI: small code-drawn widgets of the model popup.
#include "ui/browser_widgets.h"

#include "ui/draw_util.h"
#include "ui/model_source.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;

namespace {

constexpr CColor kButtonFace(0x8e, 0x8e, 0x91);
constexpr CColor kButtonEdge(0xb8, 0xb8, 0xbb);
constexpr CColor kButtonText(0x1a, 0x1a, 0x1c);
constexpr CColor kGreen(0x37, 0xd0, 0x4a);
constexpr CColor kRowHover(0x1d, 0x1d, 0x1f);
constexpr CColor kLine(0x58, 0x58, 0x5c);

SharedPointer<CFontDesc> font(CCoord size) {
  return makeOwned<CFontDesc>("Arial", size, kBoldFace);
}

// A path, not drawEllipse: the Cairo backend of drawEllipse scales the unit circle wrongly.
void fillDot(CDrawContext* ctx, const CPoint& c, CCoord r, const CColor& color) {
  if (auto path = owned(ctx->createGraphicsPath())) {
    path->addEllipse(CRect(c.x - r, c.y - r, c.x + r, c.y + r));
    ctx->setFillColor(color);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
  }
}

} // namespace

// ---------------------------------------------------------------------------
// PushButton

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
  if (!enabled_) ctx->setGlobalAlpha(0.4f);
  if (auto path = owned(ctx->createRoundRectGraphicsPath(CRect(r).inset(0.5, 0.5), 2))) {
    ctx->setFillColor(kButtonFace);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    ctx->setFrameColor(kButtonEdge);
    ctx->setLineWidth(1);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
  ctx->setFont(font(12));
  ctx->setFontColor(kButtonText);
  ctx->drawString(text_.c_str(), r, kCenterText);
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult PushButton::onMouseDown(CPoint&, const CButtonState& buttons) {
  if (!buttons.isLeftButton() || !enabled_) return kMouseEventNotHandled;
  if (onClick) onClick();
  return kMouseEventHandled;
}

// ---------------------------------------------------------------------------
// ListBox

ListBox::ListBox(const CRect& size) : CView(size) {}

void ListBox::setRows(std::vector<Row> rows) {
  rows_ = std::move(rows);
  if (selected_ >= static_cast<int>(rows_.size())) selected_ = -1;
  scrollTo(scroll_);
  invalid();
}

void ListBox::setSelected(int index) {
  const int n = static_cast<int>(rows_.size());
  index = (index < 0 || index >= n) ? -1 : index;
  if (index == selected_) return;
  selected_ = index;
  if (selected_ >= 0) {   // keep the selected row visible
    const double top = selected_ * kRowHeight, bottom = top + kRowHeight;
    if (top < scroll_) scrollTo(top);
    else if (bottom > scroll_ + getViewSize().getHeight()) scrollTo(bottom - getViewSize().getHeight());
  }
  invalid();
}

double ListBox::maxScroll() const {
  return std::max(0.0, rows_.size() * kRowHeight - getViewSize().getHeight());
}

void ListBox::scrollTo(double offset) {
  const double v = std::clamp(offset, 0.0, maxScroll());
  if (v == scroll_) return;
  scroll_ = v;
  invalid();
}

int ListBox::rowAt(const CPoint& where) const {
  const CRect r = getViewSize();
  const int i = static_cast<int>(std::floor((where.y - r.top + scroll_) / kRowHeight));
  return (i >= 0 && i < static_cast<int>(rows_.size())) ? i : -1;
}

void ListBox::draw(CDrawContext* ctx) {
  const CRect r = getViewSize();
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  ctx->setClipRect(r);
  ctx->setFont(font(13));
  if (rows_.empty() && !empty_.empty()) {
    ctx->setFontColor(kDimWhite);
    ctx->drawString(empty_.c_str(), CRect(r).inset(8, 6), kLeftText);
  }
  const int first = static_cast<int>(scroll_ / kRowHeight);
  for (int i = first; i < static_cast<int>(rows_.size()); ++i) {
    const double y = r.top + i * kRowHeight - scroll_;
    if (y >= r.bottom) break;
    const CRect row(r.left, y, r.right - 6, y + kRowHeight);
    const Row& e = rows_[i];
    const bool sel = i == selected_;
    if (sel) {
      if (auto path = owned(ctx->createRoundRectGraphicsPath(CRect(row).inset(0.5, 1.5), 4))) {
        ctx->setFrameColor(kAmber);
        ctx->setLineWidth(1);
        ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
      }
    }
    if (e.marked) fillDot(ctx, CPoint(row.left + 10, y + kRowHeight / 2), 3.5, kGreen);
    ctx->setFont(font(13));
    ctx->setFontColor(sel ? kAmber : kWhite);
    CCoord metaWidth = 0;
    if (!e.meta.empty()) {
      ctx->setFont(font(11));
      metaWidth = ctx->getStringWidth(e.meta.c_str());
      ctx->setFontColor(kDimWhite);
      ctx->drawString(e.meta.c_str(), CRect(row.left, y, row.right - 8, y + kRowHeight), kRightText);
      ctx->setFont(font(13));
      ctx->setFontColor(sel ? kAmber : kWhite);
    }
    const CCoord textLeft = row.left + 20;
    const CCoord room = row.right - 8 - metaWidth - 10 - textLeft;
    const std::string text = truncateToWidth(e.text, std::max<CCoord>(room, 10),
                                             [ctx](const std::string& s) { return ctx->getStringWidth(s.c_str()); });
    ctx->drawString(text.c_str(), CRect(textLeft, y, row.right, y + kRowHeight), kLeftText);
  }
  // thin scroll bar when the rows do not fit
  if (maxScroll() > 0) {
    const double h = r.getHeight(), total = rows_.size() * kRowHeight;
    const double barH = std::max(16.0, h * h / total), barY = r.top + (h - barH) * (scroll_ / maxScroll());
    ctx->setFillColor(kLine);
    ctx->drawRect(CRect(r.right - 4, barY, r.right - 1, barY + barH), kDrawFilled);
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

CMouseEventResult ListBox::onMouseDown(CPoint& where, const CButtonState& buttons) {
  if (!buttons.isLeftButton()) return kMouseEventNotHandled;
  const int i = rowAt(where);
  if (i < 0) return kMouseEventHandled;
  setSelected(i);
  if (buttons.isDoubleClick()) {
    if (onActivate) onActivate(i);
  } else if (onSelect) {
    onSelect(i);
  }
  return kMouseEventHandled;
}

void ListBox::onMouseWheelEvent(MouseWheelEvent& event) {
  if (maxScroll() <= 0) return;
  scrollTo(scroll_ - event.deltaY * kRowHeight * 3);
  event.consumed = true;
}

} // namespace FairyTapes::TheStripper::Ui
