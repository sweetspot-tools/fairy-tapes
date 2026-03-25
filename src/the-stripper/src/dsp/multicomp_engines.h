/**
 * Fairy Tapes · The Stripper — Multi-Comp DSP engines (port from Dusk Audio Multi-Comp, GPL-3.0).
 * Opto (Voice), FET (Drums), VCA (Classic dbx 160), Bus (SSL). No JUCE; real-time safe.
 * Includes hardware emulation (transformer, tube) from Dusk Audio.
 */

#pragma once

#include "hardware_emulation/TransformerEmulation.h"
#include "hardware_emulation/TubeEmulation.h"

namespace FairyTapes::TheStripper::DSP {

// Mode: 0=Opto (Voice), 1=FET (Drums), 2=VCA (Classic), 3=Bus (Mix bus)
enum class MultiCompMode : int { Opto = 0, FET = 1, VCA = 2, Bus = 3 };

// Shared params: amount 0..1 (more = more compression), mix 0=dry 1=wet
struct MultiCompParams {
  float amount{0.5f};
  float mix{1.0f};
};

// Opto (LA-2A style): T4B dual time-constant, feedback, variable ratio, two-stage release + transformer + tube
class MultiCompOpto {
public:
  void prepare(double sampleRate);
  void setParams(float amount, float mix);
  /** When sidechainL/sidechainR are non-null, envelope detection uses sidechain instead of main. */
  void processStereo(float* left, float* right, int numSamples,
                     const float* sidechainL = nullptr, const float* sidechainR = nullptr);

private:
  double sampleRate_{44100.0};
  float peakReduction_{0.f};
  float mix_{1.f};
  float envelopeL_{1.f};
  float envelopeR_{1.f};
  float fastMemoryL_{0.f};
  float fastMemoryR_{0.f};
  float slowMemoryL_{0.f};
  float slowMemoryR_{0.f};
  float hfFilterL_{0.f};
  float hfFilterR_{0.f};
  float releaseStartLevel_{1.f};
  int releasePhase_{0};
  float maxReduction_{0.f};
  float holdCounter_{0.f};
  float prevInputL_{0.f};
  float prevInputR_{0.f};
  HardwareEmulation::TransformerEmulation inputTransformer_;
  HardwareEmulation::TransformerEmulation outputTransformer_;
  HardwareEmulation::TubeEmulation tube_;
};

// FET (1176 style): fixed threshold, input gain from amount, fast attack/release + transformer
class MultiCompFET {
public:
  void prepare(double sampleRate);
  void setParams(float amount, float mix);
  /** When sidechainL/sidechainR are non-null, envelope detection uses sidechain instead of main. */
  void processStereo(float* left, float* right, int numSamples,
                     const float* sidechainL = nullptr, const float* sidechainR = nullptr);

private:
  double sampleRate_{44100.0};
  float inputGainDb_{-10.f};
  float mix_{1.f};
  float envelopeL_{1.f};
  float envelopeR_{1.f};
  float previousLevelL_{0.f};
  float previousLevelR_{0.f};
  float previousGR_{0.f};
  HardwareEmulation::TransformerEmulation inputTransformer_;
  HardwareEmulation::TransformerEmulation outputTransformer_;
};

// VCA (dbx 160 style): RMS detection, OverEasy soft knee, feedforward
class MultiCompVCA {
public:
  void prepare(double sampleRate);
  void setParams(float amount, float mix);
  /** When sidechainL/sidechainR are non-null, envelope detection uses sidechain (RMS) instead of main. */
  void processStereo(float* left, float* right, int numSamples,
                     const float* sidechainL = nullptr, const float* sidechainR = nullptr);

private:
  double sampleRate_{44100.0};
  float thresholdDb_{-12.f};
  float ratio_{3.f};
  float mix_{1.f};
  float envelopeL_{1.f};
  float envelopeR_{1.f};
  float rmsL_{0.f};
  float rmsR_{0.f};
  float previousInputL_{0.f};
  float previousInputR_{0.f};
  float overshootL_{0.f};
  float overshootR_{0.f};
};

// Bus (SSL G-Series style): sidechain HP 60Hz, smooth envelope + transformer
class MultiCompBus {
public:
  void prepare(double sampleRate);
  void setParams(float amount, float mix);
  /** When sidechainL/sidechainR are non-null, envelope detection uses sidechain (with 60Hz HP) instead of main. */
  void processStereo(float* left, float* right, int numSamples,
                     const float* sidechainL = nullptr, const float* sidechainR = nullptr);

private:
  double sampleRate_{44100.0};
  float thresholdDb_{-12.f};
  float ratio_{2.5f};
  float mix_{1.f};
  float envelopeL_{1.f};
  float envelopeR_{1.f};
  float hpStateL_{0.f};
  float hpStateR_{0.f};
  float prevInputL_{0.f};
  float prevInputR_{0.f};
  float previousLevelL_{0.f};
  float previousLevelR_{0.f};
  float previousGR_{0.f};
  float hpStateScL_{0.f};
  float hpStateScR_{0.f};
  float prevScL_{0.f};
  float prevScR_{0.f};
  HardwareEmulation::TransformerEmulation inputTransformer_;
  HardwareEmulation::TransformerEmulation outputTransformer_;
};

}  // namespace FairyTapes::TheStripper::DSP
