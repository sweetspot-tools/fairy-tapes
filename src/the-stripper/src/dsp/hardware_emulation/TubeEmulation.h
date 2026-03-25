/*
 * Tube emulation — from Dusk Audio Multi-Comp (GPL-3.0).
 * Fairy Tapes · The Stripper. Original: HardwareEmulation/TubeEmulation.h
 */

#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace HardwareEmulation {

class TubeEmulation {
public:
  enum class TubeType {
    Triode_12AX7,
    Triode_12AT7,
    Triode_12BH7,  // LA-2A output stage
    Triode_6SN7
  };

  TubeEmulation() {
    initializePlateTransferFunction();
    updateTubeParameters();
  }

  void prepare(double sampleRate, int numChannels = 2) {
    this->sampleRate = sampleRate;
    this->numChannels = numChannels;
    updateCoefficients();
    reset();
  }

  void reset() {
    for (int ch = 0; ch < 2; ++ch) {
      millerCapState[ch] = 0.0f;
      gridCurrent[ch] = 0.0f;
      cathodeBypassState[ch] = 0.0f;
      dcBlockerX1[ch] = 0.0f;
      dcBlockerY1[ch] = 0.0f;
    }
  }

  void setTubeType(TubeType type) {
    currentType = type;
    updateTubeParameters();
  }

  void setDrive(float driveAmount) {
    drive = std::clamp(driveAmount, 0.0f, 1.0f);
    inputGain = 1.0f + drive * 2.0f;
  }

  void setBiasPoint(float bias) {
    biasOffset = std::clamp(bias, -1.0f, 1.0f) * 0.2f;
  }

  float processSample(float input, int channel) {
    channel = std::clamp(channel, 0, 1);
    float driven = input * inputGain;
    float gridVoltage = driven + biasOffset;

    if (gridVoltage > gridCurrentThreshold) {
      float excess = gridVoltage - gridCurrentThreshold;
      gridCurrent[channel] = excess * gridCurrentCoeff;
      gridVoltage -= gridCurrent[channel];
    } else {
      gridCurrent[channel] *= gridCurrentDischargeCoeff;
    }

    float plateVoltage = applyPlateTransferFunction(gridVoltage);
    cathodeBypassState[channel] = cathodeBypassState[channel] * cathodeBypassCoeff
                                  + plateVoltage * (1.0f - cathodeBypassCoeff);
    float cathodeEffect = plateVoltage * (1.0f - cathodeBypassAmount)
                         + cathodeBypassState[channel] * cathodeBypassAmount;

    float hfContent = cathodeEffect - millerCapState[channel];
    millerCapState[channel] += hfContent * millerCapCoeff;
    float output = cathodeEffect - hfContent * millerCapEffect * drive;
    output *= outputScaling;
    return processDCBlocker(output, channel);
  }

private:
  static constexpr int TRANSFER_TABLE_SIZE = 4096;
  std::array<float, TRANSFER_TABLE_SIZE> plateTransferTable{};

  TubeType currentType = TubeType::Triode_12AX7;
  double sampleRate = 44100.0;
  int numChannels = 2;
  float drive = 0.0f;
  float inputGain = 1.0f;
  float outputScaling = 1.0f;
  float biasOffset = 0.0f;
  float gridCurrentThreshold = 0.5f;
  float gridCurrentCoeff = 0.2f;
  float cathodeBypassCoeff = 0.98f;
  float cathodeBypassAmount = 0.3f;
  float millerCapCoeff = 0.3f;
  float millerCapEffect = 0.1f;
  float millerCapState[2] = {0.0f, 0.0f};
  float gridCurrent[2] = {0.0f, 0.0f};
  float cathodeBypassState[2] = {0.0f, 0.0f};
  float dcBlockerX1[2] = {0.0f, 0.0f};
  float dcBlockerY1[2] = {0.0f, 0.0f};
  float dcBlockerCoeff = 0.999f;
  float cathodeBypassCoeffBase = 0.98f;
  float millerCapCoeffBase = 0.3f;
  float gridCurrentDischargeBase = 0.95f;
  float gridCurrentDischargeCoeff = 0.95f;

  void initializePlateTransferFunction() {
    for (int i = 0; i < TRANSFER_TABLE_SIZE; ++i) {
      float vg = (static_cast<float>(i) / (TRANSFER_TABLE_SIZE - 1)) * 4.0f - 2.0f;
      if (vg >= 0.0f) {
        if (vg < 0.8f)
          plateTransferTable[i] = vg + vg * vg * 0.02f;
        else {
          float excess = vg - 0.8f;
          float base = 0.8f + 0.8f * 0.8f * 0.02f;
          plateTransferTable[i] = base + excess / (1.0f + excess * 0.5f);
        }
      } else {
        float absVg = std::abs(vg);
        if (absVg < 0.8f)
          plateTransferTable[i] = vg - vg * absVg * 0.02f;
        else if (absVg < 1.5f) {
          float excess = absVg - 0.8f;
          float base = 0.8f + 0.8f * 0.8f * 0.02f;
          plateTransferTable[i] = -(base + excess * (1.0f - excess * 0.3f));
        } else {
          float excess = absVg - 1.5f;
          plateTransferTable[i] = -(1.01f + std::tanh(excess * 1.5f) * 0.15f);
        }
      }
    }
  }

  float applyPlateTransferFunction(float gridVoltage) {
    float normalized = (gridVoltage + 2.0f) * 0.25f;
    normalized = std::clamp(normalized, 0.0f, 0.9999f);
    float idx = normalized * (TRANSFER_TABLE_SIZE - 1);
    int i0 = static_cast<int>(idx);
    int i1 = std::min(i0 + 1, TRANSFER_TABLE_SIZE - 1);
    float frac = idx - static_cast<float>(i0);
    return plateTransferTable[i0] * (1.0f - frac) + plateTransferTable[i1] * frac;
  }

  void updateTubeParameters() {
    switch (currentType) {
      case TubeType::Triode_12BH7:
        gridCurrentThreshold = 0.8f;
        gridCurrentCoeff = 0.04f;
        cathodeBypassCoeffBase = 0.96f;
        cathodeBypassAmount = 0.1f;
        millerCapCoeffBase = 0.2f;
        millerCapEffect = 0.02f;
        outputScaling = 0.98f;
        break;
      case TubeType::Triode_12AX7:
        gridCurrentThreshold = 0.7f;
        gridCurrentCoeff = 0.08f;
        cathodeBypassCoeffBase = 0.98f;
        cathodeBypassAmount = 0.15f;
        millerCapCoeffBase = 0.35f;
        millerCapEffect = 0.05f;
        outputScaling = 0.95f;
        break;
      case TubeType::Triode_12AT7:
        gridCurrentThreshold = 0.75f;
        gridCurrentCoeff = 0.06f;
        cathodeBypassCoeffBase = 0.97f;
        cathodeBypassAmount = 0.12f;
        millerCapCoeffBase = 0.25f;
        millerCapEffect = 0.04f;
        outputScaling = 0.95f;
        break;
      case TubeType::Triode_6SN7:
        gridCurrentThreshold = 0.7f;
        gridCurrentCoeff = 0.07f;
        cathodeBypassCoeffBase = 0.975f;
        cathodeBypassAmount = 0.12f;
        millerCapCoeffBase = 0.28f;
        millerCapEffect = 0.04f;
        outputScaling = 0.95f;
        break;
      default:
        gridCurrentThreshold = 0.7f;
        gridCurrentCoeff = 0.08f;
        cathodeBypassCoeffBase = 0.98f;
        cathodeBypassAmount = 0.15f;
        millerCapCoeffBase = 0.35f;
        millerCapEffect = 0.05f;
        outputScaling = 0.95f;
    }
    updateCoefficients();
  }

  void updateCoefficients() {
    float dcCutoff = 10.0f;
    dcBlockerCoeff = 1.0f - (6.283185f * dcCutoff / static_cast<float>(sampleRate));
    float rateRatio = 44100.0f / static_cast<float>(sampleRate);
    cathodeBypassCoeff = std::pow(cathodeBypassCoeffBase, rateRatio);
    millerCapCoeff = std::pow(millerCapCoeffBase, rateRatio);
    gridCurrentDischargeCoeff = std::pow(gridCurrentDischargeBase, rateRatio);
  }

  float processDCBlocker(float input, int channel) {
    float y = input - dcBlockerX1[channel] + dcBlockerCoeff * dcBlockerY1[channel];
    dcBlockerX1[channel] = input;
    dcBlockerY1[channel] = y;
    return y;
  }
};

}  // namespace HardwareEmulation
