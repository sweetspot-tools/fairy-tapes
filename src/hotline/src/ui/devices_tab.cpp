// Hotline GUI: draws the DEVICES tab of the back (the cards of ui/devices_tab.h) as the custom tab of the shared BackView.
#include "ui/devices_tab.h"

#include "fairy_ui/draw_util.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"

#include <cctype>

namespace FairyTapes::Hotline::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::kDimWhite;
using FairyTapes::Ui::kWhite;

namespace {

// "HIGH PASS 400 Hz" -> the block name ("HIGH PASS", the leading words in capitals) and the rest ("400 Hz").
std::size_t nameLength(const std::string& line) {
  std::size_t end = 0, pos = 0;
  while (pos < line.size()) {
    std::size_t next = line.find(' ', pos);
    if (next == std::string::npos) next = line.size();
    bool caps = next > pos;
    for (std::size_t i = pos; i < next; ++i)
      if (!std::isupper(static_cast<unsigned char>(line[i]))) caps = false;
    if (!caps) break;
    end = next;
    pos = next + 1;
  }
  return end;
}

}  // namespace

void drawDevicesTab(CDrawContext* ctx, const CRect& area) {
  // The cards are laid out in the coordinates of the back; `area` is the custom box already offset by the view origin.
  const FairyTapes::Ui::BackBox custom = FairyTapes::Ui::Back::kShort.custom;
  const double dx = area.left - custom.x, dy = area.top - custom.y;
  auto title = makeOwned<CFontDesc>("Arial", kTitleFontSize, kBoldFace);
  auto font = makeOwned<CFontDesc>("Arial", kLineFontSize, kBoldFace);
  for (const DeviceCard& c : devicesLayout(custom)) {
    const CRect box(c.box.x + dx, c.box.y + dy, c.box.x + c.box.w + dx, c.box.y + c.box.h + dy);
    FairyTapes::Ui::drawTitledBox(ctx, box, c.title.c_str(), true);
    ctx->setFont(font);
    for (std::size_t i = 0; i < c.lines.size(); ++i) {
      const std::string& line = c.lines[i];
      const std::size_t n = nameLength(line);
      const double x = box.left + kLineInset, y = c.firstBaseline + dy + c.step * static_cast<double>(i);
      const std::string name = line.substr(0, n), rest = n < line.size() ? line.substr(n) : std::string();
      ctx->setFontColor(kWhite);
      ctx->drawString(name.c_str(), CPoint(x, y));
      ctx->setFontColor(kDimWhite);
      ctx->drawString(rest.c_str(), CPoint(x + ctx->getStringWidth(name.c_str()), y));
    }
  }
}

}  // namespace FairyTapes::Hotline::Ui
