/**
 * Fairy Tapes · Happy Ending — Module/factory lifecycle smoke test.
 * dlopens the built module from dist/ and drives it the way the SDK hosting code does: ModuleEntry,
 * GetPluginFactory (owned reference), class info, createInstance, release of every reference, ModuleExit.
 * Checks the registered classes (a processor and a controller, "Fx|Mastering"), the parameter table the
 * controller registers (ids, steps, defaults, automation, hidden session and display parameters), the
 * bus layouts the processor accepts, and the editor view: created for ViewType::kEditor only, 1280 x 890 times the host
 * factor times the SIZE of HappyEnding.prefs, created, attached without a window and released 50 times (no display
 * server needed: without a host run loop the editor does not open). The preferences folder is a scratch one
 * ($XDG_CONFIG_HOME, $HOME on macOS), never the user's.
 * Exits 77 (skipped) when the module is absent.
 */

#include "params.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <dlfcn.h>

#ifndef HAPPY_ENDING_MODULE
#error HAPPY_ENDING_MODULE must point to the built module binary
#endif

using namespace FairyTapes::HappyEnding;
using Steinberg::Vst::ParameterInfo;

// The pluginterfaces library does not define these interface IIDs (commoniids.cpp lives in sdk_common).
namespace Steinberg {
DEF_CLASS_IID(IPlugFrame)
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

// The scratch preferences folder (main() points the environment at it) and the plug-in's file inside it.
std::filesystem::path g_prefsHome;
std::filesystem::path prefsFile() { return g_prefsHome / "FairyTapes" / "HappyEnding.prefs"; }
void writePrefs(const std::string& text) {
  std::filesystem::create_directories(prefsFile().parent_path());
  std::ofstream(prefsFile(), std::ios::binary | std::ios::trunc) << text;
}

// The host side of a view without a window: counts its references and the resize requests; no IRunLoop, so on Linux
// the editor cannot open (VSTGUI's X11 frame needs one) and attached() leaves it closed.
class FakePlugFrame : public Steinberg::IPlugFrame {
public:
  int refs = 0, resizes = 0;
  Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView*, Steinberg::ViewRect*) override {
    ++resizes;
    return Steinberg::kResultTrue;
  }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
    QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, Steinberg::IPlugFrame)
    QUERY_INTERFACE(iid, obj, Steinberg::IPlugFrame::iid, Steinberg::IPlugFrame)
    *obj = nullptr;
    return Steinberg::kNoInterface;
  }
  Steinberg::uint32 PLUGIN_API addRef() override { return static_cast<Steinberg::uint32>(++refs); }
  Steinberg::uint32 PLUGIN_API release() override { return static_cast<Steinberg::uint32>(--refs); }
};

// createView: the editor for ViewType::kEditor, 1280 x 890 at SIZE 100 (no preferences file) and host factor 1, the
// size constraint answering only that size, a fixed size; any other name gives nullptr.
void test_create_view_returns_editor(Steinberg::Vst::IEditController* ctl) {
  std::filesystem::remove(prefsFile());
  CHECK(ctl->createView("no-such-view") == nullptr);
  CHECK(ctl->createView(nullptr) == nullptr);
  Steinberg::IPlugView* view = ctl->createView(Steinberg::Vst::ViewType::kEditor);
  CHECK(view != nullptr);
  if (!view) return;
  Steinberg::ViewRect r{};
  CHECK(view->getSize(&r) == Steinberg::kResultOk);
  CHECK(r.getWidth() == 1280 && r.getHeight() == 890);
  Steinberg::ViewRect asked(0, 0, 900, 300);
  CHECK(view->checkSizeConstraint(&asked) == Steinberg::kResultTrue);
  CHECK(asked.getWidth() == 1280 && asked.getHeight() == 890);
  CHECK(view->canResize() == Steinberg::kResultFalse);
#if !defined(__APPLE__)
  CHECK(view->isPlatformTypeSupported(Steinberg::kPlatformTypeX11EmbedWindowID) == Steinberg::kResultTrue);
  CHECK(view->isPlatformTypeSupported(Steinberg::kPlatformTypeWaylandSurfaceID) != Steinberg::kResultTrue);
  // Content scale before attach (macOS scales natively and answers kResultFalse): getSize and the constraint follow it.
  Steinberg::IPlugViewContentScaleSupport* scale = nullptr;
  CHECK(view->queryInterface(Steinberg::IPlugViewContentScaleSupport::iid, (void**)&scale) == Steinberg::kResultOk);
  if (scale) {
    CHECK(scale->setContentScaleFactor(2.0f) == Steinberg::kResultTrue);
    Steinberg::ViewRect s{};
    CHECK(view->getSize(&s) == Steinberg::kResultOk);
    CHECK(s.getWidth() == 2560 && s.getHeight() == 1780);
    Steinberg::ViewRect c(0, 0, 100, 100);
    CHECK(view->checkSizeConstraint(&c) == Steinberg::kResultTrue);
    CHECK(c.getWidth() == 2560 && c.getHeight() == 1780);
    CHECK(scale->setContentScaleFactor(-1.0f) == Steinberg::kResultTrue);   // sanitized to 1
    CHECK(view->getSize(&s) == Steinberg::kResultOk);
    CHECK(s.getWidth() == 1280 && s.getHeight() == 890);
    CHECK(scale->release() == 1);   // the view keeps the host's reference
  }
#endif
  CHECK(view->release() == 0);   // the host owned the only reference: the view is gone
}

// Created, given a plug frame, attached without a window, removed and released 50 times: every view goes away with the
// host's release and the plug frame gets back every reference the views took.
void test_editor_open_close_repeatedly(Steinberg::Vst::IEditController* ctl) {
  std::filesystem::remove(prefsFile());
  FakePlugFrame host;
  for (int i = 0; i < 50; ++i) {
    Steinberg::IPlugView* view = ctl->createView(Steinberg::Vst::ViewType::kEditor);
    CHECK(view != nullptr);
    if (!view) break;
    CHECK(view->setFrame(&host) == Steinberg::kResultTrue);
#if defined(__linux__)
    view->attached(nullptr, Steinberg::kPlatformTypeX11EmbedWindowID);   // no run loop: stays closed, no crash
    view->removed();
#endif
    CHECK(view->setFrame(nullptr) == Steinberg::kResultTrue);
    CHECK(view->release() == 0);
  }
  CHECK(host.refs == 0);
}

// The SIZE of HappyEnding.prefs read when the view is made: size=75 gives 960 x 668 (890 x 0.75 = 667.5, rounded), and the
// size constraint answers that size; with the file gone the next view is 1280 x 890 again.
void test_editor_size_from_prefs(Steinberg::Vst::IEditController* ctl) {
  writePrefs("size=75\n");
  Steinberg::IPlugView* view = ctl->createView(Steinberg::Vst::ViewType::kEditor);
  CHECK(view != nullptr);
  if (view) {
    Steinberg::ViewRect r{};
    CHECK(view->getSize(&r) == Steinberg::kResultOk);
    CHECK(r.getWidth() == 960 && r.getHeight() == 668);
    Steinberg::ViewRect asked(0, 0, 1280, 890);
    CHECK(view->checkSizeConstraint(&asked) == Steinberg::kResultTrue);
    CHECK(asked.getWidth() == 960 && asked.getHeight() == 668);
    CHECK(view->release() == 0);
  }
  std::filesystem::remove(prefsFile());
  view = ctl->createView(Steinberg::Vst::ViewType::kEditor);
  CHECK(view != nullptr);
  if (view) {
    Steinberg::ViewRect r{};
    CHECK(view->getSize(&r) == Steinberg::kResultOk);
    CHECK(r.getWidth() == 1280 && r.getHeight() == 890);
    CHECK(view->release() == 0);
  }
}

void exerciseController(Steinberg::FUnknown* obj) {
  Steinberg::Vst::IEditController* ctl = nullptr;
  CHECK(obj->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&ctl) == Steinberg::kResultOk);
  if (!ctl) return;
  CHECK(ctl->initialize(nullptr) == Steinberg::kResultOk);
  test_create_view_returns_editor(ctl);
  test_editor_open_close_repeatedly(ctl);
  test_editor_size_from_prefs(ctl);

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
  // The editor reads its SIZE from the user's preferences: a scratch folder instead of the real one.
  char scratch[] = "/tmp/happy_ending_factory_XXXXXX";
  if (!mkdtemp(scratch)) {
    std::fprintf(stderr, "mkdtemp failed\n");
    return 1;
  }
  g_prefsHome = scratch;
#if defined(__APPLE__)
  setenv("HOME", scratch, 1);
  g_prefsHome /= "Library/Application Support";
#else
  setenv("XDG_CONFIG_HOME", scratch, 1);
#endif
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
  std::error_code ec;
  std::filesystem::remove_all(scratch, ec);
  std::printf("%s\n", g_failed ? "Factory lifecycle FAILED" : "Factory lifecycle OK");
  return g_failed ? 1 : 0;
}
