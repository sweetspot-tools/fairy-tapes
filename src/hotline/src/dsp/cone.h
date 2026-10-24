#pragma once
#include "biquad.h"
#include <algorithm>

namespace FairyTapes::Hotline::DSP {

/** Speaker cone: peaking filter at hz plus a Q 3 "horn" peak at 1.6*hz. Cheap set(), keeps state. */
class Cone {
public:
  void prepare(double fs) { fs_ = fs; set(1000.0, 0.0, 1.0, 0.0); clear(); }
  void set(double hz, double gainDb, double q, double hornGainDb) {
    neutral_ = gainDb == 0.0 && hornGainDb == 0.0;
    if (neutral_) return;
    const double f1 = std::clamp(hz, 10.0, 0.45 * fs_);
    const double f2 = std::clamp(1.6 * hz, 10.0, 0.45 * fs_);
    designPeaking(main_, fs_, f1, std::max(q, 0.1), gainDb);
    designPeaking(horn_, fs_, f2, 3.0, hornGainDb);
  }
  float process(float x) { return neutral_ ? x : horn_.process(main_.process(x)); }
  void clear() { main_.clear(); horn_.clear(); }
  bool neutral() const { return neutral_; }

private:
  double fs_ = 48000.0;
  bool neutral_ = true;
  Biquad main_, horn_;
};

}  // namespace FairyTapes::Hotline::DSP
