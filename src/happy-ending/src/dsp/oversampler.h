/**
 * Fairy Tapes · Happy Ending — linear-phase polyphase oversampler (spec §9).
 *
 * Own code, no dependencies. Off / 2x / 4x: each 2x step is a symmetric half-band FIR (Kaiser
 * windowed sinc, designed in prepare()), run in polyphase form: the interpolator computes only
 * the non-zero taps, the odd output phase of the 2x step is a pure delay, and the decimator
 * skips the discarded samples. 4x is two cascaded 2x steps.
 *
 * Latency: latencySamples() is an integer number of samples at the base rate for the whole
 * up + down round trip (0 for factor 1). A half-band step of 4K+3 taps delays 2K+1 samples; in 4x
 * the inner pair would add a half sample at the base rate, so the decimating chain carries one
 * extra sample at 2x to make the total an exact integer (the engine's delay compensation, and
 * the sample-exact BYPASS, rely on it).
 *
 * One instance per channel and per zone. prepare() allocates; up(), down() and reset() do not.
 */

#pragma once

#include <vector>

namespace FairyTapes::HappyEnding::DSP {

class Oversampler {
public:
  // factor: 1, 2 or 4 (anything else is treated as 1). maxBlock: largest block at the base rate
  // (longer blocks are accepted and processed in chunks). Clears the state.
  void prepare(int factor, int maxBlock);
  void reset();

  int factor() const { return factor_; }
  // Round-trip delay (up then down) in samples at the base rate. Always an integer.
  int latencySamples() const { return latency_; }

  // n samples at the base rate -> n * factor samples at the high rate (returns that count).
  // `out` must hold n * factor samples and must not overlap `in`.
  int up(const float* in, int n, float* out);
  // nUp samples at the high rate (a multiple of factor) -> nUp / factor samples at the base rate.
  void down(const float* in, int nUp, float* out);

private:
  struct Stage {
    int k = 0;                  // the filter has 4k + 3 taps and delays c = 2k + 1 samples
    std::vector<float> even;    // taps h[2m], m = 0 .. 2k + 1, DC gain 1 for the whole filter
    std::vector<float> history; // past input followed by the current chunk
    void design(double passbandEdgeAtHighRate, double stopbandDb);
    void clear();
    int delay() const { return 2 * k + 1; }
  };
  void upStage(Stage& s, const float* in, int n, float* out);
  void downStage(Stage& s, const float* in, int nUp, float* out);

  int factor_ = 1;
  int maxBlock_ = 0;
  int latency_ = 0;
  Stage up1_, up2_, down1_, down2_;
  std::vector<float> tmp_;      // the 2x signal of the 4x chain
  float extraDelayState_ = 0.0f; // the one-sample delay at 2x of the 4x down chain
};

} // namespace FairyTapes::HappyEnding::DSP
