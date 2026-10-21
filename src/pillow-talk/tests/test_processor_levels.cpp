/**
 * Fairy Tapes · Pillow Talk — LEVEL display parameters and the clip clear request, through the real module.
 * dlopens the built bundle, creates the processor from the factory and drives process() with mock host parameter
 * queues: input peaks map to the meter norm over -60..0 dB, a sample at 1.0 lights the input clip flag (held), and a
 * kClearClips = 1 request clears the flags and is acknowledged with kClearClips = 0 in the same block.
 * Exits 77 (skipped) when the module has not been built.
 */

#include "cids.h"
#include "params.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef PILLOW_TALK_MODULE
#error PILLOW_TALK_MODULE must point to the built module binary
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace FairyTapes::PillowTalk;
using PID = Steinberg::Vst::ParamID;   // (FairyTapes::PillowTalk has an enum of the same name)

namespace {
constexpr int kSkip = 77;
constexpr int kBlock = 64;
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
  std::map<PID, std::vector<double>> published;   // every value written per display id
  std::map<PID, double> lastBlock;                // values written by the latest block only

  bool open() {
    h = dlopen(PILLOW_TALK_MODULE, RTLD_NOW | RTLD_LOCAL);
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
    ProcessSetup setup{kRealtime, kSample32, kBlock, 48000.0};
    proc->setupProcessing(setup);
    comp->setActive(true);
    ctx.sampleRate = 48000.0;
    inL.assign(kBlock, 0.f); inR.assign(kBlock, 0.f); outL.assign(kBlock, 0.f); outR.assign(kBlock, 0.f);
    f->release();
    return true;
  }

  // One block with constant amplitude (alternating sign) on the left and `ampR` on the right.
  void block(float ampL, float ampR, MockChanges* inChanges = nullptr) {
    for (int i = 0; i < kBlock; ++i) {
      inL[i] = (i & 1) ? -ampL : ampL;
      inR[i] = (i & 1) ? -ampR : ampR;
    }
    float* ib[2] = {inL.data(), inR.data()};
    float* ob[2] = {outL.data(), outR.data()};
    AudioBusBuffers in{}, out{};
    in.numChannels = 2; in.channelBuffers32 = ib; out.numChannels = 2; out.channelBuffers32 = ob;
    MockChanges outChanges;
    ProcessData d{};
    d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = kBlock;
    d.numInputs = 1; d.numOutputs = 1; d.inputs = &in; d.outputs = &out;
    d.inputParameterChanges = inChanges; d.outputParameterChanges = &outChanges; d.processContext = &ctx;
    proc->process(d);
    lastBlock.clear();
    for (auto& q : outChanges.qs) {
      ParamValue v = 0; int32 off = 0;
      if (q->getPoint(q->getPointCount() - 1, off, v) == kResultOk) {
        published[q->id].push_back(v);
        lastBlock[q->id] = v;
      }
    }
  }
  void blocks(int n, float ampL, float ampR) { for (int i = 0; i < n; ++i) block(ampL, ampR); }
  double last(PID id) const {
    auto it = published.find(id);
    return (it == published.end() || it->second.empty()) ? -1.0 : it->second.back();
  }
  void close() {
    if (proc) proc->release();
    if (comp) { comp->setActive(false); comp->terminate(); comp->release(); }
    auto ex = (bool (*)())dlsym(h, "ModuleExit"); if (ex) ex();
    dlclose(h);
  }
};

// -60..0 dB -> 0..1 (fairy_ui/knob_math.h meterNormFromDb).
double norm(double db) { return (db + 60.0) / 60.0; }

void test_input_peaks_map_to_meter_norm_and_only_display_ids_are_written() {
  Host h; CHECK(h.open());
  h.blocks(40, 0.5f, 0.125f);   // -6.02 dBFS left, -18.06 dBFS right
  CHECK(std::fabs(h.last(kDispInL) - norm(-6.02)) < 0.01);
  CHECK(std::fabs(h.last(kDispInR) - norm(-18.06)) < 0.01);
  CHECK(h.last(kDispOutL) > 0.0);
  CHECK(h.last(kDispClipIn) == 0.0 && h.last(kDispClipOut) == 0.0);
  for (const auto& kv : h.published) CHECK(kv.first >= kMeterBase && kv.first < kNumDisplayIDs);
  CHECK(h.published.count(kClearClips) == 0);   // no request, no acknowledge
  h.close();
}

void test_clip_is_held_then_cleared_on_request_with_ack() {
  Host h; CHECK(h.open());
  h.block(1.0f, 0.f);           // exactly 1.0 clips the input
  h.blocks(100, 0.f, 0.f);      // 0.13 s of silence: still held (2 s)
  CHECK(h.last(kDispClipIn) == 1.0);

  MockChanges keep;             // a value below 0.5 is not a request
  keep.set(kClearClips, 0.0);
  h.block(0.f, 0.f, &keep);
  CHECK(h.lastBlock.count(kClearClips) == 0);
  h.blocks(20, 0.f, 0.f);
  CHECK(h.last(kDispClipIn) == 1.0);

  MockChanges clear;
  clear.set(kClearClips, 1.0);
  h.block(0.f, 0.f, &clear);
  CHECK(h.lastBlock.count(kClearClips) == 1 && h.lastBlock[kClearClips] == 0.0);   // acknowledged in the same block
  h.blocks(20, 0.f, 0.f);
  CHECK(h.last(kDispClipIn) == 0.0 && h.last(kDispClipOut) == 0.0);
  CHECK(h.published[kClearClips].size() == 1);   // acknowledged once
  h.close();
}

} // namespace

int main() {
  struct stat st{};
  if (stat(PILLOW_TALK_MODULE, &st) != 0) {
    std::printf("module not built (%s): skipped\n", PILLOW_TALK_MODULE);
    return kSkip;
  }
  test_input_peaks_map_to_meter_norm_and_only_display_ids_are_written();
  test_clip_is_held_then_cleared_on_request_with_ack();
  std::printf("pillow_talk_processor_level_test: %s\n", g_failed ? "FAILED" : "all passed");
  return g_failed ? 1 : 0;
}
