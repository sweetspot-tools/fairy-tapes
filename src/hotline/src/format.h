/**
 * Fairy Tapes · Hotline — Host-visible parameter strings (pure, no SDK).
 */

#pragma once

#include "devices.h"
#include "params.h"
#include <cmath>
#include <cstdio>

namespace FairyTapes::Hotline {

// Formats the normalized value of a persistent parameter into buf (ASCII). Returns false for ids it does not format.
inline bool formatParam(Steinberg::Vst::ParamID id, double v, char* buf, size_t size) {
  switch (id) {
  case kBypass:
  case kMono:
  case kCharacterIn:
    std::snprintf(buf, size, "%s", v >= 0.5 ? "On" : "Off");
    return true;
  case kDevice:
    std::snprintf(buf, size, "%s", kDevices[deviceIndexFromNorm(v)].name);
    return true;
  case kGain:
    std::snprintf(buf, size, "%.1f dB", gainDbFromNorm(v));
    return true;
  case kMorph:
  case kDrive:
  case kNoise:
  case kRoom:
  case kMix:
    std::snprintf(buf, size, "%.0f %%", detail::clamp01(v) * 100.0);
    return true;
  case kTone: {
    const double t = std::round((detail::clamp01(v) - 0.5) * 100.0) / 10.0;   // one decimal, as the preview
    if (t == 0.0) std::snprintf(buf, size, "0.0");
    else std::snprintf(buf, size, "%+.1f", t);
    return true;
  }
  default:
    return false;
  }
}

} // namespace FairyTapes::Hotline
