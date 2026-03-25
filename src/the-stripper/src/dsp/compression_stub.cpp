/**
 * Fairy Tapes · The Stripper — Compression stage (Multi-Comp DSP).
 * Delegates to Opto / FET / VCA / Bus engines. Real-time safe. See COMPRESSION_MODES.md.
 */

#include "compression_stub.h"
#include "multicomp_engines.h"
#include <algorithm>

namespace FairyTapes::TheStripper::DSP {

void CompressionStub::setSampleRate(double sampleRate) {
  if (sampleRate > 0. && sampleRate < 500000.)
    sampleRate_ = sampleRate;
  opto_.prepare(sampleRate_);
  fet_.prepare(sampleRate_);
  vca_.prepare(sampleRate_);
  bus_.prepare(sampleRate_);
}

void CompressionStub::prepare(int /* maxBlockSize */) {
  opto_.prepare(sampleRate_);
  fet_.prepare(sampleRate_);
  vca_.prepare(sampleRate_);
  bus_.prepare(sampleRate_);
}

void CompressionStub::setMode(int mode) {
  mode_ = std::max(0, std::min(3, mode));
}

void CompressionStub::setParams(float amount, float mix) {
  amount_ = std::max(0.f, std::min(1.f, amount));
  mix_ = std::max(0.f, std::min(1.f, mix));
  opto_.setParams(amount_, mix_);
  fet_.setParams(amount_, mix_);
  vca_.setParams(amount_, mix_);
  bus_.setParams(amount_, mix_);
}

void CompressionStub::process(float* left, float* right, int numSamples,
                              const float* sidechainL, const float* sidechainR) {
  const float* scL = sidechainL;
  const float* scR = (sidechainR != nullptr) ? sidechainR : sidechainL;
  switch (mode_) {
    case 0:
      opto_.processStereo(left, right, numSamples, scL, scR);
      break;
    case 1:
      fet_.processStereo(left, right, numSamples, scL, scR);
      break;
    case 2:
      vca_.processStereo(left, right, numSamples, scL, scR);
      break;
    case 3:
      bus_.processStereo(left, right, numSamples, scL, scR);
      break;
    default:
      vca_.processStereo(left, right, numSamples, scL, scR);
  }
}

}  // namespace FairyTapes::TheStripper::DSP
