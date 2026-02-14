/**
 * SSL-style saturation for 4K-EQ stage.
 * Based on Luna Co. Audio SSLSaturation.h (4K-EQ plugin, GPL-3.0).
 * E-Series (Brown) vs G-Series (Black) harmonic character.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <random>

namespace FairyTapes::TheStripper::DSP {

class SSLSaturation {
public:
  enum class ConsoleType { ESeries, GSeries };

  SSLSaturation() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-0.05f, 0.05f);
    transformerTolerance_ = 1.0f + dist(gen);
    opAmpTolerance_ = 1.0f + dist(gen);
    outputTransformerTolerance_ = 1.0f + dist(gen);
    noiseGen_ = std::mt19937(rd());
    noiseDist_ = std::uniform_real_distribution<float>(-1.0f, 1.0f);
  }

  void setConsoleType(ConsoleType type) { consoleType_ = type; }

  void setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate;
    const float cutoffFreq = 5.0f;
    const float twoPi = 6.28318530718f;
    const float RC = 1.0f / (twoPi * cutoffFreq);
    dcBlockerCoeff_ = RC / (RC + 1.0f / static_cast<float>(sampleRate));
  }

  void reset() {
    dcBlockerX1_L_ = dcBlockerY1_L_ = 0.0f;
    dcBlockerX1_R_ = dcBlockerY1_R_ = 0.0f;
    lastSample_L_ = lastSample_R_ = 0.0f;
    highFreqEstimate_L_ = highFreqEstimate_R_ = 0.0f;
  }

  float processSample(float input, float drive, bool isLeftChannel) {
    if (drive < 0.001f)
      return input;

    float limited = input;
    float absInput = std::abs(input);
    if (absInput > 0.95f) {
      float excess = absInput - 0.95f;
      float compressed = 0.95f + std::tanh(excess * 3.0f) * 0.05f;
      limited = (input > 0.0f) ? compressed : -compressed;
    }

    float highFreqContent = estimateHighFrequencyContent(limited, isLeftChannel);
    float hfReduction = highFreqContent * (0.25f + drive * 0.35f);
    float effectiveDrive = drive * (1.0f - hfReduction);

    float transformed = processInputTransformer(limited, effectiveDrive * transformerTolerance_);
    float opAmpOut = processOpAmpStage(transformed, effectiveDrive * opAmpTolerance_);
    float output = (consoleType_ == ConsoleType::ESeries)
                       ? processOutputTransformer(opAmpOut, drive * 0.7f * outputTransformerTolerance_)
                       : opAmpOut;

    float noiseLevel = 0.00003162f * (1.0f + drive * 0.5f);
    output += noiseDist_(noiseGen_) * noiseLevel;
    output = processDCBlocker(output, isLeftChannel);

    float wetMix = std::clamp(drive * 1.4f, 0.0f, 1.0f);
    return input * (1.0f - wetMix) + output * wetMix;
  }

private:
  static float clamp01(float v) {
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    return v;
  }

  float estimateHighFrequencyContent(float input, bool isLeftChannel) {
    float& lastSample = isLeftChannel ? lastSample_L_ : lastSample_R_;
    float& estimate = isLeftChannel ? highFreqEstimate_L_ : highFreqEstimate_R_;
    float difference = std::abs(input - lastSample);
    lastSample = input;
    const float smoothing = 0.95f;
    estimate = estimate * smoothing + difference * (1.0f - smoothing);
    return clamp01(estimate * highFreqScale_);
  }

  float processInputTransformer(float input, float drive) {
    const float transformerDrive = 1.0f + drive * 7.0f;
    float driven = input * transformerDrive;
    float abs_x = std::abs(driven);

    float saturated;
    if (abs_x < 0.9f) {
      saturated = driven;
    } else if (abs_x < 1.5f) {
      float excess = abs_x - 0.9f;
      float compressed = 0.9f + excess * (1.0f - excess * 0.15f);
      saturated = (driven > 0.0f) ? compressed : -compressed;
    } else {
      float excess = abs_x - 1.5f;
      float compressed = 1.5f + std::tanh(excess * 1.5f) * 0.3f;
      saturated = (driven > 0.0f) ? compressed : -compressed;
    }

    float threshold = (consoleType_ == ConsoleType::ESeries) ? 0.6f : 0.05f;
    if (abs_x > threshold) {
      float saturationAmount = (abs_x - threshold) / (1.2f - threshold);
      saturationAmount = clamp01(saturationAmount);
      if (consoleType_ == ConsoleType::ESeries) {
        saturated += saturated * saturated * (0.12f * saturationAmount);
      } else {
        saturated += saturated * saturated * (0.025f * saturationAmount);
        saturated += saturated * saturated * saturated * (0.050f * saturationAmount);
      }
    }
    return saturated / transformerDrive;
  }

  float processOpAmpStage(float input, float drive) {
    const float opAmpDrive = 1.0f + drive * 9.0f;
    float driven = input * opAmpDrive;
    float output;

    if (driven > 0.0f) {
      if (driven < 1.0f) {
        output = driven;
      } else if (driven < 1.8f) {
        float excess = driven - 1.0f;
        output = 1.0f + excess * (1.0f - excess * 0.2f);
      } else {
        float clipHardness = (consoleType_ == ConsoleType::ESeries) ? 1.5f : 2.0f;
        output = 1.5f + std::tanh((driven - 1.8f) * clipHardness) * 0.3f;
      }
    } else {
      if (driven > -1.0f) {
        output = driven;
      } else if (driven > -1.9f) {
        float excess = -driven - 1.0f;
        output = -1.0f - excess * (1.0f - excess * 0.18f);
      } else {
        float clipHardness = (consoleType_ == ConsoleType::ESeries) ? 1.5f : 2.0f;
        output = -1.55f + std::tanh((driven + 1.9f) * clipHardness) * 0.3f;
      }
    }

    float threshold = (consoleType_ == ConsoleType::ESeries) ? 0.6f : 0.05f;
    if (std::abs(driven) > threshold) {
      float saturationAmount = (std::abs(driven) - threshold) / (1.5f - threshold);
      saturationAmount = clamp01(saturationAmount);
      if (consoleType_ == ConsoleType::ESeries) {
        output += output * output * std::copysign(0.10f * saturationAmount, output);
      } else {
        output += output * output * std::copysign(0.022f * saturationAmount, output);
        output += output * output * output * (0.040f * saturationAmount);
      }
    }
    return output / opAmpDrive;
  }

  float processOutputTransformer(float input, float drive) {
    const float transformerDrive = 1.0f + drive * 2.0f;
    float driven = input * transformerDrive;
    float abs_x = std::abs(driven);

    float saturated;
    if (abs_x < 0.5f) {
      saturated = driven;
    } else if (abs_x < 0.9f) {
      float excess = abs_x - 0.5f;
      float compressed = 0.5f + excess * (1.0f - excess * 0.25f);
      saturated = (driven > 0.0f) ? compressed : -compressed;
    } else {
      float excess = abs_x - 0.9f;
      float compressed = 0.9f + std::tanh(excess * 1.5f) * 0.15f;
      saturated = (driven > 0.0f) ? compressed : -compressed;
    }
    saturated += saturated * saturated * 0.05f;
    return saturated / transformerDrive;
  }

  float processDCBlocker(float input, bool isLeftChannel) {
    float& x1 = isLeftChannel ? dcBlockerX1_L_ : dcBlockerX1_R_;
    float& y1 = isLeftChannel ? dcBlockerY1_L_ : dcBlockerY1_R_;
    float output = input - x1 + dcBlockerCoeff_ * y1;
    x1 = input;
    y1 = output;
    return output;
  }

  ConsoleType consoleType_ = ConsoleType::ESeries;
  double sampleRate_ = 44100.0;
  float dcBlockerX1_L_ = 0.0f, dcBlockerY1_L_ = 0.0f;
  float dcBlockerX1_R_ = 0.0f, dcBlockerY1_R_ = 0.0f;
  float dcBlockerCoeff_ = 0.999f;
  float lastSample_L_ = 0.0f, lastSample_R_ = 0.0f;
  float highFreqEstimate_L_ = 0.0f, highFreqEstimate_R_ = 0.0f;
  float highFreqScale_ = 3.0f;
  float transformerTolerance_ = 1.0f, opAmpTolerance_ = 1.0f, outputTransformerTolerance_ = 1.0f;
  std::mt19937 noiseGen_;
  std::uniform_real_distribution<float> noiseDist_;
};

}  // namespace FairyTapes::TheStripper::DSP
