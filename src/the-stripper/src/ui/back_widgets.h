// The Stripper GUI: widgets of the back panel (segmented selector, tape-font stepper, text link).
#pragma once

#include "ui/tape_kit.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <string>
#include <vector>

namespace FairyTapes::TheStripper::Ui {

// A row of exclusive options with a white outline; the selected one is filled white.
class SegmentRow : public VSTGUI::CView {
public:
  SegmentRow(const VSTGUI::CRect& size, std::vector<std::string> options, int selected);
  void setSelected(int index);
  int selected() const { return selected_; }
  void setEnabled(bool enabled);
  std::function<void(int)> onSelect;

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(SegmentRow, VSTGUI::CView)

private:
  std::vector<std::string> options_;
  int selected_;
  bool enabled_ = true;
};

// Two arrow buttons around the name of the selected tape font, written in that font.
class FontStepper : public VSTGUI::CView {
public:
  FontStepper(const VSTGUI::CRect& size, int selected);
  void setSelected(int index);
  int selected() const { return selected_; }
  std::function<void(int delta)> onStep;   // -1 or +1

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(FontStepper, VSTGUI::CView)

private:
  int selected_;
};

// A small text that acts as a link (right-aligned at the right edge of the view); `active` draws it bright.
class TextLink : public VSTGUI::CView {
public:
  TextLink(const VSTGUI::CRect& size, std::string text);
  void setActive(bool active);
  void setText(std::string text);
  void setLeftAligned(bool left) { left_ = left; }   // default: right-aligned at the right edge of the view
  std::function<void()> onClick;

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(TextLink, VSTGUI::CView)

private:
  std::string text_;
  bool active_ = false;
  bool left_ = false;
};

} // namespace FairyTapes::TheStripper::Ui
