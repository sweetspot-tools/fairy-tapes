#include "dsp/tape_echo.h"
#include <algorithm>

namespace FairyTapes::PillowTalk::DSP {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxSeconds = 2.0;
constexpr double kGlideMs = 120.0;
constexpr double kHpHz = 100.0;
constexpr double kButterworthQ = 0.7071068;
constexpr double kWowHz = 0.6, kFlutterHz = 7.0, kNoiseLpHz = 30.0;
// Peak delay modulation (ms) at wowFlutter = 1.
constexpr float kWowMs = 2.0f, kFlutterMs = 0.15f, kNoiseMs = 0.1f;
constexpr float kBias = 0.1f;
constexpr float kMaxFeedback = 0.95f;
// Group delay of the 2x up/down half-band pair, in base-rate samples.
constexpr float kOsLatency = 2.5f;
constexpr float kMinDelay = 2.f;
constexpr float kDenormal = 1e-20f;

float flushDenormal(float x) { return std::fabs(x) < kDenormal ? 0.f : x; }
} // namespace

void TapeEcho::Channel::clear() {
  std::fill(tape.begin(), tape.end(), 0.f);
  lp.clear();
  hp.clear();
  std::fill(std::begin(up), std::end(up), 0.f);
  std::fill(std::begin(down), std::end(down), 0.f);
}

void TapeEcho::prepare(double sampleRate, int /*maxBlockSize*/) {
  sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  int len = 1;
  while (len < (int)(kMaxSeconds * sr_) + 8) len <<= 1;
  mask_ = len - 1;
  for (auto& c : ch_) {
    c.tape.assign((size_t)len, 0.f);
    designHighpass(c.hp, sr_, kHpHz, kButterworthQ);
  }
  glideCoef_ = (float)std::exp(-1.0 / (0.001 * kGlideMs * sr_));
  wowInc_ = kWowHz / sr_;
  flutterInc_ = kFlutterHz / sr_;
  noiseCoef_ = (float)std::exp(-2.0 * kPi * kNoiseLpHz / sr_);
  designTone();
  reset();
}

void TapeEcho::setParams(float timeMs, float feedback, float toneHz, float saturation, float wowFlutter) {
  timeMs_ = std::max(0.f, timeMs);
  feedback_ = std::clamp(feedback, 0.f, kMaxFeedback);
  drive_ = 1.f + 3.f * std::clamp(saturation, 0.f, 1.f);
  wowFlutter_ = std::clamp(wowFlutter, 0.f, 1.f);
  if (toneHz != toneHz_) {
    toneHz_ = toneHz;
    designTone();
  }
}

void TapeEcho::designTone() {
  const double fc = std::clamp((double)toneHz_, 20.0, 0.45 * sr_);
  for (auto& c : ch_) designLowpass(c.lp, sr_, fc, kButterworthQ);
}

void TapeEcho::reset() {
  for (auto& c : ch_) c.clear();
  writePos_ = 0;
  delayInit_ = false;
  delaySmoothed_ = 0.f;
  wowPhase_ = flutterPhase_ = 0.0;
  noiseState_ = 0.f;
  rng_ = 0x12345678u;
}

float TapeEcho::nextNoise() {
  rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
  return (float)((rng_ >> 8) * (1.0 / 16777216.0) * 2.0 - 1.0);
}

float TapeEcho::readTape(const Channel& c, float delay) const {
  const int di = (int)delay;
  const float f = delay - (float)di;
  const auto at = [&](int k) { return c.tape[(size_t)((writePos_ - k) & mask_)]; };
  // Hermite between y0 (delay di) and y1 (delay di + 1).
  const float ym1 = at(di - 1), y0 = at(di), y1 = at(di + 1), y2 = at(di + 2);
  const float c1 = 0.5f * (y1 - ym1);
  const float c2 = ym1 - 2.5f * y0 + 2.f * y1 - 0.5f * y2;
  const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
  return ((c3 * f + c2) * f + c1) * f + y0;
}

float TapeEcho::saturate2x(Channel& c, float x) const {
  // Upsample: cubic half-band interpolator (taps -1, 9, 9, -1 / 16).
  std::copy(c.up + 1, c.up + 4, c.up);
  c.up[3] = x;
  const float mid = (-c.up[0] + 9.f * c.up[1] + 9.f * c.up[2] - c.up[3]) * (1.f / 16.f);
  const float os[2] = {mid, c.up[2]};

  // (tanh(d(s + b)) - tanh(d b)) / (d sech^2(d b)), rewritten without
  // cancellation: exactly 0 for s = 0, unity small-signal gain, no float limit
  // cycles near zero. Bounded by 1 / (d (1 - tanh(d b))).
  const float tb = std::tanh(drive_ * kBias);
  const float inv = 1.f / drive_;
  for (float s : os) {
    std::copy(c.down + 1, c.down + 7, c.down);
    const float ts = std::tanh(drive_ * s);
    c.down[6] = ts / (1.f + ts * tb) * inv;
  }
  // Decimate: 7-tap half-band (-1, 0, 9, 16, 9, 0, -1) / 32.
  return (-c.down[0] + 9.f * c.down[2] + 16.f * c.down[3] + 9.f * c.down[4] - c.down[6]) * (1.f / 32.f);
}

float TapeEcho::recordPath(Channel& c, float x) const {
  return flushDenormal(c.hp.process(saturate2x(c, c.lp.process(x))));
}

void TapeEcho::process(float* L, float* R, const float* sendGain, int numSamples) {
  if (numSamples <= 0 || ch_[0].tape.empty()) return;
  const float target = (float)(0.001 * timeMs_ * sr_) - kOsLatency;
  const float maxDelay = (float)(mask_ - 4);
  if (!delayInit_) {
    delaySmoothed_ = target;
    delayInit_ = true;
  }
  const float wowAmp = (float)(0.001 * sr_) * kWowMs * wowFlutter_;
  const float flutterAmp = (float)(0.001 * sr_) * kFlutterMs * wowFlutter_;
  // Scale one-pole-filtered uniform noise to roughly unit RMS.
  const float noiseAmp = (float)(0.001 * sr_) * kNoiseMs * wowFlutter_ *
                         std::sqrt(3.f * (1.f + noiseCoef_) / (1.f - noiseCoef_));

  for (int i = 0; i < numSamples; ++i) {
    delaySmoothed_ = target + glideCoef_ * (delaySmoothed_ - target);
    noiseState_ = noiseCoef_ * noiseState_ + (1.f - noiseCoef_) * nextNoise();
    const float mod = wowAmp * (float)std::sin(2.0 * kPi * wowPhase_) +
                      flutterAmp * (float)std::sin(2.0 * kPi * flutterPhase_) +
                      noiseAmp * noiseState_;
    wowPhase_ += wowInc_;
    if (wowPhase_ >= 1.0) wowPhase_ -= 1.0;
    flutterPhase_ += flutterInc_;
    if (flutterPhase_ >= 1.0) flutterPhase_ -= 1.0;
    const float delay = std::clamp(delaySmoothed_ + mod, kMinDelay, maxDelay);

    const float send = sendGain ? sendGain[i] : 1.f;
    float* io[2] = {L + i, R + i};
    for (int k = 0; k < 2; ++k) {
      Channel& c = ch_[k];
      const float wet = readTape(c, delay);
      c.tape[(size_t)writePos_] = recordPath(c, *io[k] * send + feedback_ * wet);
      *io[k] = wet;
    }
    writePos_ = (writePos_ + 1) & mask_;
  }
}

} // namespace FairyTapes::PillowTalk::DSP
