/**
 * Fairy Tapes · Pillow Talk — Signal chain used by the VST3 Processor (pure, no SDK libs).
 * Per sample: gain = ducker(detector); wet = tape echo fed with the full input;
 * out = dry * (1 - mix) + wet * gain * mix. The gain ducks the wet OUTPUT, so the
 * phrase stays on tape and its repeats surface once the gate reopens.
 * Bypass ramps mix to 0, leaving the dry signal.
 */

#pragma once

#include "dsp/ducker.h"
#include "dsp/tape_echo.h"
#include "params.h"
#include <vector>

namespace FairyTapes::PillowTalk {

constexpr double kDefaultTimeNote = 2.0 / 3.0;  // 1/4
constexpr double kDefaultHoldNote = 1.0 / 3.0;  // 1/8
constexpr double kFallbackBpm = 120.0;

// Fills kNumParams normalized defaults.
void loadDefaults(double* values);

// Index 0..3 (1/16, 1/8, 1/4, 1/2), same rounding as a 4-entry StringListParameter.
int noteIndexFromNorm(double n);

struct Settings {
  float timeMs, feedback, toneHz, saturation, wowFlutter, mix;
  float thresholdDb, depthDb, holdMs, releaseMs;
  bool bypass;
};

// bpm <= 0 or non-finite means "host gave no tempo" and falls back to 120. Normalized
// values are clamped to [0,1]; non-finite ones fall back to defaults. Always finite.
Settings mapParams(const double* values, double bpm);

struct SidechainInput {
  const float* L;
  const float* R;
};

// Sidechain buffers only when the switch is on and the bus is active with usable
// buffers; otherwise {nullptr, nullptr}, meaning "detect on the main input".
SidechainInput pickSidechain(bool sidechainOn, bool busActive, int numChannels,
                             float* const* buffers);

struct DisplayValues {
  float voiceDb = -120.f;  // peak of the detector input, dB (floor -120)
  float sendGain = 1.f;    // last ducker gain, linear 0..1
  float echoDb = -120.f;   // peak of the wet signal after ducking, before the mix, dB
};

// Allows one display write per kDisplayIntervalMs of audio time; the first block writes.
constexpr double kDisplayIntervalMs = 16.0;
class DisplayRateLimiter {
public:
  void prepare(double sampleRate) {
    interval_ = (long long)((sampleRate > 0.0 ? sampleRate : 48000.0) * 0.001 * kDisplayIntervalMs);
    since_ = interval_;
  }
  // Call once per block with its length; true when a write is due now.
  bool advance(int numSamples) {
    if (numSamples <= 0) return false;
    since_ += numSamples;
    if (since_ < interval_) return false;
    since_ = 0;
    return true;
  }

private:
  long long interval_ = 768, since_ = 768;
};

class Engine {
public:
  // Allocates all buffers; never allocates afterwards.
  void prepare(double sampleRate, int maxBlockSize);
  // Settings with any non-finite field are ignored (the previous ones stay).
  void setSettings(const Settings& s);
  // Inputs may alias outputs and inL may equal inR. scL/scR null: detect on the main
  // input. outR may be null (mono output). Blocks longer than maxBlockSize are split.
  void process(const float* inL, const float* inR, const float* scL, const float* scR,
               float* outL, float* outR, int numSamples);

  // Display values of the last process() call (peaks over that call); no allocation.
  DisplayValues lastDisplay() const { return display_; }

private:
  void processChunk(const float* inL, const float* inR, const float* scL, const float* scR,
                    float* outL, float* outR, int n);

  DSP::Ducker ducker_;
  DSP::TapeEcho echo_;
  std::vector<float> dryL_, dryR_, wetL_, wetR_, gain_;
  int maxBlock_ = 0;
  float mix_ = 0.f, mixTarget_ = 0.f, mixCoef_ = 0.f;
  bool mixInit_ = false;
  DisplayValues display_;
  float voicePeak_ = 0.f, echoPeak_ = 0.f;
};

} // namespace FairyTapes::PillowTalk
