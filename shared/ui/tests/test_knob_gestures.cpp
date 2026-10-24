/** Fairy Tapes · shared UI — knob double-click gestures that do not need a window (no drawing). */
#include "fairy_ui/controls.h"
#include "fairy_ui/knob_math.h"
#include "fairy_ui/rack_controls.h"

#include "vstgui/lib/cbuttonstate.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"

#include <cmath>
#include <cstdio>

using namespace FairyTapes::Ui;
using namespace VSTGUI;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

AnalogKnob* makeKnob() {
  auto* k = new AnalogKnob(CRect(0, 0, 64, 88), nullptr, 1, "GAIN");
  k->setDefaultValue(0.5f);
  k->setValueNormalized(0.9f);
  return k;
}

void test_double_click_goes_to_default() {
  auto k = owned(makeKnob());
  CPoint p(32, 30);
  k->onMouseDown(p, CButtonState(kLButton | kDoubleClick));
  CHECK(near(k->getValue(), 0.5f));
}

void test_alt_double_click_goes_to_target() {
  auto k = owned(makeKnob());
  k->altDoubleClickTarget = [](float& v) { v = 0.2f; return true; };
  CPoint p(32, 30);
  k->onMouseDown(p, CButtonState(kLButton | kDoubleClick));   // no modifier: still the default
  CHECK(near(k->getValue(), 0.5f));
  k->setValueNormalized(0.9f);
  k->onMouseDown(p, CButtonState(kLButton | kDoubleClick | kAlt));
  CHECK(near(k->getValue(), 0.2f));
  k->setValueNormalized(0.9f);
  k->onMouseDown(p, CButtonState(kLButton | kDoubleClick | kControl));   // Ctrl/Cmd works too
  CHECK(near(k->getValue(), 0.2f));
}

void test_alt_double_click_without_target_falls_back_to_default() {
  auto k = owned(makeKnob());
  CPoint p(32, 30);
  k->onMouseDown(p, CButtonState(kLButton | kDoubleClick | kAlt));   // hook not set
  CHECK(near(k->getValue(), 0.5f));
  k->setValueNormalized(0.9f);
  k->altDoubleClickTarget = [](float&) { return false; };            // hook declines (nothing recalled)
  k->onMouseDown(p, CButtonState(kLButton | kDoubleClick | kAlt));
  CHECK(near(k->getValue(), 0.5f));
}
// A cycling selector steps to the next value at each click and wraps; its LED is lit away from the default value.
void test_cycle_steps_and_wraps() {
  auto c = owned(new RackCycle(CRect(0, 0, 24, 36), nullptr, 1, 3));
  c->setDefaultValue(static_cast<float>(normFromChoiceIndex(0, 3)));
  c->setValueNormalized(static_cast<float>(normFromChoiceIndex(0, 3)));
  CHECK(c->selectedIndex() == 0 && !c->ledLit());
  CPoint p(12, 11);
  c->onMouseDown(p, CButtonState(kLButton));
  CHECK(c->selectedIndex() == 1 && c->ledLit());
  c->onMouseDown(p, CButtonState(kLButton));
  c->onMouseDown(p, CButtonState(kLButton));
  CHECK(c->selectedIndex() == 0 && !c->ledLit());   // wrapped back to the default
  c->onMouseDown(p, CButtonState(kRButton));          // only the left button steps
  CHECK(c->selectedIndex() == 0);
}
} // namespace

int main() {
  test_cycle_steps_and_wraps();
  std::puts("knob gesture tests");
  test_double_click_goes_to_default();
  test_alt_double_click_goes_to_target();
  test_alt_double_click_without_target_falls_back_to_default();
  if (g_failed) { std::fprintf(stderr, "FAILED (%d)\n", g_failed); return 1; }
  std::puts("All knob gesture tests passed");
  return 0;
}
