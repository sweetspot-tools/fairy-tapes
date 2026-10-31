/**
 * Fairy Tapes · Happy Ending — the texts of the parameter values and the parsing of typed values.
 * Header-only, pure (no VSTGUI, no SDK beyond the ParamID types of params.h). One formatter for the whole plug-in:
 * the editor prints formatValue() under every control and the controller's getParamStringByValue /
 * getParamValueByString call these same functions, so the host and the panel always say the same thing.
 * Tested in tests/test_ui.cpp (the texts) and tests/test_controller.cpp (the controller agrees).
 */

#pragma once

#include "params.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace FairyTapes::HappyEnding::Ui {

namespace detail {

inline std::string fmt(const char* f, double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, f, v);
  return buf;
}

inline std::string freqText(double hz) {
  if (hz < 1000.0) return fmt("%g Hz", hz);
  return fmt("%g kHz", hz / 1000.0);
}

inline bool isSwitch(ParamID id) { return kParams[id].steps == 2 && id != kFinalMode; }

// The text of position `index`. hpMs: the current HI PASS M/S position (SIDE reads the mono-bass scale).
inline std::string paramText(ParamID id, int index, int hpMs) {
  const int last = kParams[id].steps - 1;
  index = std::clamp(index, 0, last);
  if (isSwitch(id)) return index ? "On" : "Off";
  const ValueTable t = valueTable(id);
  const double v = t.size ? t.data[index] : 0.0;
  switch (id) {
    case kInputGain: case kOutputGain:
    case kLsGain: case kLmGain: case kMidGain: case kHmGain: case kHsGain:
    case kTilt:
      return fmt("%+.1f dB", v);
    case kCeiling: case kGlueMakeup: case kClipDrive: case kLimitThresh: case kFinalDrive: case kPush:
      return fmt("%.1f dB", v);
    case kGlueThresh:
      return fmt("%.0f dB", v);
    case kLoudness:
      return fmt("%.1f", v);
    case kLow: case kAir: case kTube:
      return fmt("%.0f", v);
    case kTarget:
      return fmt("%.0f LUFS", v);
    case kOversampling: {
      static const char* const names[3] = {"Off", "2x", "4x"};
      return names[index];
    }
    case kHpMs: case kLsMs: case kLmMs: case kMidMs: case kHmMs: case kHsMs: {
      static const char* const names[3] = {"Stereo", "Mid", "Side"};
      return names[index];
    }
    case kHpFreq: {
      const double hz = hpFreqHz(hpMs == 2, index);
      return hz <= 0.0 ? "Off" : freqText(hz);
    }
    case kLsFreq: case kLmFreq: case kMidFreq: case kHmFreq: case kHsFreq:
    case kLowFreq: case kAirFreq:
      return freqText(v);
    case kLmQ: case kMidQ: case kHmQ: {
      static const char* const names[4] = {"Wide", "Medium", "Narrow", "Surgical"};
      return names[index];
    }
    case kGlueAttack:  return fmt("%g ms", v);
    case kGlueRelease: return v <= 0.0 ? "Auto" : fmt("%g s", v);
    case kGlueRatio:   return fmt("%g:1", v);
    case kGlueMix: case kFinalEffect: case kFinalCurve:
      return fmt("%.0f %%", v);
    case kGlueScHpf:   return v <= 0.0 ? "Off" : freqText(v);
    case kLimitStyle: {
      static const char* const names[3] = {"Modern", "Punch", "Dyn"};
      return names[index];
    }
    case kLimitRelease: return index == 4 ? "Style" : fmt("%+.0f", static_cast<double>(index - 4));
    case kFinalMode:   return index ? "Clip" : "Inflate";
    default:           return fmt("%.0f", static_cast<double>(index));
  }
}

// What a typed text is compared on: no spaces, lower case.
inline std::string lowered(std::string s) {
  s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// HI PASS M/S position from a table of normalized values (nullptr: STEREO, the normal scale).
inline int hpMsOf(const double* allNorms) {
  return allNorms ? indexFromNorm(kHpMs, allNorms[kHpMs]) : 0;
}

} // namespace detail

// The text printed under a control for the normalized value `norm` (clamped to 0..1). allNorms: the whole table of
// normalized values (kNumParams entries, ParamID order); only HI PASS FREQ reads it, for the scale of its own M/S.
// nullptr means the normal scale. An id outside the stored table gives an empty string.
inline std::string formatValue(ParamID id, double norm, const double* allNorms) {
  if (id >= kNumParams) return {};
  return detail::paramText(id, indexFromNorm(id, norm), detail::hpMsOf(allNorms));
}

// A typed value: the text of a step (case and spaces ignored) or a bare number, which picks the nearest step of the
// table in use (HI PASS FREQ on the scale of its M/S: typing "60" on SIDE gives the 60 Hz step). On success `norm` is
// the normalized value of the step and the result is true; any other text gives false and leaves `norm` alone.
inline bool parseValue(ParamID id, const std::string& text, const double* allNorms, double& norm) {
  if (id >= kNumParams) return false;
  const int hpMs = detail::hpMsOf(allNorms);
  const std::string want = detail::lowered(text);
  const int steps = kParams[id].steps;
  for (int i = 0; i < steps; ++i)
    if (detail::lowered(detail::paramText(id, i, hpMs)) == want) {
      norm = normFromIndex(id, i);
      return true;
    }
  // A bare number picks the nearest value of the table (e.g. "-3" for a gain)
  const ValueTable t = valueTable(id);
  char* end = nullptr;
  const double num = std::strtod(text.c_str(), &end);
  if (t.size && end != text.c_str() && std::isfinite(num)) {
    // HI PASS FREQ is read on the scale of the current HI PASS M/S (SIDE has the mono-bass scale)
    const auto valueAt = [&](int i) { return id == kHpFreq ? hpFreqHz(hpMs == 2, i) : t.data[i]; };
    int best = 0;
    for (int i = 1; i < steps; ++i)
      if (std::fabs(valueAt(i) - num) < std::fabs(valueAt(best) - num)) best = i;
    norm = normFromIndex(id, best);
    return true;
  }
  return false;
}

} // namespace FairyTapes::HappyEnding::Ui
