// Deep Throat GUI: the read-outs and the BREATH lamp (colours and sizes from deep_throat_rack.html).
#include "ui/readout_view.h"

#include "fairy_ui/draw_util.h"
#include "fairy_ui/silkscreen.h"
#include "fairy_ui/tape_draw.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::DeepThroat::Ui {

using namespace VSTGUI;

namespace {
constexpr CColor kLampOff(0x3a, 0x10, 0x10);   // preview OFFL2
constexpr CColor kLampOn(0xe8, 0x39, 0x2b);     // preview RED

constexpr bool readoutsInEnumOrder() {
  for (int i = 0; i < ReadoutView::kCount; ++i)
    if (static_cast<int>(kReadouts[i].id) != i) return false;
  return true;
}
static_assert(readoutsInEnumOrder(), "kReadouts[i].id == i: the view indexes its texts by ReadoutId");
} // namespace

ReadoutView::ReadoutView(const CRect& size, Part part) : CView(size), part_(part) {
  font10_ = makeOwned<CFontDesc>("Arial", 10, kBoldFace);
  font9_ = makeOwned<CFontDesc>("Arial", 9, kBoldFace);
  setMouseEnabled(false);
}

int ReadoutView::indexOf(ReadoutId id) {
  const int i = static_cast<int>(id);
  return i >= 0 && i < kCount ? i : 0;
}

void ReadoutView::setText(ReadoutId id, const std::string& text) {
  std::string& t = texts_[static_cast<std::size_t>(indexOf(id))];
  if (t == text) return;
  t = text;
  invalid();
}

const std::string& ReadoutView::text(ReadoutId id) const { return texts_[static_cast<std::size_t>(indexOf(id))]; }

void ReadoutView::setDimmed(ReadoutId id, bool dimmed) {
  bool& d = dimmed_[static_cast<std::size_t>(indexOf(id))];
  if (d == dimmed) return;
  d = dimmed;
  invalid();
}

bool ReadoutView::dimmed(ReadoutId id) const { return dimmed_[static_cast<std::size_t>(indexOf(id))]; }

void ReadoutView::setLampDimmed(bool dimmed) {
  if (lampDimmed_ == dimmed) return;
  lampDimmed_ = dimmed;
  invalid();
}

void ReadoutView::setLampLit(bool lit) {
  if (lampLit_ == lit) return;
  lampLit_ = lit;
  invalid();
}

void ReadoutView::setHandwriting(bool on, FairyTapes::Ui::TapeFontId font, uint32_t seed) {
  handwritten_ = on;
  font_ = font;
  seed_ = seed;
  invalid();
}

void ReadoutView::draw(CDrawContext* ctx) {
  using FairyTapes::Ui::TapeAlign;
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CPoint o = getViewSize().getTopLeft();
  const float base = ctx->getGlobalAlpha();
  for (int i = 0; i < kCount && part_ != Part::Lamp; ++i) {
    const Readout& r = kReadouts[i];
    const std::string& t = texts_[static_cast<std::size_t>(i)];
    if (t.empty()) continue;
    ctx->setGlobalAlpha(base * (dimmed_[static_cast<std::size_t>(i)] ? FairyTapes::Ui::kSilkDimmedAlpha : 1.f));
    const CColor colour = r.amber ? FairyTapes::Ui::kAmber : FairyTapes::Ui::kDimWhite;
    if (r.amber && r.handwritable && handwritten_) {
      const TapeAlign a = r.align == TextAlign::Left ? TapeAlign::Left : r.align == TextAlign::Right ? TapeAlign::Right : TapeAlign::Center;
      FairyTapes::Ui::drawTapeText(ctx, font_, t, CPoint(o.x + r.x, o.y + r.y), FairyTapes::Ui::normalizedFontSize(font_, r.size),
                                   colour, seed_ + 977u * static_cast<uint32_t>(i + 1), 0.5, a);
      continue;
    }
    ctx->setFont(r.size < 10 ? font9_ : font10_);
    ctx->setFontColor(colour);
    const CCoord w = ctx->getStringWidth(t.c_str());
    ctx->drawString(t.c_str(), CPoint(o.x + FairyTapes::Ui::alignedLeft(r.x, w, r.align), o.y + r.y));
  }
  // BREATH lamp
  if (part_ == Part::Texts) {
    ctx->restoreGlobalState();
    setDirty(false);
    return;
  }
  ctx->setGlobalAlpha(base * (lampDimmed_ ? FairyTapes::Ui::kSilkDimmedAlpha : 1.f));
  if (auto path = owned(ctx->createGraphicsPath())) {
    const Lamp& l = kBreathLamp;
    path->addEllipse(CRect(o.x + l.cx - l.r, o.y + l.cy - l.r, o.x + l.cx + l.r, o.y + l.cy + l.r));
    ctx->setFillColor(lampLit_ ? kLampOn : kLampOff);
    ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
    ctx->setFrameColor(CColor(0, 0, 0));
    ctx->setLineWidth(0.6);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
  }
  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::DeepThroat::Ui
