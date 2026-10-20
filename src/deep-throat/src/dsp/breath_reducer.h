// Fairy Tapes · Deep Throat — BreathReducer: breath probability -> smoothed linear gain.
// No VST3 SDK dependency. No allocation; process() is per-sample.
#pragma once

namespace FairyTapes::DeepThroat::DSP {

// Target dB = -prob * amount * 12 (never below -12 dB), followed with a one-pole smoother
// (attack 5 ms when reducing more, release 30 ms when recovering). No added latency.
class BreathReducer {
public:
  void prepare(double sampleRate);
  void reset();
  void setAmount(float amount01);  // 0..1 -> 0..12 dB maximum reduction
  float process(float breathProb); // returns linear gain

private:
  double attackCoeff_ = 0.0;
  double releaseCoeff_ = 0.0;
  float amount_ = 0.0f;
  double gainDb_ = 0.0;
  float gainLin_ = 1.0f;
};

} // namespace FairyTapes::DeepThroat::DSP
