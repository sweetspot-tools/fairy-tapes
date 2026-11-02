// Happy Ending GUI: the small readout views of LEVEL and VIBE EQ that the shared widgets do not cover (the peak rows,
// the GR rows and the clip LEDs are the shared MeterBar / ClipLed): the LUFS screen, the tags of the GR rows and the ACT
// LEDs of the DYN buttons. Positions, sizes and colours from ui/layout.h and the preview (docs/mockups/
// happy_ending_rack.html); the texts and the green LED come from ui/readouts.h (pure, tested in tests/test_ui.cpp).
// The views only show what the editor hands them; none writes a parameter (the editor wires onReset).
#pragma once

#include "ui/layout.h"

#include "fairy_ui/meters.h"

#include "vstgui/lib/cview.h"

#include <array>
#include <functional>
#include <string>

namespace FairyTapes::HappyEnding::Ui {

// The shared LED bar that can be asked whether it is dimmed (MeterBar draws the dimming but keeps it private).
class LevelBar : public FairyTapes::Ui::MeterBar {
public:
  using MeterBar::MeterBar;

  void setDimmed(bool dimmed) {   // hides MeterBar::setDimmed; always called through a LevelBar
    dimmed_ = dimmed;
    MeterBar::setDimmed(dimmed);
  }
  bool isDimmed() const { return dimmed_; }

private:
  bool dimmed_ = false;
};

// The text of the LUFS screen: short-term (amber, large), integrated (amber; a click on its zone calls onReset) and TO TARGET
// (white) with its green LED. The screen's dark box and its labels are on the panel layer below. The view is live under
// BYPASS (never dims): the editor keeps feeding it.
class LufsView : public VSTGUI::CView {
public:
  explicit LufsView(const VSTGUI::CRect& size);

  // The short-term and integrated loudness in LUFS (-inf: no measurement) and the TARGET index of kTargets.
  void setReadout(double lufsShort, double lufsIntegrated, int targetIndex);
  const std::string& shortText() const { return short_; }
  const std::string& integratedText() const { return integrated_; }
  const std::string& targetGapText() const { return toTarget_; }
  bool targetLit() const { return targetLed_; }

  std::function<void()> onReset;   // a click on the integrated zone (kLufsResetRect)

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(LufsView, VSTGUI::CView)

private:
  std::string short_, integrated_, toTarget_;
  bool targetLed_ = false;
};

// The tags G C L F M left of the GR rows; each dims with its row. Clicks go through.
class GrTags : public VSTGUI::CView {
public:
  GrTags();

  void setDimmed(int row, bool dimmed);
  bool dimmed(int row) const { return row >= 0 && row < kNumRows && dimmed_[static_cast<std::size_t>(row)]; }

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(GrTags, VSTGUI::CView)

  static constexpr int kNumRows = 5;

private:
  std::array<bool, kNumRows> dimmed_{};
};

// The ACT LED of a DYN button: dark amber when the boost is untouched, amber as it is pulled back (the preview mixes the two
// at min(1, 1.4 x level)). Clicks go through.
class ActLed : public VSTGUI::CView {
public:
  explicit ActLed(const Led& at);

  void setLevel(double norm);   // 0..1, sanitized
  double level() const { return level_; }
  void setDimmed(bool dimmed);
  bool isDimmed() const { return dimmed_; }

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(ActLed, VSTGUI::CView)

private:
  Led led_;
  double level_ = 0.0;
  bool dimmed_ = false;
};

} // namespace FairyTapes::HappyEnding::Ui
