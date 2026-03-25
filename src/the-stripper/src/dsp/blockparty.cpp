/**
 * BlockParty DSP — ported from Airwindows BlockPartyProc.cpp (float processReplacing), MIT license.
 * Dither and denormal guard removed; sample rate from sampleRate_; parameters pound_ / wet_.
 */

#include "blockparty.h"

#include <cmath>
#include <cstdint>
#include <random>

namespace FairyTapes::TheStripper::DSP {

namespace {

uint32_t random_fpd_seed() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> dist(16387u, UINT32_MAX);
  return dist(gen);
}

}  // namespace

BlockParty::BlockParty() { reset(); }

void BlockParty::setSampleRate(double sampleRate) { sampleRate_ = sampleRate; }

void BlockParty::reset() {
  muSpeedAL_ = muSpeedBL_ = muSpeedCL_ = muSpeedDL_ = muSpeedEL_ = 10000.0;
  muCoefficientAL_ = muCoefficientBL_ = muCoefficientCL_ = muCoefficientDL_ = muCoefficientEL_ = 1.0;
  lastCoefficientAL_ = lastCoefficientBL_ = lastCoefficientCL_ = lastCoefficientDL_ = 1.0;
  mergedCoefficientsL_ = 1.0;
  thresholdL_ = 1.0;
  thresholdBL_ = 1.0;
  muVaryL_ = 1.0;

  muSpeedAR_ = muSpeedBR_ = muSpeedCR_ = muSpeedDR_ = muSpeedER_ = 10000.0;
  muCoefficientAR_ = muCoefficientBR_ = muCoefficientCR_ = muCoefficientDR_ = muCoefficientER_ = 1.0;
  lastCoefficientAR_ = lastCoefficientBR_ = lastCoefficientCR_ = lastCoefficientDR_ = 1.0;
  mergedCoefficientsR_ = 1.0;
  thresholdR_ = 1.0;
  thresholdBR_ = 1.0;
  muVaryR_ = 1.0;

  count_ = 1;
  fpFlip_ = true;

  fpdL_ = random_fpd_seed();
  fpdR_ = random_fpd_seed();
}

void BlockParty::setParams(float pound, float wet) {
  pound_ = pound;
  wet_ = wet;
}

void BlockParty::process(float* left, float* right, int numSamples) {
  if (left == nullptr || right == nullptr || numSamples <= 0) {
    return;
  }

  double overall_scale = 1.0;
  overall_scale /= 44100.0;
  overall_scale *= sampleRate_;

  double target_threshold = 1.01 - (1.0 - std::pow(1.0 - (static_cast<double>(pound_) * 0.5), 4));
  double wet_mix = static_cast<double>(wet_);
  double voicing = 0.618033988749894848204586;
  if (overall_scale > 0.0) {
    voicing /= overall_scale;
  }
  if (voicing < 0.0) {
    voicing = 0.0;
  }
  if (voicing > 1.0) {
    voicing = 1.0;
  }

  while (numSamples-- > 0) {
    double input_sample_l = static_cast<double>(*left);
    double input_sample_r = static_cast<double>(*right);

    double dry_sample_l = input_sample_l;
    double dry_sample_r = input_sample_r;

    double mu_makeup_gain_l = 1.0 / thresholdL_;
    double out_makeup_gain_l = std::sqrt(mu_makeup_gain_l);
    mu_makeup_gain_l += out_makeup_gain_l;
    mu_makeup_gain_l *= 0.5;
    double release_l = mergedCoefficientsL_ * 32768.0;
    release_l /= overall_scale;
    double fastest_l = std::sqrt(release_l);
    double last_correction_l = mergedCoefficientsL_;

    double mu_makeup_gain_r = 1.0 / thresholdR_;
    double out_makeup_gain_r = std::sqrt(mu_makeup_gain_r);
    mu_makeup_gain_r += out_makeup_gain_r;
    mu_makeup_gain_r *= 0.5;
    double release_r = mergedCoefficientsR_ * 32768.0;
    release_r /= overall_scale;
    double fastest_r = std::sqrt(release_r);
    double last_correction_r = mergedCoefficientsR_;

    if (mu_makeup_gain_l != 1.0) {
      input_sample_l = input_sample_l * mu_makeup_gain_l;
    }
    if (mu_makeup_gain_r != 1.0) {
      input_sample_r = input_sample_r * mu_makeup_gain_r;
    }

    if (count_ < 1 || count_ > 3) {
      count_ = 1;
    }
    switch (count_) {
      case 1:
        if (std::fabs(input_sample_l) > thresholdL_) {
          if (input_sample_l > 0.0) {
            input_sample_l = (input_sample_l * voicing) + (target_threshold * (1.0 - voicing));
            thresholdL_ = std::fabs(input_sample_l);
          } else {
            input_sample_l = (input_sample_l * voicing) - (target_threshold * (1.0 - voicing));
            thresholdL_ = std::fabs(input_sample_l);
          }
          muVaryL_ = target_threshold / std::fabs(input_sample_l);
          muAttackL_ = std::sqrt(std::fabs(muSpeedAL_));
          muCoefficientAL_ = muCoefficientAL_ * (muAttackL_ - 1.0);
          if (muVaryL_ < thresholdL_) {
            muCoefficientAL_ = muCoefficientAL_ + target_threshold;
          } else {
            muCoefficientAL_ = muCoefficientAL_ + muVaryL_;
          }
          muCoefficientAL_ = muCoefficientAL_ / muAttackL_;
        } else {
          thresholdL_ = target_threshold;
          muCoefficientAL_ = muCoefficientAL_ * ((muSpeedAL_ * muSpeedAL_) - 1.0);
          muCoefficientAL_ = muCoefficientAL_ + 1.0;
          muCoefficientAL_ = muCoefficientAL_ / (muSpeedAL_ * muSpeedAL_);
        }
        muNewSpeedL_ = muSpeedAL_ * (muSpeedAL_ - 1);
        muNewSpeedL_ = muNewSpeedL_ + std::fabs(input_sample_l * release_l) + fastest_l;
        muSpeedAL_ = muNewSpeedL_ / muSpeedAL_;
        lastCoefficientAL_ = std::pow(muCoefficientAL_, 2);
        mergedCoefficientsL_ = lastCoefficientBL_;
        mergedCoefficientsL_ += lastCoefficientAL_;
        lastCoefficientAL_ *= (1.0 - last_correction_l);
        lastCoefficientAL_ += (muCoefficientAL_ * last_correction_l);
        lastCoefficientBL_ = lastCoefficientAL_;

        if (std::fabs(input_sample_r) > thresholdR_) {
          if (input_sample_r > 0.0) {
            input_sample_r = (input_sample_r * voicing) + (target_threshold * (1.0 - voicing));
            thresholdR_ = std::fabs(input_sample_r);
          } else {
            input_sample_r = (input_sample_r * voicing) - (target_threshold * (1.0 - voicing));
            thresholdR_ = std::fabs(input_sample_r);
          }
          muVaryR_ = target_threshold / std::fabs(input_sample_r);
          muAttackR_ = std::sqrt(std::fabs(muSpeedAR_));
          muCoefficientAR_ = muCoefficientAR_ * (muAttackR_ - 1.0);
          if (muVaryR_ < thresholdR_) {
            muCoefficientAR_ = muCoefficientAR_ + target_threshold;
          } else {
            muCoefficientAR_ = muCoefficientAR_ + muVaryR_;
          }
          muCoefficientAR_ = muCoefficientAR_ / muAttackR_;
        } else {
          thresholdR_ = target_threshold;
          muCoefficientAR_ = muCoefficientAR_ * ((muSpeedAR_ * muSpeedAR_) - 1.0);
          muCoefficientAR_ = muCoefficientAR_ + 1.0;
          muCoefficientAR_ = muCoefficientAR_ / (muSpeedAR_ * muSpeedAR_);
        }
        muNewSpeedR_ = muSpeedAR_ * (muSpeedAR_ - 1);
        muNewSpeedR_ = muNewSpeedR_ + std::fabs(input_sample_r * release_r) + fastest_r;
        muSpeedAR_ = muNewSpeedR_ / muSpeedAR_;
        lastCoefficientAR_ = std::pow(muCoefficientAR_, 2);
        mergedCoefficientsR_ = lastCoefficientBR_;
        mergedCoefficientsR_ += lastCoefficientAR_;
        lastCoefficientAR_ *= (1.0 - last_correction_r);
        lastCoefficientAR_ += (muCoefficientAR_ * last_correction_r);
        lastCoefficientBR_ = lastCoefficientAR_;

        break;
      case 2:
        if (std::fabs(input_sample_l) > thresholdL_) {
          if (input_sample_l > 0.0) {
            input_sample_l = (input_sample_l * voicing) + (target_threshold * (1.0 - voicing));
            thresholdL_ = std::fabs(input_sample_l);
          } else {
            input_sample_l = (input_sample_l * voicing) - (target_threshold * (1.0 - voicing));
            thresholdL_ = std::fabs(input_sample_l);
          }
          muVaryL_ = target_threshold / std::fabs(input_sample_l);
          muAttackL_ = std::sqrt(std::fabs(muSpeedBL_));
          muCoefficientBL_ = muCoefficientBL_ * (muAttackL_ - 1);
          if (muVaryL_ < thresholdL_) {
            muCoefficientBL_ = muCoefficientBL_ + target_threshold;
          } else {
            muCoefficientBL_ = muCoefficientBL_ + muVaryL_;
          }
          muCoefficientBL_ = muCoefficientBL_ / muAttackL_;
        } else {
          thresholdL_ = target_threshold;
          muCoefficientBL_ = muCoefficientBL_ * ((muSpeedBL_ * muSpeedBL_) - 1.0);
          muCoefficientBL_ = muCoefficientBL_ + 1.0;
          muCoefficientBL_ = muCoefficientBL_ / (muSpeedBL_ * muSpeedBL_);
        }
        muNewSpeedL_ = muSpeedBL_ * (muSpeedBL_ - 1);
        muNewSpeedL_ = muNewSpeedL_ + std::fabs(input_sample_l * release_l) + fastest_l;
        muSpeedBL_ = muNewSpeedL_ / muSpeedBL_;
        lastCoefficientAL_ = std::pow(muCoefficientBL_, 2);
        mergedCoefficientsL_ = lastCoefficientBL_;
        mergedCoefficientsL_ += lastCoefficientAL_;
        lastCoefficientAL_ *= (1.0 - last_correction_l);
        lastCoefficientAL_ += (muCoefficientBL_ * last_correction_l);
        lastCoefficientBL_ = lastCoefficientAL_;

        if (std::fabs(input_sample_r) > thresholdR_) {
          if (input_sample_r > 0.0) {
            input_sample_r = (input_sample_r * voicing) + (target_threshold * (1.0 - voicing));
            thresholdR_ = std::fabs(input_sample_r);
          } else {
            input_sample_r = (input_sample_r * voicing) - (target_threshold * (1.0 - voicing));
            thresholdR_ = std::fabs(input_sample_r);
          }
          muVaryR_ = target_threshold / std::fabs(input_sample_r);
          muAttackR_ = std::sqrt(std::fabs(muSpeedBR_));
          muCoefficientBR_ = muCoefficientBR_ * (muAttackR_ - 1);
          if (muVaryR_ < thresholdR_) {
            muCoefficientBR_ = muCoefficientBR_ + target_threshold;
          } else {
            muCoefficientBR_ = muCoefficientBR_ + muVaryR_;
          }
          muCoefficientBR_ = muCoefficientBR_ / muAttackR_;
        } else {
          thresholdR_ = target_threshold;
          muCoefficientBR_ = muCoefficientBR_ * ((muSpeedBR_ * muSpeedBR_) - 1.0);
          muCoefficientBR_ = muCoefficientBR_ + 1.0;
          muCoefficientBR_ = muCoefficientBR_ / (muSpeedBR_ * muSpeedBR_);
        }
        muNewSpeedR_ = muSpeedBR_ * (muSpeedBR_ - 1);
        muNewSpeedR_ = muNewSpeedR_ + std::fabs(input_sample_r * release_r) + fastest_r;
        muSpeedBR_ = muNewSpeedR_ / muSpeedBR_;
        lastCoefficientAR_ = std::pow(muCoefficientBR_, 2);
        mergedCoefficientsR_ = lastCoefficientBR_;
        mergedCoefficientsR_ += lastCoefficientAR_;
        lastCoefficientAR_ *= (1.0 - last_correction_r);
        lastCoefficientAR_ += (muCoefficientBR_ * last_correction_r);
        lastCoefficientBR_ = lastCoefficientAR_;

        break;
      case 3:
        if (std::fabs(input_sample_l) > thresholdL_) {
          if (input_sample_l > 0.0) {
            input_sample_l = (input_sample_l * voicing) + (target_threshold * (1.0 - voicing));
            thresholdL_ = std::fabs(input_sample_l);
          } else {
            input_sample_l = (input_sample_l * voicing) - (target_threshold * (1.0 - voicing));
            thresholdL_ = std::fabs(input_sample_l);
          }
          muVaryL_ = target_threshold / std::fabs(input_sample_l);
          muAttackL_ = std::sqrt(std::fabs(muSpeedCL_));
          muCoefficientCL_ = muCoefficientCL_ * (muAttackL_ - 1);
          if (muVaryL_ < thresholdL_) {
            muCoefficientCL_ = muCoefficientCL_ + target_threshold;
          } else {
            muCoefficientCL_ = muCoefficientCL_ + muVaryL_;
          }
          muCoefficientCL_ = muCoefficientCL_ / muAttackL_;
        } else {
          thresholdL_ = target_threshold;
          muCoefficientCL_ = muCoefficientCL_ * ((muSpeedCL_ * muSpeedCL_) - 1.0);
          muCoefficientCL_ = muCoefficientCL_ + 1.0;
          muCoefficientCL_ = muCoefficientCL_ / (muSpeedCL_ * muSpeedCL_);
        }
        muNewSpeedL_ = muSpeedCL_ * (muSpeedCL_ - 1);
        muNewSpeedL_ = muNewSpeedL_ + std::fabs(input_sample_l * release_l) + fastest_l;
        muSpeedCL_ = muNewSpeedL_ / muSpeedCL_;
        lastCoefficientAL_ = std::pow(muCoefficientCL_, 2);
        mergedCoefficientsL_ = lastCoefficientBL_;
        mergedCoefficientsL_ += lastCoefficientAL_;
        lastCoefficientAL_ *= (1.0 - last_correction_l);
        lastCoefficientAL_ += (muCoefficientCL_ * last_correction_l);
        lastCoefficientBL_ = lastCoefficientAL_;

        if (std::fabs(input_sample_r) > thresholdR_) {
          if (input_sample_r > 0.0) {
            input_sample_r = (input_sample_r * voicing) + (target_threshold * (1.0 - voicing));
            thresholdR_ = std::fabs(input_sample_r);
          } else {
            input_sample_r = (input_sample_r * voicing) - (target_threshold * (1.0 - voicing));
            thresholdR_ = std::fabs(input_sample_r);
          }
          muVaryR_ = target_threshold / std::fabs(input_sample_r);
          muAttackR_ = std::sqrt(std::fabs(muSpeedCR_));
          muCoefficientCR_ = muCoefficientCR_ * (muAttackR_ - 1);
          if (muVaryR_ < thresholdR_) {
            muCoefficientCR_ = muCoefficientCR_ + target_threshold;
          } else {
            muCoefficientCR_ = muCoefficientCR_ + muVaryR_;
          }
          muCoefficientCR_ = muCoefficientCR_ / muAttackR_;
        } else {
          thresholdR_ = target_threshold;
          muCoefficientCR_ = muCoefficientCR_ * ((muSpeedCR_ * muSpeedCR_) - 1.0);
          muCoefficientCR_ = muCoefficientCR_ + 1.0;
          muCoefficientCR_ = muCoefficientCR_ / (muSpeedCR_ * muSpeedCR_);
        }
        muNewSpeedR_ = muSpeedCR_ * (muSpeedCR_ - 1);
        muNewSpeedR_ = muNewSpeedR_ + std::fabs(input_sample_r * release_r) + fastest_r;
        muSpeedCR_ = muNewSpeedR_ / muSpeedCR_;
        lastCoefficientAR_ = std::pow(muCoefficientCR_, 2);
        mergedCoefficientsR_ = lastCoefficientBR_;
        mergedCoefficientsR_ += lastCoefficientAR_;
        lastCoefficientAR_ *= (1.0 - last_correction_r);
        lastCoefficientAR_ += (muCoefficientCR_ * last_correction_r);
        lastCoefficientBR_ = lastCoefficientAR_;

        break;
    }
    count_++;

    if (fpFlip_) {
      if (std::fabs(input_sample_l) > thresholdBL_) {
        if (input_sample_l > 0.0) {
          input_sample_l = (input_sample_l * voicing) + (target_threshold * (1.0 - voicing));
          thresholdBL_ = std::fabs(input_sample_l);
        } else {
          input_sample_l = (input_sample_l * voicing) - (target_threshold * (1.0 - voicing));
          thresholdBL_ = std::fabs(input_sample_l);
        }
        muVaryL_ = target_threshold / std::fabs(input_sample_l);
        muAttackL_ = std::sqrt(std::fabs(muSpeedDL_));
        muCoefficientDL_ = muCoefficientDL_ * (muAttackL_ - 1.0);
        if (muVaryL_ < thresholdBL_) {
          muCoefficientDL_ = muCoefficientDL_ + target_threshold;
        } else {
          muCoefficientDL_ = muCoefficientDL_ + muVaryL_;
        }
        muCoefficientDL_ = muCoefficientDL_ / muAttackL_;
      } else {
        thresholdBL_ = target_threshold;
        muCoefficientDL_ = muCoefficientDL_ * ((muSpeedDL_ * muSpeedDL_) - 1.0);
        muCoefficientDL_ = muCoefficientDL_ + 1.0;
        muCoefficientDL_ = muCoefficientDL_ / (muSpeedDL_ * muSpeedDL_);
      }
      muNewSpeedL_ = muSpeedDL_ * (muSpeedDL_ - 1);
      muNewSpeedL_ = muNewSpeedL_ + std::fabs(input_sample_l * release_l) + fastest_l;
      muSpeedDL_ = muNewSpeedL_ / muSpeedDL_;
      lastCoefficientCL_ = std::pow(muCoefficientEL_, 2);
      mergedCoefficientsL_ += lastCoefficientDL_;
      mergedCoefficientsL_ += lastCoefficientCL_;
      lastCoefficientCL_ *= (1.0 - last_correction_l);
      lastCoefficientCL_ += (muCoefficientDL_ * last_correction_l);
      lastCoefficientDL_ = lastCoefficientCL_;

      if (std::fabs(input_sample_r) > thresholdBR_) {
        if (input_sample_r > 0.0) {
          input_sample_r = (input_sample_r * voicing) + (target_threshold * (1.0 - voicing));
          thresholdBR_ = std::fabs(input_sample_r);
        } else {
          input_sample_r = (input_sample_r * voicing) - (target_threshold * (1.0 - voicing));
          thresholdBR_ = std::fabs(input_sample_r);
        }
        muVaryR_ = target_threshold / std::fabs(input_sample_r);
        muAttackR_ = std::sqrt(std::fabs(muSpeedDR_));
        muCoefficientDR_ = muCoefficientDR_ * (muAttackR_ - 1.0);
        if (muVaryR_ < thresholdBR_) {
          muCoefficientDR_ = muCoefficientDR_ + target_threshold;
        } else {
          muCoefficientDR_ = muCoefficientDR_ + muVaryR_;
        }
        muCoefficientDR_ = muCoefficientDR_ / muAttackR_;
      } else {
        thresholdBR_ = target_threshold;
        muCoefficientDR_ = muCoefficientDR_ * ((muSpeedDR_ * muSpeedDR_) - 1.0);
        muCoefficientDR_ = muCoefficientDR_ + 1.0;
        muCoefficientDR_ = muCoefficientDR_ / (muSpeedDR_ * muSpeedDR_);
      }
      muNewSpeedR_ = muSpeedDR_ * (muSpeedDR_ - 1);
      muNewSpeedR_ = muNewSpeedR_ + std::fabs(input_sample_r * release_r) + fastest_r;
      muSpeedDR_ = muNewSpeedR_ / muSpeedDR_;
      lastCoefficientCR_ = std::pow(muCoefficientER_, 2);
      mergedCoefficientsR_ += lastCoefficientDR_;
      mergedCoefficientsR_ += lastCoefficientCR_;
      lastCoefficientCR_ *= (1.0 - last_correction_r);
      lastCoefficientCR_ += (muCoefficientDR_ * last_correction_r);
      lastCoefficientDR_ = lastCoefficientCR_;

    } else {
      if (std::fabs(input_sample_l) > thresholdBL_) {
        if (input_sample_l > 0.0) {
          input_sample_l = (input_sample_l * voicing) + (target_threshold * (1.0 - voicing));
          thresholdBL_ = std::fabs(input_sample_l);
        } else {
          input_sample_l = (input_sample_l * voicing) - (target_threshold * (1.0 - voicing));
          thresholdBL_ = std::fabs(input_sample_l);
        }
        muVaryL_ = target_threshold / std::fabs(input_sample_l);
        muAttackL_ = std::sqrt(std::fabs(muSpeedEL_));
        muCoefficientEL_ = muCoefficientEL_ * (muAttackL_ - 1.0);
        if (muVaryL_ < thresholdBL_) {
          muCoefficientEL_ = muCoefficientEL_ + target_threshold;
        } else {
          muCoefficientEL_ = muCoefficientEL_ + muVaryL_;
        }
        muCoefficientEL_ = muCoefficientEL_ / muAttackL_;
      } else {
        thresholdBL_ = target_threshold;
        muCoefficientEL_ = muCoefficientEL_ * ((muSpeedEL_ * muSpeedEL_) - 1.0);
        muCoefficientEL_ = muCoefficientEL_ + 1.0;
        muCoefficientEL_ = muCoefficientEL_ / (muSpeedEL_ * muSpeedEL_);
      }
      muNewSpeedL_ = muSpeedEL_ * (muSpeedEL_ - 1);
      muNewSpeedL_ = muNewSpeedL_ + std::fabs(input_sample_l * release_l) + fastest_l;
      muSpeedEL_ = muNewSpeedL_ / muSpeedEL_;
      lastCoefficientCL_ = std::pow(muCoefficientEL_, 2);
      mergedCoefficientsL_ += lastCoefficientDL_;
      mergedCoefficientsL_ += lastCoefficientCL_;
      lastCoefficientCL_ *= (1.0 - last_correction_l);
      lastCoefficientCL_ += (muCoefficientEL_ * last_correction_l);
      lastCoefficientDL_ = lastCoefficientCL_;

      if (std::fabs(input_sample_r) > thresholdBR_) {
        if (input_sample_r > 0.0) {
          input_sample_r = (input_sample_r * voicing) + (target_threshold * (1.0 - voicing));
          thresholdBR_ = std::fabs(input_sample_r);
        } else {
          input_sample_r = (input_sample_r * voicing) - (target_threshold * (1.0 - voicing));
          thresholdBR_ = std::fabs(input_sample_r);
        }
        muVaryR_ = target_threshold / std::fabs(input_sample_r);
        muAttackR_ = std::sqrt(std::fabs(muSpeedER_));
        muCoefficientER_ = muCoefficientER_ * (muAttackR_ - 1.0);
        if (muVaryR_ < thresholdBR_) {
          muCoefficientER_ = muCoefficientER_ + target_threshold;
        } else {
          muCoefficientER_ = muCoefficientER_ + muVaryR_;
        }
        muCoefficientER_ = muCoefficientER_ / muAttackR_;
      } else {
        thresholdBR_ = target_threshold;
        muCoefficientER_ = muCoefficientER_ * ((muSpeedER_ * muSpeedER_) - 1.0);
        muCoefficientER_ = muCoefficientER_ + 1.0;
        muCoefficientER_ = muCoefficientER_ / (muSpeedER_ * muSpeedER_);
      }
      muNewSpeedR_ = muSpeedER_ * (muSpeedER_ - 1);
      muNewSpeedR_ = muNewSpeedR_ + std::fabs(input_sample_r * release_r) + fastest_r;
      muSpeedER_ = muNewSpeedR_ / muSpeedER_;
      lastCoefficientCR_ = std::pow(muCoefficientER_, 2);
      mergedCoefficientsR_ += lastCoefficientDR_;
      mergedCoefficientsR_ += lastCoefficientCR_;
      lastCoefficientCR_ *= (1.0 - last_correction_r);
      lastCoefficientCR_ += (muCoefficientER_ * last_correction_r);
      lastCoefficientDR_ = lastCoefficientCR_;
    }
    mergedCoefficientsL_ *= 0.25;
    input_sample_l *= mergedCoefficientsL_;

    mergedCoefficientsR_ *= 0.25;
    input_sample_r *= mergedCoefficientsR_;

    if (out_makeup_gain_l != 1.0) {
      input_sample_l = input_sample_l * out_makeup_gain_l;
    }
    if (out_makeup_gain_r != 1.0) {
      input_sample_r = input_sample_r * out_makeup_gain_r;
    }

    fpFlip_ = !fpFlip_;

    if (wet_mix < 1.0) {
      input_sample_l = (input_sample_l * wet_mix) + (dry_sample_l * (1.0 - wet_mix));
      input_sample_r = (input_sample_r * wet_mix) + (dry_sample_r * (1.0 - wet_mix));
    }

    if (input_sample_l > 0.999) {
      input_sample_l = 0.999;
    }
    if (input_sample_l < -0.999) {
      input_sample_l = -0.999;
    }
    if (input_sample_r > 0.999) {
      input_sample_r = 0.999;
    }
    if (input_sample_r < -0.999) {
      input_sample_r = -0.999;
    }

    *left = static_cast<float>(input_sample_l);
    *right = static_cast<float>(input_sample_r);
    left++;
    right++;
  }
}

}  // namespace FairyTapes::TheStripper::DSP
