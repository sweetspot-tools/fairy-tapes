// Fairy Tapes · Deep Throat — spectral breath detector (Speak and Sing modes).
// A breath is noisy (high spectral flatness), bright (high centroid), unpitched (low
// autocorrelation peak), well below the phrase level and above silence. A quiet low voice is
// harmonic and is never a breath. No VST3 SDK dependency.
#pragma once

#include "dsp/breath_detector.h"
#include "dsp/fft.h"

#include <vector>

namespace FairyTapes::DeepThroat::DSP {

struct SpectralTuning {
  float flatnessMin = 0.12f;      // spectral flatness 300-8000 Hz (geometric / arithmetic mean);
                                  // brief's start 0.30: a band-limited breath measures ~0.2
  float centroidMinHz = 2000.f;   // spectral centroid 300-8000 Hz
  float harmonicityMax = 0.30f;   // normalized autocorrelation peak, pitch 60-1000 Hz
  float belowPhraseDb = 10.f;     // frame level at least this far below the phrase level
  float silenceDb = -60.f;        // frames below this are silence, never breath
  float onThreshold = 0.6f;       // overwritten by setSensitivity()
  float offThreshold = 0.4f;      // overwritten by setSensitivity()
  float minDurationMs = 60.f;     // score above onThreshold this long before switching on
  // Tuning added on top of the brief's starting values (see the task report).
  float depthFullDb = 15.f;       // confidence is full down to this depth below the phrase...
  float depthZeroDb = 45.f;       // ...and fades to zero here (faint noise is room, not breath)
  float voicedMin = 0.60f;        // harmonicity of the frames that drive the phrase tracker
  float phraseReleaseMs = 1500.f; // phrase tracker release (attack is immediate)
  float attackMs = 5.f;           // output smoothing toward a higher value
  float releaseMs = 2.f;          // output smoothing toward a lower value: fast, so the
                                  // phrase after a breath is free within one frame
};

class SpectralBreathDetector final : public BreathDetector {
public:
  // Per-frame analysis values, exposed for tests, tuning and metering.
  struct Features {
    float levelDb = -120.f;
    float phraseDb = -120.f;
    float flatness = 0.f;
    float centroidHz = 0.f;
    float harmonicity = 0.f;
    float score = 0.f;   // soft "all conditions true" in [0, 1]
    bool active = false; // hysteresis state after the minimum duration
  };

  explicit SpectralBreathDetector(const SpectralTuning& tuning = {});

  void prepare(double sampleRate) override;
  void reset() override;
  float process(float monoSample) override;
  int latencySamples() const override { return frame_; }
  void setSensitivity(float s01) override;

  const SpectralTuning& tuning() const { return tuning_; }
  const Features& lastFeatures() const { return feat_; }

private:
  void analyze(const float* history);  // history: histLen_ samples, oldest first
  float frameScore();

  SpectralTuning tuning_;
  double sampleRate_ = 48000.0;
  int frame_ = 1024;   // nextPow2(20 ms * sr)
  int hop_ = 512;      // frame / 2
  int minLag_ = 48;    // sr / 1000 Hz
  int maxLag_ = 800;   // sr / 60 Hz
  int histLen_ = 0;    // frame_ + maxLag_
  int corrSize_ = 0;   // nextPow2(histLen_)
  int binLo_ = 0, binHi_ = 0;  // 300-8000 Hz analysis band
  double binHz_ = 0.0;

  Fft specFft_, corrFft_;
  std::vector<float> window_;
  std::vector<float> ring_;  // 2 * histLen_ (mirrored so the history is always contiguous)
  std::vector<float> specRe_, specIm_, corrRe_, corrIm_;
  std::vector<double> prefix_;  // prefix sums of squares over the history

  int writePos_ = 0;
  int hopCount_ = 0;
  bool seenVoice_ = false;
  float phraseDb_ = -120.f;
  double phraseRelease_ = 0.0;  // per-hop one-pole coefficient
  int runSamples_ = 0;          // consecutive samples with score >= onThreshold
  int minDurationSamples_ = 0;
  bool active_ = false;
  float target_ = 0.f;
  float prob_ = 0.f;
  float attackCoef_ = 0.f, releaseCoef_ = 0.f;
  Features feat_;
};

} // namespace FairyTapes::DeepThroat::DSP
