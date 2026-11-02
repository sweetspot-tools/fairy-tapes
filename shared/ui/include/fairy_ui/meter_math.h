// Fairy Tapes shared UI: meter display math (LED counts, colours, peak hold), no VSTGUI dependency.
#pragma once

#include "fairy_ui/knob_math.h"
#include "fairy_ui/sanitize.h"

namespace FairyTapes::Ui {

enum class MeterKind { Level, GainReduction };
enum class LedColor { Off, Green, Lime, Yellow, Red, Amber };

// The dB range a level meter's LEDs span: the lowest LED is minDb, the highest maxDb (the same range the processor
// normalizes its peaks with, meterNormFromDb(db, minDb, maxDb)). The default, -60..+6 dBFS, is the one of The Stripper,
// Deep Throat and Hotline; Pillow Talk's meters span -60..0.
struct MeterScale {
  double minDb = -60.0;
  double maxDb = 6.0;
};
inline constexpr MeterScale kDefaultMeterScale{};

// A scale with a finite minDb below a finite maxDb, else the default.
inline constexpr MeterScale sanitizeMeterScale(MeterScale s) {
  const bool finite = s.minDb == s.minDb && s.maxDb == s.maxDb && s.minDb > -1e9 && s.maxDb < 1e9;
  return finite && s.maxDb > s.minDb ? s : kDefaultMeterScale;
}

// How many of `count` LEDs are lit for a normalized value (nearest LED; NaN and out-of-range safe).
inline constexpr int ledsLit(double norm, int count) {
  if (count <= 0) return 0;
  const double v = sanitizeNorm(norm, 0.0);
  const int n = static_cast<int>(v * count + 0.5);
  return n < 0 ? 0 : (n > count ? count : n);
}

// Colour of LED `index` (0 = lowest) when lit. Level: the LEDs span the meter's scale (default -60..+6 dBFS) and the
// colour follows the dB value of the LED: green below -18, lime to -9, yellow to 0, red from 0 dB. On a -60..0 meter only
// the top LED is red. GainReduction: amber, the last four of fifteen (about the last quarter) red; the scale is not used.
inline constexpr LedColor ledColor(int index, int count, MeterKind kind, MeterScale scale = kDefaultMeterScale) {
  if (count <= 1) return kind == MeterKind::Level ? LedColor::Green : LedColor::Amber;
  if (kind == MeterKind::GainReduction) {
    const double pos = static_cast<double>(index) / (count - 1);   // 0 = lowest LED, 1 = highest
    return pos > 0.72 ? LedColor::Red : LedColor::Amber;
  }
  const MeterScale s = sanitizeMeterScale(scale);
  const double db = s.minDb + (s.maxDb - s.minDb) * index / (count - 1);
  constexpr double kEps = 1e-9;   // an LED that sits on a threshold takes the upper colour
  if (db >= 0.0 - kEps) return LedColor::Red;
  if (db >= -9.0 - kEps) return LedColor::Yellow;
  if (db >= -18.0 - kEps) return LedColor::Lime;
  return LedColor::Green;
}

// UI-side peak-hold marker on a normalized value: holds a new peak for 1.5 s, then falls (20 dB/s of the meter's scale)
// until it meets the live value. Never below the live value. Call from the UI timer.
class PeakHold {
public:
  static constexpr double kHoldSeconds = 1.5;
  static constexpr double kFallDbPerSecond = 20.0;

  // The dB range the normalized values span (default -60..+6): the fall in dB/s is turned into bar per second with it.
  // A scale that makes no sense keeps the default. The dB/s setting is kept.
  void setScale(MeterScale scale) { scale_ = sanitizeMeterScale(scale); }
  MeterScale scale() const { return scale_; }

  // User settings (METER HOLD / METER RELEASE): hold time in seconds (0 = no hold marker, >= kForeverSeconds = held until
  // a higher peak) and the fall in dB per second of the meter's scale. Values that are not finite or not positive keep the
  // defaults.
  static constexpr double kForeverSeconds = 10.0;
  void configure(double holdSeconds, double fallDbPerSecond) {
    holdSeconds_ = (holdSeconds == holdSeconds && holdSeconds >= 0.0) ? holdSeconds : kHoldSeconds;
    fallDbPerSecond_ = (fallDbPerSecond == fallDbPerSecond && fallDbPerSecond > 0.0) ? fallDbPerSecond : kFallDbPerSecond;
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
      hold_ -= fallDbPerSecond_ / (scale_.maxDb - scale_.minDb) * dt;
      if (hold_ < v) hold_ = v;
    }
  }
  double hold() const { return hold_; }
  void reset() { hold_ = 0.0; timeLeft_ = 0.0; }

private:
  double hold_ = 0.0;
  double timeLeft_ = 0.0;
  double holdSeconds_ = kHoldSeconds;
  double fallDbPerSecond_ = kFallDbPerSecond;
  MeterScale scale_ = kDefaultMeterScale;
};

} // namespace FairyTapes::Ui
