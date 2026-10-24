// Deep Throat GUI: the panel layer, drawn in code (docs/DESIGN_SYSTEM.md, layers 1-2): the shared panel body (#161617
// tint, the grain tile shared/ui/assets/panel_grain.png from the bundle's Resources, plain tint when it is missing, soft
// light, edge), the boxes of ui/layout.h with their titles in a gap of the bottom border, and the printed texts of
// kPanelTexts. Texts dim (35 %) by panelTextDimmed: with the global bypass only the flagged ones, with a module's IN
// off only that module's. Clicks go through.
#pragma once

#include "ui/layout.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

#include "fairy_ui/draw_util.h"

#include <array>
#include <cstddef>
#include <vector>

namespace FairyTapes::Ui {
class Skin;
}

namespace FairyTapes::DeepThroat::Ui {

class PanelView : public VSTGUI::CView {
public:
  static constexpr const char* kGrainTile = FairyTapes::Ui::kPanelGrainTile;   // 256 x 256 tile, optional
  static constexpr float kDimmedAlpha = 0.35f;                                 // DESIGN_SYSTEM.md: dimmed parts

  // One printed text as the last draw() put it (kPanelTexts order): left end of the baseline, width, opacity.
  struct DrawnText {
    const char* text;
    double left, y, width;
    float alpha;
  };

  PanelView(const VSTGUI::CRect& size, const FairyTapes::Ui::Skin* skin);

  void draw(VSTGUI::CDrawContext* context) override;

  // Global bypass only (the INs as they are).
  void setDimmed(bool bypass);
  void setDimState(const DimState& s);
  const DimState& dimState() const { return dim_; }
  bool textDimmed(std::size_t index) const;
  const std::vector<DrawnText>& drawnTexts() const { return drawn_; }

private:
  static constexpr std::size_t kNumStyles = static_cast<std::size_t>(TextStyle::LegendBreath) + 1;

  const FairyTapes::Ui::Skin* skin_;
  std::array<VSTGUI::SharedPointer<VSTGUI::CFontDesc>, kNumStyles> fonts_;
  DimState dim_{};
  std::vector<DrawnText> drawn_;
};

} // namespace FairyTapes::DeepThroat::Ui
