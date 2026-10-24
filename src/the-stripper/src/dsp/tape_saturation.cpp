/**
 * Tape DSP — ported from Airwindows TapeProc.cpp (processReplacing), MIT license.
 * Dither removed; sample rate from sampleRate_; Slam/Bump driven by the Amount macro.
 * Head bump is scaled by the slam gain so the low-end lift stays a property of the machine
 * instead of shrinking as Amount rises (in the original, Slam boosts the signal but not the bump).
 * The sin() pre-shaper input is clamped to ±pi/2 so signals above 0 dBFS saturate instead of folding.
 */

#include "tape_saturation.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace FairyTapes::TheStripper::DSP {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = 1.57079633;
constexpr double kSoftness = 0.618033988749894848204586;
constexpr double kSpiralClip = 1.2533141373155;
constexpr double kClip = 0.99;

// Macro ranges. Tracking hits the tape harder with more head bump; mixdown is a gentler master machine.
constexpr float kTrackingSlamMaxDb = 12.0f;
constexpr float kMixdownSlamMaxDb = 8.0f;
constexpr float kTrackingBumpMin = 0.30f, kTrackingBumpMax = 0.50f;
constexpr float kMixdownBumpMin = 0.20f, kMixdownBumpMax = 0.30f;

// Auto-compensation
constexpr float kRmsTimeConstantSec = 0.1f;
constexpr float kRmsFloor = 1e-6f;
constexpr float kCompensationMinDb = -24.0f;
constexpr float kCompensationMaxDb = 6.0f;

uint32_t random_fpd_seed() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> dist(16387u, UINT32_MAX);
  return dist(gen);
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

}  // namespace

TapeSaturation::TapeSaturation() { reset(); }

void TapeSaturation::setSampleRate(double sampleRate) {
  if (sampleRate == sampleRate_)
    return;
  sampleRate_ = sampleRate;
  updateCoefficients();
}

void TapeSaturation::reset() {
  for (Channel& ch : channels_) {
    ch.iirMidRoller[0] = ch.iirMidRoller[1] = 0.0;
    ch.iirHeadBump[0] = ch.iirHeadBump[1] = 0.0;
    ch.headBumpFilter[0] = ch.headBumpFilter[1] = BiquadState{};
    ch.mainFilter[0] = ch.mainFilter[1] = BiquadState{};
    ch.lastSample = 0.0;
    ch.fpd = random_fpd_seed();
  }
  flip_ = false;
  inputRmsSmooth_ = 0.0f;
  outputRmsSmooth_ = 0.0f;
  compensation_ = 1.0f;
  updateCoefficients();
}

void TapeSaturation::setParams(float amount, Voicing voicing) {
  amount_ = clamp01(amount);
  voicing_ = voicing;
}

float TapeSaturation::slamDb(float amount, Voicing voicing) {
  const float maxDb = (voicing == Voicing::Tracking) ? kTrackingSlamMaxDb : kMixdownSlamMaxDb;
  return clamp01(amount) * maxDb;
}

float TapeSaturation::bump(float amount, Voicing voicing) {
  const float lo = (voicing == Voicing::Tracking) ? kTrackingBumpMin : kMixdownBumpMin;
  const float hi = (voicing == Voicing::Tracking) ? kTrackingBumpMax : kMixdownBumpMax;
  return lo + clamp01(amount) * (hi - lo);
}

void TapeSaturation::updateCoefficients() {
  const double overallscale = sampleRate_ / 44100.0;
  auto bandpass = [](double freq, double reso) {
    BiquadCoefs c;
    const double K = std::tan(kPi * freq);
    const double norm = 1.0 / (1.0 + K / reso + K * K);
    c.a0 = K / reso * norm;
    c.a2 = -c.a0;
    c.b1 = 2.0 * (K * K - 1.0) * norm;
    c.b2 = (1.0 - K / reso + K * K) * norm;
    return c;
  };
  headBumpCoefs_ = bandpass(0.0072 / overallscale, 0.0009);
  mainCoefs_ = bandpass(0.032 / overallscale, 0.0007);
}

double TapeSaturation::runBiquad(const BiquadCoefs& c, BiquadState& st, double x) {
  const double out = (x * c.a0) + st.s1;
  st.s1 = -(out * c.b1) + st.s2;
  st.s2 = (x * c.a2) - (out * c.b2);
  return out;
}

double TapeSaturation::processSample(Channel& ch, double input, bool flip, double inputGain,
                                     double bumpGain, double headBumpFreq,
                                     double rollAmount) const {
  if (std::fabs(input) < 1.18e-23) {
    ch.fpd ^= ch.fpd << 13;
    ch.fpd ^= ch.fpd >> 17;
    ch.fpd ^= ch.fpd << 5;
    input = ch.fpd * 1.18e-17;
  }
  const double drySample = input;
  const int k = flip ? 0 : 1;

  ch.iirMidRoller[k] = (ch.iirMidRoller[k] * (1.0 - rollAmount)) + (input * rollAmount);
  const double highsSample = input - ch.iirMidRoller[k];

  double headBump = ch.iirHeadBump[k];
  headBump += input * 0.05;
  headBump -= headBump * headBump * headBump * headBumpFreq;
  headBump = std::sin(headBump);
  headBump = std::clamp(runBiquad(headBumpCoefs_, ch.headBumpFilter[k], headBump), -1.0, 1.0);
  ch.iirHeadBump[k] = std::asin(headBump);

  double sample = std::sin(std::clamp(input, -kHalfPi, kHalfPi));
  sample = std::clamp(runBiquad(mainCoefs_, ch.mainFilter[k], sample), -1.0, 1.0);
  sample = std::asin(sample);

  const double groundSample = drySample - sample;  // UnBox: fringe audio bypasses the gain stage
  sample *= inputGain;

  double applySoften = std::min(std::fabs(highsSample) * kHalfPi, kHalfPi);
  applySoften = 1.0 - std::cos(applySoften);
  if (highsSample > 0.0) sample -= applySoften;
  if (highsSample < 0.0) sample += applySoften;

  // Spiral
  sample = std::clamp(sample, -kSpiralClip, kSpiralClip);
  const double absSample = std::fabs(sample);
  sample = std::sin(sample * absSample) / ((absSample == 0.0) ? 1.0 : absSample);

  // Restrain the resonant quality of the head bump
  const double suppress = (1.0 - std::fabs(sample)) * 0.00013;
  for (double& hb : ch.iirHeadBump) {
    if (hb > suppress) hb -= suppress;
    if (hb < -suppress) hb += suppress;
  }

  sample += groundSample;
  sample += (ch.iirHeadBump[0] + ch.iirHeadBump[1]) * bumpGain;

  // ADClip
  if (ch.lastSample >= kClip) {
    ch.lastSample = (sample < kClip) ? (kClip * kSoftness) + (sample * (1.0 - kSoftness)) : kClip;
  }
  if (ch.lastSample <= -kClip) {
    ch.lastSample = (sample > -kClip) ? (-kClip * kSoftness) + (sample * (1.0 - kSoftness)) : -kClip;
  }
  if (sample > kClip) {
    sample = (ch.lastSample < kClip) ? (kClip * kSoftness) + (ch.lastSample * (1.0 - kSoftness)) : kClip;
  }
  if (sample < -kClip) {
    sample = (ch.lastSample > -kClip) ? (-kClip * kSoftness) + (ch.lastSample * (1.0 - kSoftness)) : -kClip;
  }
  ch.lastSample = sample;

  return std::clamp(sample, -kClip, kClip);  // final iron bar
}

void TapeSaturation::process(float* left, float* right, int numSamples) {
  if (left == nullptr || right == nullptr || numSamples <= 0)
    return;

  const double overallscale = sampleRate_ / 44100.0;
  const double inputGain = std::pow(10.0, slamDb(amount_, voicing_) / 20.0);
  const double bumpGain = bump(amount_, voicing_) * 0.1 * inputGain;
  const double headBumpFreq = 0.12 / overallscale;
  const double rollAmount = (1.0 - kSoftness) / overallscale;

  double inEnergy = 0.0;
  double outEnergy = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    const double l = left[i];
    const double r = right[i];
    inEnergy += l * l + r * r;
    const double outL = processSample(channels_[0], l, flip_, inputGain, bumpGain, headBumpFreq, rollAmount);
    const double outR = processSample(channels_[1], r, flip_, inputGain, bumpGain, headBumpFreq, rollAmount);
    flip_ = !flip_;
    outEnergy += outL * outL + outR * outR;
    left[i] = static_cast<float>(outL);
    right[i] = static_cast<float>(outR);
  }

  // Stereo-linked RMS tracking; gain ramps across the block to avoid zipper steps.
  const float inRms = static_cast<float>(std::sqrt(inEnergy / (2.0 * numSamples)));
  const float outRms = static_cast<float>(std::sqrt(outEnergy / (2.0 * numSamples)));
  const double blockDuration = static_cast<double>(numSamples) / sampleRate_;
  const float alpha = static_cast<float>(1.0 - std::exp(-blockDuration / kRmsTimeConstantSec));
  inputRmsSmooth_ += alpha * (inRms - inputRmsSmooth_);
  outputRmsSmooth_ += alpha * (outRms - outputRmsSmooth_);

  float target = compensation_;
  if (inputRmsSmooth_ > kRmsFloor && outputRmsSmooth_ > kRmsFloor) {
    const float minLin = std::pow(10.0f, kCompensationMinDb / 20.0f);
    const float maxLin = std::pow(10.0f, kCompensationMaxDb / 20.0f);
    target = std::clamp(inputRmsSmooth_ / outputRmsSmooth_, minLin, maxLin);
  }

  const float start = compensation_;
  const float step = (target - start) / static_cast<float>(numSamples);
  for (int i = 0; i < numSamples; ++i) {
    const float g = start + step * static_cast<float>(i + 1);
    left[i] *= g;
    right[i] *= g;
  }
  compensation_ = target;
}

}  // namespace FairyTapes::TheStripper::DSP
