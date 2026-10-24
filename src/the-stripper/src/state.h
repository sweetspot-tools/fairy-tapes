/**
 * Fairy Tapes · The Stripper — persistent plugin state (read/write, all versions).
 *
 * Header-only and independent of the VST3 SDK: the functions are templates over a "streamer" that offers
 * readInt32 / readDouble / readRaw and writeInt32 / writeDouble / writeRaw (Steinberg::IBStreamer does,
 * and so does the in-memory stream used by the tests).
 *
 * v8 = the parameters, then the NAM model path (int32 length + UTF-8 bytes, empty when no model).
 * Older versions stay readable; parameters they do not know get their defaults.
 */

#pragma once

#include "params.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace FairyTapes::TheStripper {

constexpr int kStateVersion = 12;
constexpr std::int32_t kMaxModelPathBytes = 4096;
constexpr int kMaxTapePieces = 4;            // pieces of tape whose writing is saved
constexpr std::int32_t kMaxTapeTextBytes = 64;   // 14 characters, UTF-8, with room to spare

/// What is written on one piece of tape: `set` false = never written (the default text is shown), true = the user's text
/// (possibly empty, erased on purpose).
struct TapeText {
  bool set = false;
  std::string text;
};

struct PluginState {
  double params[kNumParams];
  std::string modelPath;
  std::uint32_t wearSeed = 0;          // 0 = none yet (the controller creates one); v10
  std::vector<TapeText> tapeTexts;     // v10
  // v11: the four screws of the panel (angle in degrees, 0 <= a < 360), the recall marks (the parameter values at the last
  // load or "Mark now") and how the marks are set (0 = Auto: at every load, 1 = Manual: only with "Mark now").
  bool hasScrewAngles = false;
  double screwAngles[4] = {0, 0, 0, 0};
  bool recallValid = false;
  double recall[kNumParams] = {};
  int recallMode = 0;
};

/// Default value of every parameter (the table the frozen-parameter test pins down).
inline void fillDefaultParams(double* p) {
  for (int i = 0; i < kNumParams; ++i)
    p[i] = 0.0;
  p[kBypass] = kDefaultBypass;
  p[kStripMode] = kDefaultStripMode;
  p[kInputGain] = kDefaultInputGain;
  p[kOutputGain] = kDefaultOutputGain;
  p[kBypassGainStaging] = kDefaultBypassStage;
  p[kBypassLPHP] = kDefaultBypassStage;
  p[kBypassEQ] = kDefaultBypassStage;
  p[kBypassCompression] = kDefaultBypassStage;
  p[kBypassDrive] = kDefaultBypassStage;
  p[kBypassLimiter] = kDefaultBypassStage;
  p[kBypassGate] = kDefaultBypassStage;
  p[kGainStagingTrim] = kDefaultGainStagingTrim;
  p[kCompressAmount] = kDefaultCompressAmount;
  p[kCompressMix] = kDefaultCompressMix;
  p[kCompressorMode] = kDefaultCompressorMode;
  p[kEqHPFFreq] = kDefaultEqHPFFreq;
  p[kEqHPFOn] = kDefaultEqHPFOn;
  p[kEqLPFFreq] = kDefaultEqLPFFreq;
  p[kEqLPFOn] = kDefaultEqLPFOn;
  p[kEqLFGain] = kDefaultEqLFGain;
  p[kEqLFFreq] = kDefaultEqLFFreq;
  p[kEqLFBell] = kDefaultEqLFBell;
  p[kEqLMGain] = kDefaultEqLMGain;
  p[kEqLMFreq] = kDefaultEqLMFreq;
  p[kEqLMQ] = kDefaultEqLMQ;
  p[kEqHMGain] = kDefaultEqHMGain;
  p[kEqHMFreq] = kDefaultEqHMFreq;
  p[kEqHMQ] = kDefaultEqHMQ;
  p[kEqHFGain] = kDefaultEqHFGain;
  p[kEqHFFreq] = kDefaultEqHFFreq;
  p[kEqHFBell] = kDefaultEqHFBell;
  p[kEqSaturation] = kDefaultEqSaturation;
  p[kDriveAmount] = kDefaultDriveAmount;
  p[kLimiterPound] = kDefaultLimiterPound;
  p[kGateThreshold] = kDefaultGateThreshold;
  p[kDriveNamOn] = kDefaultDriveNamOn;
  p[kDriveTapeOn] = kDefaultDriveTapeOn;
  p[kDriveTapeAmount] = kDefaultDriveTapeAmount;
}

namespace state_detail {
// Parameter counts of the old layouts.
constexpr int kV2 = 18;   // before the 4K-EQ parameters
constexpr int kV3 = 32;   // before Compressor Mode
constexpr int kV4 = 33;   // before kEqType was removed
constexpr int kV5 = 32;   // before the Drive parameters
constexpr int kV6 = 35;   // kDriveAmount + kDriveOutput + kDriveMix
constexpr int kOldEqTypeSlotV4 = 31;
constexpr int kOldDriveOutputSlotV6 = 33;
constexpr int kOldDriveMixSlotV6 = 34;

template <class S>
bool readDoubles(S& s, double* buf, int n) {
  for (int i = 0; i < n; ++i)
    if (!s.readDouble(buf[i]))
      return false;
  return true;
}
} // namespace state_detail

/// Reads any state version (1..12). Returns false for an unknown version or a truncated stream; `out` is then unspecified.
template <class S>
bool readState(S& s, PluginState& out) {
  using namespace state_detail;
  fillDefaultParams(out.params);
  out.modelPath.clear();

  std::int32_t version = 0;
  if (!s.readInt32(version))
    return false;
  if (version < 1 || version > kStateVersion)
    return false;

  double* P = out.params;
  if (version == 4) {
    double buf[kV4];
    if (!readDoubles(s, buf, kV4))
      return false;
    int dst = 0;
    for (int i = 0; i < kV4 && dst < kNumParamsV7; ++i) {
      if (i == kOldEqTypeSlotV4)
        continue;
      P[dst++] = buf[i];
    }
  } else if (version == 5) {
    double buf[kV5];
    if (!readDoubles(s, buf, kV5))
      return false;
    for (int i = 0; i < kV5; ++i)
      P[i] = buf[i];
    P[kDriveAmount] = kDefaultDriveAmount;
  } else if (version == 6) {
    double buf[kV6];
    if (!readDoubles(s, buf, kV6))
      return false;
    int dst = 0;
    for (int i = 0; i < kV6 && dst < kNumParamsV7; ++i) {
      if (i == kOldDriveOutputSlotV6 || i == kOldDriveMixSlotV6)
        continue;
      P[dst++] = buf[i];
    }
  } else if (version == 7) {
    if (!readDoubles(s, P, kNumParamsV7))
      return false;
  } else if (version >= 8) {   // 8..12: parameters, then the model path (10: then seed and tape; 11: screws and recall)
    const int nParams = version == 8 ? kNumParamsV8 : version <= 11 ? kNumParamsV11 : kNumParams;
    if (!readDoubles(s, P, nParams))
      return false;
    std::int32_t len = 0;
    if (!s.readInt32(len) || len < 0 || len > kMaxModelPathBytes)
      return false;
    if (len > 0) {
      out.modelPath.resize(static_cast<std::size_t>(len));
      if (s.readRaw(&out.modelPath[0], len) != len)
        return false;
    }
    if (version >= 10) {
      std::int32_t seed = 0, count = 0;
      if (!s.readInt32(seed) || !s.readInt32(count) || count < 0 || count > kMaxTapePieces)
        return false;
      out.wearSeed = static_cast<std::uint32_t>(seed);
      for (std::int32_t i = 0; i < count; ++i) {
        std::int32_t n = 0;
        if (!s.readInt32(n) || n < -1 || n > kMaxTapeTextBytes)
          return false;
        TapeText t;
        t.set = n >= 0;
        if (n > 0) {
          t.text.resize(static_cast<std::size_t>(n));
          if (s.readRaw(&t.text[0], n) != n)
            return false;
        }
        out.tapeTexts.push_back(std::move(t));
      }
    }
    if (version >= 11) {
      std::int32_t screws = 0;
      if (!s.readInt32(screws) || (screws != 0 && screws != 4))
        return false;
      if (screws == 4) {
        out.hasScrewAngles = true;
        for (int i = 0; i < 4; ++i) {
          double a = 0;
          if (!s.readDouble(a))
            return false;
          if (!std::isfinite(a))
            a = 0.0;
          a = std::fmod(a, 360.0);
          out.screwAngles[i] = a < 0.0 ? a + 360.0 : a;
        }
      }
      std::int32_t recallFlag = 0;
      if (!s.readInt32(recallFlag) || (recallFlag != 0 && recallFlag != 1))
        return false;
      if (recallFlag == 1) {
        out.recallValid = true;
        for (int i = 0; i < nParams; ++i) {
          double v = 0;
          if (!s.readDouble(v))
            return false;
          out.recall[i] = std::isfinite(v) ? std::min(1.0, std::max(0.0, v)) : 0.0;
        }
        for (int i = nParams; i < kNumParams; ++i)
          out.recall[i] = P[i];   // a parameter the state did not know: no mark away from its loaded value
      }
      std::int32_t mode = 0;
      if (!s.readInt32(mode) || (mode != 0 && mode != 1))
        return false;
      out.recallMode = mode;
    }
  } else {
    // v1..v3: read what is available, then fill the rest with defaults
    const int n = (version == 1) ? 3 : (version == 2) ? kV2 : kV3;
    double legacy[kV3];
    if (!readDoubles(s, legacy, n))
      return false;
    double def[kNumParams];
    fillDefaultParams(def);
    for (int i = 0; i < n && i < kNumParamsV7; ++i)
      P[i] = legacy[i];
    if (version == 1) {
      P[kBypass] = 0.0;
      P[kStripMode] = kDefaultStripMode;
      P[kInputGain] = kDefaultInputGain;
      P[kOutputGain] = kDefaultOutputGain;
      for (int i = kBypassGainStaging; i <= kBypassGate; ++i)
        P[i] = kDefaultBypassStage;
      P[kGainStagingTrim] = kDefaultGainStagingTrim;
      P[kCompressAmount] = kDefaultCompressAmount;
      P[kCompressMix] = kDefaultCompressMix;
      P[kCompressorMode] = kDefaultCompressorMode;
    }
    if (version == 2) {
      P[kEqHPFFreq] = legacy[14];
      P[kEqHPFOn] = legacy[15];
      P[kEqLPFFreq] = legacy[16];
      P[kEqLPFOn] = legacy[17];
      P[kCompressorMode] = kDefaultCompressorMode;
    }
    if (version == 3) {
      for (int i = kNumParamsV7 - 1; i >= static_cast<int>(kCompressorMode) + 1; --i)
        P[i] = P[i - 1];
      P[kCompressorMode] = kDefaultCompressorMode;
    }
    for (int i = kV2; i < kNumParamsV7; ++i)
      P[i] = 0.5;
    P[kEqHPFOn] = kDefaultEqHPFOn;
    P[kEqLPFOn] = kDefaultEqLPFOn;
    P[kEqLFBell] = kDefaultEqLFBell;
    P[kEqHFBell] = kDefaultEqHFBell;
    P[kEqSaturation] = kDefaultEqSaturation;
    P[kEqHPFFreq] = kDefaultEqHPFFreq;
    P[kEqLPFFreq] = kDefaultEqLPFFreq;
    P[kEqLFGain] = kDefaultEqLFGain;
    P[kEqLFFreq] = kDefaultEqLFFreq;
    P[kEqLMGain] = kDefaultEqLMGain;
    P[kEqLMFreq] = kDefaultEqLMFreq;
    P[kEqLMQ] = kDefaultEqLMQ;
    P[kEqHMGain] = kDefaultEqHMGain;
    P[kEqHMFreq] = kDefaultEqHMFreq;
    P[kEqHMQ] = kDefaultEqHMQ;
    P[kEqHFGain] = kDefaultEqHFGain;
    P[kEqHFFreq] = kDefaultEqHFFreq;
    P[kDriveAmount] = kDefaultDriveAmount;
    (void)def;
  }
  // Parameters introduced after state v7 keep their defaults when an older state is read.
  if (version < 8) {
    P[kLimiterPound] = kDefaultLimiterPound;
    P[kGateThreshold] = kDefaultGateThreshold;
  }
  if (version < 9)
    P[kDriveNamOn] = kDefaultDriveNamOn;
  if (version < 12) {
    P[kDriveTapeOn] = 0.0;   // TAPE did not exist: off, so the project keeps its sound
    P[kDriveTapeAmount] = kDefaultDriveTapeAmount;
  }
  return true;
}

/// Writes the current version (12).
template <class S>
bool writeState(S& s, const PluginState& in) {
  if (!s.writeInt32(kStateVersion))
    return false;
  for (int i = 0; i < kNumParams; ++i)
    if (!s.writeDouble(in.params[i]))
      return false;
  const std::int32_t len = static_cast<std::int32_t>(in.modelPath.size() > static_cast<std::size_t>(kMaxModelPathBytes) ? kMaxModelPathBytes : in.modelPath.size());
  if (!s.writeInt32(len))
    return false;
  if (len > 0 && s.writeRaw(in.modelPath.data(), len) != len)
    return false;
  if (!s.writeInt32(static_cast<std::int32_t>(in.wearSeed)))
    return false;
  const std::int32_t pieces = static_cast<std::int32_t>(std::min<std::size_t>(in.tapeTexts.size(), kMaxTapePieces));
  if (!s.writeInt32(pieces))
    return false;
  for (std::int32_t i = 0; i < pieces; ++i) {
    const TapeText& t = in.tapeTexts[static_cast<std::size_t>(i)];
    const std::int32_t n = t.set ? static_cast<std::int32_t>(std::min<std::size_t>(t.text.size(), kMaxTapeTextBytes)) : -1;
    if (!s.writeInt32(n))
      return false;
    if (n > 0 && s.writeRaw(t.text.data(), n) != n)
      return false;
  }
  if (!s.writeInt32(in.hasScrewAngles ? 4 : 0))
    return false;
  if (in.hasScrewAngles)
    for (int i = 0; i < 4; ++i)
      if (!s.writeDouble(in.screwAngles[i]))
        return false;
  if (!s.writeInt32(in.recallValid ? 1 : 0))
    return false;
  if (in.recallValid)
    for (int i = 0; i < kNumParams; ++i)
      if (!s.writeDouble(in.recall[i]))
        return false;
  return s.writeInt32(in.recallMode == 1 ? 1 : 0);
}

} // namespace FairyTapes::TheStripper
