// The Stripper GUI: the BUTTONS tab of the back of the panel (what the buttons impose), a custom tab of the common BackView.
#pragma once

#include "vstgui/lib/crect.h"

namespace VSTGUI {
class CDrawContext;
}

namespace FairyTapes::TheStripper::Ui {

// Draws the box "WHAT THE BUTTONS DO" with the help entries generated from the layout table (helpEntries()) inside `area`.
void drawButtonsTab(VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& area);

} // namespace FairyTapes::TheStripper::Ui
