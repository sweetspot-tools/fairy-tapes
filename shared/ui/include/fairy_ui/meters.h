// Fairy Tapes shared UI: code-drawn level meters (read-only LED bar, clip LED).
// Values are normalized 0..1 and sanitized (NaN/inf -> 0, clamped). The editor feeds them from its UI timer.
#pragma once

#include "fairy_ui/meter_math.h"

#include "vstgui/lib/cview.h"

#include <functional>
#include <string>

namespace FairyTapes::Ui {

// A row of round LEDs lit up to the value, with a peak-hold marker (the hold LED is drawn brighter and
// always lit, so the held peak stays visible after the level has dropped).
class MeterBar : public VSTGUI::CView {
public:
  static constexpr int kDefaultLeds = 15;

  MeterBar(const VSTGUI::CRect& size, MeterKind kind, int ledCount = kDefaultLeds);

  // Live value, 0..1. Updates the peak hold with `dtSeconds` elapsed since the previous call.
  void setLevel(double norm, double dtSeconds);
  double level() const { return level_; }
  double hold() const { return peak_.hold(); }
  // Dims the whole bar (a bypassed section).
  void setDimmed(bool dimmed);

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(MeterBar, VSTGUI::CView)

private:
  MeterKind kind_;
  int count_;
  double level_ = 0.0;
  bool dimmed_ = false;
  PeakHold peak_;
};

// One clip indicator: a LED with a label that lights on 0/1 and, when clicked, calls onClick (the editor then
// writes the clear request, which clears every clip LED of the plugin).
class ClipLed : public VSTGUI::CView {
public:
  ClipLed(const VSTGUI::CRect& size, std::string label);

  void setLit(bool lit);
  bool isLit() const { return lit_; }
  void setDimmed(bool dimmed);
  std::function<void()> onClick;

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(ClipLed, VSTGUI::CView)

private:
  std::string label_;
  bool lit_ = false;
  bool dimmed_ = false;
};

} // namespace FairyTapes::Ui
