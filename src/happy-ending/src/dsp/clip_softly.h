/**
 * Fairy Tapes · Happy Ending — CLIP: level-matched soft clipper (spec §5.4).
 *
 * Ported from ClipSoftly by Chris Johnson (Airwindows, MIT licence, Copyright (c) 2016 airwindows),
 * submodule path vendors/airwindows/plugins/LinuxVST/src/ClipSoftly/ClipSoftlyProc.cpp.
 *
 * Kept from the source: the sine shaper (input limited to +-pi/2, so the output saturates smoothly
 * at 1.0) and the "soft speed" blend: above full scale the shaped sample is mixed with the previous
 * one in proportion to 1/|input|, which rounds the very top of a hard-driven peak.
 *
 * Changes: the output dither and the float / double duplication are dropped (one double path); the
 * source's 0.9549925859 scale ("what ClipOnly uses", -0.4 dB) is dropped so a small signal passes at
 * exactly unity; the source's sample-delay buffer is dropped as well. It was meant to give a delay
 * of about one 44.1 kHz sample, but its shifting loop copies the newest sample into every slot, so
 * the effect was always one sample of delay at any rate, with no influence on the shaped values:
 * this port has zero latency and the same curve. The shaper is memoryless except for that blend,
 * so it behaves the same at any sample rate (the engine runs it at the oversampled rate).
 *
 * DRIVE (0..+12 dB accepted, the plugin uses 0..+6) is the gain in front of the shaper and its
 * reciprocal behind it: level matched, quiet parts pass unchanged and the loudest possible output
 * is 1 / drive. The gain glides linearly over Tuning::kClipRampMs. Not safe against NaN / Inf in
 * the input: the engine replaces non-finite samples at its entry.
 */

#pragma once

#include "tuning.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::HappyEnding::DSP {

class ClipSoftly {
public:
  void prepare(double sampleRate) {
    rampSamples_ = std::max(1, static_cast<int>(std::lround(Tuning::kClipRampMs * 0.001 * sampleRate)));
    lastL_ = lastR_ = 0.0;
    gain_ = target_;
    remaining_ = 0;
    shavedDb_ = 0.0f;
  }

  // The same drive again (the processor sends it every block) leaves a glide in progress alone.
  void setDriveDb(float db) {
    const double target = std::pow(10.0, std::clamp(static_cast<double>(db), 0.0, 12.0) / 20.0);
    if (target == target_) return;
    target_ = target;
    remaining_ = rampSamples_;
    step_ = (target_ - gain_) / rampSamples_;
  }

  void process(float* left, float* right, int n) {
    double inPeak = 0.0, outPeak = 0.0;
    for (int i = 0; i < n; ++i) {
      if (remaining_ > 0) {
        gain_ += step_;
        if (--remaining_ == 0) gain_ = target_;
      }
      const double inL = left[i], inR = right[i];
      inPeak = std::max(inPeak, std::max(std::fabs(inL), std::fabs(inR)));
      const double outL = shape(inL * gain_, lastL_) / gain_;
      const double outR = shape(inR * gain_, lastR_) / gain_;
      outPeak = std::max(outPeak, std::max(std::fabs(outL), std::fabs(outR)));
      left[i] = static_cast<float>(outL);
      right[i] = static_cast<float>(outR);
    }
    shavedDb_ = (inPeak > 1e-9 && outPeak > 1e-12)
                    ? static_cast<float>(std::max(0.0, 20.0 * std::log10(inPeak / outPeak)))
                    : 0.0f;
  }

  // dB taken off the peaks of the last block (both channels together), for the GR row "C".
  float shavedDb() const { return shavedDb_; }

private:
  // One channel of ClipSoftly's shaper: `last` is the previous shaped sample.
  static double shape(double u, double& last) {
    constexpr double kHalfPi = 1.57079633;
    const double mag = std::fabs(u);
    const double softSpeed = mag < 1.0 ? 1.0 : 1.0 / mag;
    const double clipped = std::clamp(u, -kHalfPi, kHalfPi);
    const double y = std::sin(clipped) * softSpeed + last * (1.0 - softSpeed);
    last = y;
    return y;
  }

  int rampSamples_ = 480;
  int remaining_ = 0;
  double gain_ = 1.0;
  double target_ = 1.0;
  double step_ = 0.0;
  double lastL_ = 0.0;
  double lastR_ = 0.0;
  float shavedDb_ = 0.0f;
};

} // namespace FairyTapes::HappyEnding::DSP
