// Fairy Tapes — Deep Throat: per-mode preset constants shared by the engine. Header-only.
// The Rider Range parameter (0..1) scales riderMaxRangeDb.
#pragma once

#include "params.h"

namespace FairyTapes::DeepThroat {

struct ModePreset {
  float riderMaxRangeDb;
  float riderSlewDbPerSec;
};

constexpr ModePreset kModePresets[3] = {
    {6.0f, 8.0f},  // Speak
    {4.0f, 4.0f},  // Sing
    {3.0f, 2.0f},  // Puke
};

constexpr const ModePreset& presetFor(Mode mode) {
  const int i = static_cast<int>(mode);
  return kModePresets[i < 0 ? 0 : (i > 2 ? 2 : i)];
}

}  // namespace FairyTapes::DeepThroat
