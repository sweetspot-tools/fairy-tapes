// Fairy Tapes shared UI: one screw of the rack panel (docs/DESIGN_SYSTEM.md, layer 5: over the wear, below the controls).
// Drag it to turn it (right or up clockwise); a missing screw is a bare hole that cannot be turned. Only the screw itself
// takes clicks (kScrewHitRadius around the centre): the rest of its box, and a missing screw, let them through.
// onTurning reports every move of a drag; onTurned reports the end of a drag that changed the angle (once per gesture:
// the moment to save the angle and mark the project as changed). A cancelled drag puts the screw back.
#pragma once

#include "fairy_ui/screws.h"

#include "vstgui/lib/cview.h"

#include <functional>

namespace FairyTapes::Ui {

class ScrewView : public VSTGUI::CView {
public:
  ScrewView(const ScrewSpec& spec, double angle);

  // A new screw (e.g. after a re-roll): its look, place and angle; any drag in progress ends without a report.
  void setScrew(const ScrewSpec& spec, double angle);
  // The angle changed elsewhere (state recall, undo); ignored while the user is dragging this screw.
  void setAngle(double angle);
  double angle() const { return angle_; }
  const ScrewSpec& spec() const { return spec_; }
  bool dragging() const { return dragging_; }
  // Whether a click at `where` (parent coordinates) grabs the screw: hitTest uses it.
  bool takesClickAt(const VSTGUI::CPoint& where) const;

  std::function<void(double angle)> onTurning;   // while dragging
  std::function<void(double angle)> onTurned;    // the drag ended with a new angle

  bool hitTest(const VSTGUI::CPoint& where, const VSTGUI::Event& event = VSTGUI::noEvent()) override;
  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  VSTGUI::CMouseEventResult onMouseCancel() override;
  CLASS_METHODS_NOCOPY(ScrewView, VSTGUI::CView)

private:
  ScrewSpec spec_;
  double angle_;
  bool dragging_ = false;
  VSTGUI::CPoint anchor_;
  double startAngle_ = 0;
};

}  // namespace FairyTapes::Ui
