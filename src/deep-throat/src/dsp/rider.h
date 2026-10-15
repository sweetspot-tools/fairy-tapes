// Fairy Tapes · Deep Throat — Rider (level-only gain rider) and LevelMeter.
// No VST3 SDK dependency. Setters never allocate and may be called every block.
#pragma once

namespace FairyTapes::DeepThroat::DSP {

// Running RMS in dB (floor -120) over an exponential window of the given length.
class LevelMeter {
public:
  void prepare(double sampleRate, float windowMs);
  void reset();
  float process(float monoSample);

private:
  double coeff_ = 0.0;   // one-pole smoothing coefficient
  double meanSq_ = 0.0;
};

// Moves a dB gain toward (target - level), clamped to +/- range, at a limited dB/s slew.
// Holds still while frozen or while the level is below -60 dB (silence / breath gaps).
// Level only: never touches pitch.
class Rider {
public:
  void prepare(double sampleRate);
  void reset();
  void setTargetDb(float db) { targetDb_ = db; }
  void setMaxRangeDb(float db) { rangeDb_ = db < 0.0f ? 0.0f : db; }
  void setSlewDbPerSec(float dbPerSec) { slewDbPerSec_ = dbPerSec < 0.0f ? 0.0f : dbPerSec; }
  // levelDb: RMS level in dB computed by the caller. Returns the linear gain to apply.
  float processFrame(float levelDb, bool freeze);

private:
  double sampleRate_ = 48000.0;
  float targetDb_ = -20.0f;
  float rangeDb_ = 6.0f;
  float slewDbPerSec_ = 8.0f;
  double gainDb_ = 0.0;
  float gainLin_ = 1.0f;
};

} // namespace FairyTapes::DeepThroat::DSP
