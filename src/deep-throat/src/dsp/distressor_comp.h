// Fairy Tapes · Deep Throat — distressor-style compressor. No VST3 SDK dependency.
//
// Log-domain feedforward design (Giannoulis, Massberg, Reiss 2012): the gain computer works on
// the instantaneous detector level in dB, then the gain reduction (dB) is smoothed.
//
// Detector: each channel goes through its own 2nd-order Butterworth high-pass (OFF / 80 / 150 Hz,
// detector path only); the linked level is max(|hpL|, |hpR|, ...), so every channel gets the
// same gain. Gain computer: threshold -6 - 24 * Amount dBFS, 6 dB soft knee, ratio from the
// choice (AUTO = mode: Speak 2:1, Sing 3:1, Puke 8:1).
//
// Ballistics (dB domain): attack 6 / r ms (x 0.5 in Puke, min 0.05 ms). Release blends a fast
// (50 ms) and a slow (clamp(100 r, 200, 3500) ms) one-pole coefficient by a sustain weight taken
// from the detector crest factor (peak envelope over RMS envelope, both 100 ms): crest <= 6 dB
// (sustained, e.g. a sine) -> slow, >= 12 dB (transient) -> fast, linear in dB between. The
// blended coefficient always lies between two stable coefficients.
//
// Wet path: shaper (on the input, before the gain) -> gain -> makeup. Dist 2: x + k2 g x^2 (even,
// asymmetric; the x^2 term is DC-blocked). Dist 3: x + LP12k(HP30(k3 g x^3)): only the added
// odd product is band-limited, the signal itself is never filtered (flat with no reduction).
// g = clamp(GR / 12, 0, 1) * (1 + 0.5 * attackFactor), attackFactor 1 at <= 0.5 ms attack, 0 at
// >= 10 ms, linear in log(time) between. The three variants (clean, Dist 2, Dist 3) always run
// and are crossfaded (20 ms) so a Dist change does not click.
// Makeup: max(0, GR_ref - 2 dB) on the wet path only; GR_ref = static reduction of a sine with a
// -15 dBFS peak (-18 dBFS RMS) at the current threshold and ratio. Threshold, makeup and mix are
// smoothed (20 ms). The wet path ends in a safety soft clip (identity up to 0 dBFS, never above 2)
// for the onset overshoot of a loud transient after silence (no lookahead, makeup up to ~12 dB).
// Mix: out = dry * (1 - mix) + wet * mix, dry = the (sanitised: non-finite -> 0, bounded to
// +-1e6) input.
//
// processFrame() takes one sample per channel and is called per sample.
#pragma once

#include "params.h"
#include "dsp/biquad.h"

#include <vector>

namespace FairyTapes::DeepThroat::DSP {

class DistressorComp {
public:
  void prepare(double sampleRate, int channels);
  void reset();
  void setMode(Mode mode);
  void setAmount(float amount01);  // 0..1
  void setMix(float mix01);        // 1 = all compressed, 0 = input only (parallel)
  void setRatioChoice(int c);      // 0 AUTO, 1..7 = 2, 3, 4, 6, 8, 10, 20 :1
  void setDistChoice(int c);       // 0 AUTO, 1 OFF, 2 Dist 2, 3 Dist 3
  void setDetectorHpf(int c);      // 0 OFF, 1 80 Hz (default), 2 150 Hz
  void processFrame(float* samples);
  float lastGainReductionDb() const { return (float)gr_; }

private:
  struct Channel {
    Biquad det;        // detector high-pass
    Biquad hp3, lp3;   // Dist 3 band limits (distortion product only)
    double dcX = 0.0, dcY = 0.0;  // Dist 2 DC blocker state
  };

  void updateTargets();

  double sr_ = 48000.0;
  Mode mode_ = Mode::Speak;
  float amount_ = 0.5f, mix_ = 1.0f;
  int ratioChoice_ = 0, distChoice_ = 0, hpfChoice_ = 1;

  // Derived targets (updated by the setters).
  double ratio_ = 2.0, thresholdTarget_ = -18.0, makeupTarget_ = 0.0;
  double w2Target_ = 0.0, w3Target_ = 0.0;
  double attackCoeff_ = 0.0, fastCoeff_ = 0.0, slowCoeff_ = 0.0, attackFactor_ = 0.0;

  // Smoothed values.
  double threshold_ = -18.0, makeup_ = 0.0, mixS_ = 1.0, w2_ = 0.0, w3_ = 0.0;
  double smoothCoeff_ = 0.0, envCoeff_ = 0.0, dcCoeff_ = 0.0;

  // Detector / ballistics state.
  double peakEnv_ = 0.0, msEnv_ = 0.0, gr_ = 0.0;
  std::vector<Channel> ch_;
};

}  // namespace FairyTapes::DeepThroat::DSP
