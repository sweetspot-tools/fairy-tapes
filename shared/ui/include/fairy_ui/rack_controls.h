// Fairy Tapes shared UI: the rack look of the controls (approved preview stripper_rack.html): a knob cap of turned,
// anodised aluminium and grey button faces with an LED. The printed parts (scale dots, end values, names, values,
// button labels) are NOT drawn here: they belong to the silkscreen layer of the editor, below the wear, so that
// scratches and tape can cover them. The controls draw only the parts that stand above the wear.
#pragma once

#include "fairy_ui/control_wear.h"
#include "fairy_ui/controls.h"
#include "fairy_ui/rack_geometry.h"

#include <cstdint>
#include <vector>

namespace FairyTapes::Ui {

enum class RackLed { Red, Yellow };

// Knob cap and pointer. The view is the usual knob cell (AnalogKnob::kPreferredWidth x kPreferredHeight); the cap is
// centred kCenterOffsetY below its top.
class RackKnob : public AnalogKnob {
public:
  RackKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
           double capRadius = Rack::kDefaultCapRadius, const Skin* skin = nullptr);

  // Wear of the cap (0 = none, 1 = light, 2.2 = heavy) from a per-control seed; drawn at 25 %.
  void setWear(std::uint32_t seed, double amount);

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(RackKnob, AnalogKnob)

private:
  double capRadius_;
  std::vector<ControlWearMark> wear_;
};

// On/off button: a grey face with an LED under it (lit = on), or the bell icon variant (amber face, no LED).
// The face is drawn at the top of the view; the LED sits Rack::kLedOffsetY below the face centre, so the view must be
// at least face height + about 14 px tall.
class RackToggle : public ToggleButton {
public:
  RackToggle(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag, RackLed led,
             bool bellIcon = false, const Skin* skin = nullptr);

  void setWear(std::uint32_t seed, double amount);

  void draw(VSTGUI::CDrawContext* context) override;
  CLASS_METHODS_NOCOPY(RackToggle, ToggleButton)

private:
  RackLed led_;
  bool bell_;
  std::vector<ControlWearMark> wear_;
};

// N exclusive buttons side by side (slot width = view width / N), each a grey face with an LED.
class RackChoice : public ChoiceButtons {
public:
  RackChoice(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag, int count, RackLed led,
             const Skin* skin = nullptr);

  void setWear(std::uint32_t seed, double amount);   // every button gets its own marks

  void draw(VSTGUI::CDrawContext* context) override;
  VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override;
  CLASS_METHODS_NOCOPY(RackChoice, ChoiceButtons)

private:
  VSTGUI::CRect slot(int index) const;
  RackLed led_;
  int count_;
  std::vector<std::vector<ControlWearMark>> wear_;
};

} // namespace FairyTapes::Ui
