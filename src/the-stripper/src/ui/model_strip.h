// The Stripper GUI: the model name written on a strip of paper tape (Drive box). A click opens the model picker.
#pragma once

#include "ui/tape_kit.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <string>

namespace FairyTapes::TheStripper::Ui {

class ModelStrip : public VSTGUI::CView {
public:
  enum class Kind { NoModel, Loading, Loaded, Error };

  explicit ModelStrip(const VSTGUI::CRect& size);

  // `text` is the model name (no extension) or the error message; ignored for NoModel and Loading.
  void setModel(Kind kind, std::string text);
  Kind kind() const { return kind_; }
  void setDimmed(bool dimmed);
  void setFont(TapeFontId font);   // the handwriting font chosen in the preferences
  std::function<void()> onClick;

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(ModelStrip, VSTGUI::CView)

private:
  Kind kind_ = Kind::NoModel;
  std::string text_;
  bool dimmed_ = false;
  TapeFontId font_ = TapeFontId::Zeyada;
};

} // namespace FairyTapes::TheStripper::Ui
