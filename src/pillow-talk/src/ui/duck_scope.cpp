// Pillow Talk GUI: scrolling ducking display. Look: the approved mockup scope.
#include "ui/duck_scope.h"

#include "fairy_ui/sanitize.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::PillowTalk::Ui {

using namespace VSTGUI;

namespace {

// Colours of the rack preview (docs/mockups/pillow_rack.html).
constexpr CColor kScopeBg(0x08, 0x08, 0x0a);
constexpr CColor kScopeBorder(0xf4, 0xf4, 0xf0, 230);   // white, opacity .9
constexpr CColor kVoice(0x8e, 0x8e, 0x93, 140);         // opacity .55
constexpr CColor kEcho(0xf0, 0xb4, 0x29, 204);          // opacity .8
constexpr CColor kEchoText(0xf0, 0xb4, 0x29);
constexpr CColor kSend(0x37, 0xd0, 0x4a);
constexpr CColor kThreshold(0xf4, 0xf4, 0xf0, 217);     // opacity .85
constexpr CColor kVoiceText(0xb9, 0xb9, 0xbe);
constexpr CColor kDimText(0xc9, 0xc9, 0xc2);

constexpr CCoord kRadius = 3;
constexpr CCoord kBaselineInset = 8;   // baseline above the bottom edge
constexpr CCoord kHeadroom = 34;       // value 1.0 reaches height - kHeadroom
constexpr CCoord kLegendX = 10;
constexpr CCoord kLegendBaseline = 16;
constexpr CCoord kLegendGap = 10;      // VOICE at +10, ECHO at +58, SEND LEVEL at +98 in the preview
constexpr CCoord kSendWidth = 1.6;

enum class Trace { Voice, Echo };

// Filled area under a trace, newest sample on the right edge.
SharedPointer<CGraphicsPath> areaPath(CDrawContext* ctx, const History& h, Trace which, const CRect& r,
                                      CCoord base, CCoord span, int points) {
  auto path = owned(ctx->createGraphicsPath());
  if (!path || h.size() == 0) return path;
  const CCoord step = r.getWidth() / (points - 1);
  const int offset = points - h.size();  // empty slots on the left until the buffer fills
  auto value = [&](int i) { return which == Trace::Voice ? h.voice(i) : h.echo(i); };
  const CCoord x0 = r.left + offset * step;
  path->beginSubpath(CPoint(x0, base));
  for (int i = 0; i < h.size(); ++i)
    path->addLine(CPoint(r.left + (offset + i) * step, base - value(i) * span));
  path->addLine(CPoint(r.right, base));
  path->closeSubpath();
  return path;
}

} // namespace

DuckScope::DuckScope(const CRect& size)
: CView(size), legendFont_(makeOwned<CFontDesc>("Arial", 10, kBoldFace)),
  thresholdFont_(makeOwned<CFontDesc>("Arial", 9, kBoldFace)) {}

void DuckScope::setThreshold(float norm) {
  const float v = static_cast<float>(FairyTapes::Ui::sanitizeNorm(norm, 0.0));
  if (v == threshold_) return;
  threshold_ = v;
  invalid();
}

void DuckScope::push(float voice, float send, float echo) {
  history_.push(voice, send, echo);
  invalid();
}

void DuckScope::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  const CRect r = getViewSize();
  ctx->setDrawMode(kAntiAliasing);

  // Background and border (inset half a pixel so the 1 px border stays inside the view).
  CRect frame(r);
  frame.inset(0.5, 0.5);
  if (auto bg = owned(ctx->createRoundRectGraphicsPath(frame, kRadius))) {
    ctx->setFillColor(kScopeBg);
    ctx->drawGraphicsPath(bg, CDrawContext::kPathFilled);
    ctx->setFrameColor(kScopeBorder);
    ctx->setLineWidth(1);
    ctx->drawGraphicsPath(bg, CDrawContext::kPathStroked);
  }

  const CCoord base = r.bottom - kBaselineInset;
  const CCoord span = r.getHeight() - kHeadroom;

  // Traces are clipped to the display; the clip is restored by restoreGlobalState.
  CRect clip(r);
  clip.inset(1, 1);
  ctx->setClipRect(clip);

  if (history_.size() > 0) {
    if (auto p = areaPath(ctx, history_, Trace::Voice, r, base, span, kPoints)) {
      ctx->setFillColor(kVoice);
      ctx->drawGraphicsPath(p, CDrawContext::kPathFilled);
    }
    if (auto p = areaPath(ctx, history_, Trace::Echo, r, base, span, kPoints)) {
      ctx->setFillColor(kEcho);
      ctx->drawGraphicsPath(p, CDrawContext::kPathFilled);
    }
    if (auto line = owned(ctx->createGraphicsPath())) {
      const CCoord step = r.getWidth() / (kPoints - 1);
      const int offset = kPoints - history_.size();
      for (int i = 0; i < history_.size(); ++i) {
        const CPoint pt(r.left + (offset + i) * step, base - history_.send(i) * span);
        if (i == 0) line->beginSubpath(pt); else line->addLine(pt);
      }
      ctx->setFrameColor(kSend);
      ctx->setLineWidth(kSendWidth);
      ctx->drawGraphicsPath(line, CDrawContext::kPathStroked);
    }
  }

  // Threshold: dashed 1 px line.
  const CCoord ty = base - threshold_ * span;
  const CCoord dashes[] = {4, 4};
  ctx->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dashes));
  ctx->setLineWidth(1);
  ctx->setFrameColor(kThreshold);
  ctx->drawLine(CPoint(r.left, ty), CPoint(r.right, ty));
  ctx->setLineStyle(kLineSolid);

  // Legend: the three traces from the left, THRESHOLD at the right.
  ctx->setFont(legendFont_);
  CCoord x = r.left + kLegendX;
  const struct { const char* text; CColor color; } legend[] = {
    {"VOICE", kVoiceText}, {"ECHO", kEchoText}, {"SEND LEVEL", kSend}};
  for (const auto& item : legend) {
    ctx->setFontColor(item.color);
    ctx->drawString(item.text, CPoint(x, r.top + kLegendBaseline));
    x += ctx->getStringWidth(item.text) + kLegendGap;
  }
  ctx->setFont(thresholdFont_);
  ctx->setFontColor(kDimText);
  const char* th = "THRESHOLD";
  ctx->drawString(th, CPoint(r.right - 8 - ctx->getStringWidth(th), r.top + kLegendBaseline));

  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::PillowTalk::Ui
