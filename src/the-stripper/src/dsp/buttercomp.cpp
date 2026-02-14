/**
 * Fairy Tapes · The Stripper — ButterComp algorithm (Airwindows, MIT).
 * Ported from Airwindows ButterCompProc.cpp.
 */

#include "buttercomp.h"
#include <cmath>
#include <cstdlib>

namespace FairyTapes::TheStripper::DSP {

ButterComp::ButterComp()
  : sampleRate_(44100.0),
    controlAposL_(1.0), controlAnegL_(1.0), targetposL_(1.0), targetnegL_(1.0),
    controlAposR_(1.0), controlAnegR_(1.0), targetposR_(1.0), targetnegR_(1.0),
    fpdL_(1u), fpdR_(1u) {
  while (fpdL_ < 16386u) fpdL_ = static_cast<unsigned int>(rand()) * 32767u;
  while (fpdR_ < 16386u) fpdR_ = static_cast<unsigned int>(rand()) * 32767u;
}

void ButterComp::setSampleRate(double sampleRate) {
  if (sampleRate > 0.0)
    sampleRate_ = sampleRate;
}

void ButterComp::process(float* left, float* right, int numSamples,
                        float amountNorm, float mixNorm) {
  const double A = static_cast<double>(amountNorm);
  const double B = static_cast<double>(mixNorm);
  double overallscale = 1.0 / 44100.0 * sampleRate_;

  const double inputgain = std::pow(10.0, (A * 14.0) / 20.0);
  double outputgain = inputgain;
  outputgain -= 1.0;
  outputgain /= 1.5;
  outputgain += 1.0;
  double divisor = 0.012 * (A / 135.0);
  divisor /= overallscale;
  const double remainder = divisor;
  divisor = 1.0 - divisor;

  for (int i = 0; i < numSamples; ++i) {
    double inputSampleL = static_cast<double>(left[i]);
    double inputSampleR = static_cast<double>(right[i]);
    if (std::fabs(inputSampleL) < 1.18e-23) inputSampleL = fpdL_ * 1.18e-17;
    if (std::fabs(inputSampleR) < 1.18e-23) inputSampleR = fpdR_ * 1.18e-17;
    const double drySampleL = inputSampleL;
    const double drySampleR = inputSampleR;

    inputSampleL *= inputgain;
    inputSampleR *= inputgain;

    double inputposL = inputSampleL + 1.0;
    if (inputposL < 0.0) inputposL = 0.0;
    double outputposL = inputposL / 2.0;
    if (outputposL > 1.0) outputposL = 1.0;
    inputposL *= inputposL;
    targetposL_ *= divisor;
    targetposL_ += (inputposL * remainder);
    double calcposL = std::pow(1.0 / targetposL_, 2);

    double inputnegL = (-inputSampleL) + 1.0;
    if (inputnegL < 0.0) inputnegL = 0.0;
    double outputnegL = inputnegL / 2.0;
    if (outputnegL > 1.0) outputnegL = 1.0;
    inputnegL *= inputnegL;
    targetnegL_ *= divisor;
    targetnegL_ += (inputnegL * remainder);
    double calcnegL = std::pow(1.0 / targetnegL_, 2);

    double inputposR = inputSampleR + 1.0;
    if (inputposR < 0.0) inputposR = 0.0;
    double outputposR = inputposR / 2.0;
    if (outputposR > 1.0) outputposR = 1.0;
    inputposR *= inputposR;
    targetposR_ *= divisor;
    targetposR_ += (inputposR * remainder);
    double calcposR = std::pow(1.0 / targetposR_, 2);

    double inputnegR = (-inputSampleR) + 1.0;
    if (inputnegR < 0.0) inputnegR = 0.0;
    double outputnegR = inputnegR / 2.0;
    if (outputnegR > 1.0) outputnegR = 1.0;
    inputnegR *= inputnegR;
    targetnegR_ *= divisor;
    targetnegR_ += (inputnegR * remainder);
    double calcnegR = std::pow(1.0 / targetnegR_, 2);

    if (inputSampleL > 0) {
      controlAposL_ *= divisor;
      controlAposL_ += (calcposL * remainder);
    } else {
      controlAnegL_ *= divisor;
      controlAnegL_ += (calcnegL * remainder);
    }
    if (inputSampleR > 0) {
      controlAposR_ *= divisor;
      controlAposR_ += (calcposR * remainder);
    } else {
      controlAnegR_ *= divisor;
      controlAnegR_ += (calcnegR * remainder);
    }

    double totalmultiplierL = (controlAposL_ * outputposL) + (controlAnegL_ * outputnegL);
    double totalmultiplierR = (controlAposR_ * outputposR) + (controlAnegR_ * outputnegR);

    inputSampleL *= totalmultiplierL;
    inputSampleL /= outputgain;
    inputSampleR *= totalmultiplierR;
    inputSampleR /= outputgain;

    if (B != 1.0) {
      inputSampleL = (inputSampleL * B) + (drySampleL * (1.0 - B));
      inputSampleR = (inputSampleR * B) + (drySampleR * (1.0 - B));
    }

    // 32-bit dither (simplified)
    fpdL_ ^= fpdL_ << 13;
    fpdL_ ^= fpdL_ >> 17;
    fpdL_ ^= fpdL_ << 5;
    fpdR_ ^= fpdR_ << 13;
    fpdR_ ^= fpdR_ >> 17;
    fpdR_ ^= fpdR_ << 5;

    left[i] = static_cast<float>(inputSampleL);
    right[i] = static_cast<float>(inputSampleR);
  }
}

} // namespace FairyTapes::TheStripper::DSP
