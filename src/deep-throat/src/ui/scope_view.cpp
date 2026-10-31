// Deep Throat GUI: the SCOPE display. Colours, widths and opacities from deep_throat_rack.html (the scope block);
// structure from Pillow Talk's DuckScope.
#include "ui/scope_view.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::DeepThroat::Ui {

using namespace VSTGUI;

namespace {

constexpr CColor kScopeBg(0x05, 0x05, 0x05);
constexpr CColor kScopeBorder(0x4a, 0x4a, 0x4e);
constexpr CColor kBand(0xe8, 0x39, 0x2b, 77);       // red, opacity .3
constexpr CColor kMidLine(0xc9, 0xc9, 0xc2, 89);    // dim white, opacity .35
constexpr CColor kVoice(0xf4, 0xf4, 0xf0, 230);     // white, opacity .9
constexpr CColor kRider(0xf0, 0xb4, 0x29);          // amber
constexpr CCoord kRadius = 3, kBorderWidth = 1.5, kVoiceWidth = 1.2, kRiderWidth = 1.6;
constexpr float kDimmedAlpha = 0.35f;   // DESIGN_SYSTEM.md: dimmed parts at 35 %

} // namespace

ScopeView::ScopeView(const CRect& size) : CView(size) { setMouseEnabled(false); }

Rect ScopeView::area() const {
  const CRect r = getViewSize();
  return {static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.getWidth()), static_cast<float>(r.getHeight())};
}

void ScopeView::push(const ScopePoint& p) {
  history_.push(p);
  invalid();
}

std::vector<ScopeColumn> ScopeView::columns() const { return scopeColumns(history_, area()); }

double ScopeView::columnX(int i) const {
  const CRect r = getViewSize();
  const double step = r.getWidth() / (kPoints - 1);
  const int offset = kPoints - history_.size();   // empty columns on the left until the history is full
  return r.left + (offset + i) * step;
}

void ScopeView::setDimmed(bool dimmed) {
  if (dimmed_ == dimmed) return;
  dimmed_ = dimmed;
  invalid();
}

void ScopeView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  if (dimmed_) ctx->setGlobalAlpha(ctx->getGlobalAlpha() * kDimmedAlpha);
  const CRect r = getViewSize();

  // Background and border (the border inside the view).
  CRect frame(r);
  frame.inset(kBorderWidth / 2, kBorderWidth / 2);
  if (auto bg = owned(ctx->createRoundRectGraphicsPath(frame, kRadius))) {
    ctx->setFillColor(kScopeBg);
    ctx->drawGraphicsPath(bg, CDrawContext::kPathFilled);
    ctx->setFrameColor(kScopeBorder);
    ctx->setLineWidth(kBorderWidth);
    ctx->drawGraphicsPath(bg, CDrawContext::kPathStroked);
  }

  CRect clip(r);
  clip.inset(1, 1);
  ctx->setClipRect(clip);

  const std::vector<ScopeColumn> cols = columns();
  const int n = static_cast<int>(cols.size());

  // Breath band: each run of detected columns, full height, up to the next column.
  ctx->setFillColor(kBand);
  for (int i = 0; i < n;) {
    if (!cols[static_cast<std::size_t>(i)].breath) {
      ++i;
      continue;
    }
    int e = i;
    while (e + 1 < n && cols[static_cast<std::size_t>(e + 1)].breath) ++e;
    ctx->drawRect(CRect(columnX(i), r.top, columnX(e + 1), r.bottom), kDrawFilled);
    i = e + 1;
  }

  // Rider gain 0 dB: dashed line at mid height.
  const CCoord dashes[] = {3, 3};
  ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dashes));
  ctx->setLineWidth(1);
  ctx->setFrameColor(kMidLine);
  const CCoord mid = r.top + r.getHeight() / 2;
  ctx->drawLine(CPoint(r.left, mid), CPoint(r.right, mid));
  ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinRound));

  // Voice envelope, then the Rider gain line over it.
  auto trace = [&](bool voice, const CColor& colour, CCoord width) {
    if (n < 2) return;
    auto path = owned(ctx->createGraphicsPath());
    if (!path) return;
    for (int i = 0; i < n; ++i) {
      const ScopeColumn& c = cols[static_cast<std::size_t>(i)];
      const CPoint pt(columnX(i), voice ? c.voiceTop : c.riderY);
      if (i == 0) path->beginSubpath(pt);
      else path->addLine(pt);
    }
    ctx->setFrameColor(colour);
    ctx->setLineWidth(width);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  };
  trace(true, kVoice, kVoiceWidth);
  trace(false, kRider, kRiderWidth);

  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::DeepThroat::Ui
