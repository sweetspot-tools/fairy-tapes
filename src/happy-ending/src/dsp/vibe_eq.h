/**
 * Fairy Tapes · Happy Ending — VIBE EQ: Pultec-style passive EQ with a tube stage (spec §5.2).
 *
 * Ported without JUCE from the Tube EQ of Multi-Q (Dusk Audio, GPL-3.0), submodule path
 * vendors/dusk-audio-plugins/plugins/multi-q/TubeEQProcessor.h (InductorModel, TubeEqLFSection,
 * TubeEQTubeStage and the high-frequency boost path of TubeEQProcessor) and
 * vendors/dusk-audio-plugins/plugins/multi-q/ADAASaturation.h (first-order antiderivative
 * antialiasing of the tube polynomial).
 *
 * Changes from the source: plain C++ (std::array, double precision state), deterministic component
 * variation shared by the two channels, parameter smoothing replaced by linear coefficient ramps,
 * presets / transformers / UI hooks dropped. Behavioural changes, all voiced in tuning.h:
 *  - LOW: one knob drives the boost (bell at the chosen frequency) and the cut; the cut is a bell
 *    above the boost (the "Pultec trick"), because the source's low shelf at the same frequency
 *    cuts the whole bottom and cannot make a dip above the boost.
 *  - AIR: fixed broad Q, matched peaking (matched::computePeakingMatched) so that a bell high in the
 *    spectrum keeps its width near Nyquist; the source's frequency-dependent Q polynomial is built for the low end and
 *    would turn a 10 kHz boost into a flat lift of the whole band.
 *  - TILT: pair of opposite shelves around 1 kHz.
 *  - TUBE: the stage at the output of the network, run by processTube() so that the engine can put
 *    it inside an oversampled zone (prepareTube() prepares it at that rate). Faithful to the
 *    source: about -44 dB THD at most.
 *
 * processLinear() is the network (biquads and the inductor model, whose core saturation only
 * acts on loud boosted program); processTube() is the polynomial tube stage alone.
 */

#pragma once

#include "dsp/matched_biquad.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace FairyTapes::HappyEnding::DSP {

// Positions of the controls (indices into the value tables of params.h; LOW, AIR and TUBE are the
// 0-10 integers themselves).
struct VibeSettings {
  bool on = true;
  int lowFreqIndex = 2;   // 20 / 30 / 60 / 100 Hz
  int low = 0;
  int airFreqIndex = 3;   // 5 / 8 / 10 / 12 / 16 kHz
  int air = 0;
  int tiltIndex = 6;      // -3 .. +3 dB in 0.5 steps (6 = flat)
  int tube = 2;
  bool dynLow = false;
  bool dynAir = false;

  bool operator==(const VibeSettings&) const = default;
};

// Inductor of the LC network: frequency-dependent Q, core saturation and hysteresis.
class InductorModel {
public:
  void prepare(double sampleRate, std::uint32_t characterSeed = 0);
  void reset();
  // Q of the resonance: baseQ shaped by frequency, program level and component tolerance.
  double frequencyDependentQ(double hz, double baseQ) const;
  // The same without the program-dependent part (what the response curve draws).
  double staticQ(double hz, double baseQ) const;
  double processNonlinearity(double input, double driveLevel);
  double saturationLevel() const;
  double rmsLevel() const;

private:
  static double qShape(double hz);

  double rmsDecay_ = 0.9995, fluxDecay_ = 0.97, hystDecay_ = 0.92;
  double prevInput_ = 0.0, hysteresisState_ = 0.0, coreFlux_ = 0.0;
  double rmsLevel_ = 0.0, saturation_ = 0.0;
  double qVariation_ = 1.0, satVariation_ = 1.0;
};

// Tube stage: polynomial waveshaper with ADAA, extra transformer H3 on the low band, DC blocker.
class VibeTubeStage {
public:
  void prepare(double sampleRate);
  void reset();
  // `driveStart` -> `driveEnd` is a linear ramp over the block (drive 1.0 = the Multi-Q maximum).
  void process(float* left, float* right, int n, double driveStart, double driveEnd);

private:
  double processSample(double input, int ch, double drive);

  struct Channel {
    double lf = 0.0;     // low band (about 300 Hz) feeding the transformer H3
    double prevXd = 0.0; // ADAA previous sample
    double dcX1 = 0.0, dcY1 = 0.0;
  };
  double lfCoeff_ = 0.04;
  double dcCoeff_ = 0.999;
  std::array<Channel, 2> ch_{};
};

class VibeEq {
public:
  void prepare(double sampleRate, int maxBlock);
  // (Re)prepares only the tube stage, for a sample rate other than the one of the network: the
  // engine runs processTube() at the oversampled rate. prepare() calls it with its own rate.
  void prepareTube(double sampleRate);
  void set(const VibeSettings& settings);
  // The network: LOW (boost + cut), AIR, TILT. Identity while the stage is off.
  void processLinear(float* left, float* right, int n);
  // The tube stage at the output of the network; identity at TUBE 0 or while the stage is off.
  void processTube(float* left, float* right, int n);

  // How much the DYN is pulling the LOW / AIR boost back, 0..1 (the ACT LEDs).
  float dynLowAmount() const { return dynLow_.load(std::memory_order_relaxed); }
  float dynAirAmount() const { return dynAir_.load(std::memory_order_relaxed); }

  // Response in dB of the linear network at `hz`: with the settings alone (`staticSetting`) or
  // with the DYN pull-back at this moment. Call it from the thread that calls set().
  double magnitudeDb(double hz, bool staticSetting) const;

private:
  // One biquad whose coefficients glide linearly to a new target.
  struct Section {
    BiquadCoeffs current, target, step;
    double z[2][2] = {{0.0, 0.0}, {0.0, 0.0}};
    bool moving = false;

    void aim(const BiquadCoeffs& t, int len, bool snap);
    void advance();
    void finish();
    double run(double x, int ch);
    void clearState();
  };

  struct Network {
    BiquadCoeffs peak, dip, air, tiltLow, tiltHigh;
  };

  // The network for the settings, with the LOW / AIR amounts pulled back by `lowPull` / `airPull`
  // (0..1); `program` adds the program-dependent Q of the inductors (audio path only).
  Network designNetwork(double lowPull, double airPull, bool program) const;
  void updateDetectors(const float* l, const float* r, int len);
  void clearForBypass();

  double sampleRate_ = 48000.0;
  VibeSettings settings_;
  bool snap_ = true;
  int flushLeft_ = 0;  // samples still to run after the stage went off (the filters settle to identity)

  enum { kPeak, kDip, kAir, kTiltLow, kTiltHigh, kSections };
  std::array<Section, kSections> sections_{};
  std::array<InductorModel, 2> lowInductor_{}, airInductor_{};
  double lowInductorDrive_ = 0.0, airInductorDrive_ = 0.0;

  // DYN detectors (power followers on the band each boost works on).
  double attackCoeff_ = 0.0, releaseCoeff_ = 0.0;
  double lowLp_ = 0.0, airLp_ = 0.0, lowLpCoeff_ = 0.0, airLpCoeff_ = 0.0;
  double lowPower_ = 0.0, airPower_ = 0.0;
  double lowPull_ = 0.0, airPull_ = 0.0;
  std::atomic<float> dynLow_{0.0f}, dynAir_{0.0f};

  VibeTubeStage tube_;
  double tubeDrive_ = 0.0;
};

} // namespace FairyTapes::HappyEnding::DSP
