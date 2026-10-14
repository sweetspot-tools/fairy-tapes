// Fairy Tapes shared UI: drawing helpers for panels and popups (docs/DESIGN_SYSTEM.md, "Pannello"): the suite colours
// and the white box with its title in a gap of the border.
#pragma once

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/crect.h"

namespace FairyTapes::Ui {

constexpr VSTGUI::CColor kWhite(0xf4, 0xf4, 0xf0);
constexpr VSTGUI::CColor kDimWhite(0xc9, 0xc9, 0xc2);
constexpr VSTGUI::CColor kAmber(0xf0, 0xb4, 0x29);
constexpr VSTGUI::CColor kPanelBlack(0x0e, 0x0e, 0x0f);

// Rounded white box border with a gap on the top or bottom edge, and `title` centred in the gap.
void drawTitledBox(VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& box, const char* title, bool titleOnTop);

} // namespace FairyTapes::Ui
