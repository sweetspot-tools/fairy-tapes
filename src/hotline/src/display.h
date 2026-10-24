/**
 * Fairy Tapes · Hotline — Display publishing helpers (pure, no SDK).
 * The processor writes the hidden meter/clip parameters at most once per kDisplayIntervalMs of audio time; between two
 * writes the accumulator keeps the extremes so a short peak or clip is never missed.
 */

#pragma once

#include "engine.h"
#include <algorithm>

namespace FairyTapes::Hotline {

// Allows one display write per kDisplayIntervalMs of audio time; the first block writes.
constexpr double kDisplayIntervalMs = 16.0;

class DisplayRateLimiter {
public:
  void prepare(double sampleRate) {
    interval_ = static_cast<long long>((sampleRate > 0.0 ? sampleRate : 48000.0) * 0.001 * kDisplayIntervalMs);
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

// Max of the peaks, OR of the clip flags, until take().
class DisplayAccumulator {
public:
  void add(const DisplayValues& d) {
    for (int c = 0; c < 2; ++c) {
      acc_.inPeakDb[c] = std::max(acc_.inPeakDb[c], d.inPeakDb[c]);
      acc_.outPeakDb[c] = std::max(acc_.outPeakDb[c], d.outPeakDb[c]);
    }
    acc_.clipIn = acc_.clipIn || d.clipIn;
    acc_.clipDrive = acc_.clipDrive || d.clipDrive;
    acc_.clipOut = acc_.clipOut || d.clipOut;
  }
  DisplayValues take() {
    const DisplayValues r = acc_;
    acc_ = DisplayValues{{-120.f, -120.f}, {-120.f, -120.f}, false, false, false};
    return r;
  }
  void clearClips() { acc_.clipIn = acc_.clipDrive = acc_.clipOut = false; }

private:
  DisplayValues acc_{{-120.f, -120.f}, {-120.f, -120.f}, false, false, false};
};

} // namespace FairyTapes::Hotline
