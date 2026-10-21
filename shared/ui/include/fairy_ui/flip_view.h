// Fairy Tapes shared UI: the turn of the panel (front -> back and back). A flat imitation of a 3D flip: the picture of
// the old side narrows around the vertical axis until it is edge-on, then the picture of the new side widens, with a
// little shade and perspective (fairy_ui/flip_pose.h). About 0.8 s.
//  - FlipView draws the turn from two snapshots.
//  - PanelFlip runs it for an editor: snapshots both sides into bitmaps, hides the real views while the turn runs, and
//    shows the new side at the end. Without offscreen drawing the sides are swapped at once.
// Generalised from The Stripper (src/the-stripper/src/ui/flip_view.{h,cpp} and Editor::startFlip/finishFlip).
#pragma once

#include "fairy_ui/flip_pose.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cview.h"

#include <chrono>

namespace VSTGUI {
class CViewContainer;
}

namespace FairyTapes::Ui {

class FlipView : public VSTGUI::CView {
public:
  FlipView(const VSTGUI::CRect& size, VSTGUI::SharedPointer<VSTGUI::CBitmap> from, VSTGUI::SharedPointer<VSTGUI::CBitmap> to);
  void setProgress(double progress);   // 0..1
  double progress() const { return progress_; }

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&, const VSTGUI::CButtonState&) override { return VSTGUI::kMouseEventHandled; }
  CLASS_METHODS_NOCOPY(FlipView, VSTGUI::CView)

private:
  VSTGUI::SharedPointer<VSTGUI::CBitmap> from_, to_;
  double progress_ = 0;
};

// The turn between two sides that fill `host` (both children of it, same size). UI thread only. The views stay owned by
// the host; call reset() before the host removes its views (editor close).
class PanelFlip {
public:
  PanelFlip() = default;
  PanelFlip(VSTGUI::CViewContainer* host, VSTGUI::CView* front, VSTGUI::CView* back, double snapshotScale = 1.0);

  // Starts turning to the back (true) or to the front (false). False when a turn is running, the side is already
  // shown or the views are missing. Without offscreen drawing the side changes at once (and true is returned).
  bool start(bool toBack);
  // Advances a running turn (call from the UI timer); true while it is still running.
  bool tick();
  // Ends a running turn now, showing the new side.
  void finish();
  bool running() const { return flip_ != nullptr; }
  bool showingBack() const { return showingBack_; }
  // Forgets the views (they are going away with the host).
  void reset();

private:
  VSTGUI::SharedPointer<VSTGUI::CBitmap> snapshot(VSTGUI::CView* side) const;

  VSTGUI::CViewContainer* host_ = nullptr;
  VSTGUI::CView* front_ = nullptr;
  VSTGUI::CView* back_ = nullptr;
  double scale_ = 1.0;
  FlipView* flip_ = nullptr;
  bool showingBack_ = false;
  std::chrono::steady_clock::time_point start_{};
};

} // namespace FairyTapes::Ui
