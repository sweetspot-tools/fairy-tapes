/**
 * Fairy Tapes · Happy Ending — METALHEAD: optional brickwall after FINAL (spec §5.7).
 *
 * Own design (GPL-3.0), no third-party code: the LIMIT engine (limiter.h) run in its fourth,
 * fixed style (look-ahead 1 ms, release 30 ms, hard knee, see Tuning::kMetal*), with the threshold
 * placed so that PUSH drives into the CEILING: THRESH = CEILING - PUSH, so the gain recovery is
 * exactly PUSH dB (a program under the ceiling comes out PUSH dB louder) and anything that would
 * go over the ceiling is brought down to it. Peaks never exceed the ceiling (the limiter's final
 * per-sample guard).
 *
 * Off = the input delayed by latencySamples(), bit for bit (10 ms crossfade on switching); the
 * latency is constant, on or off. As for the limiter it is a multiple of `latencyMultiple`.
 */

#pragma once

#include "dsp/limiter.h"

namespace FairyTapes::HappyEnding::DSP {

class Metalhead {
public:
  void prepare(double sampleRate, int maxBlock, int latencyMultiple = 1);
  // pushDb: 0..+6 (accepted up to +12). ceilingDb: <= 0.
  void set(bool on, float pushDb, float ceilingDb);
  void process(float* left, float* right, int n) { limiter_.process(left, right, n); }

  int latencySamples() const { return limiter_.latencySamples(); }
  // Largest gain reduction applied in the last block, in dB (>= 0), relative to the PUSH gain.
  float grDb() const { return limiter_.grDb(); }

private:
  Limiter limiter_;
};

} // namespace FairyTapes::HappyEnding::DSP
