// Pillow Talk GUI: static editor background. Positions and colors from the approved mockup.
#include "ui/panel_view.h"

#include "fairy_ui/skin.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

namespace FairyTapes::PillowTalk::Ui {

using namespace VSTGUI;

namespace {

constexpr CColor kPanel(0x2a, 0x29, 0x26);
constexpr CColor kHairline(0x4a, 0x46, 0x3e);
constexpr CColor kInk(0xe8, 0xe2, 0xd4);
constexpr CColor kDimText(0x9a, 0x93, 0x84);

constexpr CCoord kPanelRadius = 8;
constexpr CCoord kBorderInset = 3;
constexpr CCoord kBorderRadius = 6;
constexpr CCoord kTitleSize = 18;

enum class Style { Title, Subtitle, Label };

struct PanelText {
  const char* text;
  CPoint baseline;  // left end of the text baseline
  Style style;
};

constexpr PanelText kTexts[] = {
    {"PILLOW TALK", {24, 34}, Style::Title},
    {"Fairy Tapes", {24, 49}, Style::Subtitle},
    {"TIME", {24, 350}, Style::Label},
    {"HOLD", {24, 520}, Style::Label},
};

// Section title followed by a hairline from the end of the title to kRuleRight.
struct SectionRule {
  const char* title;
  CPoint baseline;
};

constexpr SectionRule kSections[] = {
    {"DELAY", {24, 222}},
    {"AUTO DUCK", {24, 392}},
};
constexpr CCoord kRuleGap = 8;        // between title and hairline
constexpr CCoord kRuleRise = 4;       // hairline above the baseline
constexpr CCoord kRuleRight = 456;

} // namespace

PanelView::PanelView(const CRect& size, const FairyTapes::Ui::Skin* skin)
: CView(size), skin_(skin), titleFont_(makeOwned<CFontDesc>(*kNormalFont)) {
  titleFont_->setSize(kTitleSize);
  setMouseEnabled(false);
}

void PanelView::draw(CDrawContext* ctx) {
  ctx->saveGlobalState();
  ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
  const CRect& r = getViewSize();

  if (CBitmap* bmp = skin_ ? skin_->bitmap(FairyTapes::Ui::SkinAsset::kPanel) : nullptr) {
    ctx->drawBitmap(bmp, r);
  } else {
    if (auto body = owned(ctx->createRoundRectGraphicsPath(r, kPanelRadius))) {
      ctx->setFillColor(kPanel);
      ctx->drawGraphicsPath(body, CDrawContext::kPathFilled);
    }
    CRect border(r);
    border.inset(kBorderInset + 0.5, kBorderInset + 0.5);  // 1 px line on the pixel grid
    if (auto path = owned(ctx->createRoundRectGraphicsPath(border, kBorderRadius))) {
      ctx->setFrameColor(kHairline);
      ctx->setLineWidth(1);
      ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
  }

  for (const auto& t : kTexts) {
    const bool title = t.style == Style::Title;
    ctx->setFont(title ? titleFont_.get() : kNormalFontSmall);
    ctx->setFontColor(title ? kInk : kDimText);
    ctx->drawString(t.text, r.getTopLeft() + t.baseline);
  }

  ctx->setFont(kNormalFontSmall);
  ctx->setFontColor(kDimText);
  ctx->setFrameColor(kHairline);
  ctx->setLineWidth(1);
  for (const auto& s : kSections) {
    const CPoint at = r.getTopLeft() + s.baseline;
    ctx->drawString(s.title, at);
    const CCoord y = at.y - kRuleRise + 0.5;
    ctx->drawLine(CPoint(at.x + ctx->getStringWidth(s.title) + kRuleGap, y), CPoint(r.left + kRuleRight, y));
  }

  ctx->restoreGlobalState();
  setDirty(false);
}

} // namespace FairyTapes::PillowTalk::Ui
