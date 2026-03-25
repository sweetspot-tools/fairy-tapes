/*
 * Transformer emulation — from Dusk Audio Multi-Comp (GPL-3.0).
 * Fairy Tapes · The Stripper. Original: HardwareEmulation/TransformerEmulation.h
 */

#pragma once

#include "HardwareProfiles.h"
#include "WaveshaperCurves.h"
#include <algorithm>
#include <cmath>

namespace HardwareEmulation {

class TransformerEmulation {
public:
  TransformerEmulation() = default;

  void prepare(double sampleRate, int numChannels = 2) {
    this->sampleRate = sampleRate;
    this->numChannels = numChannels;
    const float dcCutoff = 10.0f;
    dcBlockerCoeff = 1.0f - (6.283185f * dcCutoff / static_cast<float>(sampleRate));
    updateHFRolloff(20000.0f);
    reset();
  }

  void reset() {
    for (int ch = 0; ch < 2; ++ch) {
      dcBlockerX1[ch] = 0.0f;
      dcBlockerY1[ch] = 0.0f;
      hfFilterState[ch] = 0.0f;
      lastSample[ch] = 0.0f;
      hfEstimate[ch] = 0.0f;
    }
  }

  void setProfile(const TransformerProfile& newProfile) {
    profile = newProfile;
    enabled = profile.hasTransformer;
    updateHFRolloff(profile.highFreqRolloff);
  }

  void setEnabled(bool shouldBeEnabled) {
    enabled = shouldBeEnabled && profile.hasTransformer;
  }

  bool isEnabled() const { return enabled; }

  float processSample(float input, int channel) {
    if (!enabled)
      return input;
    channel = std::clamp(channel, 0, 1);
    float hfContent = estimateHighFrequencyContent(input, channel);
    float lfMultiplier = profile.lowFreqSaturation * (1.0f - hfContent * 0.5f);
    float driven = input * lfMultiplier;
    float saturated = getWaveshaperCurves().process(driven, WaveshaperCurves::CurveType::Transformer);
    float output = input + (saturated - input) * profile.saturationAmount;
    output = addHarmonics(output, profile.harmonics);
    output = applyHFRolloff(output, channel);
    output = processDCBlocker(output, channel);
    return output;
  }

private:
  TransformerProfile profile;
  double sampleRate = 44100.0;
  int numChannels = 2;
  bool enabled = false;

  float dcBlockerCoeff = 0.999f;
  float dcBlockerX1[2] = {0.0f, 0.0f};
  float dcBlockerY1[2] = {0.0f, 0.0f};
  float hfRolloffCoeff = 0.99f;
  float hfFilterState[2] = {0.0f, 0.0f};
  float lastSample[2] = {0.0f, 0.0f};
  float hfEstimate[2] = {0.0f, 0.0f};

  void updateHFRolloff(float cutoffFreq) {
    float w = 6.283185f * cutoffFreq / static_cast<float>(sampleRate);
    hfRolloffCoeff = w / (w + 1.0f);
  }

  float estimateHighFrequencyContent(float input, int channel) {
    float diff = std::abs(input - lastSample[channel]);
    lastSample[channel] = input;
    const float smoothing = 0.95f;
    hfEstimate[channel] = hfEstimate[channel] * smoothing + diff * (1.0f - smoothing);
    return std::clamp(hfEstimate[channel] * 3.0f, 0.0f, 1.0f);
  }

  float addHarmonics(float input, const HarmonicProfile& harmonics) {
    if (harmonics.h2 <= 0.0f && harmonics.h3 <= 0.0f && harmonics.h4 <= 0.0f)
      return input;
    float x = input;
    float x2 = x * x;
    float x3 = x2 * x;
    float output = x;
    output += harmonics.h2 * x2;
    output += harmonics.h3 * x3;
    if (harmonics.h4 > 0.0f)
      output += harmonics.h4 * x2 * x2;
    return output;
  }

  float applyHFRolloff(float input, int channel) {
    hfFilterState[channel] += hfRolloffCoeff * (input - hfFilterState[channel]);
    return hfFilterState[channel];
  }

  float processDCBlocker(float input, int channel) {
    float y = input - dcBlockerX1[channel] + dcBlockerCoeff * dcBlockerY1[channel];
    dcBlockerX1[channel] = input;
    dcBlockerY1[channel] = y;
    return y;
  }
};

}  // namespace HardwareEmulation
