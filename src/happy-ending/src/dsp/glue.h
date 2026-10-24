/**
 * Fairy Tapes · Happy Ending — GLUE: SSL-style bus compressor, in parallel (spec §5.3).
 *
 * Ported without JUCE from UniversalCompressor::BusCompressor of Multi-Comp (Dusk Audio, GPL-3.0),
 * submodule path vendors/dusk-audio-plugins/plugins/multi-comp/multicomp.cpp (class around line
 * 2538) with the Console Bus profile of HardwareEmulation/HardwareMeasurements.h and the active
 * transformer of HardwareEmulation/TransformerEmulation.h.
 *
 * Kept from the source: feedback detector (the high-passed, rectified, 5 ms RMS-averaged previous
 * compressed output), over-easy soft knee of 10 dB, 20 dB maximum reduction, exponential attack
 * and release, program-dependent Auto release (150-450 ms), input transformer, console harmonics
 * (k2, k3), output transformer and the gain compensation measured at prepare().
 * The short convolution of the Console Bus profile is a unity bypass in the source and is dropped.
 *
 * Changes: plain C++ and no allocation; the sidechain high pass is selectable (Off / 60 / 90 /
 * 150 Hz, 2 poles) instead of fixed at 60 Hz; the ratios come from the controls; stereo is always
 * fully linked (one gain for both channels, from the louder detector); WARM multiplies the attack
 * time constant; MAKEUP acts on the compressed branch only and MIX blends dry and compressed inside
 * the block (parallel compression); MAKEUP, MIX and the on/off glide linearly.
 */

#pragma once

namespace FairyTapes::HappyEnding::DSP {

struct GlueSettings {
  bool on = true;
  float threshDb = 0.0f;
  float attackMs = 3.0f;
  float releaseS = 0.3f;   // 0 = Auto
  float ratio = 2.0f;
  float makeupDb = 0.0f;
  float mix = 0.5f;        // 0..1
  float scHpfHz = 60.0f;   // 0 = Off
  bool warm = false;
};

class Glue {
public:
  void prepare(double sampleRate, int maxBlock);
  void set(const GlueSettings& settings);
  void process(float* left, float* right, int n);

  // Gain reduction of the compressor itself (dB, >= 0), at the end of the last block.
  float grDb() const;
  // Gain reduction seen at the output of the stage: after MIX, before MAKEUP (for the screen).
  float netGrDb() const;

private:
  // Active transformer of the Console Bus profile (saturation, hysteresis, harmonics, DC blocker).
  struct Transformer {
    float saturationAmount = 0.0f;
    float lowFreqSaturation = 1.0f;
    float hysteresisAmount = 0.0f;
    float h2 = 0.0f, h3 = 0.0f, h4 = 0.0f;
    float hfCoeff = 1.0f;
    bool hfEnabled = false;
    float dcCoeff = 0.999f;
    float dcX1[2] = {0.0f, 0.0f}, dcY1[2] = {0.0f, 0.0f};
    float hfState[2] = {0.0f, 0.0f};
    float lastSample[2] = {0.0f, 0.0f};
    float hfEstimate[2] = {0.0f, 0.0f};
    float hysteresis[2] = {0.0f, 0.0f};

    void prepare(double sampleRate, float saturation, float lowFreqSat, float hysteresisAmt,
                 float harmonic2, float harmonic3, float harmonic4, float hfRolloffHz, float dcHz);
    void reset();
    float process(float input, int channel);
  };

  // Sidechain of one channel: 2-pole high pass and RMS average of the previous compressed output.
  struct Detector {
    float hp1 = 0.0f, prev1 = 0.0f, hp2 = 0.0f, prev2 = 0.0f;
    float rms = 0.0f;
    float prevCompressed = 0.0f;
  };

  // Linear glide to a target over a number of samples.
  struct Ramp {
    float cur = 0.0f, target = 0.0f, step = 0.0f;
    int left = 0;
    void snap(float v) { cur = target = v; step = 0.0f; left = 0; }
    void to(float v, int samples);
    float next();
  };

  void reset();
  void calibrate();
  float reduction(float detection) const;

  double sampleRate_ = 48000.0;
  int rampSamples_ = 480;
  bool primed_ = false;
  GlueSettings s_;

  float rmsCoeff_ = 0.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float hpAlpha_ = 0.0f;
  bool hpOn_ = true;
  float hardwareGain_ = 1.0f;

  Transformer inT_, outT_;
  Detector det_[2];
  float envelope_ = 1.0f;
  float previousLevel_ = 0.0f;

  Ramp makeup_, mix_, engage_;
  float grDb_ = 0.0f;
  float netGrDb_ = 0.0f;
};

} // namespace FairyTapes::HappyEnding::DSP
