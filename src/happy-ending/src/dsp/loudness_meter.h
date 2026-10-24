/**
 * Fairy Tapes · Happy Ending — LUFS meter (ITU-R BS.1770-4, spec §3).
 *
 * Own implementation (GPL-3.0) of the published algorithm; the way the K-weighting biquads are
 * derived for any sample rate (analog prototype + bilinear transform) follows the approach of
 * libebur128 (MIT, Jan Kokemüller), no code copied.
 *
 * K-weighting = pre-filter (high shelf, about +4 dB at high frequencies) + RLB high pass, then the
 * mean square of each channel (weights 1.0 for L and R) summed. The signal is cut into 100 ms
 * sub-blocks (the hop); a gating block is 400 ms = 4 sub-blocks (75 % overlap), the short-term
 * window is 3 s = 30 sub-blocks.
 *
 *  - short-term: mean over the last 3 s, refreshed every 100 ms; -INFINITY until 3 s are in.
 *  - integrated: gated mean of the 400 ms blocks since the last reset: absolute gate -70 LUFS,
 *    then relative gate 10 LU under the mean of the blocks that passed the first one. The block
 *    loudness is kept in a fixed histogram (0.1 LU bins, -70 to +5 LUFS, each bin with the exact
 *    energy sum of its blocks), so the memory never grows however long the meter runs.
 *    -INFINITY while no block has passed the gates.
 *
 * Threading: process() and resetIntegrated() belong to the audio thread (a UI reset request must be
 * forwarded to it); shortTermLufs() / integratedLufs() read values published at each hop and are
 * safe from any thread. No allocation after construction.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace FairyTapes::HappyEnding::DSP {

// One K-weighting stage: normalised biquad (a0 = 1), y = b0 x + s1; s1 = b1 x - a1 y + s2; s2 = b2 x - a2 y.
struct KBiquad {
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct KWeighting {
  KBiquad shelf;     // BS.1770 pre-filter (high shelf)
  KBiquad highPass;  // RLB weighting
};

// The two biquads of the K-weighting, derived for `sampleRate` (matches the standard's tables at 48 kHz).
KWeighting designKWeighting(double sampleRate);

class LoudnessMeter {
public:
  LoudnessMeter();

  void prepare(double sampleRate);
  void process(const float* left, const float* right, int n);

  double shortTermLufs() const { return shortTerm_.load(std::memory_order_relaxed); }
  double integratedLufs() const { return integrated_.load(std::memory_order_relaxed); }
  // Forgets the gated blocks and starts again; the short-term window is not touched.
  void resetIntegrated();

private:
  static constexpr int kSubBlocksPerBlock = 4;     // 400 ms
  static constexpr int kSubBlocksShortTerm = 30;   // 3 s
  static constexpr int kBinsPerLu = 10;
  static constexpr double kHistogramMinLufs = -70.0;
  static constexpr double kHistogramMaxLufs = 5.0;
  static constexpr int kBins = static_cast<int>((kHistogramMaxLufs - kHistogramMinLufs) * kBinsPerLu);

  struct Stage {
    double s1 = 0.0, s2 = 0.0;
  };

  void subBlockDone();
  void addBlock(double energy);
  void updateIntegrated();
  double shortTermValue() const;

  KWeighting k_{};
  std::array<Stage, 2> shelfState_{};
  std::array<Stage, 2> hpState_{};

  int hop_ = 4800;             // samples per sub-block
  int hopPos_ = 0;
  double hopEnergy_ = 0.0;     // sum of the K-weighted squares (L + R) of the sub-block in progress
  std::array<double, kSubBlocksShortTerm> sub_{};  // ring of completed sub-block energies (sums)
  int subPos_ = 0;
  int subCount_ = 0;           // completed sub-blocks, saturates at kSubBlocksShortTerm
  int sinceReset_ = 0;         // completed sub-blocks that started after the last reset

  std::array<double, kBins> binEnergy_{};   // sum of the block energies (mean square) per bin
  std::array<std::uint32_t, kBins> binCount_{};

  std::atomic<double> shortTerm_;
  std::atomic<double> integrated_;
};

} // namespace FairyTapes::HappyEnding::DSP
