/** Fairy Tapes · shared UI — knob double-click gestures that do not need a window (no drawing). */
#include "fairy_ui/controls.h"
#include "fairy_ui/knob_math.h"
#include "fairy_ui/rack_controls.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cbuttonstate.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/vstguiinit.h"
#include "vstgui/lib/coffscreencontext.h"
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

// Centre offset: the default reproduces the old numbers; a custom one moves center(), valueArea() and the click areas.
void test_default_center_offset_is_unchanged() {
  auto k = owned(new RackKnob(CRect(10, 20, 74, 108), nullptr, 1, 28));
  CHECK(k->centerOffsetY() == AnalogKnob::kCenterOffsetY && AnalogKnob::kCenterOffsetY == 30);
  const CPoint c = k->center();
  CHECK(near(static_cast<float>(c.x), 42.f) && near(static_cast<float>(c.y), 50.f));
  const CRect va = k->valueArea();
  const double baseline = Rack::valueBaseline(50.0, 28);
  CHECK(near(static_cast<float>(va.left), 8.f) && near(static_cast<float>(va.right), 76.f));
  CHECK(near(static_cast<float>(va.top), static_cast<float>(baseline - 12)) && near(static_cast<float>(va.bottom), static_cast<float>(baseline + 5)));
}

void test_custom_center_offset_moves_everything() {
  auto k = owned(new RackKnob(CRect(10, 20, 86, 116), nullptr, 1, 34));
  k->setCenterOffsetY(38);
  CHECK(k->centerOffsetY() == 38);
  const CPoint c = k->center();
  CHECK(near(static_cast<float>(c.x), 48.f) && near(static_cast<float>(c.y), 58.f));
  const CRect va = k->valueArea();
  const double baseline = Rack::valueBaseline(58.0, 34);
  CHECK(near(static_cast<float>(va.top), static_cast<float>(baseline - 12)) && near(static_cast<float>(va.bottom), static_cast<float>(baseline + 5)));
  CHECK(va.pointInside(CPoint(c.x, baseline - 5)) && !va.pointInside(CPoint(c.x, c.y)));

  int clicks = 0;
  k->onValueClick = [&] { ++clicks; };
  CPoint onValue(c.x, baseline - 5), onCap(c.x, c.y), oldArea(c.x, Rack::valueBaseline(50.0, 34) - 5);
  k->onMouseDown(onValue, CButtonState(kLButton));
  CHECK(clicks == 1);
  k->onMouseDown(oldArea, CButtonState(kLButton));   // where the value area would be with the default offset
  CHECK(clicks == 1);
  k->onMouseDown(onCap, CButtonState(kLButton));       // the cap centre starts a drag, not a value click
  CHECK(clicks == 1);
  k->onMouseUp(onCap, CButtonState(kLButton));
}

void test_plain_analog_knob_center_offset() {
  auto k = owned(new AnalogKnob(CRect(0, 0, 64, 88), nullptr, 1, "GAIN"));
  CHECK(near(static_cast<float>(k->center().y), 30.f));
  k->setCenterOffsetY(36);
  CHECK(near(static_cast<float>(k->center().y), 36.f) && near(static_cast<float>(k->center().x), 32.f));
}

// Draws the knob into an offscreen bitmap of 120 x 120 and returns it.
SharedPointer<CBitmap> snapKnob(RackKnob* k) {
  auto ctx = COffscreenContext::create(CPoint(120, 120), 1.0);
  if (!ctx) return nullptr;
  ctx->beginDraw();
  ctx->setFillColor(CColor(10, 20, 30, 255));
  ctx->drawRect(CRect(0, 0, 120, 120), kDrawFilled);
  k->draw(ctx);
  ctx->endDraw();
  return SharedPointer<CBitmap>(ctx->getBitmap());
}

// draw() puts the cap at center(): a knob with offset 38 in a view 8 px higher draws the same cap, pixel for pixel, as the
// default-offset knob whose cap centre is at the same absolute point; and the cap really is at that point.
void test_draw_follows_center_offset() {
  auto a = owned(new RackKnob(CRect(10, 10, 86, 106), nullptr, 1, 34));        // centre (48, 40) with the default 30
  auto b = owned(new RackKnob(CRect(10, 2, 86, 98), nullptr, 1, 34));          // centre (48, 40) with 38
  b->setCenterOffsetY(38);
  CHECK(near(static_cast<float>(a->center().y), static_cast<float>(b->center().y)));
  auto ia = snapKnob(a), ib = snapKnob(b);
  CHECK(ia && ib);
  if (!ia || !ib) return;
  auto pa = owned(CBitmapPixelAccess::create(ia)), pb = owned(CBitmapPixelAccess::create(ib));
  CHECK(pa && pb);
  if (!pa || !pb) return;
  int diff = 0, capPixels = 0;
  for (uint32_t y = 0; y < 120; ++y)
    for (uint32_t x = 0; x < 120; ++x) {
      pa->setPosition(x, y); pb->setPosition(x, y);
      CColor ca, cb;
      pa->getColor(ca); pb->getColor(cb);
      if (ca != cb) ++diff;
      if (ca != CColor(10, 20, 30, 255)) ++capPixels;
    }
  CHECK(diff == 0);
  CHECK(capPixels > 3000);   // something was drawn: a cap of radius 34 is about 3600 px
  // the centre pixel is cap, the pixel just outside the skirt (37) above it is background
  CColor c, o;
  pb->setPosition(48, 40); pb->getColor(c);
  pb->setPosition(48, 40 - 40); pb->getColor(o);
  CHECK(c != CColor(10, 20, 30, 255) && o == CColor(10, 20, 30, 255));
}

void test_center_offset_ignores_non_finite() {
  auto k = owned(new RackKnob(CRect(0, 0, 76, 96), nullptr, 1, 34));
  k->setCenterOffsetY(38);
  k->setCenterOffsetY(std::nan(""));
  CHECK(k->centerOffsetY() == 38);
  k->setCenterOffsetY(HUGE_VAL);
  CHECK(k->centerOffsetY() == 38);
}
} // namespace

int main() {
  VSTGUI::init(nullptr);   // platform factory, for the offscreen drawing test
  test_cycle_steps_and_wraps();
  test_default_center_offset_is_unchanged();
  test_custom_center_offset_moves_everything();
  test_plain_analog_knob_center_offset();
  test_draw_follows_center_offset();
  test_center_offset_ignores_non_finite();
  std::puts("knob gesture tests");
  test_double_click_goes_to_default();
  test_alt_double_click_goes_to_target();
  test_alt_double_click_without_target_falls_back_to_default();
  if (g_failed) { std::fprintf(stderr, "FAILED (%d)\n", g_failed); return 1; }
  std::puts("All knob gesture tests passed");
  return 0;
}
