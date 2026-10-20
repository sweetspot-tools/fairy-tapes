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
#include <algorithm>
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

// The tempo for one block: the host's when it says it is valid (and it is a positive number), otherwise the instance's
// tempo fallback (Behaviour::tempoFallbackBpm in state.h); a fallback that is not a positive number gives kFallbackBpm.
inline double tempoForBlock(bool hostTempoValid, double hostTempo, double fallbackBpm) {
  if (hostTempoValid && hostTempo > 0.0 && hostTempo == hostTempo && hostTempo < 1e6) return hostTempo;
  return fallbackBpm > 0.0 && fallbackBpm < 1e6 ? fallbackBpm : kFallbackBpm;
}

struct SidechainInput {
  const float* L;
  const float* R;
};

// Sidechain buffers only when the switch is on and the bus is active with usable
// buffers; otherwise {nullptr, nullptr}, meaning "detect on the main input".
SidechainInput pickSidechain(bool sidechainOn, bool busActive, int numChannels,
                             float* const* buffers);

struct DisplayValues {
  float voiceDb = -120.f;  // max of the ducker's detector envelope (the level compared with
                           // the threshold: mono, band-passed, rectified), dB (floor -120)
  float sendGain = 1.f;    // last ducker gain, linear 0..1
  float echoDb = -120.f;   // peak of the echo at the output (after ducking and Mix), dB
};

// LEVEL block data. Peaks in dB (floor -120) over the last process() call; the input is measured
// before anything else, the output after Mix and Bypass. A clip flag is set by any |sample| >= 1.0
// and held for kClipHoldSeconds of audio time (or until Engine::clearClips()).
struct LevelValues {
  float inL = -120.f, inR = -120.f, outL = -120.f, outR = -120.f;
  bool clipIn = false, clipOut = false;
};
constexpr double kClipHoldSeconds = 2.0;

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

// Holds the extremes seen between two display writes: max of voice/echo peaks, MIN of the
// send gain (so a ducking dip is never missed). take() returns them and resets.
class DisplayAccumulator {
public:
  void add(const DisplayValues& d) {
    acc_.voiceDb = std::max(acc_.voiceDb, d.voiceDb);
    acc_.echoDb = std::max(acc_.echoDb, d.echoDb);
    acc_.sendGain = std::min(acc_.sendGain, d.sendGain);
  }
  DisplayValues take() {
    const DisplayValues r = acc_;
    acc_ = DisplayValues{};
    return r;
  }

private:
  DisplayValues acc_;
};

// Holds the extremes seen between two display writes: max of the peaks, OR of the clip flags.
class LevelAccumulator {
public:
  void add(const LevelValues& l) {
    acc_.inL = std::max(acc_.inL, l.inL);
    acc_.inR = std::max(acc_.inR, l.inR);
    acc_.outL = std::max(acc_.outL, l.outL);
    acc_.outR = std::max(acc_.outR, l.outR);
    acc_.clipIn = acc_.clipIn || l.clipIn;
    acc_.clipOut = acc_.clipOut || l.clipOut;
  }
  LevelValues take() {
    const LevelValues r = acc_;
    acc_ = LevelValues{};
    return r;
  }
  void clearClips() { acc_.clipIn = acc_.clipOut = false; }

private:
  LevelValues acc_;
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
  // Input/output peaks of the last process() call and the held clip flags; no allocation.
  LevelValues lastLevels() const {
    LevelValues l = levels_;
    l.clipIn = clipInLeft_ > 0;
    l.clipOut = clipOutLeft_ > 0;
    return l;
  }
  void clearClips() { clipInLeft_ = clipOutLeft_ = 0; }

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
  float inPeakL_ = 0.f, inPeakR_ = 0.f, outPeakL_ = 0.f, outPeakR_ = 0.f;
  LevelValues levels_;
  long long clipInLeft_ = 0, clipOutLeft_ = 0, clipHoldSamples_ = 96000;
};

} // namespace FairyTapes::PillowTalk
