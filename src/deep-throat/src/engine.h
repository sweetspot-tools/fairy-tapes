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
 * drives the reducer and the Rider freeze is the maximum of every probability produced since the
 * delayed sample entered the engine (a window of D + 1 values). Both detectors drop their output as
 * soon as the phrase reaches their newest input sample, so with this window the reduction holds
 * to the last sample of the breath in the delayed audio and the reducer's 30 ms release starts at
 * the breath end (measured at 48 kHz: >= 11 dB in every 5 ms of the last 20 ms of a breath, < 1 dB
 * on average over the 30-130 ms after it; about 4 dB is still left 25 ms after the end).
 * Known onset limit (look-ahead minus the detectors' own confirmation time): in Speak/Sing the
 * reduction starts ~25 ms before the breath onset (full at the onset); in Puke the level
 * detector's 80 ms minimum duration makes it start ~25-30 ms after the onset (full ~50 ms in).
 * The Puke level detector needs voice context only to enter a breath, so breaths longer than its
 * 300 ms context are held to their end (>= 11 dB over the last 50 ms of a 500 ms breath); a quiet
 * passage is treated as a breath for at most 1.5 s.
 * A mode switch that changes detector resets the new one: it needs a few hundred ms of phrase
 * context (its phrase-level tracker) before it detects breaths again.
 *
 * Every module keeps running when its IN is off (warm state); IN, Bypass and both Listen switches
 * crossfade linearly over 5 ms. Input and Output gains are smoothed per sample. Bypass outputs the
 * delayed input only (latency unchanged). Breath Listen (precedence) outputs the delayed input
 * (after input gain) times (1 - reducer gain): the reducer gain only, no Rider gain, and it works
 * even with Breath IN off (you hear what the reducer would remove). De-ess Listen outputs the band
 * the de-esser removes; the stages after the listened one are not heard.
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
// riderRangeDb = modeRow(mode).riderMaxRangeDb x Rider Range.
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
  int window_ = 0;  // sliding-max length - 1 (= latency_)

  // Audio delays, length latency_ + 1: the raw sanitized input (Bypass plays it) and the input
  // after the Input gain (the chain processes it): the gain belongs to the input timeline, so a
  // change reaches the audio when the audio of that moment leaves the delay.
  std::vector<float> delayL_, delayR_;
  std::vector<float> gainedL_, gainedR_;
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
