// The Stripper GUI: the common tape kit without drawing (shared/ui: fonts, text rules, paper) under the editor's own names,
// plus the one thing that depends on this plug-in's preferences: the font setting -> font id. Free of VSTGUI (unit tested).
#pragma once

#include "preferences.h"

#include "fairy_ui/tape_font.h"
#include "fairy_ui/tape_label.h"
#include "fairy_ui/tape_paper.h"

namespace FairyTapes::TheStripper::Ui {

using FairyTapes::Ui::fitTapeLine;
using FairyTapes::Ui::fitTapeText;
using FairyTapes::Ui::normalizedFontSize;
using FairyTapes::Ui::TapeFontId;
using FairyTapes::Ui::textWidth;
using FairyTapes::Ui::truncateToWidth;

inline TapeFontId tapeFontIdOf(TapeFont f) {
  return f == TapeFont::RockSalt ? TapeFontId::RockSalt : f == TapeFont::HomemadeApple ? TapeFontId::HomemadeApple : TapeFontId::Zeyada;
}

} // namespace FairyTapes::TheStripper::Ui
