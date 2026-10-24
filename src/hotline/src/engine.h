/**
 * Fairy Tapes · Hotline — Signal chain used by the VST3 Processor (pure, no SDK libs).
 *
 * Per channel: input -> Mono (lerp to (L+R)/2) -> [dry tap] -> Band -> Drive (+ DC blocker) ->
 * Cone -> Crush -> Compander -> + Noise -> Room -> Mix (dry/wet) -> Gain -> safety ceiling ->
 * Bypass (ramp to the input) -> out. Zero latency: nothing looks ahead.
 *
 * Block values (device row x Morph x user controls) are recomputed every 32 samples and follow
 * their target with a 10 ms one-pole (a Device change is a ~40 ms crossfade); frequencies move
 * in log, levels in dB. Each value snaps onto its target within a tiny epsilon, so Morph 0 lands
 * exactly on neutral and every block takes its exact pass-through path again.
 */

#pragma once

#include "devices.h"
#include "dsp/band.h"
#include "dsp/compander.h"
#include "dsp/cone.h"
#include "dsp/crush.h"
#include "dsp/noise.h"
#include "dsp/room.h"
#include "params.h"
#include <vector>

namespace FairyTapes::Hotline {

// Fills kNumParams normalized defaults.
void loadDefaults(double* values);

// drive/noise/room: multiplier of the device value (0..2, 1 = as the device); tone: -1..1
// (LPF +tone octaves, HPF -tone/2 octaves, both scaled by morph); gainDb: -20..20; mix: 0..1.
struct Settings {
  int device;
  float morph, gainDb, drive, tone, noise, room, mix;
  bool mono, bypass, characterIn;
};

// Normalized values are clamped to [0,1]; non-finite ones fall back to defaults. Always finite.
// IN of CHARACTER off: drive, tone, noise and room are 0 and mix is 1 (band, cone, crush and
// compander of the device stay).
Settings mapParams(const double* values);

// The block values the chain aims at: morphParams(device, morph), then the user controls.
// Morph 0 gives neutralParams() whatever the user controls (the tone shift scales with morph).
// Noise is capped at kNoiseCeilingDb. Applies the IN-off semantics too.
constexpr double kNoiseCeilingDb = -20.0;
BlockParams targetParams(const BlockParams& device, const Settings& s);

// Peaks in dB (floor -120) over the last process() call: input after the NaN guard, output after
// Bypass. Clip flags (|x| >= 1 at the input, after the Drive shaper, at the output) are held for
// kClipHoldSeconds of audio time or until clearClips().
struct DisplayValues {
  float inPeakDb[2], outPeakDb[2];
  bool clipIn, clipDrive, clipOut;
};
constexpr double kClipHoldSeconds = 2.0;

// Processed output never leaves +-kOutputCeiling (+24 dBFS); the exact bypass copy is not touched.
constexpr float kOutputCeiling = 16.f;

class Engine {
public:
  // Allocates everything; process() never allocates afterwards.
  void prepare(double sampleRate, int maxBlockSize);
  // Settings with any non-finite float are ignored (the previous ones stay). The first call after
  // prepare() jumps straight to its values; later calls are smoothed.
  void setSettings(const Settings& s);
  // inR null or == inL: mono host track (both outputs equal). outR null: mono output. Inputs may
  // alias outputs. Blocks longer than maxBlockSize are split; 0-length calls do nothing.
  void process(const float* inL, const float* inR, float* outL, float* outR, int numSamples);
  DisplayValues lastDisplay() const;
  void clearClips() { clipInLeft_ = clipDriveLeft_ = clipOutLeft_ = 0; }
  // Clears every DSP state and noise generator; smoothed values jump to their targets.
  void reset();

private:
  struct Channel {
    DSP::Band band;
    DSP::Cone cone;
    DSP::Crush crush;
    DSP::Compander comp;
    DSP::Noise noise;
    DSP::Room room;
    float dcX = 0.f, dcY = 0.f;  // DC blocker after the Drive shaper
    bool bandOn = false;
  };

  void computeTargets();
  void snapToTargets();
  void updateBlocks();
  void clearChannel(int c);
  float chain(int c, float x);
  void processChunk(const float* inL, const float* inR, float* outL, float* outR, int n, bool monoHost);

  Channel ch_[2];
  std::vector<float> xL_, xR_;
  int maxBlock_ = 0, counter_ = 0;
  double fs_ = 48000.0, smoothK_ = 0.0;

  Settings s_{};
  BlockParams target_{}, cur_{};
  bool init_ = false, force_ = true, dcOn_ = false;
  float drive_ = 0.f, bandW_ = 0.f, dcR_ = 0.f;
  float gain_ = 1.f, gainT_ = 1.f, mix_ = 1.f, mixT_ = 1.f, smoothC_ = 0.f;
  float mono_ = 0.f, monoT_ = 0.f, monoStep_ = 1.f;
  float byp_ = 0.f, bypT_ = 0.f, bypStep_ = 1.f;

  float inPeak_[2] = {0.f, 0.f}, outPeak_[2] = {0.f, 0.f}, drivePeak_ = 0.f;
  DisplayValues display_{{-120.f, -120.f}, {-120.f, -120.f}, false, false, false};
  long long clipInLeft_ = 0, clipDriveLeft_ = 0, clipOutLeft_ = 0, clipHoldSamples_ = 96000;
};

} // namespace FairyTapes::Hotline
