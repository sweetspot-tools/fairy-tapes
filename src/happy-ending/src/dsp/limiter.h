/**
 * Fairy Tapes · Happy Ending — LIMIT: look-ahead sample-peak limiter (spec §5.5).
 *
 * Own design (GPL-3.0), no third-party code.
 *
 * One linked gain for both channels. The gain is a smoothed envelope of the gain the signal asks for:
 * a sliding minimum over the look-ahead window, an exponential release, and a moving average as long
 * as the attack ramp. The delayed signal is multiplied by that gain, so the peaks arrive at the
 * ceiling with the ramp already complete (sample peak only, no true peak: spec §2).
 *
 * L2-style gain recovery: the gain is expressed as a total gain whose cap is ceiling / THRESH, so
 * a sample at the threshold comes out at the ceiling and everything is brought up with the reduction.
 * SET lowers only the threshold the reduction works from; the recovery stays that of THRESH, so the
 * squeeze is audible without a loudness jump (peaks then sit below the ceiling).
 *
 * Styles: MODERN (hard knee, look-ahead 1.5 ms), PUNCH (look-ahead 0.3 ms, ramp longer than the
 * look-ahead: the first instant of a transient passes), DYN (look-ahead 0.8 ms, soft knee, release
 * that follows the program). The latency is that of MODERN for every style, rounded up to a multiple
 * of `latencyMultiple` (the engine runs the limiter at 1x / 2x / 4x the base rate, and the chain
 * latency must be a whole number of base-rate samples). A final per-sample guard keeps every
 * output sample at or below the ceiling whatever the style, threshold, SET state or transition.
 * Off = the input delayed by the same latency, bit for bit (10 ms crossfade on switching).
 */

#pragma once

#include <cstdint>
#include <vector>

namespace FairyTapes::HappyEnding::DSP {

struct LimiterSettings {
  bool on = true;
  float threshDb = 0.0f;       // 0 .. -4 (the THRESH control)
  int style = 0;               // 0 MODERN, 1 PUNCH, 2 DYN (3 METAL: fixed 1 ms / 30 ms, used by METALHEAD only)
  int releaseTrimIndex = 4;    // 0..8: 4 = as the style wants, 2^((index - 4) / 3) around it
  float ceilingDb = -0.3f;     // OUTPUT CEIL
  bool set = false;            // guided release setting: threshold forced to Tuning::kSetThresholdDb

  bool operator==(const LimiterSettings&) const = default;
};

class Limiter {
public:
  // `latencyMultiple`: latencySamples() (at the rate given here) is always a multiple of it.
  void prepare(double sampleRate, int maxBlock, int latencyMultiple = 1);
  void set(const LimiterSettings& settings);
  void process(float* left, float* right, int n);

  int latencySamples() const { return delay_; }
  // Largest gain reduction applied in the last block, in dB (>= 0), relative to the gain recovery.
  float grDb() const { return grDb_; }
  // The threshold actually in force (gliding towards THRESH, or towards the SET value).
  float effectiveThreshDb() const { return static_cast<float>(effDb_); }

private:
  void apply(bool snap);
  void resetState();
  void refreshLevels();
  void recomputeEnvSum();
  double requiredGain(double peak) const;

  double sampleRate_ = 48000.0;
  LimiterSettings s_;
  bool primed_ = false;

  int delay_ = 1;          // latency, samples
  int window_ = 1;         // look-ahead of the current style, samples
  int attack_ = 1;         // attack ramp (moving average) of the current style, samples
  bool softKnee_ = false;
  double kneeFactor_ = 1.0;  // linear level (relative to the threshold) where the soft knee starts
  bool programRelease_ = false;
  double releaseSamples_ = 1.0;   // release time constant, samples
  double releaseCoef_ = 0.0;

  // Threshold and gain recovery.
  double effDb_ = 0.0;
  double targetDb_ = 0.0;      // where effDb_ is heading (THRESH, or the SET value)
  double knobDb_ = 0.0;        // THRESH, gliding: the gain recovery follows this one, even in SET
  double knobTargetDb_ = 0.0;
  double threshLin_ = 1.0;
  double ceilLin_ = 1.0;     // linear ceiling, as the largest float not above it
  double makeupLin_ = 1.0;
  double glidePerSample_ = 0.0;

  // Gain chain.
  std::vector<float> delayL_, delayR_;
  int delayPos_ = 0;
  std::vector<std::int64_t> dqIndex_;
  std::vector<double> dqValue_;
  int dqHead_ = 0, dqCount_ = 0;
  std::vector<double> envHist_;
  int envPos_ = 0;
  double envSum_ = 0.0;
  double env_ = 1.0;
  std::vector<double> gainRing_;
  std::int64_t n_ = 0;

  // DYN program-dependent release: reduction averaged over a long time, 0..1 of the reference.
  double sustain_ = 0.0;
  double sustainCoef_ = 0.0;

  // On / off crossfade (1 = limited).
  double mix_ = 1.0;
  double mixTarget_ = 1.0;
  double mixStep_ = 0.0;
  std::int64_t sinceRecompute_ = 0;

  float grDb_ = 0.0f;
};

} // namespace FairyTapes::HappyEnding::DSP
