/**
 * Fairy Tapes · Hotline — VSTGUI-side geometry tests without a window (the Morph knob: shared RackKnob with a centre offset).
 */
#include "ui/layout.h"

#include "fairy_ui/rack_controls.h"

#include "vstgui/lib/cframe.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::Hotline;
using namespace VSTGUI;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%d]: %s\n", __LINE__, #expr); ++g_failed; } } while (0)

FairyTapes::Ui::RackKnob* morphKnob() {
  const Ui::ControlSpec& spec = *Ui::controlOf(kMorph);
  const Ui::Rect v = Ui::viewRect(spec);
  auto* k = new FairyTapes::Ui::RackKnob(CRect(v.x, v.y, v.x + v.w, v.y + v.h), nullptr, kMorph, spec.capRadius, nullptr);
  k->setCenterOffsetY(Ui::knobCentreOffsetY(spec.capRadius));   // the same call as the editor's
  return k;
}
}

int main() {
  const Ui::ControlSpec& spec = *Ui::controlOf(kMorph);
  CHECK(Ui::isBigKnob(spec.capRadius));
  auto* knob = morphKnob();
  const CRect view = knob->getViewSize();

  // center() and the cap centre of the layout (and of the silkscreen) are the same point: preview (144, 337)
  const CPoint c = knob->center();
  CHECK(std::fabs(c.x - 144.0) < 1e-6 && std::fabs(c.y - 337.0) < 1e-6);
  CHECK(std::fabs(c.y - view.top - Ui::knobCentreOffsetY(spec.capRadius)) < 1e-6);
  CHECK(std::fabs(c.y - Ui::knobCentreY(spec)) < 1e-6);
  // the shared offset is the base one plus the extra height above the big cap; draw() uses center() (shared tests pin that)
  CHECK(std::fabs(knob->centerOffsetY() - FairyTapes::Ui::AnalogKnob::kCenterOffsetY - Ui::kBigKnobExtra) < 1e-6);

  // the value area is printed under the name at the silkscreen's baseline for this centre
  const CRect va = knob->valueArea();
  const double baseline = FairyTapes::Ui::Rack::valueBaseline(c.y, spec.capRadius);
  CHECK(va.top < baseline && baseline < va.bottom);
  CHECK(va.pointInside(CPoint(c.x, baseline - 5)));
  CHECK(!va.pointInside(CPoint(c.x, c.y)));   // the cap centre is not the value

  // clicks: a hit on the value area calls onValueClick; one at the cap centre does not
  int clicks = 0;
  knob->onValueClick = [&] { ++clicks; };
  CPoint onValue(c.x, baseline - 5), onCap(c.x, c.y);
  knob->onMouseDown(onValue, CButtonState(kLButton));
  CHECK(clicks == 1);
  // a point in the unshifted base area (8 px higher) but outside the true one is not a value click
  CPoint oldArea(c.x, baseline - Ui::kBigKnobExtra - 11.9);
  CHECK(!va.pointInside(oldArea));
  knob->onMouseDown(oldArea, CButtonState(kLButton));
  CHECK(clicks == 1);
  knob->onMouseDown(onCap, CButtonState(kLButton));
  CHECK(clicks == 1);
  knob->forget();

  std::printf(g_failed ? "hotline_ui_tests FAILED\n" : "hotline_ui_tests passed\n");
  return g_failed ? 1 : 0;
}
