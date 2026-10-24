/**
 * Fairy Tapes · Deep Throat — Module/factory lifecycle smoke test.
 * dlopens the built module from dist/ and drives it the way the SDK hosting code
 * does: ModuleEntry, GetPluginFactory (owned reference), class info, createInstance,
 * release of every reference, ModuleExit. Exits 77 (skipped) when the module is absent.
 * The controller is initialized and queried for its 25 automatable parameters plus the hidden display ones and
 * typed values are parsed back; the processor is set up, activated and run (latency, bus
 * layouts, in-place stereo processing, mono -> mono processing compared with the engine's mono path,
 * mono bypass delay). The editor view is created and released 50 times without attaching it to a window (no display
 * server needed): size, size constraint, content scale and IPlugView reference counting.
 */

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "engine.h"
#include "params.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef DEEP_THROAT_MODULE
#error DEEP_THROAT_MODULE must point to the built module binary
#endif

// The pluginterfaces library does not define the VST interface IIDs (vstinitiids.cpp lives in sdk).
namespace Steinberg::Vst {
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
}
namespace Steinberg {
DEF_CLASS_IID(IPlugViewContentScaleSupport)
}

namespace {
constexpr int kSkip = 77;
int g_failed = 0;

#define CHECK(expr) do { \
  if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } \
} while (0)

using namespace FairyTapes::DeepThroat;

void toStr128(const char* s, Steinberg::Vst::String128 out) {
  int i = 0;
  for (; s[i] && i < 127; ++i) out[i] = (Steinberg::Vst::TChar)s[i];
  out[i] = 0;
}

bool parsed(Steinberg::Vst::IEditController* ctl, Steinberg::Vst::ParamID id, const char* text,
            double expected, double tol) {
  Steinberg::Vst::String128 s;
  toStr128(text, s);
  Steinberg::Vst::ParamValue v = -1.0;
  if (ctl->getParamValueByString(id, s, v) != Steinberg::kResultOk) return false;
  return std::fabs(v - expected) <= tol;
}

void exerciseController(Steinberg::Vst::IEditController* ctl) {
  CHECK(parsed(ctl, kInput, "+6.0 dB", 0.75, 1e-6));
  CHECK(parsed(ctl, kOutput, "-12 dB", 0.0, 1e-6));
  CHECK(parsed(ctl, kRiderTarget, "-25.0 dBFS", 0.25, 1e-6));
  CHECK(parsed(ctl, kDeEssFreq, "10000 Hz", 1.0, 1e-6));
  CHECK(parsed(ctl, kBreathAmount, "50 %", 0.5, 1e-6));
  CHECK(parsed(ctl, kEqAmount, "250", 1.0, 1e-6));  // clamped
  CHECK(parsed(ctl, kBypass, "On", 1.0, 0.0));
  CHECK(parsed(ctl, kCompIn, "off", 0.0, 0.0));
  CHECK(parsed(ctl, kMode, "Puke", 1.0, 1e-6));
  CHECK(parsed(ctl, kCompRatio, "AUTO", 0.0, 1e-6));
  Steinberg::Vst::String128 s;
  toStr128("loud", s);
  Steinberg::Vst::ParamValue v = 0.0;
  CHECK(ctl->getParamValueByString(kInput, s, v) != Steinberg::kResultOk);
  // Every parameter: the displayed string parses back to (about) the same value.
  for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) {
    for (double n : {0.0, 0.25, 1.0}) {
      Steinberg::Vst::String128 str;
      CHECK(ctl->getParamStringByValue(id, n, str) == Steinberg::kResultOk);
      Steinberg::Vst::ParamValue back = -1.0;
      CHECK(ctl->getParamValueByString(id, str, back) == Steinberg::kResultOk);
      // Stepped parameters land on the step the SDK displays (floor of n * (steps + 1));
      // continuous ones come back within the display rounding.
      Steinberg::Vst::ParameterInfo info{};
      CHECK(ctl->getParameterInfo((Steinberg::int32)id, info) == Steinberg::kResultOk);
      const int steps = info.stepCount;
      const double expected =
          steps > 0 ? std::min<double>(steps, std::floor(n * (steps + 1))) / steps : n;
      const double tol = steps > 0 ? 1e-9 : 0.006;
      if (!(std::fabs(back - expected) <= tol)) {
        std::fprintf(stderr, "  param %u: %.3f -> %.3f\n", (unsigned)id, n, back);
        ++g_failed;
      }
    }
  }
}

// One parameter change (one point at offset 0) for ProcessData::inputParameterChanges.
class OneQueue : public Steinberg::Vst::IParamValueQueue {
public:
  OneQueue(Steinberg::Vst::ParamID id, double v) : id_(id), v_(v) {}
  Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
  Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }
  Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& offset,
                                         Steinberg::Vst::ParamValue& value) override {
    if (index != 0) return Steinberg::kResultFalse;
    offset = 0;
    value = v_;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32, Steinberg::Vst::ParamValue,
                                         Steinberg::int32&) override { return Steinberg::kResultFalse; }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
    return Steinberg::kNoInterface;
  }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }

private:
  Steinberg::Vst::ParamID id_;
  double v_;
};

class OneChange : public Steinberg::Vst::IParameterChanges {
public:
  explicit OneChange(OneQueue* q) : q_(q) {}
  Steinberg::int32 PLUGIN_API getParameterCount() override { return 1; }
  Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 i) override {
    return i == 0 ? q_ : nullptr;
  }
  Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID&,
                                                                Steinberg::int32&) override {
    return nullptr;
  }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
    return Steinberg::kNoInterface;
  }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }

private:
  OneQueue* q_;
};

float testSignal(long t) {  // 220 Hz sine plus a little 3.3 kHz, deterministic
  return (float)(0.25 * std::sin(2.0 * 3.141592653589793 * 220.0 * t / 48000.0) +
                 0.05 * std::sin(2.0 * 3.141592653589793 * 3300.0 * t / 48000.0));
}

// Mono -> mono is a supported layout: the processor must produce exactly the engine's mono
// result (null right channel), in place and out of place, and the bypass delay must equal the
// reported latency.
void exerciseMono(Steinberg::Vst::IComponent* comp, Steinberg::Vst::IAudioProcessor* proc) {
  using namespace Steinberg;
  using namespace Steinberg::Vst;
  SpeakerArrangement mo = SpeakerArr::kMono;
  CHECK(proc->setBusArrangements(&mo, 1, &mo, 1) == kResultOk);
  ProcessSetup setup{kRealtime, kSample32, 512, 48000.0};
  CHECK(proc->setupProcessing(setup) == kResultOk);
  constexpr int kBlock = 512, kBlocks = 30;

  for (int inPlace = 0; inPlace < 2; ++inPlace) {
    CHECK(comp->setActive(true) == kResultOk);
    CHECK((int32)proc->getLatencySamples() == 4864);
    double defaults[kNumParams];
    loadDefaults(defaults);
    Engine ref;
    ref.prepare(48000.0);
    static float inBuf[kBlock], outBuf[kBlock], refIn[kBlock], refOut[kBlock];
    float* inChans[1] = {inBuf};
    float* outChans[1] = {inPlace ? inBuf : outBuf};
    AudioBusBuffers in{}, out{};
    in.numChannels = out.numChannels = 1;
    in.channelBuffers32 = inChans;
    out.channelBuffers32 = outChans;
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kBlock;
    data.numInputs = data.numOutputs = 1;
    data.inputs = &in;
    data.outputs = &out;
    long mismatch = 0, bad = 0;
    double late = 0.0;
    long t = 0;
    for (int b = 0; b < kBlocks; ++b) {
      for (int i = 0; i < kBlock; ++i, ++t) inBuf[i] = refIn[i] = testSignal(t);
      CHECK(proc->process(data) == kResultOk);
      ref.setSettings(mapParams(defaults));
      ref.process(refIn, nullptr, refOut, nullptr, kBlock);
      const float* got = outChans[0];
      for (int i = 0; i < kBlock; ++i) {
        if (got[i] != refOut[i]) ++mismatch;
        if (!std::isfinite(got[i])) ++bad;
        if (b >= 15) late = std::max(late, (double)std::fabs(got[i]));
      }
    }
    CHECK(mismatch == 0);
    CHECK(bad == 0);
    CHECK(late > 0.05 && late < 2.0);
    CHECK(comp->setActive(false) == kResultOk);
  }

  // Bypass (sent as a parameter change): output = input delayed by the reported latency.
  CHECK(comp->setActive(true) == kResultOk);
  const long L = (long)proc->getLatencySamples();
  OneQueue q(kBypass, 1.0);
  OneChange changes(&q);
  static float buf[kBlock];
  static float hist[kBlock * kBlocks];
  float* chans[1] = {buf};
  AudioBusBuffers io{};
  io.numChannels = 1;
  io.channelBuffers32 = chans;
  ProcessData data{};
  data.processMode = kRealtime;
  data.symbolicSampleSize = kSample32;
  data.numSamples = kBlock;
  data.numInputs = data.numOutputs = 1;
  data.inputs = data.outputs = &io;
  data.inputParameterChanges = &changes;
  long mismatch = 0;
  long t = 0;
  for (int b = 0; b < kBlocks; ++b) {
    for (int i = 0; i < kBlock; ++i) buf[i] = hist[t + i] = testSignal(t + i);
    CHECK(proc->process(data) == kResultOk);
    for (int i = 0; i < kBlock; ++i, ++t)
      if (buf[i] != (t >= L ? hist[t - L] : 0.0f)) ++mismatch;
  }
  CHECK(mismatch == 0);
  CHECK(comp->setActive(false) == kResultOk);
  SpeakerArrangement st = SpeakerArr::kStereo;
  CHECK(proc->setBusArrangements(&st, 1, &st, 1) == kResultOk);
}

// Collects the processor's output parameter changes: per block, the points written for each ID.
class OutQueue : public Steinberg::Vst::IParamValueQueue {
public:
  Steinberg::Vst::ParamID id = 0;
  double last = -1.0;
  int points = 0;
  Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id; }
  Steinberg::int32 PLUGIN_API getPointCount() override { return points; }
  Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32, Steinberg::int32&,
                                         Steinberg::Vst::ParamValue& v) override {
    v = last;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32, Steinberg::Vst::ParamValue v,
                                         Steinberg::int32& index) override {
    last = v;
    index = points++;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
    return Steinberg::kNoInterface;
  }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

class OutChanges : public Steinberg::Vst::IParameterChanges {
public:
  OutQueue queues[32];
  int count = 0;
  void clear() { count = 0; for (auto& q : queues) { q.points = 0; q.last = -1.0; } }
  const OutQueue* find(Steinberg::Vst::ParamID id) const {
    for (int i = 0; i < count; ++i) if (queues[i].id == id) return &queues[i];
    return nullptr;
  }
  Steinberg::int32 PLUGIN_API getParameterCount() override { return count; }
  Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 i) override {
    return i >= 0 && i < count ? &queues[i] : nullptr;
  }
  Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id,
                                                                Steinberg::int32& index) override {
    for (int i = 0; i < count; ++i) if (queues[i].id == id) { index = i; return &queues[i]; }
    if (count >= 32) return nullptr;
    queues[count].id = id;
    index = count;
    return &queues[count++];
  }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
    return Steinberg::kNoInterface;
  }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

void exerciseProcessor(Steinberg::FUnknown* obj) {
  using namespace Steinberg;
  using namespace Steinberg::Vst;
  IComponent* comp = nullptr;
  IAudioProcessor* proc = nullptr;
  CHECK(obj->queryInterface(IComponent::iid, (void**)&comp) == kResultOk);
  CHECK(obj->queryInterface(IAudioProcessor::iid, (void**)&proc) == kResultOk);
  if (!comp || !proc) {
    if (comp) comp->release();
    if (proc) proc->release();
    return;
  }
  CHECK(comp->initialize(nullptr) == kResultOk);

  SpeakerArrangement st = SpeakerArr::kStereo, mo = SpeakerArr::kMono;
  CHECK(proc->setBusArrangements(&mo, 1, &mo, 1) == kResultOk);
  CHECK(proc->setBusArrangements(&mo, 1, &st, 1) != kResultOk);
  CHECK(proc->setBusArrangements(&st, 1, &mo, 1) != kResultOk);
  CHECK(proc->setBusArrangements(&st, 1, &st, 1) == kResultOk);

  const double rates[2] = {48000.0, 96000.0};
  const int32 latency[2] = {4864, 9728};
  for (int k = 0; k < 2; ++k) {
    ProcessSetup setup{kRealtime, kSample32, 512, rates[k]};
    CHECK(proc->setupProcessing(setup) == kResultOk);
    CHECK(comp->setActive(true) == kResultOk);
    CHECK((int32)proc->getLatencySamples() == latency[k]);
    proc->setProcessing(true);  // AudioEffect: kNotImplemented, harmless

    // In-place stereo processing of a sine: finite, and it comes out after the latency.
    static float bufL[512], bufR[512];
    float* chans[2] = {bufL, bufR};
    AudioBusBuffers in{}, out{};
    in.numChannels = out.numChannels = 2;
    in.channelBuffers32 = out.channelBuffers32 = chans;
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = 512;
    data.numInputs = data.numOutputs = 1;
    data.inputs = &in;
    data.outputs = &out;
    OutChanges outChanges;
    data.outputParameterChanges = &outChanges;
    long bad = 0;
    double late = 0.0;
    long t = 0;
    int dispWrites = 0, badNorm = 0;
    double inPeakNorm = 0.0;
    for (int b = 0; b < 60; ++b) {
      for (int i = 0; i < 512; ++i, ++t)
        bufL[i] = bufR[i] = (float)(0.25 * std::sin(2.0 * 3.141592653589793 * 220.0 * t / rates[k]));
      outChanges.clear();
      CHECK(proc->process(data) == kResultOk);
      if (const OutQueue* q = outChanges.find(kDispInL)) {
        ++dispWrites;
        inPeakNorm = q->last;
        CHECK(q->points == 1);
        for (Steinberg::Vst::ParamID id = kMeterBase; id < kNumDisplayIDs; ++id) {
          const OutQueue* o = outChanges.find(id);
          if (!o || !(o->last >= 0.0 && o->last <= 1.0)) ++badNorm;
        }
      }
      for (int i = 0; i < 512; ++i) {
        if (!std::isfinite(bufL[i]) || !std::isfinite(bufR[i])) ++bad;
        if (b >= 40) late = std::max(late, (double)std::fabs(bufL[i]));
      }
    }
    CHECK(bad == 0);
    CHECK(late > 0.05 && late < 2.0);
    // Display values: at most one write per 16 ms of audio, at block boundaries.
    CHECK(dispWrites > 0 && dispWrites < 60);
    const int blocksPerWrite = (int)std::ceil(std::floor(rates[k] * 0.016) / 512.0);
    CHECK(std::abs(dispWrites - 60 / blocksPerWrite) <= 1);  // block granularity: every ceil(16 ms / 512) blocks
    CHECK(badNorm == 0);
    CHECK(std::fabs(inPeakNorm - (20.0 * std::log10(0.25) + 60.0) / 66.0) < 0.02);  // -12 dB peak
    {  // kClearClips = 1 from the editor is acknowledged with 0
      OneQueue clr(kClearClips, 1.0);
      struct In : IParameterChanges {
        IParamValueQueue* q;
        int32 PLUGIN_API getParameterCount() override { return 1; }
        IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return i == 0 ? q : nullptr; }
        IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID&, int32&) override { return nullptr; }
        tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
        uint32 PLUGIN_API addRef() override { return 1; }
        uint32 PLUGIN_API release() override { return 1; }
      } inChanges;
      inChanges.q = &clr;
      data.inputParameterChanges = &inChanges;
      outChanges.clear();
      CHECK(proc->process(data) == kResultOk);
      data.inputParameterChanges = nullptr;
      const OutQueue* ack = outChanges.find(kClearClips);
      CHECK(ack && ack->last == 0.0);
    }
    data.outputParameterChanges = nullptr;
    proc->setProcessing(false);
    CHECK(comp->setActive(false) == kResultOk);
  }
  exerciseMono(comp, proc);
  CHECK(comp->terminate() == kResultOk);
  proc->release();
  comp->release();
}

using EntryFn = bool (*)(void*);
using ExitFn = bool (*)();
using FactoryFn = Steinberg::IPluginFactory* (*)();

// createView: the editor for ViewType::kEditor (1000 x 614 logical, scaled by the content scale factor), nullptr for
// any other name; created and released 50 times, each view freed by the host's release (count back to 0).
void test_create_view_returns_editor(Steinberg::Vst::IEditController* ctl) {
  CHECK(ctl->createView("no-such-view") == nullptr);
  CHECK(ctl->createView(nullptr) == nullptr);
  for (int i = 0; i < 50; ++i) {
    Steinberg::IPlugView* view = ctl->createView(Steinberg::Vst::ViewType::kEditor);
    CHECK(view != nullptr);
    if (!view) break;
    Steinberg::ViewRect r{};
    CHECK(view->getSize(&r) == Steinberg::kResultOk);
    CHECK(r.getWidth() == 1000 && r.getHeight() == 614);
    Steinberg::ViewRect asked(0, 0, 900, 300);
    CHECK(view->checkSizeConstraint(&asked) == Steinberg::kResultTrue);
    CHECK(asked.getWidth() == 1000 && asked.getHeight() == 614);
    CHECK(view->canResize() == Steinberg::kResultFalse);
#if !defined(__APPLE__)
    CHECK(view->isPlatformTypeSupported(Steinberg::kPlatformTypeX11EmbedWindowID) == Steinberg::kResultTrue);
    CHECK(view->isPlatformTypeSupported(Steinberg::kPlatformTypeWaylandSurfaceID) != Steinberg::kResultTrue);
#endif
    if (i == 0) {  // content scale before attach: getSize and the size constraint follow it
      Steinberg::IPlugViewContentScaleSupport* scale = nullptr;
      CHECK(view->queryInterface(Steinberg::IPlugViewContentScaleSupport::iid, (void**)&scale) == Steinberg::kResultOk);
      if (scale) {
        CHECK(scale->setContentScaleFactor(2.0f) == Steinberg::kResultTrue);
        Steinberg::ViewRect s{};
        CHECK(view->getSize(&s) == Steinberg::kResultOk);
        CHECK(s.getWidth() == 2000 && s.getHeight() == 1228);
        Steinberg::ViewRect c(0, 0, 100, 100);
        CHECK(view->checkSizeConstraint(&c) == Steinberg::kResultTrue);
        CHECK(c.getWidth() == 2000 && c.getHeight() == 1228);
        CHECK(scale->setContentScaleFactor(-1.0f) == Steinberg::kResultTrue);  // sanitized to 1
        CHECK(view->getSize(&s) == Steinberg::kResultOk);
        CHECK(s.getWidth() == 1000 && s.getHeight() == 614);
        CHECK(scale->release() == 1);  // the view keeps the host's reference
      }
    }
    CHECK(view->release() == 0);  // the host owned the only reference: the view is gone
  }
}

void exerciseFactory(FactoryFn getFactory) {
  Steinberg::IPluginFactory* f = getFactory();
  CHECK(f != nullptr);
  if (!f) return;
  Steinberg::PFactoryInfo fi{};
  CHECK(f->getFactoryInfo(&fi) == Steinberg::kResultOk);
  CHECK(std::strcmp(fi.vendor, "Sweetspot Tools") == 0);
  CHECK(std::strcmp(fi.url, "https://github.com/sweetspot-tools/fairy-tapes") == 0);
  CHECK(f->countClasses() == 2);
  for (Steinberg::int32 i = 0; i < f->countClasses(); ++i) {
    Steinberg::PClassInfo ci{};
    CHECK(f->getClassInfo(i, &ci) == Steinberg::kResultOk);
    CHECK(std::strcmp(ci.name, "Deep Throat") == 0);
    if (std::strcmp(ci.category, "Audio Module Class") == 0) {
      Steinberg::FUnknown* obj = nullptr;
      CHECK(f->createInstance(ci.cid, Steinberg::FUnknown::iid, (void**)&obj) == Steinberg::kResultOk);
      CHECK(obj != nullptr);
      if (obj) {
        exerciseProcessor(obj);
        obj->release();
      }
    } else if (std::strcmp(ci.category, "Component Controller Class") == 0) {
      Steinberg::FUnknown* obj = nullptr;
      CHECK(f->createInstance(ci.cid, Steinberg::FUnknown::iid, (void**)&obj) == Steinberg::kResultOk);
      CHECK(obj != nullptr);
      if (obj) {
        Steinberg::Vst::IEditController* ctl = nullptr;
        CHECK(obj->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&ctl) == Steinberg::kResultOk);
        if (ctl) {
          CHECK(ctl->initialize(nullptr) == Steinberg::kResultOk);
          // 25 automatable parameters + the hidden display ones (kDisp* and kClearClips).
          int automatable = 0;
          for (Steinberg::int32 i = 0; i < ctl->getParameterCount(); ++i) {
            Steinberg::Vst::ParameterInfo pi{};
            if (ctl->getParameterInfo(i, pi) == Steinberg::kResultOk &&
                (pi.flags & Steinberg::Vst::ParameterInfo::kCanAutomate))
              ++automatable;
          }
          CHECK(automatable == 25);
          CHECK(ctl->getParameterCount() == 25 + (int)kNumDisplayIDs - (int)kMeterBase + 1);
          const auto hidden = Steinberg::Vst::ParameterInfo::kIsReadOnly | Steinberg::Vst::ParameterInfo::kIsHidden;
          for (Steinberg::Vst::ParamID id = kMeterBase; id < kNumDisplayIDs; ++id) {
            Steinberg::Vst::ParameterInfo pi{};
            bool found = false;
            for (Steinberg::int32 i = 0; i < ctl->getParameterCount(); ++i)
              if (ctl->getParameterInfo(i, pi) == Steinberg::kResultOk && pi.id == id) { found = true; break; }
            CHECK(found);
            CHECK((pi.flags & hidden) == hidden);
            CHECK(!(pi.flags & Steinberg::Vst::ParameterInfo::kCanAutomate));
          }
          {  // the clear-clips trigger is hidden and writable
            Steinberg::Vst::ParameterInfo pi{};
            bool found = false;
            for (Steinberg::int32 i = 0; i < ctl->getParameterCount(); ++i)
              if (ctl->getParameterInfo(i, pi) == Steinberg::kResultOk && pi.id == kClearClips) { found = true; break; }
            CHECK(found);
            CHECK((pi.flags & Steinberg::Vst::ParameterInfo::kIsHidden) != 0);
            CHECK((pi.flags & Steinberg::Vst::ParameterInfo::kIsReadOnly) == 0);
          }
          exerciseController(ctl);
          test_create_view_returns_editor(ctl);
          CHECK(ctl->terminate() == Steinberg::kResultOk);
          ctl->release();
        }
        obj->release();
      }
    }
  }
  f->release();  // the host owns the returned reference
}
} // namespace

int main() {
  struct stat st{};
  if (stat(DEEP_THROAT_MODULE, &st) != 0) {
    std::printf("SKIP: module not built: %s\n", DEEP_THROAT_MODULE);
    return kSkip;
  }
  void* h = dlopen(DEEP_THROAT_MODULE, RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
    return 1;
  }
  auto entry = (EntryFn)dlsym(h, "ModuleEntry");
  auto exitFn = (ExitFn)dlsym(h, "ModuleExit");
  auto getFactory = (FactoryFn)dlsym(h, "GetPluginFactory");
  CHECK(entry && exitFn && getFactory);
  if (entry && exitFn && getFactory) {
    CHECK(entry(h));
    exerciseFactory(getFactory);
    exerciseFactory(getFactory);  // the host may ask again after releasing
    CHECK(exitFn());
  }
  dlclose(h);
  std::printf("%s\n", g_failed ? "Factory lifecycle FAILED" : "Factory lifecycle OK");
  return g_failed ? 1 : 0;
}
