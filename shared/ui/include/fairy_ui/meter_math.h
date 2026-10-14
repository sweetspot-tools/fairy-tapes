// Fairy Tapes shared UI: meter display math (LED counts, colours, peak hold), no VSTGUI dependency.
#pragma once

#include "fairy_ui/knob_math.h"
#include "fairy_ui/sanitize.h"

namespace FairyTapes::Ui {

enum class MeterKind { Level, GainReduction };
enum class LedColor { Off, Green, Lime, Yellow, Red, Amber };

// How many of `count` LEDs are lit for a normalized value (nearest LED; NaN and out-of-range safe).
inline constexpr int ledsLit(double norm, int count) {
  if (count <= 0) return 0;
  const double v = sanitizeNorm(norm, 0.0);
  const int n = static_cast<int>(v * count + 0.5);
  return n < 0 ? 0 : (n > count ? count : n);
}

// Colour of LED `index` (0 = lowest) when lit. Level: the LEDs span -60..+6 dBFS: green to -18, lime to -9,
// yellow to 0, red from 0. GainReduction: amber, the last four of fifteen (about the last quarter) red.
inline constexpr LedColor ledColor(int index, int count, MeterKind kind) {
  if (count <= 1) return kind == MeterKind::Level ? LedColor::Green : LedColor::Amber;
  const double pos = static_cast<double>(index) / (count - 1);   // 0 = lowest LED, 1 = highest
  if (kind == MeterKind::GainReduction) return pos > 0.72 ? LedColor::Red : LedColor::Amber;
  const double db = -60.0 + pos * 66.0;
  if (db >= 0.0) return LedColor::Red;
  if (db >= -9.0) return LedColor::Yellow;
  if (db >= -18.0) return LedColor::Lime;
  return LedColor::Green;
}

// UI-side peak-hold marker on a normalized value: holds a new peak for 1.5 s, then falls (about 20 dB/s of the
// -60..+6 dB range) until it meets the live value. Never below the live value. Call from the UI timer.
class PeakHold {
public:
  static constexpr double kHoldSeconds = 1.5;
  static constexpr double kFallPerSecond = 20.0 / 66.0;

  // User settings (METER HOLD / METER RELEASE): hold time in seconds (0 = no hold marker, >= kForeverSeconds = held until
  // a higher peak) and the fall in dB per second of the -60..+6 range. Values that are not finite or not positive keep the
  // defaults.
  static constexpr double kForeverSeconds = 10.0;
  void configure(double holdSeconds, double fallDbPerSecond) {
    holdSeconds_ = (holdSeconds == holdSeconds && holdSeconds >= 0.0) ? holdSeconds : kHoldSeconds;
    fallPerSecond_ = (fallDbPerSecond == fallDbPerSecond && fallDbPerSecond > 0.0) ? fallDbPerSecond / 66.0 : kFallPerSecond;
  }
  bool holdEnabled() const { return holdSeconds_ > 0.0; }

  void update(double live, double dtSeconds) {
    const double v = sanitizeNorm(live, 0.0);
    const double dt = (dtSeconds == dtSeconds && dtSeconds > 0.0 && dtSeconds < 10.0) ? dtSeconds : 0.0;
    if (holdSeconds_ <= 0.0) {   // hold off: the marker is the live value
      hold_ = v;
      timeLeft_ = 0.0;
      return;
    }
    if (v >= hold_) {
      hold_ = v;
      timeLeft_ = holdSeconds_;
    } else if (holdSeconds_ >= kForeverSeconds) {
      // held until a higher peak
    } else if (timeLeft_ > 0.0) {
      timeLeft_ -= dt;
    } else {
      hold_ -= fallPerSecond_ * dt;
      if (hold_ < v) hold_ = v;
    }
  }
  double hold() const { return hold_; }
  void reset() { hold_ = 0.0; timeLeft_ = 0.0; }

private:
  double hold_ = 0.0;
  double timeLeft_ = 0.0;
  double holdSeconds_ = kHoldSeconds;
  double fallPerSecond_ = kFallPerSecond;
};

} // namespace FairyTapes::Ui
