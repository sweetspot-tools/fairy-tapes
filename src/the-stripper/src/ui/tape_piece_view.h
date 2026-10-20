// The Stripper GUI: one piece of writable paper tape on the panel. A click asks the editor to open the text field.
#pragma once

#include "ui/tape_font.h"
#include "ui/tape_pieces.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <string>

namespace FairyTapes::TheStripper::Ui {

class TapePieceView : public VSTGUI::CView {
public:
  TapePieceView(const TapePiece& piece, int index, TapeFontId font, std::uint32_t seed);

  void setText(const std::string& displayed);
  void setFont(TapeFontId font);
  const std::string& text() const { return text_; }
  int index() const { return index_; }
  // The rectangle of the (unrotated) tape in the view's parent coordinates: where the text field goes.
  VSTGUI::CRect tapeRect() const;
  std::function<void(TapePieceView*)> onClick;

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(TapePieceView, VSTGUI::CView)

private:
  TapePiece piece_;
  int index_;
  TapeFontId font_;
  std::uint32_t seed_;
  std::string text_;
};

} // namespace FairyTapes::TheStripper::Ui
