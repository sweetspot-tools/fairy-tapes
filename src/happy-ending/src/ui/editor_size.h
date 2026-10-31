// Happy Ending GUI: editor size (no SDK or VSTGUI dependency, unit tested).
// The editor is laid out at a fixed logical size (the rack panel of docs/mockups/happy_ending_rack.html); the window is
// that size times the host's content scale factor (IPlugViewContentScaleSupport) times the SIZE the user picked on the
// back (75 / 90 / 100 / 125 %, a preference of this plug-in). The math is shared: fairy_ui/editor_size.h.
#pragma once

#include "fairy_ui/editor_size.h"

namespace FairyTapes::HappyEnding::Ui {

constexpr int kEditorWidth = 1280;   // logical px
constexpr int kEditorHeight = 890;

using FairyTapes::Ui::EditorSize;
using FairyTapes::Ui::kMaxContentScale;
using FairyTapes::Ui::PixelSize;
using FairyTapes::Ui::sanitizeContentScale;

// Window size in pixels for a (not yet sanitized) host content scale factor and the SIZE choice.
inline PixelSize scaledEditorSize(double hostFactor, EditorSize size) {
  return FairyTapes::Ui::scaledSize(kEditorWidth, kEditorHeight, hostFactor, size);
}

} // namespace FairyTapes::HappyEnding::Ui
