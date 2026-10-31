/**
 * Fairy Tapes · Happy Ending — Module/factory lifecycle smoke test.
 * dlopens the built module from dist/ and drives it the way the SDK hosting code does: ModuleEntry,
 * GetPluginFactory (owned reference), class info, createInstance, release of every reference, ModuleExit.
 * Checks the registered classes (a processor and a controller, "Fx|Mastering"), the parameter table the
 * controller registers (ids, steps, defaults, automation, hidden session and display parameters), the
 * bus layouts the processor accepts, and that there is no custom editor (the host shows its own UI).
 * Exits 77 (skipped) when the module is absent.
 */

#include "params.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef HAPPY_ENDING_MODULE
#error HAPPY_ENDING_MODULE must point to the built module binary
#endif

using namespace FairyTapes::HappyEnding;
using Steinberg::Vst::ParameterInfo;

namespace {
constexpr int kSkip = 77;
int g_failed = 0;

#define CHECK(expr) do { \
  if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } \
} while (0)

using EntryFn = bool (*)(void*);
using ExitFn = bool (*)();
using FactoryFn = Steinberg::IPluginFactory* (*)();

void exerciseController(Steinberg::FUnknown* obj) {
  Steinberg::Vst::IEditController* ctl = nullptr;
  CHECK(obj->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&ctl) == Steinberg::kResultOk);
  if (!ctl) return;
  CHECK(ctl->initialize(nullptr) == Steinberg::kResultOk);
  // no custom editor yet: the host builds its generic UI from the parameters
  CHECK(ctl->createView(Steinberg::Vst::ViewType::kEditor) == nullptr);
  CHECK(ctl->createView("no-such-view") == nullptr);

  const int expected = static_cast<int>(kNumParams) + 2 + static_cast<int>(kNumDisplayIDs - kDisplayBase);
  CHECK(ctl->getParameterCount() == expected);
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) {
    ParameterInfo info{};
    CHECK(ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
    const auto id = static_cast<ParamID>(i);
    CHECK(info.id == static_cast<Steinberg::Vst::ParamID>(i));
    CHECK(info.stepCount == kParams[id].steps - 1);
    CHECK(std::fabs(info.defaultNormalizedValue - normFromIndex(id, kParams[id].defaultIndex)) < 1e-12);
    CHECK(((info.flags & ParameterInfo::kCanAutomate) != 0) == isAutomatable(id));
    CHECK((info.flags & (ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly)) == 0);
    CHECK(((info.flags & ParameterInfo::kIsBypass) != 0) == (id == kBypass));
  }
  // The rest, by id: hidden session parameters and the display ones
  int session = 0, display = 0, writable = 0;
  for (int i = static_cast<int>(kNumParams); i < ctl->getParameterCount(); ++i) {
    ParameterInfo info{};
    CHECK(ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
    CHECK((info.flags & ParameterInfo::kIsHidden) != 0);
    CHECK((info.flags & ParameterInfo::kCanAutomate) == 0);
    if (info.id == kListenBand || info.id == kSetActive) {
      ++session;
      CHECK((info.flags & ParameterInfo::kIsReadOnly) == 0);
      CHECK(info.defaultNormalizedValue == 0.0);
    } else {
      CHECK(info.id >= kDisplayBase && info.id < kNumDisplayIDs);
      ++display;
      if (info.id == kClearClips || info.id == kResetIntegrated) {
        ++writable;
        CHECK((info.flags & ParameterInfo::kIsReadOnly) == 0);
      } else {
        CHECK((info.flags & ParameterInfo::kIsReadOnly) != 0);
      }
    }
  }
  CHECK(session == 2 && display == static_cast<int>(kNumDisplayIDs - kDisplayBase) && writable == 2);

  // The six display values for the screen: present, hidden and read-only, outside automation and the
  // stored table; the number of automatable parameters is what it was (every stored one but OVERSAMPLING).
  for (const Steinberg::Vst::ParamID id : {static_cast<Steinberg::Vst::ParamID>(kDispGlueIn), static_cast<Steinberg::Vst::ParamID>(kDispGlueGr),
                                           static_cast<Steinberg::Vst::ParamID>(kDispLimitIn), static_cast<Steinberg::Vst::ParamID>(kDispLimitThresh),
                                           static_cast<Steinberg::Vst::ParamID>(kDispClipIn), static_cast<Steinberg::Vst::ParamID>(kDispFinalIn)}) {
    ParameterInfo info{};
    bool found = false;
    for (int i = 0; i < ctl->getParameterCount() && !found; ++i) {
      CHECK(ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
      found = info.id == id;
    }
    CHECK(found);
    CHECK(id > kResetIntegrated && id < kNumDisplayIDs);
    CHECK((info.flags & (ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden)) == (ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden));
    CHECK((info.flags & ParameterInfo::kCanAutomate) == 0);
    CHECK(info.defaultNormalizedValue == 0.0);
  }
  int automatable = 0;
  for (int i = 0; i < ctl->getParameterCount(); ++i) {
    ParameterInfo info{};
    CHECK(ctl->getParameterInfo(i, info) == Steinberg::kResultOk);
    if (info.flags & ParameterInfo::kCanAutomate) ++automatable;
  }
  CHECK(automatable == static_cast<int>(kNumParams) - 1);
  CHECK(ctl->terminate() == Steinberg::kResultOk);
  ctl->release();
}

void exerciseProcessor(Steinberg::FUnknown* obj) {
  Steinberg::Vst::IComponent* comp = nullptr;
  Steinberg::Vst::IAudioProcessor* proc = nullptr;
  CHECK(obj->queryInterface(Steinberg::Vst::IComponent::iid, (void**)&comp) == Steinberg::kResultOk);
  CHECK(obj->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&proc) == Steinberg::kResultOk);
  if (comp && proc) {
    using namespace Steinberg::Vst;
    CHECK(comp->initialize(nullptr) == Steinberg::kResultOk);
    CHECK(comp->getBusCount(kAudio, kInput) == 1 && comp->getBusCount(kAudio, kOutput) == 1);
    CHECK(comp->getBusCount(kEvent, kInput) == 0);
    CHECK(proc->canProcessSampleSize(kSample32) == Steinberg::kResultTrue);
    CHECK(proc->canProcessSampleSize(kSample64) != Steinberg::kResultTrue);
    SpeakerArrangement st = SpeakerArr::kStereo, mo = SpeakerArr::kMono, both[2] = {st, st};
    CHECK(proc->setBusArrangements(&st, 1, &st, 1) == Steinberg::kResultTrue);
    CHECK(proc->setBusArrangements(&mo, 1, &mo, 1) == Steinberg::kResultTrue);
    CHECK(proc->setBusArrangements(&st, 1, &mo, 1) != Steinberg::kResultTrue);   // mixed layouts
    CHECK(proc->setBusArrangements(&mo, 1, &st, 1) != Steinberg::kResultTrue);
    CHECK(proc->setBusArrangements(both, 2, &st, 1) != Steinberg::kResultTrue);  // no sidechain
    SpeakerArrangement surround = SpeakerArr::k51;
    CHECK(proc->setBusArrangements(&surround, 1, &surround, 1) != Steinberg::kResultTrue);
    CHECK(comp->terminate() == Steinberg::kResultOk);
  }
  if (proc) proc->release();
  if (comp) comp->release();
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
  Steinberg::IPluginFactory2* f2 = nullptr;
  CHECK(f->queryInterface(Steinberg::IPluginFactory2::iid, (void**)&f2) == Steinberg::kResultOk);
  int audio = 0, controllers = 0;
  for (Steinberg::int32 i = 0; i < f->countClasses(); ++i) {
    Steinberg::PClassInfo ci{};
    CHECK(f->getClassInfo(i, &ci) == Steinberg::kResultOk);
    CHECK(std::strcmp(ci.name, "Happy Ending") == 0);
    if (f2) {
      Steinberg::PClassInfo2 c2{};
      CHECK(f2->getClassInfo2(i, &c2) == Steinberg::kResultOk);
      if (std::strcmp(ci.category, "Audio Module Class") == 0) CHECK(std::strcmp(c2.subCategories, "Fx|Mastering") == 0);
      CHECK(std::strcmp(c2.version, "1.0.0") == 0);
    }
    Steinberg::FUnknown* obj = nullptr;
    CHECK(f->createInstance(ci.cid, Steinberg::FUnknown::iid, (void**)&obj) == Steinberg::kResultOk);
    CHECK(obj != nullptr);
    if (!obj) continue;
    if (std::strcmp(ci.category, "Audio Module Class") == 0) {
      ++audio;
      exerciseProcessor(obj);
    } else if (std::strcmp(ci.category, "Component Controller Class") == 0) {
      ++controllers;
      exerciseController(obj);
    }
    obj->release();
  }
  CHECK(audio == 1 && controllers == 1);
  if (f2) f2->release();
  f->release();  // the host owns the returned reference
}
} // namespace

int main() {
  struct stat st{};
  if (stat(HAPPY_ENDING_MODULE, &st) != 0) {
    std::printf("SKIP: module not built: %s\n", HAPPY_ENDING_MODULE);
    return kSkip;
  }
  void* h = dlopen(HAPPY_ENDING_MODULE, RTLD_NOW | RTLD_LOCAL);
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
