/**
 * Fairy Tapes · Hotline — process() through the real module.
 * dlopens the built bundle, creates the component from the factory and drives process() with hand-built ProcessData
 * and mock host parameter queues: clean line (Morph 0), bypass, the clip clear request and its acknowledgement, the
 * display parameters, degenerate blocks, mono bus handling, tail/latency, silence when not prepared, and zero heap
 * allocations inside process(). Exits 77 (skipped) when the module has not been built.
 */

#include "cids.h"
#include "params.h"
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
#include <map>
#include <memory>
#include <new>
#include <vector>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef HOTLINE_MODULE
#error HOTLINE_MODULE must point to the built module binary
#endif

// Counts heap allocations while g_countAllocs is set. The executable exports it (ENABLE_EXPORTS) so the dlopen'ed module binds to it.
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
using namespace FairyTapes::Hotline;
using PID = Steinberg::Vst::ParamID;

namespace {
constexpr int kSkip = 77;
constexpr int kBlock = 256;
constexpr double kFs = 48000.0;
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
  tresult PLUGIN_API getPoint(int32 i, int32& off, ParamValue& v) override {
    if (i < 0 || i >= (int32)pts.size()) return kResultFalse;
    off = pts[i].first; v = pts[i].second; return kResultOk;
  }
  tresult PLUGIN_API addPoint(int32 off, ParamValue v, int32& idx) override {
    pts.emplace_back(off, v); idx = (int32)pts.size() - 1; return kResultOk;
  }
};
struct MockChanges : IParameterChanges {
  std::vector<std::unique_ptr<MockQueue>> qs;
  tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  int32 PLUGIN_API getParameterCount() override { return (int32)qs.size(); }
  IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return (i >= 0 && i < (int32)qs.size()) ? qs[i].get() : nullptr; }
  IParamValueQueue* PLUGIN_API addParameterData(const PID& id, int32& index) override {
    qs.push_back(std::make_unique<MockQueue>()); qs.back()->id = id; index = (int32)qs.size() - 1; return qs.back().get();
  }
  void set(PID id, double v) { int32 i; addParameterData(id, i); int32 p; qs.back()->addPoint(0, v, p); }
};

struct Host {
  void* h = nullptr;
  IComponent* comp = nullptr;
  IAudioProcessor* proc = nullptr;
  ProcessContext ctx{};
  std::vector<float> inL, inR, outL, outR;
  std::map<PID, std::vector<double>> published;   // every value written, per id
  std::map<PID, std::vector<double>> lastBlock;   // values written by the latest block only
  int sampleCounter = 0;

  // activate = false leaves the component initialised and set up but not active.
  bool open(bool activate = true) {
    h = dlopen(HOTLINE_MODULE, RTLD_NOW | RTLD_LOCAL);
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
    SpeakerArrangement st = SpeakerArr::kStereo;
    if (proc->setBusArrangements(&st, 1, &st, 1) != kResultTrue) return false;
    ProcessSetup setup{kRealtime, kSample32, 512, kFs};
    if (proc->setupProcessing(setup) != kResultOk) return false;
    if (activate) {
      if (comp->setActive(true) != kResultOk) return false;
      proc->setProcessing(true);   // the SDK base returns kNotImplemented: the call is what matters
    }
    ctx.sampleRate = kFs;
    inL.assign(kBlock, 0.f); inR.assign(kBlock, 0.f); outL.assign(kBlock, 0.f); outR.assign(kBlock, 0.f);
    f->release();
    return true;
  }

  void fillSine() {
    for (int i = 0; i < kBlock; ++i, ++sampleCounter) {
      inL[i] = 0.3f * (float)std::sin(2 * M_PI * 440.0 * sampleCounter / kFs);
      inR[i] = 0.2f * (float)std::sin(2 * M_PI * 660.0 * sampleCounter / kFs);
    }
  }

  void collect(MockChanges& oc) {
    lastBlock.clear();
    for (auto& q : oc.qs) {
      ParamValue v = 0; int32 off = 0;
      for (int32 k = 0; k < q->getPointCount(); ++k)
        if (q->getPoint(k, off, v) == kResultOk) { published[q->id].push_back(v); lastBlock[q->id].push_back(v); }
    }
  }

  // Stereo block from inL/inR into outL/outR.
  tresult block(MockChanges* inChanges = nullptr, int numSamples = kBlock) {
    float* ib[2] = {inL.data(), inR.data()};
    float* ob[2] = {outL.data(), outR.data()};
    AudioBusBuffers in{}, out{};
    in.numChannels = 2; in.channelBuffers32 = ib; out.numChannels = 2; out.channelBuffers32 = ob;
    MockChanges outChanges;
    ProcessData d{};
    d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = numSamples;
    d.numInputs = 1; d.numOutputs = 1; d.inputs = &in; d.outputs = &out;
    d.inputParameterChanges = inChanges; d.outputParameterChanges = &outChanges; d.processContext = &ctx;
    tresult r = proc->process(d);
    collect(outChanges);
    return r;
  }

  double last(PID id) const {
    auto it = published.find(id);
    return (it == published.end() || it->second.empty()) ? -1.0 : it->second.back();
  }
  void close() {
    if (proc) { proc->setProcessing(false); proc->release(); }
    if (comp) { comp->setActive(false); comp->terminate(); comp->release(); }
    auto ex = (bool (*)())dlsym(h, "ModuleExit"); if (ex) ex();
    dlclose(h);
  }
};

double maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
  double m = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) m = std::fmax(m, std::fabs((double)a[i] - b[i]));
  return m;
}

void test_clean_line_at_morph_zero() {
  Host h; CHECK(h.open());
  MockChanges c; c.set(kMorph, 0.0);
  h.fillSine(); h.block(&c);
  for (int i = 0; i < 40; ++i) { h.fillSine(); h.block(); }   // settle
  CHECK(maxDiff(h.outL, h.inL) < 1e-6 && maxDiff(h.outR, h.inR) < 1e-6);
  h.close();
}

void test_bypass_is_bit_identical_after_ramp() {
  Host h; CHECK(h.open());
  MockChanges c; c.set(kBypass, 1.0);
  h.fillSine(); h.block(&c);   // 5 ms ramp = 240 samples, inside this block
  h.fillSine(); h.block();
  CHECK(h.outL == h.inL && h.outR == h.inR);
  h.close();
}

void test_clip_clear_is_acknowledged_and_display_is_published() {
  Host h; CHECK(h.open());
  for (int i = 0; i < 100; ++i) { h.fillSine(); h.block(); }
  for (PID id = kMeterBase; id <= kClipOut; ++id) {
    CHECK(h.published.count(id) == 1 && !h.published[id].empty());
    for (double v : h.published[id]) CHECK(std::isfinite(v) && v >= 0.0 && v <= 1.0);
  }
  CHECK(h.published.count(kClipClear) == 0);   // no request, no acknowledge
  MockChanges c; c.set(kClipClear, 1.0);
  bool acked = false;
  h.fillSine(); h.block(&c);
  for (int i = 0; i < 4 && !acked; ++i) {
    if (h.lastBlock.count(kClipClear)) acked = (h.lastBlock[kClipClear].front() == 0.0);
    if (!acked) { h.fillSine(); h.block(); }
  }
  CHECK(acked);
  size_t acks = h.published[kClipClear].size();
  for (int i = 0; i < 20; ++i) { h.fillSine(); h.block(); }
  CHECK(h.published[kClipClear].size() == acks);   // acknowledged once
  h.close();
}

void test_degenerate_blocks_do_not_crash() {
  Host h; CHECK(h.open());
  CHECK(h.block(nullptr, 0) == kResultOk);
  AudioBusBuffers in{}, out{};
  float* ib[2] = {h.inL.data(), h.inR.data()};
  in.numChannels = 2; in.channelBuffers32 = ib;
  out.numChannels = 2; out.channelBuffers32 = nullptr;   // null channel array
  ProcessData d{};
  d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = kBlock;
  d.numInputs = 1; d.numOutputs = 1; d.inputs = &in; d.outputs = &out; d.processContext = &h.ctx;
  CHECK(h.proc->process(d) == kResultOk);
  float* nb[2] = {nullptr, nullptr};   // null channel pointers
  out.channelBuffers32 = nb;
  CHECK(h.proc->process(d) == kResultOk);
  d.numInputs = 0; d.inputs = nullptr;   // no input bus
  CHECK(h.proc->process(d) == kResultOk);
  h.close();
}

void test_mono_bus() {
  Host h; CHECK(h.open());
  SpeakerArrangement mono = SpeakerArr::kMono, st = SpeakerArr::kStereo;
  CHECK(h.proc->setBusArrangements(&mono, 1, &st, 1) != kResultTrue);   // a mono input bus is rejected
  CHECK(h.proc->setBusArrangements(&mono, 1, &mono, 1) != kResultTrue);
  // A host that delivers one channel anyway must not crash and must get a finite, bounded output.
  float* ib[1] = {h.inL.data()};
  float* ob[2] = {h.outL.data(), h.outR.data()};
  AudioBusBuffers in{}, out{};
  in.numChannels = 1; in.channelBuffers32 = ib; out.numChannels = 2; out.channelBuffers32 = ob;
  ProcessData d{};
  d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = kBlock;
  d.numInputs = 1; d.numOutputs = 1; d.inputs = &in; d.outputs = &out; d.processContext = &h.ctx;
  for (int i = 0; i < 10; ++i) { h.fillSine(); CHECK(h.proc->process(d) == kResultOk); }
  bool ok = true;
  for (int i = 0; i < kBlock; ++i) ok = ok && std::isfinite(h.outL[i]) && std::isfinite(h.outR[i]) && std::fabs(h.outL[i]) < 4.f;
  CHECK(ok);
  h.close();
}

void test_tail_and_latency() {
  Host h; CHECK(h.open());
  CHECK(h.proc->getTailSamples() > 0);
  CHECK(h.proc->getLatencySamples() == 0);
  h.close();
}

void test_no_allocation_in_process() {
  Host h; CHECK(h.open());
  MockChanges first; first.set(kMorph, 0.7);
  h.fillSine(); h.block(&first);   // first block may touch lazily-built state
  g_allocs = 0;
  for (int i = 0; i < 50; ++i) {
    MockChanges* none = nullptr;
    h.fillSine();
    // The mock containers themselves allocate: count only around the process() call.
    float* ib[2] = {h.inL.data(), h.inR.data()};
    float* ob[2] = {h.outL.data(), h.outR.data()};
    AudioBusBuffers in{}, out{};
    in.numChannels = 2; in.channelBuffers32 = ib; out.numChannels = 2; out.channelBuffers32 = ob;
    ProcessData d{};
    d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = kBlock;
    d.numInputs = 1; d.numOutputs = 1; d.inputs = &in; d.outputs = &out; d.processContext = &h.ctx;
    MockChanges clear;
    if (i == 20) clear.set(kClipClear, 1.0);   // built outside the counted region
    MockChanges outChanges;
    outChanges.qs.reserve(16);
    (void)none;
    for (int k = 0; k < 16; ++k) { outChanges.qs.push_back(std::make_unique<MockQueue>()); outChanges.qs.back()->pts.reserve(4); }
    // The mock addParameterData must not allocate: hand out pre-built queues.
    struct PreBuilt : IParameterChanges {
      std::vector<std::unique_ptr<MockQueue>>* pool; int used = 0;
      tresult PLUGIN_API queryInterface(const TUID, void** o) override { *o = nullptr; return kNoInterface; }
      uint32 PLUGIN_API addRef() override { return 1; }
      uint32 PLUGIN_API release() override { return 1; }
      int32 PLUGIN_API getParameterCount() override { return used; }
      IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return i < used ? (*pool)[i].get() : nullptr; }
      IParamValueQueue* PLUGIN_API addParameterData(const PID& id, int32& index) override {
        if (used >= (int)pool->size()) return nullptr;
        (*pool)[used]->id = id; (*pool)[used]->pts.clear(); index = used; return (*pool)[used++].get();
      }
    } pre;
    pre.pool = &outChanges.qs;
    d.inputParameterChanges = (i == 20) ? &clear : nullptr;
    d.outputParameterChanges = &pre;
    g_countAllocs = true;
    h.proc->process(d);
    g_countAllocs = false;
  }
  CHECK(g_allocs == 0);
  h.close();
}

void test_silence_when_not_prepared() {
  Host h; CHECK(h.open(false));   // set up, never activated
  std::fill(h.outL.begin(), h.outL.end(), 0.7f);
  std::fill(h.outR.begin(), h.outR.end(), -0.7f);
  h.fillSine(); h.block();
  bool zero = true;
  for (int i = 0; i < kBlock; ++i) zero = zero && h.outL[i] == 0.f && h.outR[i] == 0.f;
  CHECK(zero);

  // Active, then deactivated: stale contents must not survive either.
  h.comp->setActive(true); h.proc->setProcessing(true);
  h.fillSine(); h.block();
  h.proc->setProcessing(false); h.comp->setActive(false);
  std::fill(h.outL.begin(), h.outL.end(), 0.7f);
  std::fill(h.outR.begin(), h.outR.end(), -0.7f);
  h.fillSine(); h.block();
  zero = true;
  for (int i = 0; i < kBlock; ++i) zero = zero && h.outL[i] == 0.f && h.outR[i] == 0.f;
  CHECK(zero);

  // Prepared but the input bus is missing: output is zeroed too.
  h.comp->setActive(true); h.proc->setProcessing(true);
  std::fill(h.outL.begin(), h.outL.end(), 0.7f);
  std::fill(h.outR.begin(), h.outR.end(), -0.7f);
  float* ob[2] = {h.outL.data(), h.outR.data()};
  AudioBusBuffers out{};
  out.numChannels = 2; out.channelBuffers32 = ob;
  ProcessData d{};
  d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = kBlock;
  d.numInputs = 0; d.inputs = nullptr; d.numOutputs = 1; d.outputs = &out; d.processContext = &h.ctx;
  CHECK(h.proc->process(d) == kResultOk);
  zero = true;
  for (int i = 0; i < kBlock; ++i) zero = zero && h.outL[i] == 0.f && h.outR[i] == 0.f;
  CHECK(zero);
  h.close();
}

} // namespace

int main() {
  struct stat st{};
  if (stat(HOTLINE_MODULE, &st) != 0) {
    std::printf("module not built (%s): skipped\n", HOTLINE_MODULE);
    return kSkip;
  }
  test_clean_line_at_morph_zero();
  test_bypass_is_bit_identical_after_ramp();
  test_clip_clear_is_acknowledged_and_display_is_published();
  test_degenerate_blocks_do_not_crash();
  test_mono_bus();
  test_tail_and_latency();
  test_no_allocation_in_process();
  test_silence_when_not_prepared();
  std::printf("hotline_processor_test: %s (8 tests)\n", g_failed ? "FAILED" : "all passed");
  return g_failed ? 1 : 0;
}
