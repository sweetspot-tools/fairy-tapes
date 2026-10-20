// Fairy Tapes — Deep Throat: one-knob tone EQ (mud 250 Hz, presence 3.5 kHz, air 12 kHz shelf).
// No VST3 SDK dependency.
//
// tone01 = 0.5 is flat; t = (tone01 - 0.5) * 2. The per-mode gains at t = +1 are scaled linearly
// by t (negative t inverts the sign). process(channel, x) is called per sample, all channels
// inside each sample step. Mode/tone changes glide (gains smoothed in dB): no clicks.
// The air shelf corner is clamped to 0.45 x sample rate so it stays below Nyquist at 44.1 kHz.
#pragma once

#include "params.h"
#include "dsp/biquad.h"

#include <vector>

namespace FairyTapes::DeepThroat::DSP {

class ToneEq {
public:
  void prepare(double sampleRate, int channels);
  void reset();
  void setMode(Mode mode);
  void setTone(float tone01);  // 0..1, 0.5 = neutral
  float process(int channel, float x);

private:
  struct Channel {
    Biquad mud, presence, air;
    double mudDb = 0.0, presenceDb = 0.0, airDb = 0.0;  // smoothed, applied
    int counter = 0;
  };

  void updateTargets();
  void snap(Channel& c);
  void refresh(Channel& c);

  double sr_ = 48000.0;
  Mode mode_ = Mode::Speak;
  float tone_ = 0.5f;
  double targetMudDb_ = 0.0, targetPresenceDb_ = 0.0, targetAirDb_ = 0.0;
  double airHz_ = 12000.0;
  int refreshInterval_ = 16;
  double smoothCoeff_ = 0.0;
  std::vector<Channel> ch_;
};

}  // namespace FairyTapes::DeepThroat::DSP
