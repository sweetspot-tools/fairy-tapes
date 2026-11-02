/**
 * Fairy Tapes · Happy Ending — Display parameters: normalization and the audio-side hold (pure, no SDK).
 *
 * The processor publishes the meters to the editor as hidden read-only parameters (params.h,
 * DisplayID) about every kDisplayIntervalMs. Between two writes the extremes are held, so a transient
 * that falls between two UI ticks is still seen:
 *  - peaks and GR rows: the maximum since the last write;
 *  - clip LEDs: lit if lit at any point since the last write (the engine already holds them 2 s);
 *  - LUFS and DYN: the latest value;
 *  - stage inputs and the GLUE reduction: the maximum since the last write; the LIMIT threshold: the latest.
 * Normalizations (the editor reads them back with the inverse functions):
 *  - peaks: dB over -60..0 (the suite's LEVEL scale), GR: dB over 0..12 (the five GR rows);
 *  - LUFS: over -60..0, "no measurement yet" (-inf, below -60) is 0;
 *  - stage inputs: dB over -60..+12, the LIMIT threshold: dB over -12..0 (the stage off, -120, is 0);
 *  - latency: samples over 0..kLatencyDisplayMax.
 */

#pragma once

#include "engine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace FairyTapes::HappyEnding {

constexpr double kDisplayIntervalMs = 33.0;  // ~30 writes per second
constexpr double kPeakDisplayMinDb = -60.0, kPeakDisplayMaxDb = 0.0;
constexpr double kGrDisplayMaxDb = 12.0;
constexpr double kStageDisplayMinDb = -60.0, kStageDisplayMaxDb = 12.0;  // stage inputs: headroom above 0 dBFS (LOUDNESS, drive)
constexpr double kThreshDisplayMinDb = -12.0, kThreshDisplayMaxDb = 0.0;  // LIMIT threshold: THRESH 0..-4, LOUDNESS push, SET -8
constexpr double kLufsDisplayMin = -60.0, kLufsDisplayMax = 0.0;
constexpr double kLatencyDisplayMax = 16384.0;

inline double normFromPeakDb(double db) {
  if (!(db > kPeakDisplayMinDb)) return 0.0;  // also NaN
  return std::min(1.0, (db - kPeakDisplayMinDb) / (kPeakDisplayMaxDb - kPeakDisplayMinDb));
}
inline double peakDbFromNorm(double n) {
  return kPeakDisplayMinDb + std::clamp(n, 0.0, 1.0) * (kPeakDisplayMaxDb - kPeakDisplayMinDb);
}
inline double normFromGrDb(double db) {
  if (!(db > 0.0)) return 0.0;
  return std::min(1.0, db / kGrDisplayMaxDb);
}
inline double grDbFromNorm(double n) { return std::clamp(n, 0.0, 1.0) * kGrDisplayMaxDb; }

inline double normFromStageDb(double db) {
  if (!(db > kStageDisplayMinDb)) return 0.0;  // also NaN and -120 (stage off)
  return std::min(1.0, (db - kStageDisplayMinDb) / (kStageDisplayMaxDb - kStageDisplayMinDb));
}
inline double stageDbFromNorm(double n) {
  return kStageDisplayMinDb + std::clamp(n, 0.0, 1.0) * (kStageDisplayMaxDb - kStageDisplayMinDb);
}
inline double normFromThreshDb(double db) {
  if (!(db > kThreshDisplayMinDb)) return 0.0;  // also NaN
  return std::min(1.0, (db - kThreshDisplayMinDb) / (kThreshDisplayMaxDb - kThreshDisplayMinDb));
}
inline double threshDbFromNorm(double n) {
  return kThreshDisplayMinDb + std::clamp(n, 0.0, 1.0) * (kThreshDisplayMaxDb - kThreshDisplayMinDb);
}

inline double normFromLufs(double lufs) {
  if (!(lufs > kLufsDisplayMin)) return 0.0;  // -inf, NaN and anything at or below the floor
  return std::min(1.0, (lufs - kLufsDisplayMin) / (kLufsDisplayMax - kLufsDisplayMin));
}
// 0 means "no measurement": -inf (MATCH leaves LOUDNESS alone).
inline double lufsFromNorm(double n) {
  if (!(n > 0.0)) return -std::numeric_limits<double>::infinity();
  return kLufsDisplayMin + std::min(n, 1.0) * (kLufsDisplayMax - kLufsDisplayMin);
}

inline double normFromLatency(int samples) {
  return std::clamp(static_cast<double>(samples) / kLatencyDisplayMax, 0.0, 1.0);
}
inline int latencyFromNorm(double n) {
  return static_cast<int>(std::lround(std::clamp(n, 0.0, 1.0) * kLatencyDisplayMax));
}

// A paced "is a write due" test, one call per block.
class DisplayRateLimiter {
public:
  void prepare(double sampleRate) {
    interval_ = static_cast<long long>((sampleRate > 0.0 ? sampleRate : 48000.0) * 0.001 * kDisplayIntervalMs);
    since_ = interval_;  // the first block writes
  }
  bool advance(int numSamples) {
    if (numSamples <= 0) return false;
    since_ += numSamples;
    if (since_ < interval_) return false;
    since_ = 0;
    return true;
  }

private:
  long long interval_ = 1584, since_ = 1584;
};

// What one write carries (dB for the levels, LUFS already corrected for a mono bus).
struct DisplayFrame {
  float inPeakDb[2] = {-120.0f, -120.0f};
  float outPeakDb[2] = {-120.0f, -120.0f};
  float grDb[kNumGrRows] = {};
  bool clip[kNumLeds] = {};
  double lufsShort = -std::numeric_limits<double>::infinity();
  double lufsIntegrated = -std::numeric_limits<double>::infinity();
  float dynLow = 0.0f, dynAir = 0.0f;
  float stageInDb[kNumStageIns] = {-120.0f, -120.0f, -120.0f, -120.0f};
  float glueGrDb = 0.0f;
  float limitThreshDb = 0.0f;
};

class MeterHold {
public:
  // lufsOffsetDb: added to finite LUFS readings (the dual-mono correction on a mono bus).
  void add(const Meters& m, double lufsOffsetDb) {
    for (int c = 0; c < 2; ++c) {
      f_.inPeakDb[c] = std::max(f_.inPeakDb[c], m.inPeakDb[c]);
      f_.outPeakDb[c] = std::max(f_.outPeakDb[c], m.outPeakDb[c]);
    }
    for (int r = 0; r < kNumGrRows; ++r) f_.grDb[r] = std::max(f_.grDb[r], m.grDb[r]);
    for (int l = 0; l < kNumLeds; ++l) f_.clip[l] = f_.clip[l] || m.clip[l];
    f_.lufsShort = std::isfinite(m.lufsShort) ? m.lufsShort + lufsOffsetDb : m.lufsShort;
    f_.lufsIntegrated = std::isfinite(m.lufsIntegrated) ? m.lufsIntegrated + lufsOffsetDb : m.lufsIntegrated;
    f_.dynLow = m.dynLow;
    f_.dynAir = m.dynAir;
    for (int k = 0; k < kNumStageIns; ++k) f_.stageInDb[k] = std::max(f_.stageInDb[k], m.stageInDb[k]);
    f_.glueGrDb = std::max(f_.glueGrDb, m.glueGrDb);
    f_.limitThreshDb = m.limitThreshDb;
  }
  // Returns what was held and restarts the hold (the latest LUFS / DYN are kept for the next write).
  DisplayFrame take() {
    const DisplayFrame out = f_;
    DisplayFrame fresh;
    fresh.lufsShort = f_.lufsShort;
    fresh.lufsIntegrated = f_.lufsIntegrated;
    fresh.dynLow = f_.dynLow;
    fresh.dynAir = f_.dynAir;
    fresh.limitThreshDb = f_.limitThreshDb;
    f_ = fresh;
    return out;
  }
  void clearClips() { for (bool& c : f_.clip) c = false; }

private:
  DisplayFrame f_;
};

} // namespace FairyTapes::HappyEnding
