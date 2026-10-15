#include "dsp/level_breath_detector.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::DeepThroat::DSP {

LevelBreathDetector::LevelBreathDetector(const LevelTuning& tuning) : tuning_(tuning) {
  prepare(48000.0);
  setSensitivity(0.5f);
}

void LevelBreathDetector::setSensitivity(float s01) {
  const float s = std::isfinite(s01) ? std::clamp(s01, 0.0f, 1.0f) : 0.5f;
  tuning_.belowPhraseDb = 26.0f + (14.0f - 26.0f) * s;
}

void LevelBreathDetector::prepare(double sampleRate) {
  sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
  const double sr = sampleRate_;
  meter_.prepare(sr, tuning_.levelWindowMs);
  phraseCoef_ = std::exp(-1.0 / std::max(1.0, tuning_.phraseReleaseMs * 0.001 * sr));
  contextSamples_ = static_cast<long>(tuning_.contextMs * 0.001 * sr);
  minDurationSamples_ = static_cast<long>(tuning_.minDurationMs * 0.001 * sr);
  maxDurationSamples_ = std::max(minDurationSamples_ + 1,
                                 static_cast<long>(tuning_.maxDurationMs * 0.001 * sr));
  attackCoef_ = static_cast<float>(std::exp(-1.0 / std::max(1.0, tuning_.attackMs * 0.001 * sr)));
  reset();
}

void LevelBreathDetector::reset() {
  meter_.reset();
  phraseDb_ = -120.0f;
  sinceVoice_ = 1 << 30;
  runSamples_ = 0;
  active_ = false;
  locked_ = false;
  entryPhraseDb_ = -120.0f;
  prob_ = 0.0f;
}

float LevelBreathDetector::process(float monoSample) {
  const float level = meter_.process(monoSample);
  const bool audible = level > tuning_.silenceDb;

  // Phrase tracker: immediate attack, slow release; holds through silence.
  if (audible) {
    if (level >= phraseDb_) phraseDb_ = level;
    else phraseDb_ = static_cast<float>(level + (phraseDb_ - level) * phraseCoef_);
  }

  const float depth = phraseDb_ - level;
  if (audible && depth <= tuning_.voiceWithinDb) sinceVoice_ = 0;
  else if (sinceVoice_ < (1 << 30)) ++sinceVoice_;

  // After the cap only a level back near the phrase level of the capped breath is voice.
  if (locked_ && audible && level >= entryPhraseDb_ - tuning_.voiceWithinDb) locked_ = false;

  const float need = tuning_.belowPhraseDb - (active_ ? tuning_.holdMarginDb : 0.0f);
  // Context is needed to enter a breath, not to stay in one.
  const bool candidate = !locked_ && audible && depth >= need &&
                         (active_ || sinceVoice_ <= contextSamples_);

  if (!candidate) {
    runSamples_ = 0;
    active_ = false;
    prob_ = 0.0f;  // hard drop: the voice after a breath is free at once
    return 0.0f;
  }
  if (runSamples_ == 0) entryPhraseDb_ = phraseDb_;
  if (runSamples_ >= maxDurationSamples_) {
    // Too long for a breath: release and stay out of this quiet passage.
    runSamples_ = 0;
    active_ = false;
    locked_ = true;
    prob_ = 0.0f;
    return 0.0f;
  }
  if (++runSamples_ >= minDurationSamples_) active_ = true;
  const float target = active_ ? 1.0f : 0.0f;
  prob_ = target + (prob_ - target) * attackCoef_;
  if (prob_ < 1e-6f) prob_ = 0.0f;
  return std::clamp(prob_, 0.0f, 1.0f);
}

} // namespace FairyTapes::DeepThroat::DSP
