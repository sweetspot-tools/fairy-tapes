/**
 * Fairy Tapes · Happy Ending — the geometry of the back of the panel for the shared template (fairy_ui/back_panel.h).
 * Every plug-in has its own panel size, the back too: this one is 1280 x 890, the numbers of the BACK section of the preview
 * (docs/mockups/happy_ending_rack.html): the big title, FAIRY TAPES and the tab links on the header rows (the links
 * right-aligned at x 880 / 970 / 1060 / 1150, "<- BACK" and the front's "BACK ->" at 1246), APPEARANCE and BEHAVIOUR side by
 * side (602 wide, rows every 48 px from y 146), NOTES under them, CREDITS (the plug-in box, COMPONENTS AND LICENSES, NOTICES),
 * the plug-in's own tab (BUTTONS) and ROUTING (SIGNAL FLOW and LEGEND). Pure data (no VSTGUI), unit tested
 * (tests/test_ui.cpp: test_back_layout_values).
 */
#pragma once

#include "ui/editor_size.h"

#include "fairy_ui/back_panel.h"

namespace FairyTapes::HappyEnding::Ui {

inline constexpr FairyTapes::Ui::BackLayout kBackLayout{
    static_cast<double>(kEditorHeight), static_cast<double>(kEditorWidth),
    /* appearance, behaviour, notes */ {24, 102, 602, 412}, {654, 102, 602, 412}, {24, 532, 1232, 120},
    /* subtitle baseline, first row y, row step, rows */ 130, 146, 48, 8,
    /* notes: first baseline, step */ 570, 22,
    /* CREDITS box */ {24, 102, 1232, 100}, 136, 158, 18,
    /* COMPONENTS AND LICENSES: box, first and last baseline, row step */ {24, 220, 1232, 292}, 254, 22, 492,
    /* NOTICES */ {24, 530, 1232, 80}, 556, 18,
    /* the plug-in's own tab, SIGNAL FLOW, LEGEND (symbols y, note baseline) */ {24, 102, 1232, 700}, {24, 102, 1232, 672},
    {24, 790, 1232, 88}, 826, 860,
    /* right edge of the header line and of the back link, of the last tab link, step of the links */ 1246, 1150, 90,
    /* credit columns: name, author, license (right-aligned) */ 50, 350, 1230};

} // namespace FairyTapes::HappyEnding::Ui
