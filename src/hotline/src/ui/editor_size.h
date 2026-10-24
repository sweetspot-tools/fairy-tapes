// Hotline GUI: editor size (no SDK or VSTGUI dependency, unit tested through layout.h).
// The editor is laid out at a fixed logical size (the rack panel of docs/mockups/hotline_rack.html); the host's content
// scale factor (IPlugViewContentScaleSupport) zooms the frame and scales the window in pixels (fairy_ui/editor_size.h).
#pragma once

#include "fairy_ui/editor_size.h"

namespace FairyTapes::Hotline::Ui {

constexpr int kEditorWidth = 1000;   // logical px
constexpr int kEditorHeight = 470;

using FairyTapes::Ui::kMaxContentScale;
using FairyTapes::Ui::PixelSize;
using FairyTapes::Ui::sanitizeContentScale;

// Window size in pixels for a (not yet sanitized) content scale factor.
inline PixelSize scaledEditorSize(double factor) {
  return FairyTapes::Ui::scaledSize(kEditorWidth, kEditorHeight, factor);
}

} // namespace FairyTapes::Hotline::Ui
