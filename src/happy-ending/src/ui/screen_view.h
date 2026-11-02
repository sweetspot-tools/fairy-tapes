// Happy Ending GUI: the shared screen (spec §4, docs/mockups/happy_ending_rack.html: viewTick, redraw, vibeRedraw): one view
// on kScreenRect that shows one of five views, kept by the controller (Ui::ScreenView, ui/screen_model.h):
//  - EQ: the grid, the FINE EQ curve (STEREO, or MID and SIDE once a band works in M/S), the VIBE EQ curve in amber with the
//    static setting dashed while the DYN pulls the boost back, and while LISTEN hears a bell the zone it lets through;
//  - GLUE and LIMIT: the level (grey area), the gain reduction (amber) and the threshold (GLUE, with the net reduction after
//    MIX dashed) or the ceiling (LIMIT) as lines, scrolling: both histories grow on every tick whatever view is shown;
//  - CLIP and FINAL: the transfer curve with the dot that follows the stage input (FINAL with the ceiling line).
// The shapes come from the pure model (ui/screen_model.h) and are cached: the EQ curves, the VIBE curve (the static one and
// the one with the published DYN pull-back), the CLIP and FINAL curves and the LISTEN zone are recomputed only when a
// parameter that feeds them changes (or the pull-back does), never on every tick or redraw (recomputes() counts them).
// The whole content dims (35 %) with BYPASS and with nothing else: a stage that is out shows what the engine publishes.
// LIMIT follows its state: with LIMIT out the reduction is flat and the effective-threshold line is not drawn (the engine
// publishes that threshold even then). The view only shows; clicks go through. The editor feeds it: setParams() with the
// stored table and the UiState whenever a parameter changes, tick() with the display parameters on every timer run.
#pragma once

#include "ui/layout.h"
#include "ui/screen_model.h"

#include "vstgui/lib/cview.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace FairyTapes::HappyEnding::Ui {

inline constexpr std::size_t kHistoryPoints = 150;   // points of the GLUE / LIMIT histories (about 4.5 s at the 30 ms timer)

// What the editor reads from the display parameters on a timer run, in real units (display.h converts back from the
// normalized values).
struct ScreenFrame {
  double glueInDb = -120.0, glueGrDb = 0.0;
  double limitInDb = -120.0, limitGrDb = 0.0, limitThreshDb = 0.0;
  double clipInDb = -120.0, finalInDb = -120.0;
  double dynLow = 0.0, dynAir = 0.0;   // the published pull-back amounts, 0..1
};

class ScreenDisplayView : public VSTGUI::CView {
public:
  using History = ScrollHistory<kHistoryPoints>;

  ScreenDisplayView();

  void setView(ScreenView view);
  ScreenView view() const { return view_; }

  // The stored table (kNumParams normalized values, ParamID order) and the UiState (BYPASS, the stages in, LISTEN).
  void setParams(const double* norm, const UiState& state);
  // One timer run: the display values. Both histories get a point, whatever view is shown.
  void tick(const ScreenFrame& frame);

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(ScreenDisplayView, VSTGUI::CView)

  // ---- What the view holds and what the last draw put on the screen (tests and the render tool) ----
  bool isDimmed() const { return dimmed_; }
  // draws of this view so far (counted by the drawing code of the view, not by the one asked for).
  int drawCount(ScreenView v) const { return drawCount_[static_cast<std::size_t>(v)]; }
  // Rebuilds of a cached shape so far (the FINE curves, the VIBE curves, the CLIP curve, the FINAL curve, the LISTEN zone each
  // count one when they are recomputed).
  int recomputes() const { return recomputes_; }
  std::size_t historySize(ScreenView v) const { return history(v).size(); }
  // The newest point of the GLUE or LIMIT history (the default point while empty).
  ScrollPoint newest(ScreenView v) const;
  bool listenZoneDrawn() const { return listenZoneDrawn_; }
  bool limitThreshDrawn() const { return limitThreshDrawn_; }
  bool vibeDashedDrawn() const { return vibeDashedDrawn_; }
  // The dot of the CLIP or FINAL view in layout coordinates, as drawn last (CLIP and FINAL views only).
  Point dot() const { return dot_; }
  // The GLUE or LIMIT history (LIMIT for ScreenView::Limit, GLUE for any other view).
  const History& history(ScreenView v) const { return v == ScreenView::Limit ? limitHistory_ : glueHistory_; }

private:
  // Recomputes what is stale for the shown view.
  void refreshShapes();
  void refreshEq();
  void refreshDot();

  void drawEq(VSTGUI::CDrawContext* ctx);
  void drawHistory(VSTGUI::CDrawContext* ctx, ScreenView v);
  void drawTransfer(VSTGUI::CDrawContext* ctx, ScreenView v);

  ScreenView view_ = ScreenView::Eq;
  bool dimmed_ = false;
  UiState state_{};
  std::array<double, kNumParams> norm_{};

  // the values the lines and the dot follow, from the table
  double glueThreshDb_ = 0.0, glueMix_ = 0.0, ceilingDb_ = 0.0, ceilingLin_ = 1.0;
  ScreenFrame frame_{};

  // cached shapes and what makes each stale
  EqCurves curves_;
  bool curvesStale_ = true;   // the model's FineEq / VibeEq are behind the table
  bool fineStale_ = true, vibeStaticStale_ = true, vibeLiveStale_ = true, clipStale_ = true, finalStale_ = true, dotStale_ = true;
  int listenBell_ = -1;   // the bell the zone shows (0..2), -1 none
  bool msActive_ = false;
  std::vector<Point> fineSt_, fineMid_, fineSide_, listenZone_, vibeStatic_, vibeLive_, clipCurve_, finalCurve_;
  std::string listenText_;
  Point dot_{};
  double dotInDb_ = -999.0;
  ScreenView dotView_ = ScreenView::Eq;   // the view the dot was computed for
  int recomputes_ = 0;

  History glueHistory_, limitHistory_;

  std::array<int, 5> drawCount_{};
  bool listenZoneDrawn_ = false, limitThreshDrawn_ = false, vibeDashedDrawn_ = false;
};

} // namespace FairyTapes::HappyEnding::Ui
