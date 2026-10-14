/**
 * 4K-EQ DSP for Channel mode (SSL 4000-style 4-band EQ + HPF/LPF).
 * Algorithm and coefficient logic from Luna Co. Audio 4K-EQ (GPL-3.0).
 * No oversampling in this integration; runs at host sample rate.
 */

#pragma once

#include "biquad.h"

namespace FairyTapes::TheStripper::DSP {

struct FourKEQParams {
  float hpfFreq = 20.0f;
  bool hpfEnabled = false;
  float lpfFreq = 20000.0f;
  bool lpfEnabled = false;
  float lfGain = 0.0f, lfFreq = 100.0f;
  bool lfBell = false;
  float lmGain = 0.0f, lmFreq = 600.0f, lmQ = 0.7f;
  float hmGain = 0.0f, hmFreq = 2000.0f, hmQ = 0.7f;
  float hfGain = 0.0f, hfFreq = 8000.0f;
  bool hfBell = false;
  bool eqTypeBlack = false;  // false = Brown (E), true = Black (G)
};

class FourKEQ {
public:
  FourKEQ() = default;

  /** Call when sample rate or max block size changes. */
  void prepare(double sampleRate, int maxBlockSize);

  /** Process stereo buffer in place. */
  void process(float* left, float* right, int numSamples, const FourKEQParams& p);

  void reset();

private:
  struct FilterBand {
    Biquad filterL;
    Biquad filterR;
    void reset();
    float processL(float x) { return filterL.processSample(x); }
    float processR(float x) { return filterR.processSample(x); }
  };

  struct HighPassFilter {
    Biquad stage1L, stage1R;
    FilterBand stage2;
    void reset();
    float processL(float x);
    float processR(float x);
  };

  struct AllPassBand {
    Biquad allPassL, allPassR;
    void reset();
    float processL(float x) { return allPassL.processSample(x); }
    float processR(float x) { return allPassR.processSample(x); }
  };

  void updateFilters(const FourKEQParams& p);
  void updateHPF(float hpfFreq);
  void updateLPF(float lpfFreq, bool isBlack);
  void updateLFBand(float gain, float freq, bool isBlack, bool isBell);
  void updateLMBand(float gain, float freq, float q, bool isBlack);
  void updateHMBand(float gain, float freq, float q, bool isBlack);
  void updateHFBand(float gain, float freq, bool isBlack, bool isBell);
  float preWarpFrequency(float freq, double sampleRate) const;
  float calculateDynamicQ(float gain, float baseQ) const;

  void setCoeffsFirstOrderHP(double sampleRate, float freq, Biquad& bq);
  void setCoeffsHighPass(double sampleRate, float freq, float q, Biquad& bqL, Biquad& bqR);
  void setCoeffsLowPass(double sampleRate, float freq, float q, Biquad& bqL, Biquad& bqR);
  void setCoeffsSSLShelf(double sampleRate, float freq, float q, float gainDB, bool isHighShelf, bool isBlack,
                         Biquad& bqL, Biquad& bqR);
  void setCoeffsSSLPeak(double sampleRate, float freq, float q, float gainDB, bool isBlack,
                        Biquad& bqL, Biquad& bqR);
  void setPhaseShiftFreq(double sampleRate, float freq);

  double sampleRate_ = 44100.0;
  int maxBlockSize_ = 512;

  HighPassFilter hpf_;
  FilterBand lpf_;
  FilterBand lfFilter_, lmFilter_, hmFilter_, hfFilter_;
  AllPassBand phaseShift_;

  FourKEQParams lastParams_;
  bool lastHpfEnabled_ = false;
  bool lastLpfEnabled_ = false;
};

}  // namespace FairyTapes::TheStripper::DSP
