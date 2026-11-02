/**
 * Fairy Tapes · The Stripper — processor metering through the real module.
 * dlopens the built bundle, creates the processor from the factory and drives process() with mock host
 * parameter queues: meter parameters are written to outputParameterChanges at most every 16 ms, a bypassed
 * strip shows no gain reduction and only the Input/Output clip LEDs, and a clear request clears the LEDs.
 * Exits 77 (skipped) when the module has not been built.
 */

#include "cids.h"
#include "params.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <vector>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef THE_STRIPPER_MODULE
#error THE_STRIPPER_MODULE must point to the built module binary
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace FairyTapes::TheStripper;
using PID = Steinberg::Vst::ParamID;   // (FairyTapes::TheStripper has an enum of the same name)

namespace {
constexpr int kSkip = 77;
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

struct MockQueue : IParamValueQueue {
  PID id = 0;
  std::vector<std::pair<int32, ParamValue>> pts;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  PID PLUGIN_API getParameterId() override { return id; }
  int32 PLUGIN_API getPointCount() override { return static_cast<int32>(pts.size()); }
  tresult PLUGIN_API getPoint(int32 i, int32& off, ParamValue& v) override { if (i < 0 || i >= (int32)pts.size()) return kResultFalse; off = pts[i].first; v = pts[i].second; return kResultOk; }
  tresult PLUGIN_API addPoint(int32 off, ParamValue v, int32& idx) override { pts.emplace_back(off, v); idx = (int32)pts.size() - 1; return kResultOk; }
};
struct MockChanges : IParameterChanges {
  std::vector<std::unique_ptr<MockQueue>> qs;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  int32 PLUGIN_API getParameterCount() override { return (int32)qs.size(); }
  IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return (i >= 0 && i < (int32)qs.size()) ? qs[i].get() : nullptr; }
  IParamValueQueue* PLUGIN_API addParameterData(const PID& id, int32& index) override {
    qs.push_back(std::make_unique<MockQueue>()); qs.back()->id = id; index = (int32)qs.size() - 1; return qs.back().get(); }
  void set(PID id, double v) { int32 i; addParameterData(id, i); int32 p; qs.back()->addPoint(0, v, p); }
};

struct Host {
  void* h = nullptr;
  IComponent* comp = nullptr;
  IAudioProcessor* proc = nullptr;
  ProcessContext ctx{};
  std::vector<float> inL, inR, outL, outR;
  int blocks = 0;
  std::map<PID, std::vector<double>> published;   // every value written per meter id
  std::vector<int> publishBlocks;                      // block index of each kMeterInL write

  bool open() {
    h = dlopen(THE_STRIPPER_MODULE, RTLD_NOW | RTLD_LOCAL);
    if (!h) { std::fprintf(stderr, "dlopen failed: %s\n", dlerror()); return false; }
    auto entry = (bool (*)(void*))dlsym(h, "ModuleEntry");
    auto getFactory = (IPluginFactory * (*)())dlsym(h, "GetPluginFactory");
    if (!entry || !getFactory) return false;
    entry(h);
    IPluginFactory* f = getFactory();
    FUnknown* obj = nullptr;
    if (f->createInstance(kProcessorUID.toTUID(), IComponent::iid, (void**)&obj) != kResultOk || !obj) return false;
    comp = static_cast<IComponent*>(obj);
    if (comp->initialize(nullptr) != kResultOk) return false;
    if (comp->queryInterface(IAudioProcessor::iid, (void**)&proc) != kResultOk || !proc) return false;
    ProcessSetup setup{kRealtime, kSample32, 64, 48000.0};
    proc->setupProcessing(setup);
    comp->setActive(true);
    ctx.sampleRate = 48000.0;
    inL.assign(64, 0.f); inR.assign(64, 0.f); outL.assign(64, 0.f); outR.assign(64, 0.f);
    f->release();
    return true;
  }

  /// One 64-sample block with the given constant amplitude (alternating sign) and optional parameter changes.
  void block(float amp, MockChanges* inChanges = nullptr) {
    for (int i = 0; i < 64; ++i) { inL[i] = inR[i] = (i & 1) ? -amp : amp; }
    run(inChanges);
  }
  /// One 64-sample block of a sine (phase continues across blocks).
  void sine(float amp, double hz, MockChanges* inChanges = nullptr) {
    for (int i = 0; i < 64; ++i) {
      const double t = (blocks * 64.0 + i) / 48000.0;
      inL[i] = inR[i] = amp * static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * t));
    }
    run(inChanges);
  }
  void run(MockChanges* inChanges) {
    float* ib[2] = {inL.data(), inR.data()};
    float* ob[2] = {outL.data(), outR.data()};
    AudioBusBuffers in{}, out{};
    in.numChannels = 2; in.channelBuffers32 = ib; out.numChannels = 2; out.channelBuffers32 = ob;
    MockChanges outChanges;
    ProcessData d{};
    d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = 64;
    d.numInputs = 1; d.numOutputs = 1; d.inputs = &in; d.outputs = &out;
    d.inputParameterChanges = inChanges; d.outputParameterChanges = &outChanges; d.processContext = &ctx;
    proc->process(d);
    for (auto& q : outChanges.qs) {
      ParamValue v = 0; int32 off = 0;
      if (q->getPoint(q->getPointCount() - 1, off, v) == kResultOk) {
        published[q->id].push_back(v);
        if (q->id == kMeterInL) publishBlocks.push_back(blocks);
      }
    }
    ++blocks;
  }
  double last(PID id) const { auto it = published.find(id); return (it == published.end() || it->second.empty()) ? -1.0 : it->second.back(); }
  void close() {
    if (proc) proc->release();
    if (comp) { comp->setActive(false); comp->terminate(); comp->release(); }
    auto ex = (bool (*)())dlsym(h, "ModuleExit"); if (ex) ex();
    dlclose(h);
  }
};

void test_meters_are_published_at_most_every_16ms() {
  Host h; CHECK(h.open());
  for (int i = 0; i < 188; ++i) h.block(0.5f);                // ~0.25 s at 48 kHz
  const auto& pb = h.publishBlocks;
  CHECK(pb.size() >= 12);                                     // it does update (~60 Hz)
  CHECK(pb.size() <= 17);                                     // and never faster than once per 16 ms
  for (size_t i = 1; i < pb.size(); ++i) CHECK((pb[i] - pb[i - 1]) * 64.0 / 48000.0 >= 0.0155);
  CHECK(h.last(kMeterInL) > 0.5 && h.last(kMeterInL) < 1.0);  // 0.5 -> -6 dBFS -> (54/66) = 0.82
  CHECK(std::fabs(h.last(kMeterInL) - (54.0 / 66.0)) < 0.02);
  CHECK(h.last(kClipInput) == 0.0);
  h.close();
}

void test_global_bypass_is_transparent_and_only_input_output_clip() {
  Host h; CHECK(h.open());
  MockChanges c; c.set(kBypass, 1.0);
  h.block(1.5f, &c);                                          // over 0 dBFS, global bypass on
  for (int i = 0; i < 40; ++i) h.block(1.5f);
  CHECK(h.last(kClipInput) == 1.0);
  CHECK(h.last(kClipOutput) == 1.0);
  CHECK(h.last(kClipEq) == 0.0 && h.last(kClipComp) == 0.0 && h.last(kClipDrive) == 0.0 && h.last(kClipLimiter) == 0.0);
  CHECK(h.last(kMeterGrComp) == 0.0 && h.last(kMeterGrLimiter) == 0.0 && h.last(kMeterGrGate) == 0.0);
  CHECK(std::fabs(h.last(kMeterInL) - h.last(kMeterOutL)) < 1e-9);   // output equals input
  for (int i = 0; i < 64; ++i) CHECK(h.outL[i] == h.inL[i]);
  h.close();
}

void test_compressor_gain_reduction_uses_the_ssl_encoding() {
  Host h; CHECK(h.open());
  MockChanges c; c.set(kCompressAmount, 1.0);
  h.sine(0.7f, 1000.0, &c);
  for (int i = 0; i < 200; ++i) h.sine(0.7f, 1000.0);
  const double v = h.last(kMeterGrComp);
  CHECK(v > 0.0 && v < 1.0);                     // compressing: some reduction, never the full-scale 1.0
  const double db = -20.0 * std::log10(1.0 - v); // what SSL 360 Link shows
  CHECK(db > 0.05 && db < 40.0);                 // this steady sine is only lightly compressed
  CHECK(std::fabs(v - (1.0 - std::pow(10.0, -db / 20.0))) < 1e-9);
  h.close();
}

// Output of a fresh plug-in after a few blocks, with the given parameter values set once.
std::vector<float> outputWith(std::initializer_list<std::pair<PID, double>> params) {
  Host h; CHECK(h.open());
  MockChanges c; for (const auto& p : params) c.set(p.first, p.second);
  h.sine(0.5f, 1000.0, &c);   // a musical signal: the saturations hardly touch a square wave at Nyquist
  for (int i = 0; i < 30; ++i) h.sine(0.5f, 1000.0);
  std::vector<float> out(h.outL.begin(), h.outL.end());
  h.close();
  return out;
}

double maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
  double d = 0;
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) d = std::max(d, std::fabs((double)a[i] - b[i]));
  return d;
}

void test_saturation_belongs_to_the_eq_again() {
  // the 4K saturation is the last step of the EQ (back from the Drive, 2026-10-24): audible with the EQ in ...
  CHECK(maxDiff(outputWith({{kEqSaturation, 0.0}}), outputWith({{kEqSaturation, 1.0}})) > 1e-3);
  // ... and gone with the EQ's IN off, whatever the old NAM / 4K switch says. Since 2026-11-02 the filters keep running with the
  // EQ out, so the limiter's and gate's floating-point dither (Airwindows, seeded at random per instance) no longer rounds away:
  // two fresh instances differ by a few 1e-6. The saturation itself changes the output by more than 1e-3 (first check).
  constexpr double kDither = 1e-4;
  CHECK(maxDiff(outputWith({{kBypassEQ, 1.0}, {kEqSaturation, 0.0}}), outputWith({{kBypassEQ, 1.0}, {kEqSaturation, 1.0}})) < kDither);
  CHECK(maxDiff(outputWith({{kBypassEQ, 1.0}, {kDriveNamOn, 0.0}, {kEqSaturation, 1.0}}),
                outputWith({{kBypassEQ, 1.0}, {kDriveNamOn, 1.0}, {kEqSaturation, 1.0}})) < kDither);
}

void test_tape_engine_switch_and_drive_in() {
  // TAPE on colours the signal; its switch off skips it; the Drive's IN off skips it too
  const auto on = outputWith({{kDriveTapeOn, 1.0}, {kDriveTapeAmount, 1.0}});
  const auto off = outputWith({{kDriveTapeOn, 0.0}, {kDriveTapeAmount, 1.0}});
  CHECK(maxDiff(on, off) > 1e-3);
  CHECK(maxDiff(off, outputWith({{kDriveTapeOn, 1.0}, {kDriveTapeAmount, 1.0}, {kBypassDrive, 1.0}})) < 1e-6);
  // the NAM without a model passes the signal through: its switch does not change the sound
  CHECK(maxDiff(outputWith({{kDriveNamOn, 1.0}, {kDriveTapeOn, 0.0}}), outputWith({{kDriveNamOn, 0.0}, {kDriveTapeOn, 0.0}})) < 1e-6);
}

// Drive clip LED after a hot EQ: a 40 Hz sine at -10 dBFS with the LF shelf at +20 dB (40 Hz is under its 100 Hz corner, so it
// gets the whole boost) drives the EQ far over 0 dBFS.
// In BUS the Drive comes after the EQ, so TAPE gets the hot signal and its compensated output goes over 0 dBFS too;
// in CHAN the Drive comes right after the input and sees the clean -10 dBFS sine.
double driveClipAfterHotEq(bool channel) {
  Host h; CHECK(h.open());
  MockChanges c;
  c.set(kStripMode, channel ? kStripModeChannel : kStripModeMixBus);
  c.set(kEqLFGain, 1.0); c.set(kDriveTapeOn, 1.0); c.set(kDriveTapeAmount, 0.0);
  c.set(kBypassLimiter, 1.0);
  h.sine(0.3f, 40.0, &c);
  for (int i = 0; i < 150; ++i) h.sine(0.3f, 40.0);
  CHECK(h.last(kClipEq) == 1.0);   // the setup works: the EQ clips in both modes
  const double v = h.last(kClipDrive);
  h.close();
  return v;
}


void test_drive_block_moves_with_strip_mode() {
  CHECK(driveClipAfterHotEq(false) == 1.0);   // BUS: Drive after the EQ
  CHECK(driveClipAfterHotEq(true) == 0.0);    // CHAN: Drive before the EQ
}

// Last gate gain-reduction value (SSL encoding) after a few quiet blocks with the given settings.
double gateGrWith(double threshold, bool gateIn, bool globalBypass = false) {
  Host h; CHECK(h.open());
  MockChanges c; c.set(kGateThreshold, threshold); c.set(kBypassGate, gateIn ? 0.0 : 1.0); c.set(kBypass, globalBypass ? 1.0 : 0.0);
  h.block(0.002f, &c);   // -54 dBFS: under DeNoise's threshold at 1.0 (about -48 dB)
  for (int i = 0; i < 200; ++i) h.block(0.002f);
  const double v = h.last(kMeterGrGate);
  h.close();
  return v;
}

void test_gate_gain_reduction_meter() {
  CHECK(gateGrWith(1.0, true) > 0.0);                      // threshold at 0 dB: a very quiet signal is gated away
  CHECK(gateGrWith(1.0, true) < 1.0);
  CHECK(gateGrWith(0.0, true) == 0.0);                     // threshold at the bottom: nothing is gated
  CHECK(gateGrWith(1.0, false) == 0.0);                    // gate out: no reduction
  CHECK(gateGrWith(1.0, true, true) == 0.0);               // global bypass: no reduction
}

// Filters independent of the EQ's IN (2026-11-02, spec 11): output RMS over the last 60 blocks of a sine (dB relative to the
// input), with every module out except what `params` turns back in.
struct FilterRun { double gainDb; std::vector<float> lastIn, lastOut; double clipEq; };
FilterRun filterRun(double hz, float amp, std::initializer_list<std::pair<PID, double>> params) {
  Host h; CHECK(h.open());
  MockChanges c;
  c.set(kBypassCompression, 1.0); c.set(kBypassDrive, 1.0); c.set(kBypassLimiter, 1.0); c.set(kBypassGate, 1.0);
  for (const auto& p : params) c.set(p.first, p.second);
  h.sine(amp, hz, &c);
  double in2 = 0.0, out2 = 0.0;
  for (int b = 0; b < 100; ++b) {
    h.sine(amp, hz);
    if (b >= 40)
      for (int i = 0; i < 64; ++i) { in2 += (double)h.inL[i] * h.inL[i]; out2 += (double)h.outL[i] * h.outL[i]; }
  }
  FilterRun r{10.0 * std::log10(std::max(out2, 1e-30) / in2), std::vector<float>(h.inL.begin(), h.inL.end()),
              std::vector<float>(h.outL.begin(), h.outL.end()), h.last(kClipEq)};
  h.close();
  return r;
}

void test_filters_work_with_the_eq_out() {
  // EQ out, filters in, High Pass at the top of its range: a 50 Hz sine is cut hard (before 2026-11-02 the filters went out with the EQ)
  const auto hp = filterRun(50.0, 0.5f, {{kBypassEQ, 1.0}, {kBypassLPHP, 0.0}, {kEqHPFFreq, 1.0}});
  if (hp.gainDb > -20.0) std::fprintf(stderr, "  HPF with the EQ out: %+.1f dB at 50 Hz\n", hp.gainDb);
  CHECK(hp.gainDb < -20.0);
  // ... and the Low Pass at its bottom cuts a 15 kHz sine
  const auto lp = filterRun(15000.0, 0.5f, {{kBypassEQ, 1.0}, {kBypassLPHP, 0.0}, {kEqLPFFreq, 0.0}});
  if (lp.gainDb > -6.0) std::fprintf(stderr, "  LPF with the EQ out: %+.1f dB at 15 kHz\n", lp.gainDb);
  CHECK(lp.gainDb < -6.0);
  // EQ out and filters out: a passthrough
  const auto off = filterRun(50.0, 0.5f, {{kBypassEQ, 1.0}, {kBypassLPHP, 1.0}, {kEqHPFFreq, 1.0}});
  CHECK(maxDiff(off.lastIn, off.lastOut) < 1e-6);
  // with the EQ out the bands and SATURATE stay out, whatever their values
  const auto flat = filterRun(80.0, 0.5f, {{kBypassEQ, 1.0}, {kBypassLPHP, 0.0}});
  const auto turned = filterRun(80.0, 0.5f, {{kBypassEQ, 1.0}, {kBypassLPHP, 0.0}, {kEqLFGain, 1.0}, {kEqHMGain, 0.0}, {kEqSaturation, 1.0}});
  CHECK(maxDiff(flat.lastOut, turned.lastOut) < 1e-6);
  // the default filters (HPF 20 Hz, LPF 20 kHz) leave a 1 kHz sine alone with the EQ out
  CHECK(std::fabs(filterRun(1000.0, 0.5f, {{kBypassEQ, 1.0}, {kBypassLPHP, 0.0}}).gainDb) < 0.5);
  // the EQ in with the filters in still cuts (no regression)
  CHECK(filterRun(50.0, 0.5f, {{kBypassEQ, 0.0}, {kBypassLPHP, 0.0}, {kEqHPFFreq, 1.0}}).gainDb < -20.0);
}

void test_eq_clip_led_follows_filters_or_eq() {
  // CLIP EQ taps the line after FILTERS and EQUALISER (ROUTING): lit while either is in, dark when both are out
  CHECK(filterRun(1000.0, 1.5f, {{kBypassEQ, 1.0}, {kBypassLPHP, 0.0}}).clipEq == 1.0);
  CHECK(filterRun(1000.0, 1.5f, {{kBypassEQ, 1.0}, {kBypassLPHP, 1.0}}).clipEq == 0.0);
  CHECK(filterRun(1000.0, 1.5f, {{kBypassEQ, 0.0}, {kBypassLPHP, 1.0}}).clipEq == 1.0);
}

void test_clear_clips_request() {
  Host h; CHECK(h.open());
  for (int i = 0; i < 40; ++i) h.block(1.5f);
  CHECK(h.last(kClipInput) == 1.0);
  MockChanges c; c.set(kClearClips, 1.0);
  h.block(0.01f, &c);
  for (int i = 0; i < 40; ++i) h.block(0.01f);
  CHECK(h.last(kClipInput) == 0.0);                           // cleared (the signal is now quiet)
  CHECK(h.last(kClearClips) == 0.0);                          // and the request was acknowledged
  h.close();
}
} // namespace

int main() {
  struct stat st{};
  if (stat(THE_STRIPPER_MODULE, &st) != 0) { std::printf("SKIP: module not built: %s\n", THE_STRIPPER_MODULE); return kSkip; }
  test_meters_are_published_at_most_every_16ms();
  test_global_bypass_is_transparent_and_only_input_output_clip();
  test_compressor_gain_reduction_uses_the_ssl_encoding();
  test_saturation_belongs_to_the_eq_again();
  test_tape_engine_switch_and_drive_in();
  test_drive_block_moves_with_strip_mode();
  test_gate_gain_reduction_meter();
  test_filters_work_with_the_eq_out();
  test_eq_clip_led_follows_filters_or_eq();
  test_clear_clips_request();
  std::printf(g_failed ? "Processor meter tests FAILED\n" : "Processor meter tests OK\n");
  return g_failed ? 1 : 0;
}
