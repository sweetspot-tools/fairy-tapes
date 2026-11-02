// Happy Ending GUI: the panel layer, drawn in code (docs/DESIGN_SYSTEM.md, layers 1-2): the shared panel body (#161617
// tint, the grain tile shared/ui/assets/panel_grain.png from the bundle's Resources, plain tint when it is missing, soft
// light, edge), the boxes of ui/layout.h with their titles in a gap of the bottom border, the screen's box without a
// title (a closed border), the thin header lines of FINE EQ and VIBE EQ, the printed texts of kPanelTexts and the faint
// engine tags at the top left of the boxes (engineTag: FINAL follows its mode). Dimming (35 %) by the UiState rules of
// ui/layout.h (panelTextDimmed, headerLineDimmed, tagDimmed). Clicks go through.
#pragma once

#include "ui/layout.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

#include <array>
#include <cstddef>
#include <vector>

namespace FairyTapes::Ui {
class Skin;
}

namespace FairyTapes::HappyEnding::Ui {

class PanelView : public VSTGUI::CView {
public:
  static constexpr float kDimmedAlpha = 0.35f;   // DESIGN_SYSTEM.md: dimmed parts

  // What the last draw() put on the panel, for the tests and the render tool.
  // A printed text (kPanelTexts order): left end of the baseline, width, opacity.
  struct DrawnText {
    const char* text;
    double left, y, width;
    float alpha;
  };
  // A header line (kHeaderLines order).
  struct DrawnLine {
    double x0, x1, y;
    float alpha;
  };
  // An engine tag (kBoxes order, boxes without a tag skipped): left end of the baseline, opacity.
  struct DrawnTag {
    Box box;
    const char* text;
    double x, y;
    float alpha;
  };

  PanelView(const VSTGUI::CRect& size, const FairyTapes::Ui::Skin* skin);

  void draw(VSTGUI::CDrawContext* context) override;

  // The state the dimming and the FINAL tag follow; redraws only when it changes.
  void setUiState(const UiState& s);
  const UiState& uiState() const { return state_; }

  const std::vector<DrawnText>& drawnTexts() const { return texts_; }
  const std::vector<DrawnLine>& drawnLines() const { return lines_; }
  const std::vector<DrawnTag>& drawnTags() const { return tags_; }

private:
  static constexpr std::size_t kNumStyles = static_cast<std::size_t>(TextStyle::Tag) + 1;

  const FairyTapes::Ui::Skin* skin_;
  std::array<VSTGUI::SharedPointer<VSTGUI::CFontDesc>, kNumStyles> fonts_;
  UiState state_{};
  std::vector<DrawnText> texts_;
  std::vector<DrawnLine> lines_;
  std::vector<DrawnTag> tags_;
};

} // namespace FairyTapes::HappyEnding::Ui
