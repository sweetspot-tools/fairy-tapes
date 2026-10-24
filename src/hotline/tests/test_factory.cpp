/**
 * Fairy Tapes · Hotline — Module/factory lifecycle smoke test.
 * dlopens the built module from dist/ and drives it the way the SDK hosting code does: ModuleEntry,
 * GetPluginFactory (owned reference), class info (FUIDs of cids.h), createInstance of both classes, release of every
 * reference, ModuleExit. The editor is not part of this task: createView must return nullptr for now.
 * Exits 77 (skipped) when the module is absent.
 */

#include "cids.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef HOTLINE_MODULE
#error HOTLINE_MODULE must point to the built module binary
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

using EntryFn = bool (*)(void*);
using ExitFn = bool (*)();
using FactoryFn = Steinberg::IPluginFactory* (*)();

void exerciseController(Steinberg::FUnknown* obj) {
  Steinberg::Vst::IEditController* ctl = nullptr;
  CHECK(obj->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&ctl) == Steinberg::kResultOk);
  if (!ctl) return;
  CHECK(ctl->initialize(nullptr) == Steinberg::kResultOk);
  CHECK(ctl->getParameterCount() == 11 + 8);
  Steinberg::Vst::ParameterInfo info{};
  CHECK(ctl->getParameterInfo(1, info) == Steinberg::kResultOk);   // Device
  CHECK(info.stepCount == 5);
  Steinberg::Vst::String128 str{};
  CHECK(ctl->getParamStringByValue(1, 0.0, str) == Steinberg::kResultOk);
  CHECK(str[0] == 'M' && str[1] == 'e');   // "Megaphone", first device
  CHECK(ctl->getParamStringByValue(3, 0.5, str) == Steinberg::kResultOk);
  CHECK(str[0] == '0' && str[1] == '.' && str[2] == '0');
  Steinberg::IPlugView* view = ctl->createView(Steinberg::Vst::ViewType::kEditor);
  CHECK(view != nullptr);   // the editor (Task 7), 1000 x 470 at scale 1
  if (view) {
    Steinberg::ViewRect size{};
    CHECK(view->getSize(&size) == Steinberg::kResultOk && size.getWidth() == 1000 && size.getHeight() == 470);
    view->release();
  }
  CHECK(ctl->createView("not an editor") == nullptr);
  CHECK(ctl->terminate() == Steinberg::kResultOk);
  ctl->release();
}

void exerciseProcessor(Steinberg::FUnknown* obj) {
  Steinberg::Vst::IComponent* comp = nullptr;
  CHECK(obj->queryInterface(Steinberg::Vst::IComponent::iid, (void**)&comp) == Steinberg::kResultOk);
  Steinberg::Vst::IAudioProcessor* proc = nullptr;
  CHECK(obj->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&proc) == Steinberg::kResultOk);
  if (comp && proc) {
    CHECK(comp->initialize(nullptr) == Steinberg::kResultOk);
    CHECK(comp->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput) == 1);
    CHECK(comp->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput) == 1);
    Steinberg::Vst::SpeakerArrangement st = Steinberg::Vst::SpeakerArr::kStereo;
    Steinberg::Vst::SpeakerArrangement mono = Steinberg::Vst::SpeakerArr::kMono;
    CHECK(proc->setBusArrangements(&st, 1, &st, 1) == Steinberg::kResultTrue);
    CHECK(proc->setBusArrangements(&mono, 1, &st, 1) == Steinberg::kResultFalse);
    CHECK(proc->canProcessSampleSize(Steinberg::Vst::kSample32) == Steinberg::kResultTrue);
    CHECK(proc->canProcessSampleSize(Steinberg::Vst::kSample64) == Steinberg::kResultFalse);
    CHECK(proc->getLatencySamples() == 0);
    CHECK(comp->terminate() == Steinberg::kResultOk);
  }
  if (comp) comp->release();
  if (proc) proc->release();
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
  int audio = 0, controller = 0;
  for (Steinberg::int32 i = 0; i < f->countClasses(); ++i) {
    Steinberg::PClassInfo ci{};
    CHECK(f->getClassInfo(i, &ci) == Steinberg::kResultOk);
    CHECK(std::strcmp(ci.name, "Hotline") == 0);
    Steinberg::FUID cid = Steinberg::FUID::fromTUID(ci.cid);
    Steinberg::FUnknown* obj = nullptr;
    if (std::strcmp(ci.category, kVstAudioEffectClass) == 0) {
      ++audio;
      CHECK(cid == FairyTapes::Hotline::kProcessorUID);
      CHECK(f->createInstance(ci.cid, Steinberg::FUnknown::iid, (void**)&obj) == Steinberg::kResultOk);
      CHECK(obj != nullptr);
      if (obj) { exerciseProcessor(obj); obj->release(); }
    } else if (std::strcmp(ci.category, kVstComponentControllerClass) == 0) {
      ++controller;
      CHECK(cid == FairyTapes::Hotline::kControllerUID);
      CHECK(f->createInstance(ci.cid, Steinberg::FUnknown::iid, (void**)&obj) == Steinberg::kResultOk);
      CHECK(obj != nullptr);
      if (obj) { exerciseController(obj); obj->release(); }
    }
  }
  CHECK(audio == 1 && controller == 1);
  f->release();  // the host owns the returned reference
}
} // namespace

int main() {
  struct stat st{};
  if (stat(HOTLINE_MODULE, &st) != 0) {
    std::printf("SKIP: module not built: %s\n", HOTLINE_MODULE);
    return kSkip;
  }
  void* h = dlopen(HOTLINE_MODULE, RTLD_NOW | RTLD_LOCAL);
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
