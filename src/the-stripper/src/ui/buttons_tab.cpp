// The Stripper GUI: the BUTTONS tab of the back. Layout from the approved preview (stripper_rack.html).
#include "ui/buttons_tab.h"

#include "ui/back_panel.h"

#include "fairy_ui/back_panel.h"
#include "fairy_ui/draw_util.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"

#include <algorithm>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::drawTitledBox;
using FairyTapes::Ui::kAmber;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kWhite;
using FairyTapes::Ui::wrapText;

namespace {
SharedPointer<CFontDesc> bold(CCoord size) { return makeOwned<CFontDesc>("Arial", size, kBoldFace); }

void text(CDrawContext* ctx, const std::string& s, double x, double baseline, double size, const CColor& colour) {
  ctx->setFont(bold(size));
  ctx->setFontColor(colour);
  ctx->drawString(s.c_str(), CPoint(x, baseline));
}
} // namespace

void drawButtonsTab(CDrawContext* ctx, const CRect& area) {
  drawTitledBox(ctx, area, "WHAT THE BUTTONS DO", true);
  double y = area.top + 40;
  const double titleX = area.left + 26, textX = area.left + 246, textWidth = 676;
  for (const HelpEntry& e : helpEntries()) {
    auto measure = [ctx](const std::string& s) { return static_cast<double>(ctx->getStringWidth(s.c_str())); };
    ctx->setFont(bold(12));
    const auto what = wrapText(e.what, textWidth, measure);
    ctx->setFont(bold(11));
    const auto imposes = wrapText(e.imposes, textWidth, measure);
    text(ctx, e.title, titleX, y, 13, kAmber);
    double ly = y;
    for (const auto& l : what) { text(ctx, l, textX, ly, 12, kWhite); ly += 14; }
    for (const auto& l : imposes) { text(ctx, l, textX, ly, 11, kDimWhite); ly += 13; }
    y = std::max(ly, y + 16) + 12;
  }
}

} // namespace FairyTapes::TheStripper::Ui
