#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace FairyTapes::PillowTalk::DSP {

/**
 * Stereo tape echo. Outputs only the wet signal.
 * Record path per channel: (in * sendGain + feedback * playback) -> LP(tone)
 * -> biased tanh at 2x oversampling -> HP 100 Hz -> tape. Every pass through
 * the loop darkens and compresses the repeat. Playback reads the tape with
 * 4-point Hermite interpolation at a glided time modulated by wow and flutter.
 */
class TapeEcho {
public:
  void prepare(double sampleRate, int maxBlockSize);
  void setParams(float timeMs, float feedback, float toneHz, float saturation, float wowFlutter);
  void process(float* L, float* R, const float* sendGain, int numSamples);
  void reset();

private:
  struct Biquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;
    float process(float x) {
      const float y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      if (std::fabs(z1) < 1e-20f) z1 = 0.f;
      if (std::fabs(z2) < 1e-20f) z2 = 0.f;
      return y;
    }
    void clear() { z1 = z2 = 0.f; }
  };

  struct Channel {
    std::vector<float> tape;
    Biquad lp, hp;
    float up[4] = {};    // base-rate history for the 2x interpolator
    float down[7] = {};  // 2x-rate history for the half-band decimator
    void clear();
  };

  float readTape(const Channel& c, float delaySamples) const;
  float saturate2x(Channel& c, float x) const;
  float recordPath(Channel& c, float x) const;
  float nextNoise();
  void designTone();

  double sr_ = 48000.0;
  Channel ch_[2];
  int mask_ = 0, writePos_ = 0;

  float timeMs_ = 100.f, feedback_ = 0.f, toneHz_ = 12000.f, drive_ = 1.f, wowFlutter_ = 0.f;
  float glideCoef_ = 0.f, delaySmoothed_ = 0.f;
  bool delayInit_ = false;

  double wowPhase_ = 0.0, flutterPhase_ = 0.0;
  double wowInc_ = 0.0, flutterInc_ = 0.0;
  float noiseCoef_ = 0.f, noiseState_ = 0.f;
  uint32_t rng_ = 0x12345678u;
};

} // namespace FairyTapes::PillowTalk::DSP
