#include "engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace FairyTapes::PillowTalk {

namespace {
constexpr double kMixSmoothMs = 20.0;
constexpr float kMixSnap = 1e-5f;
} // namespace

void loadDefaults(double* v) {
  v[kBypass] = 0.0;
  v[kTimeSync] = kDefaultTimeSync;
  v[kTimeMs] = kDefaultTime;
  v[kTimeNote] = kDefaultTimeNote;
  v[kFeedback] = kDefaultFeedback;
  v[kTone] = kDefaultTone;
  v[kSaturation] = kDefaultSaturation;
  v[kWowFlutter] = kDefaultWowFlutter;
  v[kMix] = kDefaultMix;
  v[kThreshold] = kDefaultThreshold;
  v[kDuckDepth] = kDefaultDepth;
  v[kHoldSync] = kDefaultHoldSync;
  v[kHoldMs] = kDefaultHold;
  v[kHoldNote] = kDefaultHoldNote;
  v[kRelease] = kDefaultRelease;
  v[kSidechain] = kDefaultSidechain;
}

int noteIndexFromNorm(double n) {
  return std::clamp((int)(n * 4.0), 0, 3);
}

Settings mapParams(const double* v, double bpm) {
  if (bpm <= 0.0) bpm = kFallbackBpm;
  Settings s;
  s.timeMs = (float)(v[kTimeSync] >= 0.5 ? noteToMs(noteIndexFromNorm(v[kTimeNote]), bpm)
                                         : timeMsFromNorm(v[kTimeMs]));
  s.feedback = (float)feedbackFromNorm(v[kFeedback]);
  s.toneHz = (float)toneHzFromNorm(v[kTone]);
  s.saturation = (float)v[kSaturation];
  s.wowFlutter = (float)v[kWowFlutter];
  s.mix = (float)v[kMix];
  s.thresholdDb = (float)thresholdDbFromNorm(v[kThreshold]);
  s.depthDb = (float)duckDepthDbFromNorm(v[kDuckDepth]);
  s.holdMs = (float)(v[kHoldSync] >= 0.5 ? noteToMs(noteIndexFromNorm(v[kHoldNote]), bpm)
                                         : holdMsFromNorm(v[kHoldMs]));
  s.releaseMs = (float)releaseMsFromNorm(v[kRelease]);
  s.bypass = v[kBypass] >= 0.5;
  return s;
}

SidechainInput pickSidechain(bool sidechainOn, bool busActive, int numChannels,
                             float* const* buffers) {
  if (!sidechainOn || !busActive || numChannels < 1 || !buffers || !buffers[0])
    return {nullptr, nullptr};
  const float* r = numChannels >= 2 && buffers[1] ? buffers[1] : buffers[0];
  return {buffers[0], r};
}

void Engine::prepare(double sampleRate, int maxBlockSize) {
  maxBlock_ = std::max(1, maxBlockSize);
  for (auto* b : {&dryL_, &dryR_, &wetL_, &wetR_, &gain_}) b->assign((size_t)maxBlock_, 0.f);
  ducker_.prepare(sampleRate);
  echo_.prepare(sampleRate, maxBlock_);
  const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
  mixCoef_ = (float)std::exp(-1.0 / (0.001 * kMixSmoothMs * sr));
  mixInit_ = false;
}

void Engine::setSettings(const Settings& s) {
  ducker_.setParams(s.thresholdDb, s.depthDb, s.holdMs, s.releaseMs);
  echo_.setParams(s.timeMs, s.feedback, s.toneHz, s.saturation, s.wowFlutter);
  mixTarget_ = s.bypass ? 0.f : std::clamp(s.mix, 0.f, 1.f);
  if (!mixInit_) {
    mix_ = mixTarget_;
    mixInit_ = true;
  }
}

void Engine::process(const float* inL, const float* inR, const float* scL, const float* scR,
                     float* outL, float* outR, int numSamples) {
  if (numSamples <= 0 || !inL || !outL) return;
  if (!inR) inR = inL;
  if (maxBlock_ == 0) {  // not prepared: pass through
    std::memmove(outL, inL, sizeof(float) * (size_t)numSamples);
    if (outR) std::memmove(outR, inR, sizeof(float) * (size_t)numSamples);
    return;
  }
  for (int off = 0; off < numSamples; off += maxBlock_) {
    const int n = std::min(maxBlock_, numSamples - off);
    processChunk(inL + off, inR + off, scL ? scL + off : nullptr, scR ? scR + off : nullptr,
                 outL + off, outR ? outR + off : nullptr, n);
  }
}

void Engine::processChunk(const float* inL, const float* inR, const float* scL, const float* scR,
                          float* outL, float* outR, int n) {
  // Copy everything we read before writing any output: buffers may alias.
  float* dL = dryL_.data();
  float* dR = dryR_.data();
  float* wL = wetL_.data();
  float* wR = wetR_.data();
  float* g = gain_.data();
  std::memcpy(dL, inL, sizeof(float) * (size_t)n);
  std::memcpy(dR, inR, sizeof(float) * (size_t)n);
  const float* detL = scL ? scL : dL;
  const float* detR = scL ? (scR ? scR : scL) : dR;
  for (int i = 0; i < n; ++i) g[i] = ducker_.processSample(detL[i], detR[i]);

  // Separate scratch per channel: TapeEcho must never see L == R.
  std::memcpy(wL, dL, sizeof(float) * (size_t)n);
  std::memcpy(wR, dR, sizeof(float) * (size_t)n);
  echo_.process(wL, wR, g, n);

  for (int i = 0; i < n; ++i) {
    mix_ = mixTarget_ + mixCoef_ * (mix_ - mixTarget_);
    if (std::fabs(mix_ - mixTarget_) < kMixSnap) mix_ = mixTarget_;
    const float m = mix_;
    outL[i] = dL[i] * (1.f - m) + wL[i] * m;
    if (outR) outR[i] = dR[i] * (1.f - m) + wR[i] * m;
  }
}

} // namespace FairyTapes::PillowTalk
