/**
 * Fairy Tapes · Deep Throat — Module/factory lifecycle smoke test.
 * dlopens the built module from dist/ and drives it the way the SDK hosting code
 * does: ModuleEntry, GetPluginFactory (owned reference), class info, createInstance,
 * release of every reference, ModuleExit. Exits 77 (skipped) when the module is absent.
 * The controller is initialized and queried for its 25 parameters (no editor view yet) and
 * typed values are parsed back; the processor is set up, activated and run (latency, bus
 * layouts, in-place stereo processing).
 */

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
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
    long bad = 0;
    double late = 0.0;
    long t = 0;
    for (int b = 0; b < 60; ++b) {
      for (int i = 0; i < 512; ++i, ++t)
        bufL[i] = bufR[i] = (float)(0.25 * std::sin(2.0 * 3.141592653589793 * 220.0 * t / rates[k]));
      CHECK(proc->process(data) == kResultOk);
      for (int i = 0; i < 512; ++i) {
        if (!std::isfinite(bufL[i]) || !std::isfinite(bufR[i])) ++bad;
        if (b >= 40) late = std::max(late, (double)std::fabs(bufL[i]));
      }
    }
    CHECK(bad == 0);
    CHECK(late > 0.05 && late < 2.0);
    proc->setProcessing(false);
    CHECK(comp->setActive(false) == kResultOk);
  }
  CHECK(comp->terminate() == kResultOk);
  proc->release();
  comp->release();
}

using EntryFn = bool (*)(void*);
using ExitFn = bool (*)();
using FactoryFn = Steinberg::IPluginFactory* (*)();

void exerciseFactory(FactoryFn getFactory) {
  Steinberg::IPluginFactory* f = getFactory();
  CHECK(f != nullptr);
  if (!f) return;
  Steinberg::PFactoryInfo fi{};
  CHECK(f->getFactoryInfo(&fi) == Steinberg::kResultOk);
  CHECK(std::strcmp(fi.vendor, "Fairy Tapes") == 0);
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
          CHECK(ctl->getParameterCount() == 25);
          exerciseController(ctl);
          CHECK(ctl->createView(Steinberg::Vst::ViewType::kEditor) == nullptr);  // no editor yet
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
