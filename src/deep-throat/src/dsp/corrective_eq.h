// Fairy Tapes · Deep Throat — corrective EQ: high-pass, 300 Hz mud cut and a dynamic notch that
// follows the most prominent resonance between 1 and 6 kHz. No VST3 SDK dependency.
//
// process(channel, x) is called per sample. Channels must be fed interleaved (all channels of
// sample n before sample n + 1). The notch analysis runs on the mono sum of the channels, every
// 50 ms of samples (so the output does not depend on block splits), and every runtime
// coefficient change is smoothed (no clicks).
#pragma once

#include "params.h"
#include "dsp/biquad.h"
#include "dsp/fft.h"

#include <vector>

namespace FairyTapes::DeepThroat::DSP {

class CorrectiveEq {
public:
  void prepare(double sampleRate, int channels);
  void reset();
  void setMode(Mode mode);
  void setAmount(float amount01);  // 0..1, default 0.5 = base values
  float process(int channel, float x);

private:
  struct Channel {
    Biquad hpf, mud, notch;
    double hpfHz = 80.0, mudDb = 0.0, notchHz = 3000.0, notchDb = 0.0;  // smoothed, applied
    int counter = 0;           // samples since the last coefficient refresh
    long long samples = 0;     // samples processed on this channel
    std::vector<float> ring;   // analysis history of this channel
  };

  void updateTargets();
  void refresh(Channel& c);
  void snap(Channel& c);
  void analyze();

  double sr_ = 48000.0;
  Mode mode_ = Mode::Speak;
  float amount_ = 0.5f;
  double targetHpfHz_ = 80.0, targetMudDb_ = -2.0, maxNotchDb_ = 6.0;
  double notchTargetHz_ = 3000.0, notchTargetDb_ = 0.0;  // written by analyze()

  int refreshInterval_ = 16;
  long long hop_ = 2400;
  int fftSize_ = 2048;
  double attackCoeff_ = 0.0, releaseCoeff_ = 0.0, smoothCoeff_ = 0.0;
  std::vector<Channel> ch_;
  Fft fft_;
  std::vector<float> window_, re_, im_, power_;
};

} // namespace FairyTapes::DeepThroat::DSP
