/**
 * Fairy Tapes · Deep Throat — values shown by the editor (no VSTGUI, no processing; only the SDK parameter types
 * through params.h, for the DisplayID values).
 * LEVEL meters, clip LEDs and SCOPE are fed by hidden read-only parameters: the engine measures,
 * the processor accumulates between two writes (one per 16 ms of audio) and maps to 0..1.
 */

#pragma once

#include "params.h"
#include "fairy_ui/knob_math.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace FairyTapes::DeepThroat {

// Peaks in dB (floor -120) over the measured span. Input before the Input gain, output after the
// Output gain and Bypass. A clip flag is set by any |sample| >= 1.0 and held for kClipHoldSeconds
// of audio time (or until Engine::clearClips()).
struct LevelValues {
  float inL = -120.f, inR = -120.f, outL = -120.f, outR = -120.f;
  bool clipIn = false, clipOut = false;
};
constexpr double kClipHoldSeconds = 2.0;

struct DisplayValues {
  float voiceDb = -120.f;   // max: the Rider's LevelMeter value
  float riderGainDb = 0.f;  // min: 20 log10 of the Rider gain actually applied (after the IN fade)
  float breathProb = 0.f;   // max: raw detector output (before the look-ahead sliding max)
  float grDb = 0.f;         // max: compressor gain reduction
};

// Allows one display write per kDisplayIntervalMs of audio time; the first block writes.
constexpr double kDisplayIntervalMs = 16.0;
class DisplayRateLimiter {
public:
  void prepare(double sampleRate) {
    interval_ = (long long)((sampleRate > 0.0 ? sampleRate : 48000.0) * 0.001 * kDisplayIntervalMs);
    since_ = interval_;
  }
  // Call once per block with its length; true when a write is due now.
  bool advance(int numSamples) {
    if (numSamples <= 0) return false;
    since_ += numSamples;
    if (since_ < interval_) return false;
    since_ = 0;
    return true;
  }

private:
  long long interval_ = 768, since_ = 768;
};

// Holds the extremes seen between two display writes: max of voice / breath / GR, MIN of the Rider
// gain (so a cut is never missed). take() returns them and resets (no data: neutral values).
class DisplayAccumulator {
public:
  void add(const DisplayValues& d) {
    acc_.voiceDb = std::max(acc_.voiceDb, d.voiceDb);
    acc_.breathProb = std::max(acc_.breathProb, d.breathProb);
    acc_.grDb = std::max(acc_.grDb, d.grDb);
    riderMin_ = std::min(riderMin_, d.riderGainDb);
    any_ = true;
  }
  DisplayValues take() {
    DisplayValues r = acc_;
    r.riderGainDb = any_ ? riderMin_ : 0.f;
    *this = DisplayAccumulator{};
    return r;
  }

private:
  DisplayValues acc_;
  float riderMin_ = 1e9f;
  bool any_ = false;
};

// Holds the extremes seen between two display writes: max of the peaks, OR of the clip flags.
class LevelAccumulator {
public:
  void add(const LevelValues& l) {
    acc_.inL = std::max(acc_.inL, l.inL);
    acc_.inR = std::max(acc_.inR, l.inR);
    acc_.outL = std::max(acc_.outL, l.outL);
    acc_.outR = std::max(acc_.outR, l.outR);
    acc_.clipIn = acc_.clipIn || l.clipIn;
    acc_.clipOut = acc_.clipOut || l.clipOut;
  }
  LevelValues take() {
    const LevelValues r = acc_;
    acc_ = LevelValues{};
    return r;
  }
  void clearClips() { acc_.clipIn = acc_.clipOut = false; }

private:
  LevelValues acc_;
};

// Normalized 0..1 values of the display parameters, in DisplayID order (index = id - kMeterBase).
// Peaks and voice: -60..+6 dB; GR: 0..21 dB; Rider: 0.5 + gain / 12 (-6..+6 dB); breath: the
// probability; clips 0/1. A non-finite value gives 0.
inline std::array<double, kNumDisplayIDs - kMeterBase> displayNorms(const DisplayValues& d,
                                                                    const LevelValues& l) {
  const auto peak = [](float db) {
    return std::isfinite(db) ? Ui::meterNormFromDb((double)db, -60.0, 6.0) : 0.0;
  };
  const auto unit = [](double v) { return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0; };
  std::array<double, kNumDisplayIDs - kMeterBase> n{};
  n[kDispInL - kMeterBase] = peak(l.inL);
  n[kDispInR - kMeterBase] = peak(l.inR);
  n[kDispOutL - kMeterBase] = peak(l.outL);
  n[kDispOutR - kMeterBase] = peak(l.outR);
  n[kDispGr - kMeterBase] = unit((double)d.grDb / 21.0);
  n[kDispVoice - kMeterBase] = peak(d.voiceDb);
  n[kDispRider - kMeterBase] = std::isfinite(d.riderGainDb) ? unit(0.5 + (double)d.riderGainDb / 12.0) : 0.0;
  n[kDispBreath - kMeterBase] = unit((double)d.breathProb);
  n[kDispClipIn - kMeterBase] = l.clipIn ? 1.0 : 0.0;
  n[kDispClipOut - kMeterBase] = l.clipOut ? 1.0 : 0.0;
  return n;
}

} // namespace FairyTapes::DeepThroat
