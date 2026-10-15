/**
 * Fairy Tapes · Deep Throat — signal chain used by the VST3 Processor (pure, no SDK libs).
 *
 * Per sample (all channels inside one sample step):
 *   input gain -> [breath detector + Rider level meter (200 ms RMS) on the live stereo-linked mono
 *   (L+R)/2] -> audio delay D -> Rider gain -> BreathReducer gain -> CorrectiveEq -> DeEsser ->
 *   DistressorComp (parallel Mix inside) -> ToneEq -> output gain.
 *
 * Latency D = lookahead (80 ms) + the spectral detector's frame, the same in every mode (the level
 * detector used in Puke reports 0 and simply gets more look-ahead). The breath probability that
 * drives the reducer and the Rider freeze is the maximum over the look-ahead window, so a breath is
 * covered in the delayed audio from its first to its last sample.
 *
 * Every module keeps running when its IN is off (warm state); IN, Bypass and both Listen switches
 * crossfade linearly over 5 ms. Input and Output gains are smoothed per sample. Bypass outputs the
 * delayed input only (latency unchanged). Breath Listen (precedence) outputs the delayed input times
 * (1 - breath gain); De-ess Listen outputs the band the de-esser removes; the stages after the
 * listened one are not heard.
 */

#pragma once

#include "params.h"
#include "dsp/breath_reducer.h"
#include "dsp/corrective_eq.h"
#include "dsp/deesser.h"
#include "dsp/distressor_comp.h"
#include "dsp/level_breath_detector.h"
#include "dsp/rider.h"
#include "dsp/spectral_breath_detector.h"
#include "dsp/tone_eq.h"

#include <vector>

namespace FairyTapes::DeepThroat {

// loadDefaults(double*) lives in params.h.

struct Settings {
  Mode mode;
  float inputDb, outputDb, riderTargetDb, riderRangeDb, breathAmount, deEssAmount, compAmount, tone;
  float breathSens, deEssFreqHz, compMix, eqAmount;
  int compRatioChoice, compDistChoice, compHpfChoice;
  bool breathListen, deEssListen, bypass;
  bool riderOn, breathOn, eqOn, deEssOn, compOn, toneOn;
};

// Normalized values are clamped to [0,1]; non-finite ones fall back to the defaults.
// riderRangeDb = presetFor(mode).riderMaxRangeDb x Rider Range.
Settings mapParams(const double* values);

constexpr double kLookaheadMs = 80.0;
constexpr double kSwitchFadeMs = 5.0;   // IN, Bypass and Listen crossfades (linear)
constexpr double kGainSmoothMs = 10.0;  // Input / Output gain one-pole
constexpr float kRiderMeterMs = 200.0f;

class Engine {
public:
  Engine();  // prepared at 48 kHz with the default settings

  // Allocates every buffer; never allocates afterwards.
  void prepare(double sampleRate);
  // Clears all audio state; the next setSettings() snaps every smoothed value to its target.
  void reset();
  // Never allocates. Call once per block (values are smoothed inside).
  void setSettings(const Settings& s);
  int latencySamples() const { return latency_; }
  // inR may be null (mono in: inR = inL), outR may be null (mono out). Inputs may alias outputs.
  void process(const float* inL, const float* inR, float* outL, float* outR, int n);

  // Test / metering hooks.
  bool usingLevelDetector() const { return detector_ == &level_; }
  double sampleRate() const { return sr_; }

private:
  struct Ramp {  // linear 0..1 crossfade
    float v = 1.f, target = 1.f;
    float step(float inc) {
      if (v < target) v = v + inc >= target ? target : v + inc;
      else if (v > target) v = v - inc <= target ? target : v - inc;
      return v;
    }
  };

  void applySettings(const Settings& s, bool force);
  void resetBlocks();
  void snapSmoothers();
  float lookaheadMax(float prob);

  DSP::SpectralBreathDetector spectral_;
  DSP::LevelBreathDetector level_;
  DSP::BreathDetector* detector_ = &spectral_;
  DSP::LevelMeter meter_;
  DSP::Rider rider_;
  DSP::BreathReducer reducer_;
  DSP::CorrectiveEq eq_;
  DSP::DeEsser deess_;
  DSP::DistressorComp comp_;
  DSP::ToneEq tone_;

  double sr_ = 48000.0;
  int latency_ = 0;
  int lookahead_ = 0;

  // Audio delay (raw sanitized input), length latency_ + 1.
  std::vector<float> delayL_, delayR_;
  int writePos_ = 0;

  // Sliding maximum of the breath probability over the look-ahead window (monotonic deque).
  std::vector<float> maxVal_;
  std::vector<long long> maxIdx_;
  int maxHead_ = 0, maxCount_ = 0;
  long long sampleIndex_ = 0;

  Settings s_{};
  bool snapPending_ = true;
  float inGain_ = 1.f, inGainTarget_ = 1.f, outGain_ = 1.f, outGainTarget_ = 1.f;
  float gainCoef_ = 0.f;
  float rampInc_ = 0.f;
  Ramp riderIn_, breathIn_, eqIn_, deEssIn_, compIn_, toneIn_;
  Ramp breathListen_, deEssListen_, bypass_;
};

} // namespace FairyTapes::DeepThroat
