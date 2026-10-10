/** Fairy Tapes · shared UI — unit tests for the meter display math (LED counts, colours, peak hold). */
#include "fairy_ui/meter_math.h"
#include <cmath>
#include <cstdio>

using namespace FairyTapes::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)
bool near(double a, double b, double e = 1e-9) { return std::fabs(a - b) <= e; }

void test_leds_lit() {
  CHECK(ledsLit(0.0, 15) == 0);
  CHECK(ledsLit(1.0, 15) == 15);
  CHECK(ledsLit(0.5, 15) == 8);        // rounds to the nearest LED
  CHECK(ledsLit(-3.0, 15) == 0);       // clamped
  CHECK(ledsLit(7.0, 15) == 15);
  CHECK(ledsLit(std::nan(""), 15) == 0);   // NaN-safe
  CHECK(ledsLit(0.5, 0) == 0);
}

void test_level_led_colours_follow_the_db_scale() {
  // 15 LEDs over -60..+6 dBFS, as in the approved preview: green below -18, lime to -9, yellow to 0, red at 0 and above.
  CHECK(ledColor(0, 15, MeterKind::Level) == LedColor::Green);
  CHECK(ledColor(7, 15, MeterKind::Level) == LedColor::Green);      // about -22 dB
  CHECK(ledColor(9, 15, MeterKind::Level) == LedColor::Lime);       // about -12 dB
  CHECK(ledColor(11, 15, MeterKind::Level) == LedColor::Yellow);    // about -3 dB
  CHECK(ledColor(14, 15, MeterKind::Level) == LedColor::Red);       // +6 dB
}

void test_gain_reduction_led_colours() {
  CHECK(ledColor(0, 15, MeterKind::GainReduction) == LedColor::Amber);
  CHECK(ledColor(10, 15, MeterKind::GainReduction) == LedColor::Amber);
  CHECK(ledColor(11, 15, MeterKind::GainReduction) == LedColor::Red);   // the last stretch warns
}

void test_peak_hold_holds_then_falls() {
  PeakHold h;
  h.update(0.8, 0.033);                         // a peak
  CHECK(near(h.hold(), 0.8));
  for (int i = 0; i < 40; ++i) h.update(0.2, 0.033);    // 1.32 s later, quiet signal
  CHECK(near(h.hold(), 0.8));                   // still held (1.5 s)
  for (int i = 0; i < 12; ++i) h.update(0.2, 0.033);    // 1.7 s
  CHECK(h.hold() < 0.8);                        // starts to fall
  for (int i = 0; i < 100; ++i) h.update(0.2, 0.033);
  CHECK(near(h.hold(), 0.2));                   // never below the live value
  h.update(0.9, 0.033);
  CHECK(near(h.hold(), 0.9));                   // a new peak takes over at once
}

void test_peak_hold_is_nan_safe() {
  PeakHold h;
  h.update(0.5, 0.03);
  h.update(std::nan(""), 0.03);
  CHECK(h.hold() >= 0.0 && h.hold() <= 1.0);
  h.update(0.5, -1.0);                          // negative time does not break the state
  CHECK(h.hold() >= 0.0 && h.hold() <= 1.0);
}
} // namespace

int main() {
  std::printf("meter math tests\n");
  test_leds_lit();
  test_level_led_colours_follow_the_db_scale();
  test_gain_reduction_led_colours();
  test_peak_hold_holds_then_falls();
  test_peak_hold_is_nan_safe();
  std::printf(g_failed ? "FAILED (%d)\n" : "All meter math tests passed\n", g_failed);
  return g_failed ? 1 : 0;
}
