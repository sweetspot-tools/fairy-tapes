#include "dsp/spectral_breath_detector.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::DeepThroat::DSP {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kFloorDb = -120.0f;
constexpr double kBandLoHz = 300.0;
constexpr double kBandHiHz = 8000.0;
constexpr double kPitchLoHz = 60.0;
constexpr double kPitchHiHz = 1000.0;
constexpr float kProbFlush = 1e-5f;

float ramp(float x, float lo, float hi) {
  if (!(hi > lo)) return x >= hi ? 1.0f : 0.0f;
  const float t = (x - lo) / (hi - lo);
  return t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : t);
}

float onePoleCoef(double ms, double sr) {
  const double samples = std::max(1.0, ms * 0.001 * sr);
  return static_cast<float>(1.0 - std::exp(-1.0 / samples));
}
} // namespace

SpectralBreathDetector::SpectralBreathDetector(const SpectralTuning& tuning) : tuning_(tuning) {
  setSensitivity(0.5f);
}

void SpectralBreathDetector::setSensitivity(float s01) {
  const float s = std::isfinite(s01) ? std::clamp(s01, 0.0f, 1.0f) : 0.5f;
  tuning_.onThreshold = 0.8f + (0.4f - 0.8f) * s;
  tuning_.offThreshold = tuning_.onThreshold - 0.2f;
}

void SpectralBreathDetector::prepare(double sampleRate) {
  sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
  const double sr = sampleRate_;
  frame_ = nextPow2(static_cast<int>(std::ceil(0.020 * sr)));
  hop_ = frame_ / 2;
  minLag_ = std::max(2, static_cast<int>(std::floor(sr / kPitchHiHz)));
  maxLag_ = std::max(minLag_ + 1, static_cast<int>(std::ceil(sr / kPitchLoHz)));
  histLen_ = frame_ + maxLag_;
  corrSize_ = nextPow2(histLen_);

  binHz_ = sr / frame_;
  binLo_ = std::max(1, static_cast<int>(std::ceil(kBandLoHz / binHz_)));
  binHi_ = std::min(frame_ / 2, static_cast<int>(std::floor(std::min(kBandHiHz, 0.5 * sr) / binHz_)));

  specFft_.prepare(frame_);
  corrFft_.prepare(corrSize_);
  window_.assign(frame_, 0.0f);
  for (int i = 0; i < frame_; ++i)
    window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / frame_));
  ring_.assign(2 * histLen_, 0.0f);
  specRe_.assign(frame_, 0.0f);
  specIm_.assign(frame_, 0.0f);
  corrRe_.assign(corrSize_, 0.0f);
  corrIm_.assign(corrSize_, 0.0f);
  prefix_.assign(histLen_ + 1, 0.0);

  phraseRelease_ = std::exp(-static_cast<double>(hop_) / (tuning_.phraseReleaseMs * 0.001 * sr));
  minDurationSamples_ = static_cast<int>(std::lround(tuning_.minDurationMs * 0.001 * sr));
  attackCoef_ = onePoleCoef(tuning_.attackMs, sr);
  releaseCoef_ = onePoleCoef(tuning_.releaseMs, sr);
  reset();
}

void SpectralBreathDetector::reset() {
  std::fill(ring_.begin(), ring_.end(), 0.0f);
  writePos_ = 0;
  hopCount_ = 0;
  seenVoice_ = false;
  phraseDb_ = kFloorDb;
  runSamples_ = 0;
  active_ = false;
  target_ = 0.0f;
  prob_ = 0.0f;
  feat_ = Features{};
}

float SpectralBreathDetector::process(float monoSample) {
  if (ring_.empty()) return 0.0f;  // not prepared
  const float x = std::isfinite(monoSample) ? std::clamp(monoSample, -16.0f, 16.0f) : 0.0f;
  ring_[writePos_] = x;
  ring_[writePos_ + histLen_] = x;
  if (++writePos_ >= histLen_) writePos_ = 0;
  if (++hopCount_ >= hop_) {
    hopCount_ = 0;
    analyze(ring_.data() + writePos_);  // oldest sample first
  }
  const float c = target_ > prob_ ? attackCoef_ : releaseCoef_;
  prob_ += (target_ - prob_) * c;
  if (target_ <= 0.0f && prob_ < kProbFlush) prob_ = 0.0f;
  return prob_;
}

void SpectralBreathDetector::analyze(const float* h) {
  const float* frame = h + maxLag_;  // the most recent frame_ samples

  // Level of the frame (unwindowed RMS) and prefix sums of squares over the whole history.
  prefix_[0] = 0.0;
  for (int i = 0; i < histLen_; ++i) prefix_[i + 1] = prefix_[i] + static_cast<double>(h[i]) * h[i];
  const double eFrame = prefix_[histLen_] - prefix_[maxLag_];
  const double meanSq = eFrame / frame_;
  feat_.levelDb = meanSq > 1e-12 ? std::max(kFloorDb, static_cast<float>(10.0 * std::log10(meanSq)))
                                 : kFloorDb;

  if (feat_.levelDb <= tuning_.silenceDb) {
    feat_.flatness = feat_.centroidHz = feat_.harmonicity = 0.0f;
  } else {
    // Spectrum of the Hann-windowed frame: flatness and centroid over 300-8000 Hz.
    for (int i = 0; i < frame_; ++i) {
      specRe_[i] = frame[i] * window_[i];
      specIm_[i] = 0.0f;
    }
    specFft_.forward(specRe_.data(), specIm_.data());
    double sum = 0.0, logSum = 0.0, wSum = 0.0;
    const double tiny = 1e-30 + 1e-12 * eFrame * frame_;  // relative floor for log()
    for (int k = binLo_; k <= binHi_; ++k) {
      const double p = static_cast<double>(specRe_[k]) * specRe_[k] +
                       static_cast<double>(specIm_[k]) * specIm_[k] + tiny;
      sum += p;
      logSum += std::log(p);
      wSum += p * k;
    }
    const int bins = binHi_ - binLo_ + 1;
    const double arith = sum / bins;
    feat_.flatness = static_cast<float>(std::exp(logSum / bins) / arith);
    feat_.centroidHz = static_cast<float>(wSum / sum * binHz_);

    // Harmonicity: normalized cross-correlation of the frame against the history,
    // cc[d] = sum_i frame[i] * h[i + d], lag = maxLag_ - d, via one packed complex FFT.
    for (int j = 0; j < corrSize_; ++j) {
      corrRe_[j] = j < frame_ ? frame[j] : 0.0f;
      corrIm_[j] = j < histLen_ ? h[j] : 0.0f;
    }
    corrFft_.forward(corrRe_.data(), corrIm_.data());
    // Unpack A (frame) and B (history), form conj(A) * B, then conj it for the inverse.
    const int m = corrSize_;
    for (int k = 0; k <= m / 2; ++k) {
      const int nk = (m - k) & (m - 1);
      const float zr = corrRe_[k], zi = corrIm_[k], wr = corrRe_[nk], wi = corrIm_[nk];
      const float ar = 0.5f * (zr + wr), ai = 0.5f * (zi - wi);
      const float br = 0.5f * (zi + wi), bi = -0.5f * (zr - wr);
      const float cr = ar * br + ai * bi, ci = ar * bi - ai * br;
      corrRe_[k] = cr;
      corrIm_[k] = -ci;  // conj for the inverse via the forward transform
      if (nk != k) {     // C is Hermitian: C[m-k] = conj(C[k])
        corrRe_[nk] = cr;
        corrIm_[nk] = ci;
      }
    }
    corrFft_.forward(corrRe_.data(), corrIm_.data());  // real part / m = cc
    float best = 0.0f;
    const double eA = eFrame;
    for (int d = 0; d <= maxLag_ - minLag_; ++d) {
      const double eB = prefix_[d + frame_] - prefix_[d];
      // A lagged segment 30 dB weaker than the frame cannot be its periodic copy, and dividing
      // the FFT rounding error by its tiny energy would fake a correlation peak.
      if (eB < 1e-3 * eA) continue;
      const double den = std::sqrt(eA * eB);
      if (den <= 1e-20) continue;
      const float r = static_cast<float>(static_cast<double>(corrRe_[d]) / m / den);
      if (r > best) best = r;
    }
    feat_.harmonicity = std::min(1.0f, best);
  }

  // Soft breath score, then the phrase tracker (updated on voiced frames only).
  feat_.score = frameScore();
  if (feat_.levelDb > tuning_.silenceDb && feat_.harmonicity >= tuning_.voicedMin) {
    if (!seenVoice_ || feat_.levelDb >= phraseDb_) {
      phraseDb_ = feat_.levelDb;
    } else {
      phraseDb_ = static_cast<float>(feat_.levelDb + (phraseDb_ - feat_.levelDb) * phraseRelease_);
    }
    seenVoice_ = true;
  }
  feat_.phraseDb = phraseDb_;

  // Hysteresis with minimum duration.
  if (!active_) {
    if (feat_.score >= tuning_.onThreshold) {
      runSamples_ += hop_;
      if (runSamples_ >= minDurationSamples_) active_ = true;
    } else {
      runSamples_ = 0;
    }
  } else if (feat_.score < tuning_.offThreshold) {
    active_ = false;
    runSamples_ = 0;
  }
  feat_.active = active_;
  target_ = active_ ? std::min(1.0f, feat_.score / tuning_.onThreshold) : 0.0f;
}

float SpectralBreathDetector::frameScore() {
  if (!seenVoice_ || feat_.levelDb <= tuning_.silenceDb) return 0.0f;
  const SpectralTuning& t = tuning_;
  const float mFlat = ramp(feat_.flatness, t.flatnessMin * (2.0f / 3.0f), t.flatnessMin * (4.0f / 3.0f));
  const float mCent = ramp(std::log2(std::max(1.0f, feat_.centroidHz) / t.centroidMinHz), -0.5f, 0.5f);
  const float mHarm = ramp(t.harmonicityMax - feat_.harmonicity, -0.1f, 0.1f);
  const float depth = phraseDb_ - feat_.levelDb;
  const float mBelow = ramp(depth, t.belowPhraseDb - 3.0f, t.belowPhraseDb + 3.0f);
  const float mDeep = 1.0f - ramp(depth, t.depthFullDb, t.depthZeroDb);
  return std::min({mFlat, mCent, mHarm, mBelow, mDeep});
}

} // namespace FairyTapes::DeepThroat::DSP
