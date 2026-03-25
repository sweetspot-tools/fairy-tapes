/**
 * Fairy Tapes · The Stripper — Multi-Comp DSP engines implementation.
 * Port from Dusk Audio Multi-Comp (GPL-3.0). Real-time safe; no allocations in process.
 * Hardware emulation: transformer + tube (Opto), transformer only (FET, Bus).
 */

#include "multicomp_engines.h"
#include "hardware_emulation/HardwareProfiles.h"
#include <algorithm>
#include <cmath>

namespace FairyTapes::TheStripper::DSP {

namespace {

constexpr float kEps = 1e-8f;
constexpr float kOutputLimit = 2.0f;

inline float gainToDb(float g) {
  if (g <= kEps) return -80.f;
  return 20.f * std::log10(g);
}

inline float dbToGain(float db) {
  return std::pow(10.f, db * 0.05f);
}

inline float clamp(float v, float lo, float hi) {
  return std::max(lo, std::min(hi, v));
}

// T4B / Opto constants (from Multi-Comp Constants namespace)
constexpr float T4B_FAST_ATTACK = 0.010f;
constexpr float T4B_FAST_RELEASE = 0.060f;
constexpr float T4B_SLOW_PERSISTENCE = 0.200f;
constexpr float T4B_MEMORY_COUPLING = 0.4f;
constexpr float OPTO_ATTACK_TIME = 0.010f;
constexpr float OPTO_RELEASE_FAST_MIN = 0.040f;
constexpr float OPTO_RELEASE_FAST_MAX = 0.080f;
constexpr float OPTO_RELEASE_SLOW_MIN = 0.5f;
constexpr float OPTO_RELEASE_SLOW_MAX = 2.0f;

// FET
constexpr float FET_THRESHOLD_DB = -10.0f;
constexpr float FET_MAX_REDUCTION_DB = 30.0f;
constexpr float FET_MIN_ATTACK = 0.0001f;
constexpr float FET_MAX_ATTACK = 0.0008f;
constexpr float FET_MIN_RELEASE = 0.05f;
constexpr float FET_MAX_RELEASE = 1.1f;

// VCA
constexpr float VCA_RMS_TIME = 0.003f;
constexpr float VCA_MAX_REDUCTION_DB = 60.0f;
constexpr float VCA_OVEREASY_KNEE = 10.0f;

// Bus
constexpr float BUS_HP_FREQ = 60.0f;
constexpr float BUS_MAX_REDUCTION_DB = 20.0f;
constexpr float BUS_ATTACK_MS = 2.0f;
constexpr float BUS_RELEASE_MS = 300.0f;

}  // namespace

// ----- Opto (Voice) -----
void MultiCompOpto::prepare(double sampleRate) {
  if (sampleRate > 0. && sampleRate < 500000.)
    sampleRate_ = sampleRate;
  envelopeL_ = envelopeR_ = 1.f;
  fastMemoryL_ = fastMemoryR_ = 0.f;
  slowMemoryL_ = slowMemoryR_ = 0.f;
  hfFilterL_ = hfFilterR_ = 0.f;
  releaseStartLevel_ = 1.f;
  releasePhase_ = 0;
  maxReduction_ = 0.f;
  holdCounter_ = 0.f;
  prevInputL_ = prevInputR_ = 0.f;
  const auto& la2a = HardwareEmulation::HardwareProfiles::getLA2A();
  inputTransformer_.setProfile(la2a.inputTransformer);
  inputTransformer_.setEnabled(true);
  inputTransformer_.prepare(sampleRate_, 2);
  outputTransformer_.setProfile(la2a.outputTransformer);
  outputTransformer_.setEnabled(true);
  outputTransformer_.prepare(sampleRate_, 2);
  tube_.setTubeType(HardwareEmulation::TubeEmulation::TubeType::Triode_12BH7);
  tube_.setDrive(0.3f);
  tube_.prepare(sampleRate_, 2);
}

void MultiCompOpto::setParams(float amount, float mix) {
  peakReduction_ = clamp(amount * 100.f, 0.f, 100.f);
  mix_ = clamp(mix, 0.f, 1.f);
}

void MultiCompOpto::processStereo(float* left, float* right, int numSamples,
                                  const float* sidechainL, const float* sidechainR) {
  const float dryMul = 1.f - mix_;
  const float wetMul = mix_;
  const float sidechainGain = dbToGain(4.f + peakReduction_ * 0.36f);
  const float fastAttackCoeff = std::exp(-1.f / static_cast<float>(T4B_FAST_ATTACK * sampleRate_));
  const float fastReleaseCoeff = std::exp(-1.f / static_cast<float>(T4B_FAST_RELEASE * sampleRate_));
  const float slowPersistCoeff = std::exp(-1.f / static_cast<float>(T4B_SLOW_PERSISTENCE * sampleRate_));
  const float attackCoeff = std::exp(-1.f / static_cast<float>(OPTO_ATTACK_TIME * sampleRate_));
  const float hfRolloff = 0.7f;
  const float baseThreshold = 0.3f;
  const bool use_sidechain = (sidechainL != nullptr);

  for (int i = 0; i < numSamples; ++i) {
    const float dryL = left[i];
    const float dryR = right[i];
    float inL = inputTransformer_.processSample(dryL, 0);
    float inR = inputTransformer_.processSample(dryR, 1);
    float compressedL = inL * envelopeL_;
    float compressedR = inR * envelopeR_;
    float detectionInput = use_sidechain
        ? 0.5f * (sidechainL[i] + (sidechainR ? sidechainR[i] : sidechainL[i]))
        : 0.5f * (compressedL + compressedR);
    float detectionLevel = std::abs(detectionInput * sidechainGain);
    detectionLevel = hfFilterL_ * hfRolloff + detectionLevel * (1.f - hfRolloff);
    hfFilterL_ = hfFilterR_ = detectionLevel;

    float absInput = std::max(std::abs(inL), std::abs(inR));
    float releaseScale = (absInput - std::max(prevInputL_, prevInputR_) > 0.05f) ? 0.6f : 1.f;
    prevInputL_ = std::abs(inL);
    prevInputR_ = std::abs(inR);

    float fastRel = std::exp(-1.f / static_cast<float>(T4B_FAST_RELEASE * sampleRate_ * releaseScale));
    float lightInput = detectionLevel;

    if (lightInput > fastMemoryL_) {
      fastMemoryL_ = lightInput + (fastMemoryL_ - lightInput) * fastAttackCoeff;
      fastMemoryR_ = fastMemoryL_;
    } else {
      fastMemoryL_ = lightInput + (fastMemoryL_ - lightInput) * fastRel;
      fastMemoryR_ = fastMemoryL_;
    }
    slowMemoryL_ = lightInput + (slowMemoryL_ - lightInput) * slowPersistCoeff;
    slowMemoryR_ = slowMemoryL_;
    float lightLevel = fastMemoryL_ + slowMemoryL_ * T4B_MEMORY_COUPLING;

    float thresholdReduction = clamp(absInput * 0.3f, 0.f, 0.2f);
    float internalThreshold = baseThreshold * (1.f - thresholdReduction);

    float reduction = 0.f;
    if (lightLevel > internalThreshold) {
      float excess = lightLevel - internalThreshold;
      float lightIntensity = clamp(lightLevel - internalThreshold, 0.f, 1.f);
      float ratioFactor = std::log10(1.f + lightIntensity * 9.f);
      float programRatio = 3.f + (10.f - 3.f) * ratioFactor;
      float variableRatio = programRatio * (1.f + excess * 2.f);
      reduction = 20.f * std::log10(1.f + excess * variableRatio);
      reduction = std::min(reduction, 40.f);
    }

    float targetGain = dbToGain(-reduction);
    if (reduction > maxReduction_) maxReduction_ = reduction;
    if (reduction > 0.5f)
      holdCounter_ = std::min(holdCounter_ + 1.f, static_cast<float>(sampleRate_ * 10.f));
    else {
      maxReduction_ *= 0.9999f;
      holdCounter_ *= 0.999f;
    }

    if (targetGain < envelopeL_) {
      envelopeL_ = targetGain + (envelopeL_ - targetGain) * attackCoeff;
      envelopeR_ = envelopeL_;
      releasePhase_ = 0;
      releaseStartLevel_ = envelopeL_;
    } else {
      float recoveryAmount = (envelopeL_ - releaseStartLevel_) / (1.f - releaseStartLevel_ + 0.0001f);
      float releaseTime;
      if (recoveryAmount < 0.5f) {
        float reductionFactor = clamp(maxReduction_ * 0.05f, 0.f, 1.f);
        releaseTime = OPTO_RELEASE_FAST_MIN + reductionFactor * (OPTO_RELEASE_FAST_MAX - OPTO_RELEASE_FAST_MIN);
        releasePhase_ = 1;
      } else {
        float lightIntensity = clamp(maxReduction_ * 0.0333f, 0.f, 1.f);
        float timeHeld = clamp(holdCounter_ / static_cast<float>(sampleRate_ * 2.f), 0.f, 1.f);
        releaseTime = OPTO_RELEASE_SLOW_MIN + lightIntensity * timeHeld * (OPTO_RELEASE_SLOW_MAX - OPTO_RELEASE_SLOW_MIN);
        releasePhase_ = 2;
      }
      float releaseCoeff = std::exp(-1.f / static_cast<float>(releaseTime * sampleRate_));
      envelopeL_ = targetGain + (envelopeL_ - targetGain) * releaseCoeff;
      envelopeR_ = envelopeL_;
    }

    float outL = inL * envelopeL_;
    float outR = inR * envelopeR_;
    outL = tube_.processSample(outL, 0);
    outR = tube_.processSample(outR, 1);
    outL = outputTransformer_.processSample(outL, 0);
    outR = outputTransformer_.processSample(outR, 1);
    left[i] = dryL * dryMul + outL * wetMul;
    right[i] = dryR * dryMul + outR * wetMul;
  }
}

// ----- FET (Drums) -----
void MultiCompFET::prepare(double sampleRate) {
  if (sampleRate > 0. && sampleRate < 500000.)
    sampleRate_ = sampleRate;
  envelopeL_ = envelopeR_ = 1.f;
  previousLevelL_ = previousLevelR_ = 0.f;
  previousGR_ = 0.f;
  const auto& fet = HardwareEmulation::HardwareProfiles::getFET1176();
  inputTransformer_.setProfile(fet.inputTransformer);
  inputTransformer_.setEnabled(true);
  inputTransformer_.prepare(sampleRate_, 2);
  outputTransformer_.setProfile(fet.outputTransformer);
  outputTransformer_.setEnabled(true);
  outputTransformer_.prepare(sampleRate_, 2);
}

void MultiCompFET::setParams(float amount, float mix) {
  inputGainDb_ = -10.f + amount * 50.f;  // -10 to +40 dB
  mix_ = clamp(mix, 0.f, 1.f);
}

void MultiCompFET::processStereo(float* left, float* right, int numSamples,
                                 const float* sidechainL, const float* sidechainR) {
  const float dryMul = 1.f - mix_;
  const float wetMul = mix_;
  const float threshold = dbToGain(FET_THRESHOLD_DB);
  const float inputGainLin = dbToGain(inputGainDb_);
  const float ratio = 4.f;
  const float attackTime = FET_MIN_ATTACK * std::pow(FET_MAX_ATTACK / FET_MIN_ATTACK, 0.5f);
  const float releaseTime = FET_MIN_RELEASE * std::pow(FET_MAX_RELEASE / FET_MIN_RELEASE, 0.5f);
  const float attackCoeff = std::exp(-1.f / static_cast<float>(attackTime * sampleRate_));
  const float releaseCoeff = std::exp(-1.f / static_cast<float>(releaseTime * sampleRate_));
  const bool use_sidechain = (sidechainL != nullptr);

  for (int i = 0; i < numSamples; ++i) {
    const float dryL = left[i];
    const float dryR = right[i];
    float inL = inputTransformer_.processSample(dryL, 0);
    float inR = inputTransformer_.processSample(dryR, 1);
    float amplifiedL = inL * inputGainLin;
    float amplifiedR = inR * inputGainLin;
    float compressedL = amplifiedL * envelopeL_;
    float compressedR = amplifiedR * envelopeR_;
    float detectionLevel = use_sidechain
        ? std::max(std::abs(sidechainL[i]), std::abs(sidechainR ? sidechainR[i] : sidechainL[i]))
        : std::max(std::abs(compressedL), std::abs(compressedR));

    float reduction = 0.f;
    if (detectionLevel > threshold) {
      float overThreshDb = gainToDb(detectionLevel / threshold);
      reduction = overThreshDb * (1.f - 1.f / ratio);
      reduction = std::min(reduction, FET_MAX_REDUCTION_DB);
    }

    float targetGain = dbToGain(-reduction);
    if (targetGain < envelopeL_) {
      envelopeL_ = attackCoeff * envelopeL_ + (1.f - attackCoeff) * targetGain;
      envelopeR_ = envelopeL_;
    } else {
      envelopeL_ = releaseCoeff * envelopeL_ + (1.f - releaseCoeff) * targetGain;
      envelopeR_ = envelopeL_;
    }
    envelopeL_ = clamp(envelopeL_, 0.001f, 1.f);
    envelopeR_ = envelopeL_;

    float currentGR = 1.f - envelopeL_;
    currentGR = 0.85f * currentGR + 0.15f * previousGR_;
    previousGR_ = currentGR;
    envelopeL_ = envelopeR_ = 1.f - currentGR;

    previousLevelL_ = std::abs(compressedL);
    previousLevelR_ = std::abs(compressedR);

    float outL = amplifiedL * envelopeL_;
    float outR = amplifiedR * envelopeR_;
    outL = outputTransformer_.processSample(outL, 0);
    outR = outputTransformer_.processSample(outR, 1);
    left[i] = dryL * dryMul + outL * wetMul;
    right[i] = dryR * dryMul + outR * wetMul;
  }
}

// ----- VCA (Classic dbx 160) -----
void MultiCompVCA::prepare(double sampleRate) {
  if (sampleRate > 0. && sampleRate < 500000.)
    sampleRate_ = sampleRate;
  envelopeL_ = envelopeR_ = 1.f;
  rmsL_ = rmsR_ = 0.f;
  previousInputL_ = previousInputR_ = 0.f;
  overshootL_ = overshootR_ = 0.f;
}

void MultiCompVCA::setParams(float amount, float mix) {
  thresholdDb_ = 6.f - amount * 30.f;  // +6 to -24 dB
  ratio_ = 3.f;
  mix_ = clamp(mix, 0.f, 1.f);
}

void MultiCompVCA::processStereo(float* left, float* right, int numSamples,
                                 const float* sidechainL, const float* sidechainR) {
  const float dryMul = 1.f - mix_;
  const float wetMul = mix_;
  const float thresholdLin = dbToGain(thresholdDb_);
  const float rmsAlpha = std::exp(-1.f / static_cast<float>(VCA_RMS_TIME * sampleRate_));
  const float attackTime = 0.003f;
  const float releaseTime = 0.08f;
  const float attackCoeff = std::exp(-1.f / static_cast<float>(attackTime * sampleRate_));
  const float releaseCoeff = std::exp(-1.f / static_cast<float>(releaseTime * sampleRate_));
  const float kneeWidth = VCA_OVEREASY_KNEE;
  const float kneeStart = -kneeWidth * 0.5f;
  const float kneeEnd = kneeWidth * 0.5f;
  const float ratioInv = 1.f / ratio_;
  const bool use_sidechain = (sidechainL != nullptr);

  for (int i = 0; i < numSamples; ++i) {
    float inL = left[i];
    float inR = right[i];
    float detectionL = use_sidechain ? std::abs(sidechainL[i]) : std::abs(inL);
    float detectionR = use_sidechain ? std::abs(sidechainR ? sidechainR[i] : sidechainL[i]) : std::abs(inR);
    rmsL_ = rmsL_ * rmsAlpha + detectionL * detectionL * (1.f - rmsAlpha);
    rmsR_ = rmsR_ * rmsAlpha + detectionR * detectionR * (1.f - rmsAlpha);
    float rmsLevel = std::max(std::sqrt(rmsL_), std::sqrt(rmsR_));
    previousInputL_ = detectionL;
    previousInputR_ = detectionR;

    float reduction = 0.f;
    if (rmsLevel > thresholdLin) {
      float overThreshDb = gainToDb(rmsLevel / thresholdLin);
      if (overThreshDb <= kneeStart) {
        reduction = 0.f;
      } else if (overThreshDb <= kneeEnd) {
        float kneePosition = (overThreshDb - kneeStart) / kneeWidth;
        float parabolaGain = kneePosition * kneePosition;
        reduction = overThreshDb * parabolaGain * (1.f - ratioInv);
      } else {
        float kneeReduction = kneeEnd * (1.f - ratioInv);
        reduction = kneeReduction + (overThreshDb - kneeEnd) * (1.f - ratioInv);
      }
      reduction = std::min(reduction, VCA_MAX_REDUCTION_DB);
    }

    float targetGain = dbToGain(-reduction);
    if (targetGain < envelopeL_) {
      envelopeL_ = targetGain + (envelopeL_ - targetGain) * attackCoeff;
      envelopeR_ = envelopeL_;
      if (attackTime < 0.005f && reduction > 5.f) {
        float overshootFactor = (0.005f - attackTime) / 0.004f;
        float reductionFactor = clamp(reduction / 20.f, 0.f, 1.f);
        float os = overshootFactor * reductionFactor * 0.02f;
        overshootL_ = overshootR_ = os;
      } else {
        overshootL_ *= 0.95f;
        overshootR_ *= 0.95f;
      }
    } else {
      envelopeL_ = targetGain + (envelopeL_ - targetGain) * releaseCoeff;
      envelopeR_ = envelopeL_;
      overshootL_ *= 0.98f;
      overshootR_ *= 0.98f;
    }
    envelopeL_ = clamp(envelopeL_, 0.0001f, 1.f);
    envelopeR_ = envelopeL_;

    float envWithOs = envelopeL_ * (1.f + overshootL_);
    envWithOs = clamp(envWithOs, 0.0001f, 1.f);
    float outL = inL * envWithOs;
    float outR = inR * envWithOs;
    left[i] = inL * dryMul + outL * wetMul;
    right[i] = inR * dryMul + outR * wetMul;
  }
}

// ----- Bus (SSL) -----
void MultiCompBus::prepare(double sampleRate) {
  if (sampleRate > 0. && sampleRate < 500000.)
    sampleRate_ = sampleRate;
  envelopeL_ = envelopeR_ = 1.f;
  hpStateL_ = hpStateR_ = 0.f;
  prevInputL_ = prevInputR_ = 0.f;
  previousLevelL_ = previousLevelR_ = 0.f;
  previousGR_ = 0.f;
  hpStateScL_ = hpStateScR_ = 0.f;
  prevScL_ = prevScR_ = 0.f;
  const auto& ssl = HardwareEmulation::HardwareProfiles::getSSLBus();
  inputTransformer_.setProfile(ssl.inputTransformer);
  inputTransformer_.setEnabled(true);
  inputTransformer_.prepare(sampleRate_, 2);
  outputTransformer_.setProfile(ssl.outputTransformer);
  outputTransformer_.setEnabled(true);
  outputTransformer_.prepare(sampleRate_, 2);
}

void MultiCompBus::setParams(float amount, float mix) {
  thresholdDb_ = 6.f - amount * 30.f;
  ratio_ = 2.5f;
  mix_ = clamp(mix, 0.f, 1.f);
}

void MultiCompBus::processStereo(float* left, float* right, int numSamples,
                                const float* sidechainL, const float* sidechainR) {
  const float dryMul = 1.f - mix_;
  const float wetMul = mix_;
  const float thresholdLin = dbToGain(thresholdDb_);
  const float hpAlpha = std::min(1.f, static_cast<float>(BUS_HP_FREQ / sampleRate_));
  const float attackTime = BUS_ATTACK_MS * 0.001f;
  const float releaseTime = BUS_RELEASE_MS * 0.001f;
  const float attackCoeff = 1.f - 1.f / std::max(kEps, attackTime * static_cast<float>(sampleRate_));
  const float releaseCoeff = 1.f - 1.f / std::max(kEps, releaseTime * static_cast<float>(sampleRate_));
  const float ratioInv = 1.f / ratio_;
  const bool use_sidechain = (sidechainL != nullptr);

  for (int i = 0; i < numSamples; ++i) {
    const float dryL = left[i];
    const float dryR = right[i];
    float inL = inputTransformer_.processSample(dryL, 0);
    float inR = inputTransformer_.processSample(dryR, 1);
    float detectionLevel;
    if (use_sidechain) {
      const float scL = sidechainL[i];
      const float scR = sidechainR ? sidechainR[i] : scL;
      hpStateScL_ = scL - prevScL_ + hpStateScL_ * (1.f - hpAlpha);
      hpStateScR_ = scR - prevScR_ + hpStateScR_ * (1.f - hpAlpha);
      prevScL_ = scL;
      prevScR_ = scR;
      detectionLevel = std::max(std::abs(hpStateScL_), std::abs(hpStateScR_));
    } else {
      hpStateL_ = inL - prevInputL_ + hpStateL_ * (1.f - hpAlpha);
      hpStateR_ = inR - prevInputR_ + hpStateR_ * (1.f - hpAlpha);
      prevInputL_ = inL;
      prevInputR_ = inR;
      detectionLevel = std::max(std::abs(hpStateL_), std::abs(hpStateR_));
    }

    float reduction = 0.f;
    if (detectionLevel > thresholdLin) {
      float overThreshDb = gainToDb(detectionLevel / thresholdLin);
      reduction = overThreshDb * (1.f - ratioInv);
      reduction = std::min(reduction, BUS_MAX_REDUCTION_DB);
    }

    float targetGain = dbToGain(-reduction);
    if (targetGain < envelopeL_) {
      envelopeL_ = targetGain + (envelopeL_ - targetGain) * attackCoeff;
      envelopeR_ = envelopeL_;
    } else {
      envelopeL_ = targetGain + (envelopeL_ - targetGain) * releaseCoeff;
      envelopeR_ = envelopeL_;
    }

    float currentGR = 1.f - envelopeL_;
    currentGR = 0.9f * currentGR + 0.1f * previousGR_;
    previousGR_ = currentGR;
    envelopeL_ = envelopeR_ = 1.f - currentGR;

    previousLevelL_ = detectionLevel;
    previousLevelR_ = detectionLevel;

    float outL = inL * envelopeL_;
    float outR = inR * envelopeR_;
    outL = outputTransformer_.processSample(outL, 0);
    outR = outputTransformer_.processSample(outR, 1);
    left[i] = dryL * dryMul + outL * wetMul;
    right[i] = dryR * dryMul + outR * wetMul;
  }
}

}  // namespace FairyTapes::TheStripper::DSP
