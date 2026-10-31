/**
 * Fairy Tapes · Happy Ending — ClipOnly2 (the safety clip of FINAL's CLIP mode, spec §5.6).
 *
 * Ported from ClipOnly2 by Chris Johnson (Airwindows, MIT licence, Copyright (c) 2016 airwindows),
 * submodule path vendors/airwindows/plugins/LinuxVST/src/ClipOnly2/ClipOnly2Proc.cpp.
 *
 * Kept from the source, unchanged: the clip at kKnee (0.9549925859, -0.4 dB) and the way the
 * samples around an overshoot are rounded: an overshooting sample is replaced by a value that leans
 * on its predecessor, and the next sample is used to re-aim that value once the signal turns back
 * (hysteresis of one sample, 0.7390851 hardness). Below the knee the signal passes untouched.
 *
 * Changes: the output dither and the float / double duplication are dropped (one double path).
 * The source's sample-delay buffer is dropped too: it was meant to delay by about one 44.1 kHz
 * sample ("spacing" samples), but its shifting loop copies the newest sample into every slot, so at
 * any rate the effect was one sample of delay with no influence on the shaped values. This port
 * therefore has a latency of exactly one sample at any rate and the same curve; it is the same
 * thing at the base rate and in the oversampled zone (no rate-dependent part is left).
 *
 * The clip works on a signal whose full scale is the knee: FinalStage scales the signal so that
 * the output CEILING sits on kKnee and scales back. Inputs are limited to +-4 as in the source.
 * Not safe against NaN / Inf in the input (the caller replaces non-finite samples).
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace FairyTapes::HappyEnding::DSP {

class ClipOnly2 {
public:
  static constexpr double kKnee = 0.9549925859;

  void reset() { ch_[0] = ch_[1] = Channel{}; }

  // One sample of channel 0 (left) or 1 (right); returns the sample of one step ago, shaped.
  double process(int channel, double x) { return ch_[channel & 1].step(x); }

private:
  struct Channel {
    double last = 0.0;
    bool wasPos = false;
    bool wasNeg = false;

    double step(double x) {
      x = std::clamp(x, -4.0, 4.0);
      if (wasPos) {  // current will be over
        if (x < last) last = 0.7058208 + (x * 0.2609148);
        else last = 0.2491717 + (last * 0.7390851);
      }
      wasPos = false;
      if (x > kKnee) {
        wasPos = true;
        x = 0.7058208 + (last * 0.2609148);
      }
      if (wasNeg) {  // current will be -over
        if (x > last) last = -0.7058208 + (x * 0.2609148);
        else last = -0.2491717 + (last * 0.7390851);
      }
      wasNeg = false;
      if (x < -kKnee) {
        wasNeg = true;
        x = -0.7058208 + (last * 0.2609148);
      }
      const double out = last;
      last = x;
      return out;
    }
  };

  Channel ch_[2];
};

} // namespace FairyTapes::HappyEnding::DSP
