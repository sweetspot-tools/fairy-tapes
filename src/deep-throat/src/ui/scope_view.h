// Deep Throat GUI: the SCOPE display (kScopeRect of ui/layout.h). Look from the approved rack preview
// (docs/mockups/deep_throat_rack.html): black rectangle with a grey border, a dashed line at mid height (Rider gain
// 0 dB), the white voice envelope, the amber Rider gain line and the red translucent band where a breath is detected.
// The legend (VOICE / RIDER GAIN / BREATH) is printed by the panel layer. One point per UI timer run, kept in a
// History of 292 points (one per pixel column); the newest point is on the right edge. Dims (35 %) with the global
// Bypass only. Clicks go through.
#pragma once

#include "ui/history.h"

#include "vstgui/lib/cview.h"

#include <vector>

namespace FairyTapes::DeepThroat::Ui {

class ScopeView : public VSTGUI::CView {
public:
  static constexpr int kPoints = 292;
  using Trace = History<kPoints>;

  explicit ScopeView(const VSTGUI::CRect& size);

  // Adds the newest point (normalized as displayNorms gives them) and invalidates the view.
  void push(const ScopePoint& p);
  const Trace& history() const { return history_; }
  // One column per stored point, oldest first, in absolute panel y (ui/history.h scopeColumnOf).
  std::vector<ScopeColumn> columns() const;
  // x of column `i` of columns(): the newest point sits on the right edge, the older ones to its left.
  double columnX(int i) const;

  void setDimmed(bool dimmed);
  bool dimmed() const { return dimmed_; }

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(ScopeView, VSTGUI::CView)

private:
  Rect area() const;

  Trace history_;
  bool dimmed_ = false;
};

} // namespace FairyTapes::DeepThroat::Ui
