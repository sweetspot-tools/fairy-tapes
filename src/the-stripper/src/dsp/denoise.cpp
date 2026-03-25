/**
 * DeNoise process path ported from Airwindows DeNoiseProc.cpp (float processReplacing),
 * MIT license. Dither and denormal guard removed; parameters mapped to class members.
 */

#include "denoise.h"

#include <cstdlib>
#include <cmath>

namespace FairyTapes::TheStripper::DSP {

DeNoise::DeNoise() {
  reset();
}

void DeNoise::setSampleRate(double sampleRate) {
  sampleRate_ = sampleRate;
}

void DeNoise::reset() {
  for (int x = 0; x < kPearTotal; ++x) {
    pearA_[x] = 0.0;
    pearB_[x] = 0.0;
    pearC_[x] = 0.0;
    pearD_[x] = 0.0;
    pearE_[x] = 0.0;
  }
  topLevlL_ = 1.0;
  pearA_[levlL] = 1.0;
  pearB_[levlL] = 1.0;
  pearC_[levlL] = 1.0;
  pearD_[levlL] = 1.0;
  pearE_[levlL] = 1.0;
  topLevlR_ = 1.0;
  pearA_[levlR] = 1.0;
  pearB_[levlR] = 1.0;
  pearC_[levlR] = 1.0;
  pearD_[levlR] = 1.0;
  pearE_[levlR] = 1.0;

  fpdL_ = 1u;
  while (fpdL_ < 16386u) {
    fpdL_ = (static_cast<uint32_t>(rand()) << 16) ^
            static_cast<uint32_t>(rand() & 0xffff);
  }
  fpdR_ = 1u;
  while (fpdR_ < 16386u) {
    fpdR_ = (static_cast<uint32_t>(rand()) << 16) ^
            static_cast<uint32_t>(rand() & 0xffff);
  }
}

void DeNoise::setParams(float threshold, float speed, float wet) {
  threshold_ = threshold;
  speed_ = speed;
  wet_ = wet;
}

void DeNoise::process(float* left, float* right, int numSamples) {
  if (!left || !right || numSamples <= 0) {
    return;
  }

  double overallscale = 1.0;
  overallscale /= 44100.0;
  overallscale *= sampleRate_;

  const double t = static_cast<double>(threshold_);
  const double topThresh = std::pow(t * 0.25, 4.0);
  const double pearAThresh = topThresh;
  const double pearBThresh = topThresh;
  const double pearCThresh = topThresh;
  const double pearDThresh = topThresh;
  const double pearEThresh = topThresh;

  const double spd =
      std::pow(static_cast<double>(speed_), 3.0) / overallscale;
  const double wet = static_cast<double>(wet_);

  const double freqFactor = std::sqrt(overallscale) + (overallscale * 0.5);
  pearA_[freq] = std::pow(0.564, freqFactor + 0.85);
  pearB_[freq] = std::pow(0.564, freqFactor + 1.9);
  pearC_[freq] = std::pow(0.564, freqFactor + 4.1);
  pearD_[freq] = std::pow(0.564, freqFactor + 6.2);
  pearE_[freq] = std::pow(0.564, freqFactor + 7.7);

  float* in1 = left;
  float* in2 = right;

  while (--numSamples >= 0) {
    double inputSampleL = static_cast<double>(*in1);
    double inputSampleR = static_cast<double>(*in2);
    double drySampleL = inputSampleL;
    double drySampleR = inputSampleR;

    for (int x = 0; x < kPearMax; x += 4) {
      pearA_[figL] = inputSampleL;
      pearA_[figR] = inputSampleR;
      pearA_[slew] =
          ((pearA_[figL] - pearA_[x]) + pearA_[x + 1]) * pearA_[freq] * 0.5;
      pearA_[x] = pearA_[figL] =
          (pearA_[freq] * pearA_[figL]) +
          ((1.0 - pearA_[freq]) * (pearA_[x] + pearA_[x + 1]));
      pearA_[x + 1] = pearA_[slew];
      pearA_[slew] =
          ((pearA_[figR] - pearA_[x + 2]) + pearA_[x + 3]) * pearA_[freq] * 0.5;
      pearA_[x + 2] = pearA_[figR] =
          (pearA_[freq] * pearA_[figR]) +
          ((1.0 - pearA_[freq]) * (pearA_[x + 2] + pearA_[x + 3]));
      pearA_[x + 3] = pearA_[slew];
      inputSampleL -= pearA_[figL];
      inputSampleR -= pearA_[figR];

      pearB_[figL] = pearA_[figL];
      pearB_[figR] = pearA_[figR];
      pearB_[slew] =
          ((pearB_[figL] - pearB_[x]) + pearB_[x + 1]) * pearB_[freq] * 0.5;
      pearB_[x] = pearB_[figL] =
          (pearB_[freq] * pearA_[figL]) +
          ((1.0 - pearB_[freq]) * (pearB_[x] + pearB_[x + 1]));
      pearB_[x + 1] = pearB_[slew];
      pearB_[slew] =
          ((pearB_[figR] - pearB_[x + 2]) + pearB_[x + 3]) * pearB_[freq] * 0.5;
      pearB_[x + 2] = pearB_[figR] =
          (pearB_[freq] * pearA_[figR]) +
          ((1.0 - pearB_[freq]) * (pearB_[x + 2] + pearB_[x + 3]));
      pearB_[x + 3] = pearB_[slew];
      pearA_[figL] -= pearB_[figL];
      pearA_[figR] -= pearB_[figR];

      pearC_[figL] = pearB_[figL];
      pearC_[figR] = pearB_[figR];
      pearC_[slew] =
          ((pearC_[figL] - pearC_[x]) + pearC_[x + 1]) * pearC_[freq] * 0.5;
      pearC_[x] = pearC_[figL] =
          (pearC_[freq] * pearB_[figL]) +
          ((1.0 - pearC_[freq]) * (pearC_[x] + pearC_[x + 1]));
      pearC_[x + 1] = pearC_[slew];
      pearC_[slew] =
          ((pearC_[figR] - pearC_[x + 2]) + pearC_[x + 3]) * pearC_[freq] * 0.5;
      pearC_[x + 2] = pearC_[figR] =
          (pearC_[freq] * pearB_[figR]) +
          ((1.0 - pearC_[freq]) * (pearC_[x + 2] + pearC_[x + 3]));
      pearC_[x + 3] = pearC_[slew];
      pearB_[figL] -= pearC_[figL];
      pearB_[figR] -= pearC_[figR];

      pearD_[figL] = pearC_[figL];
      pearD_[figR] = pearC_[figR];
      pearD_[slew] =
          ((pearD_[figL] - pearD_[x]) + pearD_[x + 1]) * pearD_[freq] * 0.5;
      pearD_[x] = pearD_[figL] =
          (pearD_[freq] * pearC_[figL]) +
          ((1.0 - pearD_[freq]) * (pearD_[x] + pearD_[x + 1]));
      pearD_[x + 1] = pearD_[slew];
      pearD_[slew] =
          ((pearD_[figR] - pearD_[x + 2]) + pearD_[x + 3]) * pearD_[freq] * 0.5;
      pearD_[x + 2] = pearD_[figR] =
          (pearD_[freq] * pearC_[figR]) +
          ((1.0 - pearD_[freq]) * (pearD_[x + 2] + pearD_[x + 3]));
      pearD_[x + 3] = pearD_[slew];
      pearC_[figL] -= pearD_[figL];
      pearC_[figR] -= pearD_[figR];

      pearE_[figL] = pearD_[figL];
      pearE_[figR] = pearD_[figR];
      pearE_[slew] =
          ((pearE_[figL] - pearE_[x]) + pearE_[x + 1]) * pearE_[freq] * 0.5;
      pearE_[x] = pearE_[figL] =
          (pearE_[freq] * pearD_[figL]) +
          ((1.0 - pearE_[freq]) * (pearE_[x] + pearE_[x + 1]));
      pearE_[x + 1] = pearE_[slew];
      pearE_[slew] =
          ((pearE_[figR] - pearE_[x + 2]) + pearE_[x + 3]) * pearE_[freq] * 0.5;
      pearE_[x + 2] = pearE_[figR] =
          (pearE_[freq] * pearD_[figR]) +
          ((1.0 - pearE_[freq]) * (pearE_[x + 2] + pearE_[x + 3]));
      pearE_[x + 3] = pearE_[slew];
      pearD_[figL] -= pearE_[figL];
      pearD_[figR] -= pearE_[figR];

      topLevlL_ *= (1.0 - spd);
      topLevlL_ += ((std::fabs(inputSampleL) < topThresh) ? 0.0 : spd * 2.0);
      inputSampleL = (inputSampleL * std::fmin(topLevlL_, 1.0));
      pearA_[levlL] *= (1.0 - (spd * pearA_[freq]));
      pearA_[levlL] +=
          ((std::fabs(pearA_[figL]) < pearAThresh) ? 0.0 : (spd * 2.0 * pearA_[freq]));
      inputSampleL += (pearA_[figL] * std::fmin(pearA_[levlL], 1.0));
      pearB_[levlL] *= (1.0 - (spd * pearB_[freq]));
      pearB_[levlL] +=
          ((std::fabs(pearB_[figL]) < pearBThresh) ? 0.0 : (spd * 2.0 * pearB_[freq]));
      inputSampleL += (pearB_[figL] * std::fmin(pearB_[levlL], 1.0));
      pearC_[levlL] *= (1.0 - (spd * pearC_[freq]));
      pearC_[levlL] +=
          ((std::fabs(pearC_[figL]) < pearCThresh) ? 0.0 : (spd * 2.0 * pearC_[freq]));
      inputSampleL += (pearC_[figL] * std::fmin(pearC_[levlL], 1.0));
      pearD_[levlL] *= (1.0 - (spd * pearD_[freq]));
      pearD_[levlL] +=
          ((std::fabs(pearD_[figL]) < pearDThresh) ? 0.0 : (spd * 2.0 * pearD_[freq]));
      inputSampleL += (pearD_[figL] * std::fmin(pearD_[levlL], 1.0));
      pearE_[levlL] *= (1.0 - (spd * pearE_[freq]));
      pearE_[levlL] +=
          ((std::fabs(pearE_[figL]) < pearEThresh) ? 0.0 : (spd * 2.0 * pearE_[freq]));
      inputSampleL += (pearE_[figL] * std::fmin(pearE_[levlL], 1.0));

      topLevlR_ *= (1.0 - spd);
      topLevlR_ += ((std::fabs(inputSampleR) < topThresh) ? 0.0 : spd * 2.0);
      inputSampleR = (inputSampleR * std::fmin(topLevlR_, 1.0));
      pearA_[levlR] *= (1.0 - (spd * pearA_[freq]));
      pearA_[levlR] +=
          ((std::fabs(pearA_[figR]) < pearAThresh) ? 0.0 : (spd * 2.0 * pearA_[freq]));
      inputSampleR += (pearA_[figR] * std::fmin(pearA_[levlR], 1.0));
      pearB_[levlR] *= (1.0 - (spd * pearB_[freq]));
      pearB_[levlR] +=
          ((std::fabs(pearB_[figR]) < pearBThresh) ? 0.0 : (spd * 2.0 * pearB_[freq]));
      inputSampleR += (pearB_[figR] * std::fmin(pearB_[levlR], 1.0));
      pearC_[levlR] *= (1.0 - (spd * pearC_[freq]));
      pearC_[levlR] +=
          ((std::fabs(pearC_[figR]) < pearCThresh) ? 0.0 : (spd * 2.0 * pearC_[freq]));
      inputSampleR += (pearC_[figR] * std::fmin(pearC_[levlR], 1.0));
      pearD_[levlR] *= (1.0 - (spd * pearD_[freq]));
      pearD_[levlR] +=
          ((std::fabs(pearD_[figR]) < pearDThresh) ? 0.0 : (spd * 2.0 * pearD_[freq]));
      inputSampleR += (pearD_[figR] * std::fmin(pearD_[levlR], 1.0));
      pearE_[levlR] *= (1.0 - (spd * pearE_[freq]));
      pearE_[levlR] +=
          ((std::fabs(pearE_[figR]) < pearEThresh) ? 0.0 : (spd * 2.0 * pearE_[freq]));
      inputSampleR += (pearE_[figR] * std::fmin(pearE_[levlR], 1.0));
    }

    if (wet == 0.0) {
      drySampleL = inputSampleL - drySampleL;
      drySampleR = inputSampleR - drySampleR;
    }

    inputSampleL = (inputSampleL * wet) + (drySampleL * (1.0 - wet));
    inputSampleR = (inputSampleR * wet) + (drySampleR * (1.0 - wet));

    *in1 = static_cast<float>(inputSampleL);
    *in2 = static_cast<float>(inputSampleR);

    ++in1;
    ++in2;
  }
}

}  // namespace FairyTapes::TheStripper::DSP
