// Fairy Tapes shared UI: drawing of the writable paper tape with VSTGUI — the paper body (fairy_ui/tape_paper.h) and the
// handwriting from the vector tape fonts (fairy_ui/tape_font.h). First written for The Stripper (ui/tape_text.h and
// ui/tape_piece_view.cpp); colours as in the approved previews (tape about 80 % opaque).
#pragma once

#include "fairy_ui/tape_font.h"
#include "fairy_ui/tape_paper.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"

#include <cstdint>
#include <string>

namespace FairyTapes::Ui {

constexpr VSTGUI::CColor kTapePaper(0xd9, 0xcf, 0xa6, 0xcc);
constexpr VSTGUI::CColor kTapeShadow(0, 0, 0, 128);
constexpr VSTGUI::CColor kTapeFibre(0x8f, 0x85, 0x60);
constexpr VSTGUI::CColor kTapeInk(0x2b, 0x26, 0x20, 0xf2);

enum class TapeAlign { Left, Center, Right };

// Draws `utf8` with its baseline starting at `origin` (left, centre or right of the text, per `align`), at font size
// `emPx`, with the per-letter variation of `seed` (`variation` 0 = straight). Returns the width drawn.
double drawTapeText(VSTGUI::CDrawContext* ctx, TapeFontId font, const std::string& utf8, const VSTGUI::CPoint& origin,
                    double emPx, const VSTGUI::CColor& colour, std::uint32_t seed, double variation = 1.0,
                    TapeAlign align = TapeAlign::Left);

// Draws the paper of a tape whose unrotated rectangle is `tape` (editor px), turned by `rotationDeg` around its centre:
// a soft shadow, the translucent paper with its ragged outline, the fibres. The context state is restored afterwards.
void drawTapePaper(VSTGUI::CDrawContext* ctx, const TapePaper& paper, const VSTGUI::CRect& tape, double rotationDeg);

} // namespace FairyTapes::Ui
