/**
 * Fairy Tapes · The Stripper — Compression stage.
 * Multi-Comp DSP (port from Dusk Audio Multi-Comp, GPL-3.0): Voice (Opto), Drums (FET), Classic (VCA), Bus.
 * Same API as before; implementation uses multicomp_engines. See COMPRESSION_MODES.md.
 */

#pragma once

#include "multicomp_engines.h"

namespace FairyTapes::TheStripper::DSP {

class CompressionStub {
public:
  CompressionStub() = default;

  void setSampleRate(double sampleRate);
  void prepare(int maxBlockSize);

  /** Mode: 0=Opto (Voice), 1=FET (Drums), 2=VCA (Classic), 3=Bus (Mix bus). */
  void setMode(int mode);
  /** amount 0..1 (more = more compression), mix 0=dry 1=wet. */
  void setParams(float amount, float mix);
  /** Process stereo in place. Optional sidechain: when non-null, used for envelope detection only. */
  void process(float* left, float* right, int numSamples,
               const float* sidechainL = nullptr, const float* sidechainR = nullptr);

private:
  double sampleRate_{44100.0};
  int mode_{0};
  float amount_{0.f};
  float mix_{1.f};

  MultiCompOpto opto_;
  MultiCompFET fet_;
  MultiCompVCA vca_;
  MultiCompBus bus_;
};

}  // namespace FairyTapes::TheStripper::DSP
