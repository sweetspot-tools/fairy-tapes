// Pillow Talk GUI: scrolling ducking display (voice, echo, send level, threshold).
#pragma once

#include "ui/history.h"

#include "vstgui/lib/cview.h"

namespace FairyTapes::PillowTalk::Ui {

class DuckScope : public VSTGUI::CView {
public:
  static constexpr VSTGUI::CCoord kWidth = 432;
  static constexpr VSTGUI::CCoord kHeight = 130;
  static constexpr int kPoints = 200;  // about 6 s at one point per 30 ms

  explicit DuckScope(const VSTGUI::CPoint& origin);

  // Threshold parameter, normalized 0..1 (same -60..0 dB mapping as the voice level).
  void setThreshold(float norm);
  // Newest sample on the right; invalidates the view.
  void push(float voice, float send, float echo);

  void draw(VSTGUI::CDrawContext* context) override;

private:
  History history_{kPoints};
  float threshold_ = 0.f;
};

} // namespace FairyTapes::PillowTalk::Ui
