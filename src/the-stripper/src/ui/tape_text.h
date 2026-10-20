// The Stripper GUI: draws handwriting from the vector tape fonts (ui/tape_font.h) with VSTGUI paths.
#pragma once

#include "ui/tape_font.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cpoint.h"

#include <string>

namespace FairyTapes::TheStripper::Ui {

enum class TapeAlign { Left, Center, Right };

// Draws `utf8` with its baseline starting at `origin` (left, centre or right of the text, per `align`), at font size
// `emPx`, with the per-letter variation of `seed` (`variation` 0 = straight). Returns the width drawn.
double drawTapeText(VSTGUI::CDrawContext* ctx, TapeFontId font, const std::string& utf8, const VSTGUI::CPoint& origin,
                    double emPx, const VSTGUI::CColor& colour, std::uint32_t seed, double variation = 1.0,
                    TapeAlign align = TapeAlign::Left);

} // namespace FairyTapes::TheStripper::Ui
