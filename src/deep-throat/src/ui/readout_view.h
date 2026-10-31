// Deep Throat GUI: the read-only values printed on the panel (silkscreen layer, below the wear), positions and styles from
// kReadouts of ui/layout.h: "MAX ±x dB · y dB/s" under the RIDER knobs (dim white), HPF and MUD CUT of EQ and the three
// Tone gains (amber; in the tape font when the values are handwritten, as the preview does with its amber texts). The
// strings come from ui/dynamic_text.h (the editor sets them); each one dims (35 %) on its own. Also the BREATH detector
// lamp (kBreathLamp): dark red, lit red while the editor says a breath is detected. Clicks go through.
#pragma once

#include "ui/layout.h"

#include "fairy_ui/tape_font.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

#include <array>
#include <cstdint>
#include <string>

namespace FairyTapes::DeepThroat::Ui {

class ReadoutView : public VSTGUI::CView {
public:
  static constexpr int kCount = static_cast<int>(sizeof(kReadouts) / sizeof(kReadouts[0]));

  explicit ReadoutView(const VSTGUI::CRect& size);

  void setText(ReadoutId id, const std::string& text);
  const std::string& text(ReadoutId id) const;
  void setDimmed(ReadoutId id, bool dimmed);
  bool dimmed(ReadoutId id) const;
  void setLampDimmed(bool dimmed);
  bool lampDimmed() const { return lampDimmed_; }
  // The lamp lit (red) while the detector hears a breath.
  void setLampLit(bool lit);
  bool lampLit() const { return lampLit_; }
  // The amber texts in the tape font `font` (with the per-text variation of `seed`), or plain bold Arial.
  void setHandwriting(bool on, FairyTapes::Ui::TapeFontId font, uint32_t seed);

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(ReadoutView, VSTGUI::CView)

private:
  static int indexOf(ReadoutId id);

  std::array<std::string, kCount> texts_{};
  std::array<bool, kCount> dimmed_{};
  bool lampDimmed_ = false;
  bool lampLit_ = false;
  bool handwritten_ = false;
  FairyTapes::Ui::TapeFontId font_ = FairyTapes::Ui::TapeFontId::Zeyada;
  uint32_t seed_ = 0;
  VSTGUI::SharedPointer<VSTGUI::CFontDesc> font10_;
};

} // namespace FairyTapes::DeepThroat::Ui
