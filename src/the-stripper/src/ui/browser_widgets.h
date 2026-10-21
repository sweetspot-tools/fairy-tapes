// The Stripper GUI: small code-drawn widgets of the model popup (push button, scrolling list).
#pragma once

#include "vstgui/lib/cview.h"

#include <functional>
#include <string>
#include <vector>

namespace FairyTapes::TheStripper::Ui {

// Grey rack push button with dark lettering.
class PushButton : public VSTGUI::CView {
public:
  PushButton(const VSTGUI::CRect& size, std::string text);
  void setText(std::string text);
  void setEnabled(bool enabled);
  bool isEnabled() const { return enabled_; }
  void setSelected(bool selected);          // the chosen one of a row of buttons: amber lettering and frame
  void setFontSize(double size);
  std::function<void()> onClick;

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(PushButton, VSTGUI::CView)

private:
  std::string text_;
  bool enabled_ = true;
  bool selected_ = false;
  double fontSize_ = 12;
};

// Vertical list of text rows with a selection, a green marker on one row and wheel scrolling.
class ListBox : public VSTGUI::CView {
public:
  struct Row {
    std::string text;
    std::string meta;      // right-aligned, dim
    bool marked = false;   // green dot (the loaded model)
  };
  static constexpr double kRowHeight = 24;

  explicit ListBox(const VSTGUI::CRect& size);
  void setRows(std::vector<Row> rows);
  const std::vector<Row>& rows() const { return rows_; }
  void setSelected(int index);   // -1 = none
  int selected() const { return selected_; }
  void setEmptyText(std::string text) { empty_ = std::move(text); invalid(); }
  std::function<void(int)> onSelect;     // single click
  std::function<void(int)> onActivate;   // double click

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override;
  CLASS_METHODS_NOCOPY(ListBox, VSTGUI::CView)

private:
  int rowAt(const VSTGUI::CPoint& where) const;
  double maxScroll() const;
  void scrollTo(double offset);

  std::vector<Row> rows_;
  int selected_ = -1;
  double scroll_ = 0;
  std::string empty_;
};

} // namespace FairyTapes::TheStripper::Ui
