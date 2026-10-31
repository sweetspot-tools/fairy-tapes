// Deep Throat GUI: one strip of writable paper tape on the panel (a TapePlacement of ui/decor.h). Draws the paper and the
// handwriting with the shared tape kit (fairy_ui/tape_draw.h); a click asks the editor to open the text field (Pillow
// Talk's TapeView). Sits above the wear and below the screws and the controls, never over a control (kTapeSlots in
// ui/layout.h, tested in tests/test_ui.cpp). The view's box is the one the decor model keeps inside the panel: the turned
// tape plus kTapeRaggedX + 1 = 4 px for the torn ends and the shadow.
#pragma once

#include "ui/decor.h"
#include "ui/tapes.h"

#include "fairy_ui/tape_paper.h"

#include "vstgui/lib/cview.h"

#include <cstdint>
#include <functional>
#include <string>

namespace FairyTapes::DeepThroat::Ui {

class TapeView : public VSTGUI::CView {
public:
  // Room around the turned tape (ui/decor.cpp clamps with the same pad).
  static constexpr double kPad = FairyTapes::Ui::kTapeRaggedX + 1.0;

  TapeView(const TapePlacement& placement, FairyTapes::Ui::TapeFontId font, std::uint32_t seed);

  void setText(const std::string& text);
  void setFont(FairyTapes::Ui::TapeFontId font);
  const std::string& text() const { return text_; }
  int slot() const { return placement_.slot; }
  // The unrotated tape in the parent's coordinates: where the text field goes.
  VSTGUI::CRect tapeRect() const;
  std::function<void(TapeView*)> onClick;

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(TapeView, VSTGUI::CView)

private:
  void refit();

  TapePlacement placement_;
  FairyTapes::Ui::TapeFontId font_;
  TapeLook look_;
  FairyTapes::Ui::TapePaper paper_;
  std::string text_;
  TapeInk ink_;
};

} // namespace FairyTapes::DeepThroat::Ui
