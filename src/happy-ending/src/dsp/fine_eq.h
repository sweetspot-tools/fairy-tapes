/**
 * Fairy Tapes · Happy Ending — FINE EQ: six stepped bands on matched biquads (spec §5.1).
 *
 * Band order: 0 HI PASS, 1 LOW SHELF, 2 LO MID, 3 MID, 4 HI MID, 5 HI SHELF. Zero latency,
 * minimum phase. Coefficient changes glide linearly over Tuning::kFineRampSamples samples.
 *
 * Each band works on ST (left and right), MID or SIDE (one filter on M or S, the other
 * component untouched; no decode when nothing works in M/S). LISTEN solos one bell as a band-pass
 * at its frequency and effective Q, on the route of that bell; the six bands then step to identity.
 * The other M/S component of the LISTEN band-pass fades out and back over the same
 * Tuning::kFineRampSamples samples, and a band whose route changes crossfades from its old route
 * (old coefficients, old memory) to the new one over as many: neither steps. Setters compare with
 * the stored value and return early, so repeating the settings in every block restarts nothing.
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
  int ms = 0;          // 0 ST, 1 MID, 2 SIDE (HI PASS on SIDE reads the mono-bass scale)
  int freqIndex = 0;
  int gainIndex = 8;   // 0 dB
  int qIndex = 1;      // Medium

  bool operator==(const BandSettings&) const = default;
};

class FineEq {
public:
  static constexpr int kNumBands = 6;

  void prepare(double sampleRate, int maxBlock);
  void setBand(int band, const BandSettings& settings);
  void setStageOn(bool on);
  // LISTEN: bell 0-2 (LO MID, MID, HI MID) is soloed, -1 = off. Not a saved state.
  void setListen(int bellOrMinus1);
  void process(float* left, float* right, int n);

  // Response in dB of the stage settings at `hz` (LISTEN does not change it). Target 0 = the
  // bands working on ST; 1 = MID: those plus the bands on MID; 2 = SIDE: those plus the bands on SIDE.
  double magnitudeDb(int target, double hz) const;

private:
  static constexpr int kListenSlot = kNumBands;  // the LISTEN band-pass runs in a seventh slot

  struct Band {
    BandSettings settings;
    int route = 0;          // route in use by the filter below: 0 ST, 1 MID, 2 SIDE
    BiquadCoeffs design;    // response of the settings alone (what the screen draws)
    BiquadCoeffs target;
    BiquadCoeffs current;
    BiquadCoeffs step;
    int rampLeft = 0;
    // LISTEN slot only: how much of the component the band-pass does not work on is muted (0..1).
    double soloLevel = 0.0;
    double soloTarget = 0.0;
    double soloStep = 0.0;
    int soloLeft = 0;
    // Route change in progress: the old route keeps filtering with the coefficients it had, the
    // output moves from it to the new route over fadeLeft more samples.
    int fadeLeft = 0;
    int oldRoute = 0;
    BiquadCoeffs oldCoeffs;
    double oldZl[2] = {0.0, 0.0};
    double oldZr[2] = {0.0, 0.0};
    double zl[2] = {0.0, 0.0};
    double zr[2] = {0.0, 0.0};
  };

  BiquadCoeffs designFor(int band) const;
  static bool atRest(const Band& band);
  void updateTarget(int slot);

  double sampleRate_ = 48000.0;
  bool stageOn_ = true;
  int listen_ = -1;
  std::array<Band, kNumBands + 1> bands_{};
};

} // namespace FairyTapes::HappyEnding::DSP
