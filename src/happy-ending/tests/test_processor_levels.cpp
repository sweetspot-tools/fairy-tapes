/**
 * Fairy Tapes · Happy Ending — the processor through the real module.
 * dlopens the built bundle, creates the processor from the factory and drives process() with mock host
 * parameter queues (fixed size: no allocation while process() runs). Covers: the display parameters (peaks,
 * clip LEDs with the clear request and its acknowledgement, LUFS with the reset and the mono correction), BYPASS
 * (the input delayed by the reported latency, in place and not, stereo and mono), the latency
 * (= Engine::latencySamples(), changing with OVERSAMPLING only at the next activation), the state (round trip,
 * LISTEN and SET not stored) and that process() does not allocate.
 * Exits 77 (skipped) when the module has not been built.
 */

#include "cids.h"
#include "display.h"
#include "engine.h"
#include "params.h"
#include "state.h"
#include "tuning.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <new>
#include <set>
#include <vector>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef HAPPY_ENDING_MODULE
#error HAPPY_ENDING_MODULE must point to the built module binary
#endif

// Counts heap allocations while g_countAllocs is set. The test executable exports its symbols, so the module's
// operator new resolves to this one.
static std::atomic<bool> g_countAllocs{false};
static std::atomic<int> g_allocs{0};
void* operator new(std::size_t n) {
  if (g_countAllocs) ++g_allocs;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace FairyTapes::HappyEnding;
using PID = Steinberg::Vst::ParamID;   // (FairyTapes::HappyEnding has an enum of the same name)

namespace {
constexpr int kSkip = 77;
constexpr int kBlock = 64;
constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

struct MockQueue : IParamValueQueue {
  PID id = 0;
  int32 n = 0;
  int32 offs[4] = {};
  ParamValue vals[4] = {};
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  PID PLUGIN_API getParameterId() override { return id; }
  int32 PLUGIN_API getPointCount() override { return n; }
  tresult PLUGIN_API getPoint(int32 i, int32& off, ParamValue& v) override {
    if (i < 0 || i >= n) return kResultFalse;
    off = offs[i]; v = vals[i]; return kResultOk;
  }
  tresult PLUGIN_API addPoint(int32 off, ParamValue v, int32& idx) override {
    if (n >= 4) return kResultFalse;
    offs[n] = off; vals[n] = v; idx = n++; return kResultOk;
  }
};
struct MockChanges : IParameterChanges {
  static constexpr int kMax = 64;
  MockQueue qs[kMax];
  int count = 0;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  int32 PLUGIN_API getParameterCount() override { return count; }
  IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return (i >= 0 && i < count) ? &qs[i] : nullptr; }
  IParamValueQueue* PLUGIN_API addParameterData(const PID& id, int32& index) override {
    if (count >= kMax) return nullptr;
    qs[count].id = id; qs[count].n = 0; index = count; return &qs[count++];
  }
  void reset() { count = 0; }
  void set(PID id, double v) { int32 i; int32 p; if (auto* q = addParameterData(id, i)) q->addPoint(0, v, p); }
};

// A growing memory stream for getState / setState.
struct VecStream : IBStream {
  std::vector<uint8_t> data;
  int64 pos = 0;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API read(void* buffer, int32 n, int32* got) override {
    const int64 left = static_cast<int64>(data.size()) - pos;
    const int32 k = static_cast<int32>(left < n ? (left < 0 ? 0 : left) : n);
    if (k > 0) std::memcpy(buffer, data.data() + pos, static_cast<size_t>(k));
    pos += k;
    if (got) *got = k;
    return kResultOk;
  }
  tresult PLUGIN_API write(void* buffer, int32 n, int32* wrote) override {
    const auto* p = static_cast<const uint8_t*>(buffer);
    data.insert(data.end(), p, p + n);
    if (wrote) *wrote = n;
    return kResultOk;
  }
  tresult PLUGIN_API seek(int64 p, int32 mode, int64* result) override {
    pos = mode == kIBSeekSet ? p : (mode == kIBSeekCur ? pos + p : static_cast<int64>(data.size()) + p);
    if (result) *result = pos;
    return kResultOk;
  }
  tresult PLUGIN_API tell(int64* p) override { if (p) *p = pos; return kResultOk; }
};

using Gen = std::function<float(long, int)>;   // absolute sample index, channel

struct Host {
  void* h = nullptr;
  IComponent* comp = nullptr;
  IAudioProcessor* proc = nullptr;
  bool mono = false, inPlace = false;
  long pos = 0;
  ProcessContext ctx{};
  std::vector<float> inL, inR, outL, outR;
  MockChanges outChanges;
  std::map<PID, std::vector<double>> published;   // every value written per display id
  std::map<PID, double> lastBlock;                // values written by the latest block only
  std::vector<float> recL;                        // output L of every block since record = true
  bool record = false;

  bool open(bool monoBus = false, bool inPlaceBuffers = false, int blockSize = kBlock) {
    mono = monoBus; inPlace = inPlaceBuffers;
    h = dlopen(HAPPY_ENDING_MODULE, RTLD_NOW | RTLD_LOCAL);
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
    SpeakerArrangement arr = monoBus ? SpeakerArr::kMono : SpeakerArr::kStereo;
    if (proc->setBusArrangements(&arr, 1, &arr, 1) != kResultTrue) return false;
    ProcessSetup setup{kRealtime, kSample32, blockSize, kSr};
    if (proc->setupProcessing(setup) != kResultOk) return false;
    if (comp->setActive(true) != kResultOk) return false;
    ctx.sampleRate = kSr;
    inL.assign(static_cast<size_t>(blockSize), 0.f); inR = inL; outL = inL; outR = inL;
    f->release();
    return true;
  }

  // One block of `gen` (channel 0 = left, 1 = right).
  void block(const Gen& gen, MockChanges* inChanges = nullptr, int n = kBlock) {
    for (int i = 0; i < n; ++i) { inL[i] = gen(pos + i, 0); inR[i] = gen(pos + i, 1); }
    float* ib[2] = {inL.data(), inR.data()};
    float* ob[2] = {outL.data(), outR.data()};
    if (inPlace) { std::memcpy(outL.data(), inL.data(), sizeof(float) * n); std::memcpy(outR.data(), inR.data(), sizeof(float) * n); ib[0] = ob[0]; ib[1] = ob[1]; }
    AudioBusBuffers in{}, out{};
    in.numChannels = mono ? 1 : 2; in.channelBuffers32 = ib; out.numChannels = mono ? 1 : 2; out.channelBuffers32 = ob;
    outChanges.reset();
    ProcessData d{};
    d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = n;
    d.numInputs = 1; d.numOutputs = 1; d.inputs = &in; d.outputs = &out;
    d.inputParameterChanges = inChanges; d.outputParameterChanges = &outChanges; d.processContext = &ctx;
    proc->process(d);
    pos += n;
    lastBlock.clear();
    for (int k = 0; k < outChanges.count; ++k) {
      ParamValue v = 0; int32 off = 0;
      auto& q = outChanges.qs[k];
      if (q.getPoint(q.getPointCount() - 1, off, v) == kResultOk) {
        published[q.id].push_back(v);
        lastBlock[q.id] = v;
      }
    }
    if (record) recL.insert(recL.end(), outL.begin(), outL.begin() + n);
  }
  void blocks(int n, const Gen& gen) { for (int i = 0; i < n; ++i) block(gen); }
  double last(PID id) const {
    auto it = published.find(id);
    return (it == published.end() || it->second.empty()) ? -1.0 : it->second.back();
  }
  uint32 latency() { return proc->getLatencySamples(); }
  void reactivate() { comp->setActive(false); comp->setActive(true); }
  void close() {
    if (comp) { comp->setActive(false); }
    if (proc) proc->release();
    if (comp) { comp->terminate(); comp->release(); }
    auto ex = (bool (*)())dlsym(h, "ModuleExit"); if (ex) ex();
    dlclose(h);
  }
};

const Gen kSilence = [](long, int) { return 0.f; };
Gen constant(float ampL, float ampR) {   // alternating sign, constant amplitude
  return [=](long n, int ch) { const float a = ch == 0 ? ampL : ampR; return (n & 1) ? -a : a; };
}
Gen sine(double hz, double amp, double phase = 0.0) {
  return [=](long n, int ch) { return static_cast<float>(amp * std::sin(2.0 * kPi * hz * static_cast<double>(n) / kSr + phase + ch)); };
}

double rms(const std::vector<float>& x, size_t from, size_t to) {
  double s = 0; for (size_t i = from; i < to; ++i) s += double(x[i]) * x[i];
  return std::sqrt(s / double(to - from));
}

int expectedLatency(int factor) {
  FairyTapes::HappyEnding::Engine e;
  e.prepare(kSr, kBlock, factor);
  return e.latencySamples();
}

PID dispClip(int led) { return kDispClip0 + static_cast<PID>(led); }

void test_input_peaks_map_to_display_norm_and_only_display_ids_are_written() {
  Host h; CHECK(h.open());
  h.blocks(40, constant(0.5f, 0.125f));   // -6.02 dBFS left, -18.06 dBFS right
  CHECK(std::fabs(h.last(kDispInL) - normFromPeakDb(-6.0206)) < 0.01);
  CHECK(std::fabs(h.last(kDispInR) - normFromPeakDb(-18.0618)) < 0.01);
  CHECK(h.last(kDispOutL) > 0.0);
  CHECK(h.last(dispClip(kLedIn)) == 0.0 && h.last(dispClip(kLedOut)) == 0.0);
  std::set<PID> ids;
  for (const auto& kv : h.published) { CHECK(kv.first >= kDisplayBase && kv.first < kNumDisplayIDs); ids.insert(kv.first); }
  // every read-only display parameter is written; the two writable triggers only when acknowledging
  for (PID id = kDisplayBase; id < kClearClips; ++id) CHECK(ids.count(id) == 1);
  for (PID id = kDispGlueIn; id < kNumDisplayIDs; ++id) CHECK(ids.count(id) == 1);
  CHECK(h.published.count(kClearClips) == 0 && h.published.count(kResetIntegrated) == 0);
  CHECK(std::fabs(h.last(kDispLatency) - normFromLatency(expectedLatency(2))) < 1e-9);
  h.close();
}

// The six values for the screen are written with the others at the 33 ms pace (about 30 times a second),
// normalized as in display.h, in the values the engine reports.
void test_stage_display_values_are_published_at_the_display_pace() {
  Host h; CHECK(h.open());
  MockChanges setup;   // GLUE threshold 0, no VIBE colour: a -6 dBFS sine reads -6 dB at every stage input
  setup.set(kGlueThresh, normFromIndex(kGlueThresh, 20));
  setup.set(kTube, normFromIndex(kTube, 0));
  setup.set(kLimitThresh, normFromIndex(kLimitThresh, 0));
  setup.set(kClipDrive, normFromIndex(kClipDrive, 0));
  setup.set(kCeiling, normFromIndex(kCeiling, 0));   // -0.1 dB: the LIMIT moves the level by 0.1 dB only
  h.block(sine(1000.0, 0.5), &setup);
  h.blocks(1000, sine(1000.0, 0.5));   // 1.33 s
  const PID ids[6] = {kDispGlueIn, kDispGlueGr, kDispLimitIn, kDispLimitThresh, kDispClipIn, kDispFinalIn};
  for (PID id : ids) {
    const size_t writes = h.published[id].size();
    CHECK(writes >= 38 && writes <= 42);          // 1.33 s / 33 ms, the same pace as the peaks
    CHECK(writes == h.published[kDispInL].size());
  }
  for (PID id : {kDispGlueIn, kDispLimitIn, kDispClipIn, kDispFinalIn})
    CHECK(std::fabs(h.last(id) - normFromStageDb(-6.02)) < 0.5 / 72.0);
  CHECK(h.last(kDispGlueGr) >= 0.0 && h.last(kDispGlueGr) < 0.1);
  CHECK(std::fabs(h.last(kDispLimitThresh) - normFromThreshDb(0.0)) < 1e-6);   // THRESH at 0 dB

  // LOUDNESS lowers the threshold, SET pulls it to -8 dB; CLIP off reads 0 at its input
  MockChanges more;
  more.set(kLoudness, normFromIndex(kLoudness, 10));   // 5.0
  more.set(kClipOn, 0.0);
  h.block(sine(1000.0, 0.5), &more);
  h.blocks(400, sine(1000.0, 0.5));
  const double lowered = -0.5 * -FairyTapes::HappyEnding::Tuning::kLoudCurve.limitDb;
  CHECK(std::fabs(h.last(kDispLimitThresh) - normFromThreshDb(lowered)) < 0.002);
  CHECK(h.last(kDispClipIn) == 0.0);
  CHECK(h.last(kDispGlueIn) > normFromStageDb(-7.0));
  MockChanges set;
  set.set(kSetActive, 1.0);
  h.block(sine(1000.0, 0.5), &set);
  h.blocks(400, sine(1000.0, 0.5));
  CHECK(std::fabs(h.last(kDispLimitThresh) - normFromThreshDb(-8.0)) < 0.002);
  h.close();
}

void test_display_hold_keeps_transient_between_writes() {
  // A one-block burst between two display writes is still seen: the write after it carries the held peak.
  Host h; CHECK(h.open());
  h.blocks(60, kSilence);
  const size_t written = h.published[kDispInL].size();
  double maxSeen = 0.0;
  h.block(constant(0.5f, 0.5f));    // burst in one block (may or may not be a write block)
  for (int i = 0; i < 40; ++i) { h.block(kSilence); }
  for (size_t k = written; k < h.published[kDispInL].size(); ++k) maxSeen = std::max(maxSeen, h.published[kDispInL][k]);
  CHECK(maxSeen > normFromPeakDb(-6.5));
  h.close();
}

void test_clip_is_held_then_cleared_on_request_with_ack() {
  Host h; CHECK(h.open());
  h.block(constant(1.0f, 0.f));   // exactly 1.0 clips the input
  h.blocks(100, kSilence);        // 0.13 s of silence: still held (2 s)
  CHECK(h.last(dispClip(kLedIn)) == 1.0);

  MockChanges keep;               // a value below 0.5 is not a request
  keep.set(kClearClips, 0.0);
  h.block(kSilence, &keep);
  CHECK(h.lastBlock.count(kClearClips) == 0);
  h.blocks(20, kSilence);
  CHECK(h.last(dispClip(kLedIn)) == 1.0);

  MockChanges clear;
  clear.set(kClearClips, 1.0);
  h.block(kSilence, &clear);
  CHECK(h.lastBlock.count(kClearClips) == 1 && h.lastBlock[kClearClips] == 0.0);   // acknowledged in the same block
  h.blocks(20, kSilence);
  CHECK(h.last(dispClip(kLedIn)) == 0.0 && h.last(dispClip(kLedOut)) == 0.0);
  CHECK(h.published[kClearClips].size() == 1);   // acknowledged once
  h.close();
}

// -20 dBFS 1 kHz on both channels for `seconds`: the LUFS read it after 3 s.
double lufsAfterSine(Host& h, double seconds) {
  const int n = static_cast<int>(seconds * kSr / kBlock);
  h.blocks(n, [](long i, int) { return static_cast<float>(0.1 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / kSr)); });
  return lufsFromNorm(h.last(kDispLufsShort));
}

void test_lufs_published_and_integrated_reset_acknowledged() {
  Host h; CHECK(h.open());
  const double lufs = lufsAfterSine(h, 5.0);
  CHECK(lufs > -30.0 && lufs < -10.0);
  CHECK(h.last(kDispLufsInt) > 0.0);
  CHECK(h.last(kDispLufsShort) > 0.0);

  MockChanges reset;
  reset.set(kResetIntegrated, 1.0);
  h.block(kSilence, &reset);
  CHECK(h.lastBlock.count(kResetIntegrated) == 1 && h.lastBlock[kResetIntegrated] == 0.0);
  h.blocks(5, kSilence);
  CHECK(h.last(kDispLufsInt) == 0.0);                 // no measurement again
  CHECK(h.last(kDispLufsShort) > 0.0);                // short-term is not touched
  CHECK(h.published[kResetIntegrated].size() == 1);
  h.close();
}

void test_mono_bus_lufs_is_corrected_by_the_dual_mono_offset() {
  Host st; CHECK(st.open(false));
  Host mo; CHECK(mo.open(true));
  const double ls = lufsAfterSine(st, 5.0);
  const double lm = lufsAfterSine(mo, 5.0);
  CHECK(std::fabs((ls - lm) - 3.0103) < 0.05);        // the mono channel reads 3.01 LU lower than the pair
  // the audio of the mono bus equals the left channel of the stereo bus fed the same signal
  st.close(); mo.close();
}

void test_mono_and_in_place_audio_match_the_stereo_chain() {
  Host a, b, c;
  CHECK(a.open(false, false)); CHECK(b.open(false, true)); CHECK(c.open(true, false));
  a.record = b.record = c.record = true;
  const Gen g = [](long n, int) { return static_cast<float>(0.4 * std::sin(2.0 * kPi * 440.0 * static_cast<double>(n) / kSr)); };
  a.blocks(300, g); b.blocks(300, g); c.blocks(300, g);
  CHECK(a.recL.size() == b.recL.size() && a.recL.size() == c.recL.size());
  CHECK(std::memcmp(a.recL.data(), b.recL.data(), sizeof(float) * a.recL.size()) == 0);   // in place == separate buffers
  CHECK(std::memcmp(a.recL.data(), c.recL.data(), sizeof(float) * a.recL.size()) == 0);   // mono == dual-mono stereo
  CHECK(rms(a.recL, a.recL.size() - 4800, a.recL.size()) > 0.1);
  a.close(); b.close(); c.close();
}

void test_bypass_is_the_input_delayed_by_the_reported_latency() {
  for (const bool mono : {false, true}) {
    Host h; CHECK(h.open(mono));
    const long lat = h.latency();
    CHECK(lat == expectedLatency(2) && lat > 0);
    MockChanges byp;
    byp.set(kBypass, 1.0);
    h.record = true;
    const Gen g = [](long n, int ch) {
      return static_cast<float>(0.5 * std::sin(2.0 * kPi * (ch ? 700.0 : 1000.0) * static_cast<double>(n) / kSr + ch));
    };
    h.block(g, &byp);
    h.blocks(40, g);
    bool exact = true;
    for (long n = lat + kBlock; n < static_cast<long>(h.recL.size()); ++n)
      if (h.recL[static_cast<size_t>(n)] != g(n - lat, 0)) { exact = false; break; }
    CHECK(exact);
    h.close();
  }
}

void test_latency_follows_oversampling_at_the_next_activation() {
  Host h; CHECK(h.open());
  CHECK(h.latency() == static_cast<uint32>(expectedLatency(2)));
  CHECK(h.proc->getTailSamples() == h.latency());

  MockChanges to4;
  to4.set(kOversampling, normFromIndex(kOversampling, 2));
  h.block(kSilence, &to4);
  h.blocks(10, kSilence);
  CHECK(h.latency() == static_cast<uint32>(expectedLatency(2)));   // never rebuilt on the audio thread
  CHECK(std::fabs(h.last(kDispLatency) - normFromLatency(expectedLatency(2))) < 1e-9);

  h.reactivate();                                                  // the host restarts the component
  CHECK(h.latency() == static_cast<uint32>(expectedLatency(4)));
  h.published.clear();
  h.blocks(10, kSilence);
  CHECK(std::fabs(h.last(kDispLatency) - normFromLatency(expectedLatency(4))) < 1e-9);

  MockChanges toOff;
  toOff.set(kOversampling, 0.0);
  h.block(kSilence, &toOff);
  h.reactivate();
  CHECK(h.latency() == static_cast<uint32>(expectedLatency(1)));
  CHECK(expectedLatency(1) < expectedLatency(2) && expectedLatency(2) < expectedLatency(4));
  h.close();
}

void test_set_state_with_other_oversampling_applies_at_the_next_activation() {
  Host h; CHECK(h.open());
  CHECK(h.latency() == static_cast<uint32>(expectedLatency(2)));
  double norm[kNumParams];
  loadDefaults(norm);
  norm[kOversampling] = normFromIndex(kOversampling, 2);   // 4x
  VecStream s;
  serializeState(norm, s.data);
  h.blocks(5, kSilence);
  CHECK(h.comp->setState(&s) == kResultOk);
  h.blocks(5, kSilence);                                  // still running: the engine is not rebuilt
  CHECK(h.latency() == static_cast<uint32>(expectedLatency(2)));
  h.reactivate();                                         // the host restarts it after the controller's request
  CHECK(h.latency() == static_cast<uint32>(expectedLatency(4)));
  h.published.clear();
  h.blocks(10, kSilence);
  CHECK(std::fabs(h.last(kDispLatency) - normFromLatency(expectedLatency(4))) < 1e-9);
  h.close();
}

void test_state_round_trip_and_session_parameters_not_stored() {
  Host h; CHECK(h.open());
  MockChanges set;
  set.set(kCeiling, normFromIndex(kCeiling, 3));
  set.set(kLoudness, normFromIndex(kLoudness, 12));
  set.set(kOversampling, normFromIndex(kOversampling, 2));
  set.set(kListenBand, normFromListen(0));       // session: must not reach the state
  set.set(kSetActive, 1.0);
  h.block(kSilence, &set);

  VecStream s;
  CHECK(h.comp->getState(&s) == kResultOk);
  CHECK(s.data.size() == kStateBytes);
  double norm[kNumParams];
  CHECK(deserializeState(s.data.data(), s.data.size(), norm));
  CHECK(indexFromNorm(kCeiling, norm[kCeiling]) == 3);
  CHECK(indexFromNorm(kLoudness, norm[kLoudness]) == 12);
  CHECK(indexFromNorm(kOversampling, norm[kOversampling]) == 2);

  // into a fresh instance
  Host g; CHECK(g.open());
  s.pos = 0;
  CHECK(g.comp->setState(&s) == kResultOk);
  VecStream s2;
  CHECK(g.comp->getState(&s2) == kResultOk);
  CHECK(s2.data == s.data);

  // refuses an unknown version, tolerates a truncated blob (the missing parameters keep their defaults)
  VecStream bad; bad.data = s.data; bad.data[0] = 9;
  CHECK(g.comp->setState(&bad) == kResultFalse);
  VecStream cut; cut.data.assign(s.data.begin(), s.data.begin() + 4 + 8 * 3);  // bypass, input, output only
  CHECK(g.comp->setState(&cut) == kResultOk);
  VecStream s3;
  CHECK(g.comp->getState(&s3) == kResultOk);
  double after[kNumParams], defaults[kNumParams];
  CHECK(deserializeState(s3.data.data(), s3.data.size(), after));
  loadDefaults(defaults);
  CHECK(after[kLoudness] == defaults[kLoudness] && after[kOversampling] == defaults[kOversampling]);
  g.close(); h.close();
}

void test_listen_is_off_after_set_state() {
  // LISTEN solos the bell (a 5 kHz sine is far from the default LO MID): the output collapses; after a
  // setState the session parameter is off again and the sine passes.
  Host h; CHECK(h.open());
  const Gen g = sine(5000.0, 0.25);
  h.record = true;
  h.blocks(200, g);
  const double base = rms(h.recL, h.recL.size() - 2400, h.recL.size());
  CHECK(base > 0.1);

  MockChanges listen;
  listen.set(kListenBand, normFromListen(0));
  h.block(g, &listen);
  h.blocks(200, g);
  const double soloed = rms(h.recL, h.recL.size() - 2400, h.recL.size());
  CHECK(soloed < base * 0.2);

  VecStream defaults;
  { Host d; CHECK(d.open()); CHECK(d.comp->getState(&defaults) == kResultOk); d.close(); }
  CHECK(h.comp->setState(&defaults) == kResultOk);
  h.blocks(200, g);
  const double back = rms(h.recL, h.recL.size() - 2400, h.recL.size());
  CHECK(back > base * 0.7);
  h.close();
}

void test_no_allocation_while_processing() {
  Host h; CHECK(h.open(false, false, 512));
  const Gen g = sine(1000.0, 0.3);
  h.blocks(20, g);
  int total = 0;
  MockChanges many;
  many.set(kInputGain, normFromIndex(kInputGain, 30));
  many.set(kClearClips, 1.0);
  many.set(kResetIntegrated, 1.0);
  many.set(kListenBand, normFromListen(1));
  many.set(kBypass, 0.0);
  for (int i = 0; i < 40; ++i) {
    for (int k = 0; k < 512; ++k) { h.inL[k] = g(h.pos + k, 0); h.inR[k] = g(h.pos + k, 1); }
    float* ib[2] = {h.inL.data(), h.inR.data()};
    float* ob[2] = {h.outL.data(), h.outR.data()};
    AudioBusBuffers in{}, out{};
    in.numChannels = 2; in.channelBuffers32 = ib; out.numChannels = 2; out.channelBuffers32 = ob;
    h.outChanges.reset();
    ProcessData d{};
    d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = 512;
    d.numInputs = 1; d.numOutputs = 1; d.inputs = &in; d.outputs = &out;
    d.inputParameterChanges = (i % 8 == 0) ? &many : nullptr; d.outputParameterChanges = &h.outChanges; d.processContext = &h.ctx;
    g_allocs = 0;
    g_countAllocs = true;
    h.proc->process(d);
    g_countAllocs = false;
    total += g_allocs.load();
    h.pos += 512;
  }
  CHECK(total == 0);
  h.close();
}

} // namespace

int main() {
  struct stat st{};
  if (stat(HAPPY_ENDING_MODULE, &st) != 0) {
    std::printf("module not built (%s): skipped\n", HAPPY_ENDING_MODULE);
    return kSkip;
  }
  test_input_peaks_map_to_display_norm_and_only_display_ids_are_written();
  test_stage_display_values_are_published_at_the_display_pace();
  test_display_hold_keeps_transient_between_writes();
  test_clip_is_held_then_cleared_on_request_with_ack();
  test_lufs_published_and_integrated_reset_acknowledged();
  test_mono_bus_lufs_is_corrected_by_the_dual_mono_offset();
  test_mono_and_in_place_audio_match_the_stereo_chain();
  test_bypass_is_the_input_delayed_by_the_reported_latency();
  test_latency_follows_oversampling_at_the_next_activation();
  test_set_state_with_other_oversampling_applies_at_the_next_activation();
  test_state_round_trip_and_session_parameters_not_stored();
  test_listen_is_off_after_set_state();
  test_no_allocation_while_processing();
  std::printf("happy_ending_processor_level_test: %s\n", g_failed ? "FAILED" : "all passed");
  return g_failed ? 1 : 0;
}
