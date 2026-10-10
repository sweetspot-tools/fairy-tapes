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

namespace {
// Normalized value clamped to [0,1]; non-finite falls back to the parameter default.
double norm(const double* v, int id, const double* defaults) {
  return std::isfinite(v[id]) ? std::clamp(v[id], 0.0, 1.0) : defaults[id];
}

bool finite(const Settings& s) {
  for (float x : {s.timeMs, s.feedback, s.toneHz, s.saturation, s.wowFlutter, s.mix,
                  s.thresholdDb, s.depthDb, s.holdMs, s.releaseMs})
    if (!std::isfinite(x)) return false;
  return true;
}
} // namespace

int noteIndexFromNorm(double n) {
  return std::clamp((int)(n * 4.0), 0, 3);
}

Settings mapParams(const double* raw, double bpm) {
  if (!std::isfinite(bpm) || bpm <= 0.0) bpm = kFallbackBpm;
  double defaults[kNumParams], v[kNumParams];
  loadDefaults(defaults);
  for (int i = 0; i < (int)kNumParams; ++i) v[i] = norm(raw, i, defaults);
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
  clipHoldSamples_ = (long long)(sr * kClipHoldSeconds);
  clipInLeft_ = clipOutLeft_ = 0;
  levels_ = LevelValues{};
}

void Engine::setSettings(const Settings& s) {
  if (!finite(s)) return;  // keep the last valid settings; never feed NaN/inf to the DSP
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
  voicePeak_ = echoPeak_ = 0.f;
  inPeakL_ = inPeakR_ = outPeakL_ = outPeakR_ = 0.f;
  for (int off = 0; off < numSamples; off += maxBlock_) {
    const int n = std::min(maxBlock_, numSamples - off);
    processChunk(inL + off, inR + off, scL ? scL + off : nullptr, scR ? scR + off : nullptr,
                 outL + off, outR ? outR + off : nullptr, n);
  }
  const auto toDb = [](float peak) { return 20.f * std::log10(std::max(peak, 1e-6f)); };
  display_.voiceDb = toDb(voicePeak_);
  display_.echoDb = toDb(echoPeak_);
  levels_.inL = toDb(inPeakL_);
  levels_.inR = toDb(inPeakR_);
  levels_.outL = toDb(outPeakL_);
  levels_.outR = toDb(outPeakR_);
  const auto hold = [&](long long& left, float peak) {
    left = peak >= 1.f ? clipHoldSamples_ : std::max(0LL, left - numSamples);
  };
  hold(clipInLeft_, std::max(inPeakL_, inPeakR_));
  hold(clipOutLeft_, std::max(outPeakL_, outPeakR_));
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
  for (int i = 0; i < n; ++i) {  // input level, measured before anything else
    inPeakL_ = std::max(inPeakL_, std::fabs(dL[i]));
    inPeakR_ = std::max(inPeakR_, std::fabs(dR[i]));
  }
  const float* detL = scL ? scL : dL;
  const float* detR = scL ? (scR ? scR : scL) : dR;
  for (int i = 0; i < n; ++i) {
    g[i] = ducker_.processSample(detL[i], detR[i]);
    // Display the level the ducker compares with the threshold (filtered mono envelope).
    voicePeak_ = std::max(voicePeak_, ducker_.detectorLevel());
  }
  display_.sendGain = g[n - 1];

  // Separate scratch per channel: TapeEcho must never see L == R. The tape input stays
  // fully open (the phrase is recorded); the ducker gain scales the wet output below.
  std::memcpy(wL, dL, sizeof(float) * (size_t)n);
  std::memcpy(wR, dR, sizeof(float) * (size_t)n);
  echo_.process(wL, wR, nullptr, n);

  for (int i = 0; i < n; ++i) {
    mix_ = mixTarget_ + mixCoef_ * (mix_ - mixTarget_);
    if (std::fabs(mix_ - mixTarget_) < kMixSnap) mix_ = mixTarget_;
    const float m = mix_;
    // Echo as it reaches the output: after ducking and the (smoothed) mix, so Bypass and Mix 0 read silent.
    echoPeak_ = std::max({echoPeak_, std::fabs(wL[i] * g[i] * m), std::fabs(wR[i] * g[i] * m)});
    outL[i] = dL[i] * (1.f - m) + (wL[i] * g[i]) * m;
    if (outR) outR[i] = dR[i] * (1.f - m) + (wR[i] * g[i]) * m;
    outPeakL_ = std::max(outPeakL_, std::fabs(outL[i]));
    outPeakR_ = std::max(outPeakR_, std::fabs(outR ? outR[i] : outL[i]));
  }
}

} // namespace FairyTapes::PillowTalk
