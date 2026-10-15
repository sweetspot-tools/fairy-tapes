// Fairy Tapes · Deep Throat — split-band de-esser. No VST3 SDK dependency.
//
// Complementary crossover: low = Linkwitz-Riley 4th-order low-pass of the input, high = input
// - low, so the output equals the input when the gain is 1. The detector reads the linked mono
// signal through a band-pass 0.62..1.38 x freq; gain reduction (threshold -30 dBFS, 4:1,
// max 12 dB x Amount, attack 1 ms, release 40 ms, smoothed in dB) hits the high band only and is
// the same for all channels. Listen outputs only the removed part: high * (1 - gain).
//
// processFrame() takes one sample per channel and is called per sample.
#pragma once

#include "params.h"
#include "dsp/biquad.h"

#include <vector>

namespace FairyTapes::DeepThroat::DSP {

class DeEsser {
public:
  void prepare(double sampleRate, int channels);
  void reset();
  void setAmount(float amount01);   // 0..1
  void setFreqHz(float hz);         // 4000..10000, default 6500
  void setListen(bool listen) { listen_ = listen; }
  void setMode(Mode mode);
  void processFrame(float* samples);

private:
  struct Channel { Biquad lp1, lp2; };

  void applyFreq(double hz);

  double sr_ = 48000.0;
  Mode mode_ = Mode::Speak;
  bool listen_ = false;
  float amount_ = 0.0f;
  double effAmount_ = 0.0;
  double targetHz_ = 6500.0, hz_ = 6500.0;
  int counter_ = 0, refreshInterval_ = 16;
  double freqCoeff_ = 0.0, powerCoeff_ = 0.0, attackCoeff_ = 0.0, releaseCoeff_ = 0.0;
  double power_ = 0.0, reductionDb_ = 0.0;
  Biquad dhp1_, dhp2_, dlp1_, dlp2_;  // detector band-pass (two cascaded 2nd-order HP and LP)
  std::vector<Channel> ch_;
};

}  // namespace FairyTapes::DeepThroat::DSP
