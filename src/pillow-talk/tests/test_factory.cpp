/**
 * Fairy Tapes · Pillow Talk — Module/factory lifecycle smoke test.
 * dlopens the built module from dist/ and drives it the way the SDK hosting code
 * does: ModuleEntry, GetPluginFactory (owned reference), class info, createInstance,
 * release of every reference, ModuleExit. Exits 77 (skipped) when the module is absent.
 * The controller's editor view is created and released without attaching it to a window
 * (no display server needed): this catches IPlugView reference counting mistakes.
 */

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef PILLOW_TALK_MODULE
#error PILLOW_TALK_MODULE must point to the built module binary
#endif

// The pluginterfaces library does not define the VST interface IIDs (vstinitiids.cpp lives in sdk).
namespace Steinberg::Vst {
DEF_CLASS_IID(IEditController)
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

using EntryFn = bool (*)(void*);
using ExitFn = bool (*)();
using FactoryFn = Steinberg::IPluginFactory* (*)();

// Editor view lifecycle on an initialized controller, never attached (no window).
void exerciseEditorView(Steinberg::FUnknown* obj) {
  Steinberg::Vst::IEditController* ctl = nullptr;
  CHECK(obj->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&ctl) == Steinberg::kResultOk);
  if (!ctl) return;
  CHECK(ctl->initialize(nullptr) == Steinberg::kResultOk);
  CHECK(ctl->createView("no-such-view") == nullptr);
  for (int i = 0; i < 50; ++i) {
    Steinberg::IPlugView* view = ctl->createView(Steinberg::Vst::ViewType::kEditor);
    CHECK(view != nullptr);
    if (!view) break;
    Steinberg::ViewRect r{};
    CHECK(view->getSize(&r) == Steinberg::kResultOk);
    CHECK(r.getWidth() == 480 && r.getHeight() == 560);
    Steinberg::ViewRect asked(0, 0, 900, 300);
    CHECK(view->checkSizeConstraint(&asked) == Steinberg::kResultTrue);
    CHECK(asked.getWidth() == 480 && asked.getHeight() == 560);
    CHECK(view->canResize() == Steinberg::kResultFalse);
    CHECK(view->isPlatformTypeSupported(Steinberg::kPlatformTypeX11EmbedWindowID) == Steinberg::kResultTrue);
    if (i == 0) {  // content scale before attach: getSize and the size constraint follow it
      Steinberg::IPlugViewContentScaleSupport* scale = nullptr;
      CHECK(view->queryInterface(Steinberg::IPlugViewContentScaleSupport::iid, (void**)&scale) == Steinberg::kResultOk);
      if (scale) {
        CHECK(scale->setContentScaleFactor(2.0f) == Steinberg::kResultTrue);
        Steinberg::ViewRect s{};
        CHECK(view->getSize(&s) == Steinberg::kResultOk);
        CHECK(s.getWidth() == 960 && s.getHeight() == 1120);
        Steinberg::ViewRect c(0, 0, 100, 100);
        CHECK(view->checkSizeConstraint(&c) == Steinberg::kResultTrue);
        CHECK(c.getWidth() == 960 && c.getHeight() == 1120);
        CHECK(scale->setContentScaleFactor(-1.0f) == Steinberg::kResultTrue);  // sanitized to 1
        CHECK(view->getSize(&s) == Steinberg::kResultOk);
        CHECK(s.getWidth() == 480 && s.getHeight() == 560);
        scale->release();
      }
    }
    view->release();  // the host owns the returned reference
  }
  CHECK(ctl->terminate() == Steinberg::kResultOk);
  ctl->release();
}

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
    CHECK(std::strcmp(ci.name, "Pillow Talk") == 0);
    if (std::strcmp(ci.category, "Audio Module Class") == 0) {
      Steinberg::FUnknown* obj = nullptr;
      CHECK(f->createInstance(ci.cid, Steinberg::FUnknown::iid, (void**)&obj) == Steinberg::kResultOk);
      CHECK(obj != nullptr);
      if (obj) obj->release();
    } else if (std::strcmp(ci.category, "Component Controller Class") == 0) {
      Steinberg::FUnknown* obj = nullptr;
      CHECK(f->createInstance(ci.cid, Steinberg::FUnknown::iid, (void**)&obj) == Steinberg::kResultOk);
      CHECK(obj != nullptr);
      if (obj) {
        exerciseEditorView(obj);
        obj->release();
      }
    }
  }
  f->release();  // the host owns the returned reference
}
} // namespace

int main() {
  struct stat st{};
  if (stat(PILLOW_TALK_MODULE, &st) != 0) {
    std::printf("SKIP: module not built: %s\n", PILLOW_TALK_MODULE);
    return kSkip;
  }
  void* h = dlopen(PILLOW_TALK_MODULE, RTLD_NOW | RTLD_LOCAL);
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
