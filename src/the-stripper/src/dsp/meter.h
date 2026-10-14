/**
 * Fairy Tapes · The Stripper — metering helpers (header-only, no allocation, safe on the audio thread).
 *
 *  - PeakMeter:        block peak with instant attack, 20 dB/s release and a 1.5 s peak hold.
 *  - ClipDetector:     lit for 2 s after a sample with |x| >= 1.0, until cleared.
 *  - GainReductionMeter: gain reduction (dB, >= 0) from the in/out peaks of a stage; instant attack, 60 dB/s release.
 *  - UpdateLimiter:    lets a value be written to the host at most once per interval (16 ms by default).
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace FairyTapes::TheStripper::DSP {

constexpr float kMeterFloorDb = -120.0f;

inline float linToDb(float x) {
  return x > 1e-6f ? 20.0f * std::log10(x) : kMeterFloorDb;
}

inline float peakOf(const float* x, int n) {
  float p = 0.0f;
  for (int i = 0; i < n; ++i)
    p = std::max(p, std::fabs(x[i]));
  return p;
}

inline float peakOf(const float* l, const float* r, int n) {
  return std::max(peakOf(l, n), peakOf(r, n));
}

/// dB value -> 0..1 between minDb and maxDb (linear in dB, clamped).
inline double meterNormFromDb(double db, double minDb, double maxDb) {
  if (maxDb <= minDb)
    return 0.0;
  return std::clamp((db - minDb) / (maxDb - minDb), 0.0, 1.0);
}

/// Gain reduction as SSL 360 Link reads read-only VST3 meter parameters: 0 = none, 1 = full scale reduced to
/// silence; the host side computes dB = 20 * log10(1 - value). Capped just below 1 so that stays finite.
inline double grValueFromDb(double db) {
  if (!(db > 0.0))
    return 0.0;   // also NaN
  return std::min(1.0 - 1e-9, 1.0 - std::pow(10.0, -db / 20.0));
}
inline double grDbFromValue(double value) {
  if (!(value > 0.0))
    return 0.0;   // also NaN
  return -20.0 * std::log10(1.0 - std::min(value, 1.0 - 1e-9));
}

class PeakMeter {
public:
  void prepare(double sampleRate) {
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
  }
  void reset() {
    level_ = kMeterFloorDb;
    hold_ = kMeterFloorDb;
    holdLeft_ = 0.0;
  }
  /// Feeds one block; returns the block peak (linear).
  float process(const float* x, int n) {
    const float peak = peakOf(x, n);
    const float pdb = linToDb(peak);
    const float fall = static_cast<float>(kReleaseDbPerSec * n / sr_);
    level_ = std::max(pdb, level_ - fall);
    if (pdb >= hold_) {
      hold_ = pdb;
      holdLeft_ = kHoldSeconds * sr_;
    } else if (holdLeft_ > 0.0) {
      holdLeft_ -= n;
    } else {
      hold_ = std::max(level_, hold_ - fall);   // the hold marker falls after its time
    }
    return peak;
  }
  float levelDb() const { return level_; }
  float holdDb() const { return hold_; }

private:
  static constexpr double kReleaseDbPerSec = 20.0;
  static constexpr double kHoldSeconds = 1.5;
  double sr_{48000.0};
  float level_{kMeterFloorDb};
  float hold_{kMeterFloorDb};
  double holdLeft_{0.0};
};

class ClipDetector {
public:
  void prepare(double sampleRate) {
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    clear();
  }
  void process(const float* x, int n) {
    if (peakOf(x, n) >= 1.0f)
      left_ = kHoldSeconds * sr_;
    else if (left_ > 0.0)
      left_ -= n;
  }
  void process(const float* l, const float* r, int n) {
    if (std::max(peakOf(l, n), peakOf(r, n)) >= 1.0f)
      left_ = kHoldSeconds * sr_;
    else if (left_ > 0.0)
      left_ -= n;
  }
  bool lit() const { return left_ > 0.0; }
  void clear() { left_ = 0.0; }

private:
  static constexpr double kHoldSeconds = 2.0;
  double sr_{48000.0};
  double left_{0.0};
};

class GainReductionMeter {
public:
  void prepare(double sampleRate) {
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    gr_ = 0.0f;
  }
  /// Gain reduction in dB (>= 0) from the peaks of the stage's input and output blocks.
  float update(float inPeak, float outPeak, int n) {
    float target = 0.0f;
    if (inPeak > 1e-6f)
      target = std::max(0.0f, linToDb(inPeak) - linToDb(std::max(outPeak, 1e-6f)));
    const float fall = static_cast<float>(release_ * n / sr_);
    gr_ = std::max(target, gr_ - fall);
    return gr_;
  }
  /// Same smoothing, for a gain reduction (dB) the stage reports itself.
  float updateDb(float grDb, int n) {
    const float fall = static_cast<float>(release_ * n / sr_);
    gr_ = std::max(std::max(0.0f, grDb), gr_ - fall);
    return gr_;
  }
  /// How fast the read-out falls (dB/s). Default 60; the gate meter uses a much faster one. Values <= 0 keep the default.
  void setReleaseDbPerSec(double dbPerSec) { release_ = dbPerSec > 0.0 ? dbPerSec : kDefaultReleaseDbPerSec; }
  void reset() { gr_ = 0.0f; }
  float value() const { return gr_; }

private:
  static constexpr double kDefaultReleaseDbPerSec = 60.0;
  double release_{kDefaultReleaseDbPerSec};
  double sr_{48000.0};
  float gr_{0.0f};
};

class UpdateLimiter {
public:
  void prepare(double sampleRate, double intervalMs = 16.0) {
    interval_ = (sampleRate > 0.0 ? sampleRate : 48000.0) * intervalMs / 1000.0;
    acc_ = interval_;   // the first call reports at once
  }
  /// True when the interval has elapsed (call once per block).
  bool advance(int n) {
    acc_ += n;
    if (acc_ >= interval_) {
      acc_ = 0.0;   // the next one is a full interval away: two reports are never closer than the interval
      return true;
    }
    return false;
  }

private:
  double interval_{768.0};
  double acc_{0.0};
};

} // namespace FairyTapes::TheStripper::DSP
