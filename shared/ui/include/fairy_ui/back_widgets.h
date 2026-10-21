// Fairy Tapes shared UI: the widgets of the back of the panel (segmented selector, option stepper, text link, push
// button). Look from the approved previews: white outlines, the selected option filled white, grey push buttons.
// First written for The Stripper (src/the-stripper/src/ui/back_widgets.h, PushButton in browser_widgets.h); the font
// stepper is generalised to any list of options (optionally written in the tape fonts).
#pragma once

#include "vstgui/lib/cview.h"

#include <functional>
#include <string>
#include <vector>

namespace FairyTapes::Ui {

// A row of exclusive options with a white outline; the selected one is filled white.
class SegmentRow : public VSTGUI::CView {
public:
  SegmentRow(const VSTGUI::CRect& size, std::vector<std::string> options, int selected);
  void setSelected(int index);
  int selected() const { return selected_; }
  void setEnabled(bool enabled);
  bool enabled() const { return enabled_; }
  std::function<void(int)> onSelect;   // only when the selection changes

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(SegmentRow, VSTGUI::CView)

private:
  std::vector<std::string> options_;
  int selected_;
  bool enabled_ = true;
};

// Two arrow buttons around the name of the selected option; with `tapeFonts` option i is written in tape font i.
class OptionStepper : public VSTGUI::CView {
public:
  OptionStepper(const VSTGUI::CRect& size, std::vector<std::string> options, int selected, bool tapeFonts);
  void setSelected(int index);
  int selected() const { return selected_; }
  void setEnabled(bool enabled);
  std::function<void(int delta)> onStep;   // -1 or +1

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(OptionStepper, VSTGUI::CView)

private:
  std::vector<std::string> options_;
  int selected_;
  bool tapeFonts_;
  bool enabled_ = true;
};

// A small text that acts as a link, right-aligned in the view with its baseline 6 px above the bottom; `active` draws it
// bright (white), otherwise dim.
class TextLink : public VSTGUI::CView {
public:
  TextLink(const VSTGUI::CRect& size, std::string text);
  void setActive(bool active);
  bool active() const { return active_; }
  void setText(std::string text);
  void setLeftAligned(bool left);   // text starts at the left edge instead of ending at the right one (footers, link rows)
  bool leftAligned() const { return left_; }
  std::function<void()> onClick;

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(TextLink, VSTGUI::CView)

private:
  std::string text_;
  bool active_ = false;
  bool left_ = false;
};

// A grey push button with a dark caption.
class PushButton : public VSTGUI::CView {
public:
  PushButton(const VSTGUI::CRect& size, std::string text);
  void setText(std::string text);
  void setEnabled(bool enabled);
  bool isEnabled() const { return enabled_; }
  std::function<void()> onClick;

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(PushButton, VSTGUI::CView)

private:
  std::string text_;
  bool enabled_ = true;
};

} // namespace FairyTapes::Ui
