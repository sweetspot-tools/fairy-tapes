// Fairy Tapes shared UI: code-drawn VSTGUI controls (knob, toggle, choice buttons).
// All controls work on normalized values 0..1 (min 0, max 1); incoming values are
// sanitized (NaN/inf -> default or 0, clamped). Every user gesture reaches the
// IControlListener as beginEdit / valueChanged / endEdit.
#pragma once

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <functional>
#include <string>
#include <vector>

namespace FairyTapes::Ui {

class Skin;

// Common base: normalized range, sanitized setters, one-shot edit gesture.
class NormalizedControl : public VSTGUI::CControl {
public:
  NormalizedControl(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
                    const Skin* skin);

  void setValue(float val) override;
  void setDefaultValue(float val) override;

  // Draws attenuated; the control stays fully operable.
  void setDimmed(bool dimmed);
  bool isDimmed() const { return dimmed_; }

protected:
  // Maps any incoming value to a valid one (default: sanitizeNorm). Must be NaN-safe.
  virtual double quantize(double v, double fallback) const;
  // beginEdit, set value, valueChanged, endEdit, invalid.
  void editTo(float newValue);

  const Skin* skin_ = nullptr;
  bool dimmed_ = false;
};

class AnalogKnob : public NormalizedControl {
public:
  static constexpr VSTGUI::CCoord kPreferredWidth = 64;
  static constexpr VSTGUI::CCoord kPreferredHeight = 88;
  static constexpr VSTGUI::CCoord kCenterOffsetY = 30;  // knob centre below the top of the view

  using ValueFormatter = std::function<std::string(float normalized)>;

  AnalogKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
             std::string label, const Skin* skin = nullptr);

  void setLabel(std::string label);
  // Where the knob centre sits below the top of the view (default kCenterOffsetY). A bigger cap needs a taller view above
  // the centre: the caller picks the view rect and the offset together. center(), the drawing and (in RackKnob) the value
  // area all follow this value.
  void setCenterOffsetY(VSTGUI::CCoord offset) { centerOffsetY_ = offset; invalid(); }
  VSTGUI::CCoord centerOffsetY() const { return centerOffsetY_; }
  VSTGUI::CPoint center() const;   // the knob centre in the parent's coordinates
  // Alt (or Ctrl/Cmd) + double click goes here instead of to the default value: return true and set `value` (0..1)
  // to jump to it. Unset or false = the plain double-click behaviour.
  std::function<bool(float& value)> altDoubleClickTarget;
  // Value text under the label; formatted only when the value changes.
  void setValueFormatter(ValueFormatter formatter);

  void draw(VSTGUI::CDrawContext* context) override;

  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseCancel() override;
  // Closes a drag gesture left open when the view goes away (CFrame sends no mouse cancel).
  bool removed(VSTGUI::CView* parent) override;

  CLASS_METHODS_NOCOPY(AnalogKnob, NormalizedControl)

private:
  const std::string& valueText();

  VSTGUI::CCoord centerOffsetY_ = kCenterOffsetY;

  std::string label_;
  ValueFormatter formatter_;
  std::string valueText_;
  float valueTextFor_ = -1.f;  // value valueText_ was formatted for (-1 = stale)

  bool dragging_ = false;
  bool dragFine_ = false;
  VSTGUI::CCoord dragAnchorY_ = 0;
  double dragAnchorValue_ = 0;
  float gestureStartValue_ = 0;
};

class ToggleButton : public NormalizedControl {
public:
  ToggleButton(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
               std::string label, const Skin* skin = nullptr);

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;

  CLASS_METHODS_NOCOPY(ToggleButton, NormalizedControl)

protected:
  double quantize(double v, double fallback) const override;  // 0 or 1

private:
  std::string label_;
};

// N side-by-side buttons, exclusive; value = normFromChoiceIndex(index, N).
class ChoiceButtons : public NormalizedControl {
public:
  ChoiceButtons(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
                std::vector<std::string> labels, const Skin* skin = nullptr);

  int count() const { return static_cast<int>(labels_.size()); }
  int selectedIndex() const;

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;

  CLASS_METHODS_NOCOPY(ChoiceButtons, NormalizedControl)

protected:
  double quantize(double v, double fallback) const override;  // nearest choice grid value

private:
  VSTGUI::CRect buttonRect(int index) const;

  std::vector<std::string> labels_;
};

} // namespace FairyTapes::Ui
