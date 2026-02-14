/**
 * Fairy Tapes · The Stripper — Simple compressor (Voice / Drums / Classic / Bus).
 * No JUCE; real-time safe. See COMPRESSION_MODES.md.
 */

#include "compression_stub.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::TheStripper::DSP {

namespace {

constexpr float kEps = 1e-8f;

inline float linearToDb(float x) {
  if (x <= kEps) return -80.f;
  return 20.f * std::log10(x);
}

inline float dbToLinear(float db) {
  return std::pow(10.f, db * 0.05f);
}

float timeToCoeff(float timeSec, double sampleRate) {
  if (timeSec <= 0.f || sampleRate <= 0.) return 1.f;
  return 1.f - std::exp(-1.f / (static_cast<float>(timeSec * sampleRate)));
}

}  // namespace

void CompressionStub::setSampleRate(double sampleRate) {
  if (sampleRate > 0. && sampleRate < 500000.)
    sampleRate_ = sampleRate;
  updateCoeffs();
}

void CompressionStub::prepare(int /* maxBlockSize */) {
  envelopeL_ = kEps;
  envelopeR_ = kEps;
}

void CompressionStub::setMode(int mode) {
  mode_ = std::max(0, std::min(3, mode));
  updateCoeffs();
}

void CompressionStub::setParams(float amount, float mix) {
  amount_ = std::max(0.f, std::min(1.f, amount));
  mix_ = std::max(0.f, std::min(1.f, mix));
  // Amount 0 = no compression (threshold above signal), 1 = max (threshold -24 dB)
  thresholdDb_ = 6.f - amount_ * 30.f;  // +6 dB to -24 dB
}

void CompressionStub::updateCoeffs() {
  float attackSec;
  float releaseSec;
  switch (mode_) {
    case 0:  // Voice (Opto LA-2A style): slow, musical
      attackSec = 0.010f;
      releaseSec = 0.100f;
      ratio_ = 2.5f;
      break;
    case 1:  // Drums (FET 1176 style): fast, punchy
      attackSec = 0.00005f;
      releaseSec = 0.050f;
      ratio_ = 4.f;
      break;
    case 2:  // Classic (VCA dbx 160): medium, OverEasy
      attackSec = 0.003f;
      releaseSec = 0.080f;
      ratio_ = 3.f;
      break;
    case 3:  // Bus (SSL style)
      attackSec = 0.002f;
      releaseSec = 0.100f;
      ratio_ = 2.5f;
      break;
    default:
      attackSec = 0.003f;
      releaseSec = 0.080f;
      ratio_ = 3.f;
  }
  attackCoeff_ = timeToCoeff(attackSec, sampleRate_);
  releaseCoeff_ = timeToCoeff(releaseSec, sampleRate_);
}

void CompressionStub::process(float* left, float* right, int numSamples) {
  const float dryMul = 1.f - mix_;
  const float wetMul = mix_;
  const float ratioInv = 1.f / ratio_;

  for (int i = 0; i < numSamples; ++i) {
    float inL = left[i];
    float inR = right[i];
    float peak = std::max(std::abs(inL), std::abs(inR));
    if (peak < kEps) peak = kEps;

    // Stereo-linked envelope: one-pole attack/release
    float env = std::max(envelopeL_, envelopeR_);
    if (peak > env)
      env = env + attackCoeff_ * (peak - env);
    else
      env = env + releaseCoeff_ * (peak - env);
    if (env < kEps) env = kEps;
    envelopeL_ = envelopeR_ = env;

    float envDb = linearToDb(env);
    float gainDb = 0.f;
    if (envDb > thresholdDb_) {
      float over = envDb - thresholdDb_;
      float grDb = over * (1.f - ratioInv);
      gainDb = -grDb;
    }
    float gainLin = dbToLinear(gainDb);

    float outL = inL * gainLin;
    float outR = inR * gainLin;
    left[i] = inL * dryMul + outL * wetMul;
    right[i] = inR * dryMul + outR * wetMul;
  }
}

}  // namespace FairyTapes::TheStripper::DSP
