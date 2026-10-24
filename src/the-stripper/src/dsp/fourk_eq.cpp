/**
 * 4K-EQ DSP implementation.
 * Filter coefficient logic from Luna Co. Audio 4K-EQ (GPL-3.0).
 */

#include "fourk_eq.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::TheStripper::DSP {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530718f;

template <typename T>
T clamp(T v, T lo, T hi) {
  if (v <= lo) return lo;
  if (v >= hi) return hi;
  return v;
}
}  // namespace

void FourKEQ::FilterBand::reset() {
  filterL.reset();
  filterR.reset();
}

void FourKEQ::HighPassFilter::reset() {
  stage1L.reset();
  stage1R.reset();
  stage2.reset();
}

float FourKEQ::HighPassFilter::processL(float x) {
  return stage2.processL(stage1L.processSample(x));
}

float FourKEQ::HighPassFilter::processR(float x) {
  return stage2.processR(stage1R.processSample(x));
}

void FourKEQ::AllPassBand::reset() {
  allPassL.reset();
  allPassR.reset();
}

void FourKEQ::reset() {
  hpf_.reset();
  lpf_.reset();
  lfFilter_.reset();
  lmFilter_.reset();
  hmFilter_.reset();
  hfFilter_.reset();
  phaseShift_.reset();
}

void FourKEQ::prepare(double sampleRate, int maxBlockSize) {
  if (sampleRate <= 0.0 || maxBlockSize <= 0)
    return;
  sampleRate_ = clamp(sampleRate, 8000.0, 384000.0);
  maxBlockSize_ = maxBlockSize;

  setPhaseShiftFreq(sampleRate_, 200.0f);
}

void FourKEQ::setCoeffsFirstOrderHP(double sampleRate, float freq, Biquad& bq) {
  if (sampleRate <= 0.0f || freq <= 0.0f)
    return;
  float K = std::tan(kPi * freq / static_cast<float>(sampleRate));
  float norm = 1.0f / (1.0f + K);
  // Biquad::processSample subtracts a1 * w1, so the pole at (1 - K) / (1 + K) goes in with a minus sign (a plus put it
  // near Nyquist and turned the HPF into a differentiator that cut 1 kHz by 24 dB).
  bq.setCoefficients(norm, -norm, 0.0f, -(1.0f - K) * norm, 0.0f);
}

void FourKEQ::setCoeffsHighPass(double sampleRate, float freq, float q, Biquad& bqL, Biquad& bqR) {
  if (sampleRate <= 0.0f || q < 0.1f)
    return;
  float w0 = kTwoPi * freq / static_cast<float>(sampleRate);
  float cosW0 = std::cos(w0);
  float sinW0 = std::sin(w0);
  float alpha = sinW0 / (2.0f * q);
  float norm = 1.0f / (1.0f + alpha);
  float b0 = (1.0f + cosW0) * 0.5f * norm;
  float b1 = -(1.0f + cosW0) * norm;
  float b2 = (1.0f + cosW0) * 0.5f * norm;
  float a1 = -2.0f * cosW0 * norm;
  float a2 = (1.0f - alpha) * norm;
  bqL.setCoefficients(b0, b1, b2, a1, a2);
  bqR.setCoefficients(b0, b1, b2, a1, a2);
}

void FourKEQ::setCoeffsLowPass(double sampleRate, float freq, float q, Biquad& bqL, Biquad& bqR) {
  if (sampleRate <= 0.0f || q < 0.1f)
    return;
  float w0 = kTwoPi * freq / static_cast<float>(sampleRate);
  float cosW0 = std::cos(w0);
  float sinW0 = std::sin(w0);
  float alpha = sinW0 / (2.0f * q);
  float norm = 1.0f / (1.0f + alpha);
  float b0 = (1.0f - cosW0) * 0.5f * norm;
  float b1 = (1.0f - cosW0) * norm;
  float b2 = (1.0f - cosW0) * 0.5f * norm;
  float a1 = -2.0f * cosW0 * norm;
  float a2 = (1.0f - alpha) * norm;
  bqL.setCoefficients(b0, b1, b2, a1, a2);
  bqR.setCoefficients(b0, b1, b2, a1, a2);
}

float FourKEQ::preWarpFrequency(float freq, double sampleRate) const {
  float nyquist = static_cast<float>(sampleRate * 0.5);
  float omega = kPi * freq / static_cast<float>(sampleRate);
  float warpedFreq = static_cast<float>(sampleRate) / kPi * std::tan(omega);
  if (freq > 3000.0f) {
    float ratio = freq / nyquist;
    float compensation = 1.0f;
    if (ratio < 0.3f)
      compensation = 1.0f + (ratio - 0.136f) * 0.15f;
    else if (ratio < 0.5f)
      compensation = 1.0f + (ratio - 0.3f) * 0.4f;
    else
      compensation = 1.0f + (ratio - 0.5f) * 0.6f;
    warpedFreq = freq * compensation;
  }
  return std::min(warpedFreq, nyquist * 0.99f);
}

void FourKEQ::setCoeffsSSLShelf(double sampleRate, float freq, float q, float gainDB,
                                bool isHighShelf, bool isBlack, Biquad& bqL, Biquad& bqR) {
  float A = std::pow(10.0f, gainDB / 40.0f);
  float w0 = kTwoPi * freq / static_cast<float>(sampleRate);
  float cosw0 = std::cos(w0);
  float sinw0 = std::sin(w0);
  float sslQ = isBlack ? (q * 1.4f) : (q * 0.65f);
  float alpha = sinw0 / (2.0f * sslQ);

  float b0, b1, b2, a0, a1, a2;
  if (isHighShelf) {
    b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha);
    b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
    b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha);
    a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
    a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
    a2 = (A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha;
  } else {
    b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha);
    b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
    b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha);
    a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * std::sqrt(A) * alpha;
    a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
    a2 = (A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * std::sqrt(A) * alpha;
  }
  b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
  bqL.setCoefficients(b0, b1, b2, a1, a2);
  bqR.setCoefficients(b0, b1, b2, a1, a2);
}

void FourKEQ::setCoeffsSSLPeak(double sampleRate, float freq, float q, float gainDB,
                               bool isBlack, Biquad& bqL, Biquad& bqR) {
  float A = std::pow(10.0f, gainDB / 40.0f);
  float w0 = kTwoPi * freq / static_cast<float>(sampleRate);
  float cosw0 = std::cos(w0);
  float sinw0 = std::sin(w0);
  float sslQ = q;
  if (isBlack && std::abs(gainDB) > 0.1f) {
    float gainFactor = std::abs(gainDB) / 15.0f;
    if (gainDB > 0.0f)
      sslQ *= (1.0f + gainFactor * 1.2f);
    else
      sslQ *= (1.0f + gainFactor * 0.6f);
  }
  sslQ = clamp(sslQ, 0.1f, 10.0f);
  float alpha = sinw0 / (2.0f * sslQ);

  float b0 = 1.0f + alpha * A;
  float b1 = -2.0f * cosw0;
  float b2 = 1.0f - alpha * A;
  float a0 = 1.0f + alpha / A;
  float a1 = -2.0f * cosw0;
  float a2 = 1.0f - alpha / A;
  b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
  bqL.setCoefficients(b0, b1, b2, a1, a2);
  bqR.setCoefficients(b0, b1, b2, a1, a2);
}

void FourKEQ::setPhaseShiftFreq(double sampleRate, float freq) {
  float w0 = kTwoPi * freq / static_cast<float>(sampleRate);
  float tan_w0 = std::tan(w0 / 2.0f);
  float a0 = 1.0f + tan_w0;
  float a1 = (1.0f - tan_w0) / a0;
  float b0 = a1;
  float b1 = 1.0f;
  phaseShift_.allPassL.setCoefficients(b0, b1, 0.0f, a1, 0.0f);
  phaseShift_.allPassR.setCoefficients(b0, b1, 0.0f, a1, 0.0f);
}

float FourKEQ::calculateDynamicQ(float gain, float baseQ) const {
  float absGain = std::abs(gain);
  float scale = (gain >= 0.0f) ? 2.0f : 1.5f;
  float dynamicQ = baseQ * (1.0f + (absGain / 20.0f) * scale);
  return clamp(dynamicQ, 0.5f, 8.0f);
}

void FourKEQ::updateHPF(float hpfFreq) {
  if (sampleRate_ <= 0.0f)
    return;
  setCoeffsFirstOrderHP(sampleRate_, hpfFreq, hpf_.stage1L);
  setCoeffsFirstOrderHP(sampleRate_, hpfFreq, hpf_.stage1R);
  const float sslHPFQ = 0.54f;
  setCoeffsHighPass(sampleRate_, hpfFreq, sslHPFQ, hpf_.stage2.filterL, hpf_.stage2.filterR);
}

void FourKEQ::updateLPF(float lpfFreq, bool isBlack) {
  if (sampleRate_ <= 0.0f)
    return;
  float processFreq = lpfFreq;
  if (lpfFreq > sampleRate_ * 0.3f)
    processFreq = preWarpFrequency(lpfFreq, sampleRate_);
  float q = isBlack ? 0.8f : 0.707f;
  setCoeffsLowPass(sampleRate_, processFreq, q, lpf_.filterL, lpf_.filterR);
}

void FourKEQ::updateLFBand(float gain, float freq, bool isBlack, bool isBell) {
  if (sampleRate_ <= 0.0f)
    return;
  if (isBlack && isBell)
    setCoeffsSSLPeak(sampleRate_, freq, 0.7f, gain, isBlack, lfFilter_.filterL, lfFilter_.filterR);
  else
    setCoeffsSSLShelf(sampleRate_, freq, 0.7f, gain, false, isBlack, lfFilter_.filterL, lfFilter_.filterR);
}

void FourKEQ::updateLMBand(float gain, float freq, float q, bool isBlack) {
  if (sampleRate_ <= 0.0f)
    return;
  if (isBlack)
    q = calculateDynamicQ(gain, q);
  setCoeffsSSLPeak(sampleRate_, freq, q, gain, isBlack, lmFilter_.filterL, lmFilter_.filterR);
}

void FourKEQ::updateHMBand(float gain, float freq, float q, bool isBlack) {
  if (sampleRate_ <= 0.0f)
    return;
  if (isBlack)
    q = calculateDynamicQ(gain, q);
  else if (freq > 7000.0f)
    freq = 7000.0f;
  float processFreq = (freq > 3000.0f) ? preWarpFrequency(freq, sampleRate_) : freq;
  setCoeffsSSLPeak(sampleRate_, processFreq, q, gain, isBlack, hmFilter_.filterL, hmFilter_.filterR);
}

void FourKEQ::updateHFBand(float gain, float freq, bool isBlack, bool isBell) {
  if (sampleRate_ <= 0.0f)
    return;
  float warpedFreq = preWarpFrequency(freq, sampleRate_);
  if (isBlack && isBell)
    setCoeffsSSLPeak(sampleRate_, warpedFreq, 0.7f, gain, isBlack, hfFilter_.filterL, hfFilter_.filterR);
  else
    setCoeffsSSLShelf(sampleRate_, warpedFreq, 0.7f, gain, true, isBlack, hfFilter_.filterL, hfFilter_.filterR);
}

void FourKEQ::updateFilters(const FourKEQParams& p) {
  updateHPF(p.hpfFreq);
  updateLPF(p.lpfFreq, p.eqTypeBlack);
  updateLFBand(p.lfGain, p.lfFreq, p.eqTypeBlack, p.lfBell);
  updateLMBand(p.lmGain, p.lmFreq, p.lmQ, p.eqTypeBlack);
  updateHMBand(p.hmGain, p.hmFreq, p.hmQ, p.eqTypeBlack);
  updateHFBand(p.hfGain, p.hfFreq, p.eqTypeBlack, p.hfBell);
}

void FourKEQ::process(float* left, float* right, int numSamples, const FourKEQParams& p) {
  if (numSamples <= 0 || !left || !right)
    return;

  if (sampleRate_ <= 0.0)
    return;

  bool paramsChanged = (p.hpfFreq != lastParams_.hpfFreq || p.hpfEnabled != lastParams_.hpfEnabled ||
                       p.lpfFreq != lastParams_.lpfFreq || p.lpfEnabled != lastParams_.lpfEnabled ||
                       p.lfGain != lastParams_.lfGain || p.lfFreq != lastParams_.lfFreq || p.lfBell != lastParams_.lfBell ||
                       p.lmGain != lastParams_.lmGain || p.lmFreq != lastParams_.lmFreq || p.lmQ != lastParams_.lmQ ||
                       p.hmGain != lastParams_.hmGain || p.hmFreq != lastParams_.hmFreq || p.hmQ != lastParams_.hmQ ||
                       p.hfGain != lastParams_.hfGain || p.hfFreq != lastParams_.hfFreq || p.hfBell != lastParams_.hfBell ||
                       p.eqTypeBlack != lastParams_.eqTypeBlack);
  if (paramsChanged)
    updateFilters(p);
  lastParams_ = p;

  if (p.hpfEnabled && !lastHpfEnabled_)
    hpf_.reset();
  lastHpfEnabled_ = p.hpfEnabled;
  if (p.lpfEnabled && !lastLpfEnabled_)
    lpf_.reset();
  lastLpfEnabled_ = p.lpfEnabled;

  for (int i = 0; i < numSamples; ++i) {
    float l = left[i];
    float r = right[i];

    if (p.hpfEnabled) {
      l = hpf_.processL(l);
      r = hpf_.processR(r);
    }
    l = lfFilter_.processL(l);
    r = lfFilter_.processR(r);
    l = lmFilter_.processL(l);
    r = lmFilter_.processR(r);
    l = hmFilter_.processL(l);
    r = hmFilter_.processR(r);
    l = hfFilter_.processL(l);
    r = hfFilter_.processR(r);
    if (p.lpfEnabled) {
      l = lpf_.processL(l);
      r = lpf_.processR(r);
    }
    if (!p.eqTypeBlack) {
      l = phaseShift_.processL(l);
      r = phaseShift_.processR(r);
    }

    left[i] = l;
    right[i] = r;
  }
}

}  // namespace FairyTapes::TheStripper::DSP