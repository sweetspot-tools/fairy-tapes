/**
 * Fairy Tapes · The Stripper — Compression stage.
 * Simple compressor with Voice (Opto-like), Drums (FET-like), Classic (VCA dbx160-like), Bus.
 * Amount and Mix only; no JUCE. See COMPRESSION_MODES.md.
 */

#pragma once

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
  /** Process stereo in place. */
  void process(float* left, float* right, int numSamples);

private:
  double sampleRate_{44100.0};
  int mode_{0};
  float amount_{0.f};
  float mix_{1.f};

  float envelopeL_{1e-6f};
  float envelopeR_{1e-6f};
  float attackCoeff_{0.f};
  float releaseCoeff_{0.f};
  float thresholdDb_{-24.f};
  float ratio_{2.f};

  void updateCoeffs();
};

}  // namespace FairyTapes::TheStripper::DSP
