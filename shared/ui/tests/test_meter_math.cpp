/** Fairy Tapes · shared UI — unit tests for the meter display math (LED counts, colours, peak hold). */
#include "fairy_ui/meter_math.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

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

void test_level_led_colours_on_a_0_dB_meter() {
  // Pillow Talk: 12 LEDs over -60..0 dB (one every 60/11 dB). Same thresholds in dB: only the 0 dB LED is red.
  const MeterScale s{-60.0, 0.0};
  CHECK(ledColor(7, 12, MeterKind::Level, s) == LedColor::Green);     // about -21.8 dB
  CHECK(ledColor(8, 12, MeterKind::Level, s) == LedColor::Lime);      // about -16.4 dB
  CHECK(ledColor(9, 12, MeterKind::Level, s) == LedColor::Lime);      // about -10.9 dB
  CHECK(ledColor(10, 12, MeterKind::Level, s) == LedColor::Yellow);   // about -5.5 dB (red on a -60..+6 meter)
  CHECK(ledColor(11, 12, MeterKind::Level, s) == LedColor::Red);      // 0 dB
  // 21 LEDs, one every 3 dB: the thresholds fall exactly on LEDs and belong to the upper colour
  CHECK(ledColor(13, 21, MeterKind::Level, s) == LedColor::Green);    // -21 dB
  CHECK(ledColor(14, 21, MeterKind::Level, s) == LedColor::Lime);     // -18 dB
  CHECK(ledColor(16, 21, MeterKind::Level, s) == LedColor::Lime);     // -12 dB
  CHECK(ledColor(17, 21, MeterKind::Level, s) == LedColor::Yellow);   // -9 dB
  CHECK(ledColor(19, 21, MeterKind::Level, s) == LedColor::Yellow);   // -3 dB
  CHECK(ledColor(20, 21, MeterKind::Level, s) == LedColor::Red);      // 0 dB
}

void test_default_scale_is_minus_60_to_plus_6() {
  // The plug-ins that pass no scale (The Stripper, Deep Throat, Hotline) keep the colours they had.
  CHECK(kDefaultMeterScale.minDb == -60.0 && kDefaultMeterScale.maxDb == 6.0);
  for (int count : {12, 15, 24})
    for (int i = 0; i < count; ++i) {
      CHECK(ledColor(i, count, MeterKind::Level) == ledColor(i, count, MeterKind::Level, MeterScale{-60.0, 6.0}));
      const double db = -60.0 + 66.0 * i / (count - 1);
      const LedColor want = db >= 0.0 ? LedColor::Red : db >= -9.0 ? LedColor::Yellow : db >= -18.0 ? LedColor::Lime : LedColor::Green;
      CHECK(ledColor(i, count, MeterKind::Level) == want);
    }
  // a scale that makes no sense (empty, reversed, not finite) falls back to the default
  for (MeterScale bad : {MeterScale{0.0, 0.0}, MeterScale{6.0, -60.0}, MeterScale{std::nan(""), 0.0}, MeterScale{-60.0, INFINITY}}) {
    const MeterScale s = sanitizeMeterScale(bad);
    CHECK(s.minDb == -60.0 && s.maxDb == 6.0);
    for (int i = 0; i < 15; ++i) CHECK(ledColor(i, 15, MeterKind::Level, bad) == ledColor(i, 15, MeterKind::Level));
  }
  const MeterScale ok = sanitizeMeterScale(MeterScale{-60.0, 0.0});
  CHECK(ok.minDb == -60.0 && ok.maxDb == 0.0);
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
void test_peak_hold_user_settings() {
  // hold off: the marker is the live value
  { PeakHold h; h.configure(0.0, 20.0); CHECK(!h.holdEnabled()); h.update(0.8, 0.01); h.update(0.3, 0.01); CHECK(near(h.hold(), 0.3)); }
  // 3 seconds: held for 3 s, not 1.5
  { PeakHold h; h.configure(3.0, 20.0); h.update(0.9, 0.01); h.update(0.1, 2.9); CHECK(near(h.hold(), 0.9)); h.update(0.1, 0.5); h.update(0.1, 1.0); CHECK(h.hold() < 0.9); }
  // forever: never falls, until a higher peak
  { PeakHold h; h.configure(PeakHold::kForeverSeconds, 20.0); h.update(0.9, 0.01); for (int i = 0; i < 100; ++i) h.update(0.1, 1.0); CHECK(near(h.hold(), 0.9)); h.update(0.95, 0.01); CHECK(near(h.hold(), 0.95)); }
  // the fall speed: 40 dB/s falls twice as fast as 20 dB/s
  { PeakHold slow, fast; slow.configure(0.5, 20.0); fast.configure(0.5, 40.0);
    slow.update(1.0, 0.01); fast.update(1.0, 0.01); slow.update(0.0, 0.5); fast.update(0.0, 0.5);   // the hold runs out
    slow.update(0.0, 0.5); fast.update(0.0, 0.5);
    const double ds = 1.0 - slow.hold(), df = 1.0 - fast.hold(); CHECK(df > ds * 1.8 && df < ds * 2.2); }
  // the fall is in dB per second of the meter's own scale: 20 dB/s on -60..+6 is 20/66 of the bar per second,
  // on -60..0 it is 20/60 (2026-11-02: Pillow Talk's meters fell at about 18 dB/s)
  { PeakHold d, z; z.setScale(MeterScale{-60.0, 0.0}); d.configure(0.5, 20.0); z.configure(0.5, 20.0);
    d.update(1.0, 0.01); z.update(1.0, 0.01); d.update(0.0, 0.5); z.update(0.0, 0.5);   // the hold runs out
    d.update(0.0, 0.5); z.update(0.0, 0.5);                                               // half a second of fall
    CHECK(near(1.0 - d.hold(), 10.0 / 66.0)); CHECK(near(1.0 - z.hold(), 10.0 / 60.0)); }
  // the default fall (no configure) follows the scale too
  { PeakHold z; z.setScale(MeterScale{-60.0, 0.0}); z.update(1.0, 0.01);
    for (int i = 0; i < 2; ++i) z.update(0.0, 0.75);    // 1.5 s hold
    z.update(0.0, 0.3);                                 // 0.3 s of fall at 20 dB/s
    CHECK(near(1.0 - z.hold(), 6.0 / 60.0)); }
  // setScale after configure keeps the dB/s setting; a nonsense scale keeps the default one
  { PeakHold z; z.configure(0.5, 30.0); z.setScale(MeterScale{-60.0, 0.0}); z.update(1.0, 0.01); z.update(0.0, 0.5); z.update(0.0, 0.5);
    CHECK(near(1.0 - z.hold(), 15.0 / 60.0)); }
  { PeakHold z; z.setScale(MeterScale{1.0, 1.0}); z.update(1.0, 0.01); z.update(0.0, 1.5); z.update(0.0, 0.5);
    CHECK(near(1.0 - z.hold(), 10.0 / 66.0)); CHECK(z.scale().minDb == -60.0 && z.scale().maxDb == 6.0); }
  // nonsense settings keep the defaults
  { PeakHold h; h.configure(std::nan(""), -4.0); CHECK(h.holdEnabled()); h.update(0.5, 0.01); h.update(0.0, 1.0); CHECK(near(h.hold(), 0.5)); }
}

} // namespace

int main() {
  std::printf("meter math tests\n");
  test_leds_lit();
  test_level_led_colours_follow_the_db_scale();
  test_level_led_colours_on_a_0_dB_meter();
  test_default_scale_is_minus_60_to_plus_6();
  test_gain_reduction_led_colours();
  test_peak_hold_holds_then_falls();
  test_peak_hold_is_nan_safe();
  test_peak_hold_user_settings();
  std::printf(g_failed ? "FAILED (%d)\n" : "All meter math tests passed\n", g_failed);
  return g_failed ? 1 : 0;
}
