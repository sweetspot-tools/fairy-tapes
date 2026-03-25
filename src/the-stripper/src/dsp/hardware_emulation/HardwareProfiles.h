/*
 * Hardware emulation profiles — from Dusk Audio Multi-Comp (GPL-3.0).
 * Fairy Tapes · The Stripper — compressor hardware profiles.
 * Original: vendors/dusk-audio-plugins/plugins/multi-comp/HardwareEmulation/HardwareMeasurements.h
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace HardwareEmulation {

struct HarmonicProfile {
  float h2 = 0.0f;
  float h3 = 0.0f;
  float h4 = 0.0f;
  float h5 = 0.0f;
  float h6 = 0.0f;
  float h7 = 0.0f;
  float evenOddRatio = 0.5f;

  static HarmonicProfile create(float h2_, float h3_, float evenOddRatio_,
                                float h4_ = 0.0f, float h5_ = 0.0f,
                                float h6_ = 0.0f, float h7_ = 0.0f) {
    HarmonicProfile hp;
    hp.h2 = h2_;
    hp.h3 = h3_;
    hp.h4 = h4_;
    hp.h5 = h5_;
    hp.h6 = h6_;
    hp.h7 = h7_;
    hp.evenOddRatio = evenOddRatio_;
    return hp;
  }
};

struct TransformerProfile {
  bool hasTransformer = true;
  float saturationThreshold = 0.8f;
  float saturationAmount = 0.0f;
  float lowFreqSaturation = 1.0f;
  float highFreqRolloff = 20000.0f;
  float dcBlockingFreq = 10.0f;
  HarmonicProfile harmonics;

  static TransformerProfile createActive(float satThresh, float satAmt, float lfSat,
                                         float hfRolloff, float dcBlock,
                                         float h2, float h3, float evenOdd) {
    TransformerProfile tp;
    tp.hasTransformer = true;
    tp.saturationThreshold = satThresh;
    tp.saturationAmount = satAmt;
    tp.lowFreqSaturation = lfSat;
    tp.highFreqRolloff = hfRolloff;
    tp.dcBlockingFreq = dcBlock;
    tp.harmonics = HarmonicProfile::create(h2, h3, evenOdd, 0.0f, 0.0f, 0.0f, 0.0f);
    tp.harmonics.h4 = 0.0f;
    tp.harmonics.h5 = 0.0f;
    return tp;
  }

  static TransformerProfile createInactive() {
    TransformerProfile tp;
    tp.hasTransformer = false;
    return tp;
  }
};

struct TimingProfile {
  float attackMinMs = 0.0f;
  float attackMaxMs = 0.0f;
  float releaseMinMs = 0.0f;
  float releaseMaxMs = 0.0f;
  float attackCurve = 0.0f;
  float releaseCurve = 0.0f;
  bool programDependent = false;
};

struct FrequencyResponse {
  float lowShelfFreq = 100.0f;
  float lowShelfGain = 0.0f;
  float highShelfFreq = 10000.0f;
  float highShelfGain = 0.0f;
  float resonanceFreq = 0.0f;
  float resonanceQ = 0.707f;
  float resonanceGain = 0.0f;
};

struct HardwareUnitProfile {
  const char* name = nullptr;
  const char* modeledUnit = nullptr;
  HarmonicProfile inputStageHarmonics;
  HarmonicProfile compressionStageHarmonics;
  HarmonicProfile outputStageHarmonics;
  TransformerProfile inputTransformer;
  TransformerProfile outputTransformer;
  FrequencyResponse preCompressionEQ;
  FrequencyResponse postCompressionEQ;
  TimingProfile timing;
  float noiseFloor = -90.0f;
  float headroom = 20.0f;
};

namespace Profiles {

inline HardwareUnitProfile createLA2A() {
  HardwareUnitProfile profile;
  profile.name = "LA-2A";
  profile.modeledUnit = "Teletronix LA-2A";
  profile.inputStageHarmonics = HarmonicProfile::create(0.025f, 0.008f, 0.75f, 0.003f, 0.001f);
  profile.compressionStageHarmonics = HarmonicProfile::create(0.015f, 0.003f, 0.85f);
  profile.outputStageHarmonics = HarmonicProfile::create(0.035f, 0.012f, 0.70f, 0.004f);
  profile.inputTransformer = TransformerProfile::createActive(0.75f, 0.15f, 1.3f, 18000.0f, 20.0f, 0.008f, 0.003f, 0.7f);
  profile.outputTransformer = TransformerProfile::createActive(0.8f, 0.1f, 1.2f, 16000.0f, 15.0f, 0.006f, 0.002f, 0.75f);
  profile.timing = TimingProfile();
  profile.timing.attackMinMs = 10.0f;
  profile.timing.attackMaxMs = 10.0f;
  profile.timing.releaseMinMs = 60.0f;
  profile.timing.releaseMaxMs = 5000.0f;
  profile.timing.programDependent = true;
  profile.noiseFloor = -70.0f;
  profile.headroom = 18.0f;
  return profile;
}

inline HardwareUnitProfile createFET1176() {
  HardwareUnitProfile profile;
  profile.name = "1176";
  profile.modeledUnit = "UREI 1176 Rev A";
  profile.inputStageHarmonics = HarmonicProfile::create(0.008f, 0.015f, 0.35f, 0.002f, 0.005f);
  profile.compressionStageHarmonics = HarmonicProfile::create(0.012f, 0.025f, 0.30f, 0.0f, 0.008f);
  profile.outputStageHarmonics = HarmonicProfile::create(0.006f, 0.010f, 0.40f, 0.0f, 0.003f);
  profile.inputTransformer = TransformerProfile::createActive(0.85f, 0.08f, 1.15f, 20000.0f, 15.0f, 0.004f, 0.002f, 0.65f);
  profile.outputTransformer = TransformerProfile::createActive(0.9f, 0.05f, 1.1f, 22000.0f, 12.0f, 0.003f, 0.002f, 0.6f);
  profile.timing = TimingProfile();
  profile.timing.attackMinMs = 0.02f;
  profile.timing.attackMaxMs = 0.8f;
  profile.timing.releaseMinMs = 50.0f;
  profile.timing.releaseMaxMs = 1100.0f;
  profile.timing.programDependent = true;
  profile.noiseFloor = -80.0f;
  profile.headroom = 24.0f;
  return profile;
}

inline HardwareUnitProfile createDBX160() {
  HardwareUnitProfile profile;
  profile.name = "DBX 160";
  profile.modeledUnit = "DBX 160 VCA";
  profile.inputTransformer = TransformerProfile::createInactive();
  profile.outputTransformer = TransformerProfile::createInactive();
  return profile;
}

inline HardwareUnitProfile createSSLBus() {
  HardwareUnitProfile profile;
  profile.name = "SSL Bus";
  profile.modeledUnit = "SSL G-Series Bus Compressor";
  profile.inputStageHarmonics = HarmonicProfile::create(0.004f, 0.008f, 0.35f, 0.0f, 0.003f);
  profile.compressionStageHarmonics = HarmonicProfile::create(0.006f, 0.012f, 0.40f, 0.0f, 0.004f);
  profile.outputStageHarmonics = HarmonicProfile::create(0.008f, 0.015f, 0.35f, 0.0f, 0.004f);
  profile.inputTransformer = TransformerProfile::createActive(0.9f, 0.03f, 1.05f, 22000.0f, 10.0f, 0.002f, 0.004f, 0.4f);
  profile.outputTransformer = TransformerProfile::createActive(0.92f, 0.02f, 1.03f, 24000.0f, 8.0f, 0.002f, 0.003f, 0.45f);
  profile.timing = TimingProfile();
  profile.noiseFloor = -88.0f;
  profile.headroom = 22.0f;
  return profile;
}

}  // namespace Profiles

class HardwareProfiles {
public:
  static const HardwareUnitProfile& getLA2A() {
    static const HardwareUnitProfile profile = Profiles::createLA2A();
    return profile;
  }
  static const HardwareUnitProfile& getFET1176() {
    static const HardwareUnitProfile profile = Profiles::createFET1176();
    return profile;
  }
  static const HardwareUnitProfile& getDBX160() {
    static const HardwareUnitProfile profile = Profiles::createDBX160();
    return profile;
  }
  static const HardwareUnitProfile& getSSLBus() {
    static const HardwareUnitProfile profile = Profiles::createSSLBus();
    return profile;
  }
};

}  // namespace HardwareEmulation
