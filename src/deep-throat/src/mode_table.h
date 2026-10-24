// Fairy Tapes — Deep Throat: the single per-mode table (SPEAK / SING / PUKE).
// Every block and the engine read their per-mode constants from here. Header-only.
#pragma once

#include "params.h"

namespace FairyTapes::DeepThroat {

struct ModeRow {
  const char* name;
  const char* description;
  bool levelDetector;          // true: breath detector is the level one (Puke); false: spectral
  float riderMaxRangeDb;       // Rider Range parameter (0..1) scales this
  float riderSlewDbPerSec;
  double hpfHz;                // corrective EQ high-pass at Amount 50%
  double mudCutDb;             // corrective EQ mud cut at Amount 50%
  double deEssScale;           // multiplier of the De-Ess amount
  double compRatio;            // Auto-ratio of the compressor
  int compDist;                // 1 OFF, 2 Dist 2, 3 Dist 3
  float toneMudDb, tonePresenceDb, toneAirDb;  // tone EQ gains at Tone = +100%
};

inline constexpr ModeRow kModeTable[3] = {
    {"SPEAK", "Spoken word: podcast, voice-over", false, 6.0f, 8.0f, 80.0, -2.0, 1.0, 2.0, 1,
     -2.0f, 3.0f, 2.0f},
    {"SING", "Sung voice", false, 4.0f, 4.0f, 70.0, -3.0, 1.0, 3.0, 2, -1.5f, 2.0f, 4.0f},
    {"PUKE", "Extreme voices: scream, growl", true, 3.0f, 2.0f, 60.0, -4.0, 0.15, 8.0, 3, -3.0f,
     1.0f, 0.0f},
};

constexpr const ModeRow& modeRow(Mode m) {
  const int i = static_cast<int>(m);
  return kModeTable[i < 0 ? 0 : (i > 2 ? 2 : i)];
}

}  // namespace FairyTapes::DeepThroat
