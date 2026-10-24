/**
 * Fairy Tapes · Happy Ending — FINE EQ: six stepped bands on matched biquads (spec §5.1).
 *
 * Band order: 0 HI PASS, 1 LOW SHELF, 2 LO MID, 3 MID, 4 HI MID, 5 HI SHELF. Zero latency,
 * minimum phase. Coefficient changes glide linearly over Tuning::kFineRampSamples samples.
 */

#pragma once

#include "dsp/matched_biquad.h"
#include <array>

namespace FairyTapes::HappyEnding::DSP {

// Effective Q of a bell: the more gain, the narrower the bell.
double effectiveQ(double q, double gainDb);

// Positions of a band's controls (indices into the value tables of params.h).
struct BandSettings {
  bool on = true;
  int ms = 0;          // 0 ST, 1 MID, 2 SIDE: routed by the M/S stage, ignored here for now
  int freqIndex = 0;
  int gainIndex = 8;   // 0 dB
  int qIndex = 1;      // Medium
};

class FineEq {
public:
  static constexpr int kNumBands = 6;

  void prepare(double sampleRate, int maxBlock);
  void setBand(int band, const BandSettings& settings);
  void setStageOn(bool on);
  void process(float* left, float* right, int n);

  // Response in dB of the whole stage at `hz`; target 0 = ST. MID and SIDE curves come with the
  // M/S routing: they read flat until then.
  double magnitudeDb(int target, double hz) const;

private:
  struct Band {
    BandSettings settings;
    BiquadCoeffs target;
    BiquadCoeffs current;
    BiquadCoeffs step;
    int rampLeft = 0;
    double zl[2] = {0.0, 0.0};
    double zr[2] = {0.0, 0.0};
  };

  void updateTarget(int band);

  double sampleRate_ = 48000.0;
  bool stageOn_ = true;
  std::array<Band, kNumBands> bands_{};
};

} // namespace FairyTapes::HappyEnding::DSP
