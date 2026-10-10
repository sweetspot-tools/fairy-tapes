/** Fairy Tapes · shared UI — behaviour of the meter controls that does not need a window (no drawing). */
#include "fairy_ui/meters.h"

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

void test_clip_led_click_calls_back_and_lit_state() {
  ClipLed led(CRect(0, 0, 50, 12), "EQ");
  int clicks = 0;
  led.onClick = [&] { ++clicks; };
  CPoint p(5, 5);
  CHECK(led.onMouseDown(p, CButtonState(kLButton)) == kMouseEventHandled);
  CHECK(clicks == 1);
  CHECK(led.onMouseDown(p, CButtonState(kRButton)) == kMouseEventNotHandled);   // only the left button
  CHECK(clicks == 1);
  CHECK(!led.isLit());
  led.setLit(true);
  CHECK(led.isLit());
  led.setLit(false);
  CHECK(!led.isLit());
}

void test_clip_led_without_callback_is_harmless() {
  ClipLed led(CRect(0, 0, 50, 12), "IN");
  CPoint p(1, 1);
  CHECK(led.onMouseDown(p, CButtonState(kLButton)) == kMouseEventHandled);   // no crash with an empty std::function
}

void test_meter_bar_tracks_level_and_hold() {
  MeterBar bar(CRect(0, 0, 300, 12), MeterKind::Level);
  bar.setLevel(0.8, 0.03);
  CHECK(std::fabs(bar.level() - 0.8) < 1e-9);
  CHECK(std::fabs(bar.hold() - 0.8) < 1e-9);
  bar.setLevel(0.2, 0.03);
  CHECK(std::fabs(bar.level() - 0.2) < 1e-9);
  CHECK(std::fabs(bar.hold() - 0.8) < 1e-9);          // the peak stays held
  bar.setLevel(std::nan(""), 0.03);
  CHECK(bar.level() == 0.0);                           // NaN becomes 0
  bar.setLevel(5.0, 0.03);
  CHECK(bar.level() == 1.0);                           // clamped
}
} // namespace

int main() {
  std::printf("meter control tests\n");
  test_clip_led_click_calls_back_and_lit_state();
  test_clip_led_without_callback_is_harmless();
  test_meter_bar_tracks_level_and_hold();
  std::printf(g_failed ? "FAILED (%d)\n" : "All meter control tests passed\n", g_failed);
  return g_failed ? 1 : 0;
}
