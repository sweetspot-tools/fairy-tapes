#include "dsp/metalhead.h"
#include <algorithm>

namespace FairyTapes::HappyEnding::DSP {

void Metalhead::prepare(double sampleRate, int maxBlock, int latencyMultiple) {
  limiter_.prepare(sampleRate, maxBlock, latencyMultiple);
}

void Metalhead::set(bool on, float pushDb, float ceilingDb) {
  const float ceiling = std::min(ceilingDb, 0.0f);
  LimiterSettings s;
  s.on = on;
  s.style = 3;  // METAL: look-ahead 1 ms, release 30 ms, hard knee
  s.releaseTrimIndex = 4;
  s.ceilingDb = ceiling;
  s.threshDb = ceiling - std::clamp(pushDb, 0.0f, 12.0f);
  s.set = false;
  limiter_.set(s);
}

} // namespace FairyTapes::HappyEnding::DSP
