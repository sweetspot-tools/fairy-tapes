// Fairy Tapes · Deep Throat — level-based breath detector (Puke mode).
// Growl and scream are noisy and pitchless, so there is no spectral analysis: a breath is a
// segment well below the running phrase level that is surrounded by voice. A loud inhalation
// (close to the phrase level) and continuous loud noise are never breaths. Voice context is
// needed to ENTER a breath, not to stay in one, so long breaths are held to their end; an active
// breath is capped at maxDurationMs, after which the same quiet passage cannot re-enter until the
// level comes back within voiceWithinDb of the phrase level seen when the breath began, or rises
// unlockRiseDb above the level of the capped passage (a softer voice coming back). The phrase
// tracker's decay does not count as voice. No VST3 SDK dependency.
#pragma once

#include "dsp/breath_detector.h"
#include "dsp/rider.h"

namespace FairyTapes::DeepThroat::DSP {

struct LevelTuning {
  float belowPhraseDb = 20.f;     // overwritten by setSensitivity(): lerp(26, 14, s)
  float contextMs = 300.f;        // voice must have been present within this time to ENTER
  float maxDurationMs = 1500.f;   // an active breath ends after this long (no re-entry until voice)
  float unlockRiseDb = 8.f;       // after the cap, a rise this far above the capped passage is voice
  float minDurationMs = 80.f;     // the candidate must last this long before the output rises
  float silenceDb = -60.f;        // below this is silence, never breath
  // Tuning added on top of the brief's values (see the task report).
  float levelWindowMs = 10.f;     // level meter window: short, so a 25 dB drop is seen in ~60 ms
  float voiceWithinDb = 15.f;     // level within this of the phrase counts as voice
  float phraseReleaseMs = 2000.f; // phrase tracker release (attack is immediate)
  float holdMarginDb = 3.f;       // an active breath survives this much level wobble
  float attackMs = 5.f;           // output smoothing toward a higher value; falls are immediate
};

class LevelBreathDetector final : public BreathDetector {
public:
  explicit LevelBreathDetector(const LevelTuning& tuning = {});

  void prepare(double sampleRate) override;
  void reset() override;
  float process(float monoSample) override;
  int latencySamples() const override { return 0; }  // the engine aligns the detector itself
  void setSensitivity(float s01) override;

  const LevelTuning& tuning() const { return tuning_; }

private:
  LevelTuning tuning_;
  double sampleRate_ = 48000.0;
  LevelMeter meter_;
  float phraseDb_ = -120.f;
  double phraseCoef_ = 0.0;     // per-sample one-pole coefficient (dB domain)
  long sinceVoice_ = 1 << 30;   // samples since the level was last within voiceWithinDb of phrase
  long runSamples_ = 0;         // consecutive samples of breath candidate
  long contextSamples_ = 0;
  long minDurationSamples_ = 0;
  long maxDurationSamples_ = 0;
  bool active_ = false;
  bool locked_ = false;         // capped: no re-entry until real voice is seen again
  float entryPhraseDb_ = -120.f;  // phrase level when the current candidate run began
  float lockLevelDb_ = -120.f;    // meter level of the quiet passage when the cap latched
  float prob_ = 0.f;
  float attackCoef_ = 0.f;
};

} // namespace FairyTapes::DeepThroat::DSP
