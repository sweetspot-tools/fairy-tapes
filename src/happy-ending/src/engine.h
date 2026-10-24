/**
 * Fairy Tapes · Happy Ending — The whole mastering chain used by the VST3 Processor (pure, no SDK libs).
 *
 * INPUT gain -> FINE EQ -> VIBE EQ network -> [zone A, oversampled: VIBE TUBE] -> GLUE ->
 * [zone B, oversampled: CLIP -> LIMIT -> FINAL -> METALHEAD] -> ceiling guard -> OUTPUT gain ->
 * BYPASS crossfade -> OUT peak meter and LUFS (both read the plug-in output).
 *
 *  - CLIP (ClipSoftly) has no off state of its own: the engine crossfades it with its dry signal over
 *    Tuning::kClipRampMs (zero latency, so the two are aligned).
 *
 *  - LOUDNESS is applied here, on top of the stage values (loudPush): CLIP drive + clipDb, LIMIT
 *    threshold - limitDb, FINAL EFFECT + 0.6 * final01 (capped at 100 %) or DRIVE + 2 dB * final01
 *    (capped at +2 dB). The stage values in EngineSettings never change.
 *  - Latency (base rate) = zone A oversampler + zone B oversampler + (LIMIT + FINAL + METALHEAD
 *    latencies at the zone-B rate) / factor; it depends only on the oversampling factor given to
 *    prepare() (constant across styles, modes and on/off).
 *  - Ceiling guard: the decimation filter of zone B can rebuild base-rate samples slightly above
 *    the ceiling the stages held at the oversampled rate, so whenever LIMIT, FINAL or METALHEAD is
 *    on, every sample is clamped at the CEILING after zone B (global constraint: no output sample
 *    above the CEIL with one of them on). OUTPUT gain comes after it, as in the preview.
 *  - BYPASS: the output crossfades (kGainRampSamples) to the sanitized input delayed by
 *    latencySamples() through one delay line, sample-exact once settled. The chain keeps running,
 *    so meters, LUFS and stage LEDs stay live.
 *  - Clip LEDs: a sample with |x| >= 1.0 at the output of a stage lights its LED for
 *    kClipHoldSeconds of audio time (or until clearClips()). Zone-B stages are checked at the
 *    oversampled rate. IN is after the INPUT gain, OUT is the plug-in output.
 *  - Non-finite input samples are replaced by 0 at the entry. FTZ / DAZ are set for the duration of
 *    process() (ScopedNoDenormals). No allocation in set() or process().
 *
 * Threading: set(), process(), clearClips() and resetIntegrated() belong to the audio thread;
 * prepare() allocates and is called with processing stopped. FineEq / VibeEq curves for a screen are
 * NOT read from this engine (VibeEq::magnitudeDb is not safe against set()): the editor computes
 * them on its own FineEq / VibeEq instances from the parameter values.
 *
 * Channels: process() always takes two distinct buffers. For a mono bus the processor feeds a copy
 * of the channel as R and keeps L; the LUFS of that dual-mono pair reads 3.01 LU above the BS.1770
 * value of the mono channel (kDualMonoLufsOffsetDb corrects it, applied by the processor).
 */

#pragma once

#include "dsp/clip_softly.h"
#include "dsp/final_stage.h"
#include "dsp/fine_eq.h"
#include "dsp/glue.h"
#include "dsp/limiter.h"
#include "dsp/loudness_meter.h"
#include "dsp/metalhead.h"
#include "dsp/oversampler.h"
#include "dsp/vibe_eq.h"
#include "params.h"
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#define FT_HE_HAS_SSE 1
#endif

namespace FairyTapes::HappyEnding {

constexpr int kGainRampSamples = 64;        // INPUT / OUTPUT gain and BYPASS glide length
constexpr double kClipHoldSeconds = 2.0;     // clip LEDs stay lit this long
constexpr double kPeakFloorDb = -120.0;
constexpr double kDualMonoLufsOffsetDb = -3.0103;

// Flush-to-zero and denormals-are-zero for the lifetime of the object (x86 MXCSR, ARM FPCR / FPSCR
// FZ bit); the previous mode is restored on destruction.
class ScopedNoDenormals {
public:
  ScopedNoDenormals() {
#if defined(FT_HE_HAS_SSE)
    old_ = _mm_getcsr();
    _mm_setcsr(static_cast<unsigned>(old_) | 0x8040u);
#elif defined(__aarch64__)
    std::uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    old_ = fpcr;
    asm volatile("msr fpcr, %0" : : "r"(fpcr | (1ull << 24)));
#elif defined(__arm__) && defined(__ARM_FP)
    std::uint32_t fpscr;
    asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
    old_ = fpscr;
    asm volatile("vmsr fpscr, %0" : : "r"(fpscr | (1u << 24)));
#endif
  }
  ~ScopedNoDenormals() {
#if defined(FT_HE_HAS_SSE)
    _mm_setcsr(static_cast<unsigned>(old_));
#elif defined(__aarch64__)
    asm volatile("msr fpcr, %0" : : "r"(old_));
#elif defined(__arm__) && defined(__ARM_FP)
    asm volatile("vmsr fpscr, %0" : : "r"(static_cast<std::uint32_t>(old_)));
#endif
  }
  ScopedNoDenormals(const ScopedNoDenormals&) = delete;
  ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
  std::uint64_t old_ = 0;
};

// Every control in units (dB, Hz, 0..1, factors). FINE EQ bands and VIBE EQ keep the positions their
// blocks take (indices into the tables of params.h; LOW / AIR / TUBE are the 0-10 integers).
struct EngineSettings {
  bool bypass = false;
  float inputGainDb = 0.0f;
  float outputGainDb = 0.0f;
  float ceilingDb = -0.3f;
  float loudness = 0.0f;      // 0..10
  float targetLufs = -14.0f;  // read by MATCH (processor / controller), not by the chain
  int oversampling = 2;       // factor 1 / 2 / 4: the processor passes it to prepare(); set() ignores it

  bool fineOn = true;
  std::array<DSP::BandSettings, DSP::FineEq::kNumBands> bands{};
  int listenBand = -1;        // -1 or 0..2 (LO MID, MID, HI MID); -1 when that band or the stage is off

  DSP::VibeSettings vibe{};
  DSP::GlueSettings glue{};

  bool clipOn = true;
  float clipDriveDb = 2.0f;

  bool limitOn = true;
  float limitThreshDb = -2.0f;
  int limitStyle = 0;         // 0 MODERN, 1 PUNCH, 2 DYN
  int limitReleaseIndex = 4;  // 0..8, 4 = as the style wants
  bool setActive = false;     // SET (hidden, not stored)

  bool finalOn = true;
  int finalMode = 0;          // 0 INFLATE, 1 CLIP
  float finalEffect = 0.5f;   // 0..1
  float finalCurve = 0.25f;   // 0..1
  float finalDriveDb = 2.0f;  // 0..+2

  bool metalOn = false;
  float pushDb = 2.0f;

  bool operator==(const EngineSettings&) const = default;
};

// norm: kNumParams normalized values in ParamID order. listenBand / setActive: the hidden session
// parameters (kListenBand, kSetActive).
EngineSettings mapParams(const double* norm, int listenBand, bool setActive);

// Clip LEDs, in signal order, and GR rows.
enum ClipLed { kLedIn = 0, kLedFeq, kLedVeq, kLedGlu, kLedClp, kLedLim, kLedFin, kLedMtl, kLedOut, kNumLeds };
enum GrRow { kGrGlue = 0, kGrClip, kGrLimit, kGrFinal, kGrMetal, kNumGrRows };

struct Meters {
  float inPeakDb[2] = {-120.0f, -120.0f};   // after the INPUT gain, over the last process() call
  float outPeakDb[2] = {-120.0f, -120.0f};  // the plug-in output, over the last process() call
  float grDb[kNumGrRows] = {};              // G C L F M, dB >= 0 (0 while the stage is off)
  bool clip[kNumLeds] = {};                 // IN FEQ VEQ GLU CLP LIM FIN MTL OUT
  double lufsShort = -std::numeric_limits<double>::infinity();  // -inf until 3 s are in
  double lufsIntegrated = -std::numeric_limits<double>::infinity();
  float dynLow = 0.0f, dynAir = 0.0f;       // VIBE DYN pull-back, 0..1
};

class Engine {
public:
  // oversampling: factor 1, 2 or 4 (anything else is 1). Allocates; re-applies the last settings.
  void prepare(double sampleRate, int maxBlock, int oversampling);
  void set(const EngineSettings& settings);
  // L and R: two distinct buffers of n samples, processed in place. Any n (chunks of maxBlock).
  void process(float* left, float* right, int n);

  int latencySamples() const { return latency_; }
  int oversampling() const { return factor_; }
  const Meters& meters() const { return meters_; }
  const EngineSettings& settings() const { return applied_; }
  void clearClips();
  void resetIntegrated();

private:
  struct Ramp {
    double cur = 1.0, target = 1.0, step = 0.0;
    int left = 0;
    void snap(double v) { cur = target = v; step = 0.0; left = 0; }
    void to(double v, int samples);
    double next() {
      if (left > 0) {
        cur += step;
        if (--left == 0) cur = target;
      }
      return cur;
    }
    bool settled() const { return left == 0; }
  };

  void apply(const EngineSettings& s, bool force);
  void processChunk(float* left, float* right, int n, double* inPeak, double* outPeak);
  void markClip(int led, const float* l, const float* r, int n, bool* hit) const;

  double sampleRate_ = 48000.0;
  int maxBlock_ = 512;
  int factor_ = 1;
  int latency_ = 0;
  bool prepared_ = false;
  bool hasSettings_ = false;
  bool forceNext_ = true;
  EngineSettings applied_{};

  // blocks
  DSP::FineEq fine_;
  DSP::VibeEq vibe_;
  DSP::Glue glue_;
  DSP::ClipSoftly clip_;
  DSP::Limiter limiter_;
  DSP::FinalStage final_;
  DSP::Metalhead metal_;
  DSP::LoudnessMeter loud_;
  DSP::Oversampler osA_[2], osB_[2];

  // what the blocks were last given (the setters run only on a change)
  float clipDriveApplied_ = -1.0f;
  DSP::LimiterSettings limitApplied_{};
  struct FinalArgs {
    bool on; int mode; float effect, curve, driveDb, ceilingDb;
    bool operator==(const FinalArgs&) const = default;
  } finalApplied_{};
  struct MetalArgs {
    bool on; float pushDb, ceilingDb;
    bool operator==(const MetalArgs&) const = default;
  } metalApplied_{};

  // gains, bypass, CLIP on/off crossfade
  Ramp inGain_, outGain_, bypassMix_;  // bypassMix_: 1 = the delayed input
  double clipMix_ = 1.0, clipMixTarget_ = 1.0, clipMixStep_ = 1.0;
  bool guardOn_ = true;
  float ceilLin_ = 1.0f;

  // buffers (allocated in prepare)
  std::vector<float> bypassRingL_, bypassRingR_;
  int bypassPos_ = 0;
  std::vector<float> delayedL_, delayedR_;  // maxBlock: the input delayed by the latency
  std::vector<float> osL_, osR_;            // maxBlock * factor: the oversampled zones
  std::vector<float> dryL_, dryR_;          // maxBlock * factor: CLIP dry signal for its crossfade

  int clipHoldSamples_ = 96000;
  std::array<int, kNumLeds> clipHold_{};
  Meters meters_{};
};

} // namespace FairyTapes::HappyEnding
