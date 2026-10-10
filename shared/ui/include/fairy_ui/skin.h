// Fairy Tapes shared UI: optional bitmap skin looked up in the bundle's Resources folder.
// The asset format is provisional until the final graphics arrive (docs/UI_ASSETS.md).
#pragma once

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/vstguibase.h"

#include <map>
#include <string>

namespace FairyTapes::Ui {

// Agreed resource names. A control draws the bitmap stretched to its view rect when present.
namespace SkinAsset {
inline constexpr const char* kPanel = "panel.png";  // whole editor background
inline constexpr const char* kKnobBody = "knob_body.png";
inline constexpr const char* kButtonOn = "button_on.png";
inline constexpr const char* kButtonOff = "button_off.png";
} // namespace SkinAsset

// UI thread only. Lookups are cached (including misses), so draw() never touches the disk twice.
class Skin {
public:
  // nullptr when the file is not in the bundle's Resources folder (controls then draw vectors).
  VSTGUI::CBitmap* bitmap(const char* name) const;

private:
  mutable std::map<std::string, VSTGUI::SharedPointer<VSTGUI::CBitmap>> cache_;
};

} // namespace FairyTapes::Ui
