// Developer tool: draws the Pillow Talk editor offscreen into a PNG, no host or window needed.
//   pillow_talk_render out.png [bypass] [sync] [meters] [scope] [wear=off|light|heavy] [seed=<n>] [no-grain]
// The seed goes through the controller's decor (as a stored state would), the wear level through Editor::setWearLevel;
// without wear= the level comes from the user preferences file, as when the editor opens.
// Used to check the look of the code-drawn GUI against docs/mockups/pillow_rack.html on a machine without a host.
#include "controller.h"
#include "ui/duck_scope.h"
#include "ui/editor.h"
#include "ui/prefs.h"

#include "fairy_ui/wear_view.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/vstguiinit.h"
#if SMTG_OS_LINUX
#include "vstgui/lib/platform/linux/linuxfactory.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

// The SDK's VSTGUI init hook asks the module for its factory; there is none in this tool.
extern "C" Steinberg::IPluginFactory* GetPluginFactory() { return nullptr; }
void* moduleHandle = nullptr;   // normally set by the platform module entry point

namespace FairyTapes::PillowTalk::Ui {

struct EditorPreview {
  struct Options {
    bool scope = false;
    bool customWear = false;
    FairyTapes::Ui::WearLevel wear = FairyTapes::Ui::WearLevel::Light;
  };

  static bool render(Steinberg::Vst::EditController& ctl, const char* path, const Options& opt) {
    Editor ed(&ctl);
    ed.wearLevel_ = loadPrefs(prefsPath()).wear;   // what open() does
    ed.refreshDecor();
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(root);
    for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) ed.syncFromController(id);
    if (opt.customWear) ed.setWearLevel(opt.wear);
    std::printf("seed %u, wear %d, %zu marks\n", ed.wearSeed(), static_cast<int>(ed.wearLevel()),
                ed.wear_ ? ed.wear_->marks().size() : 0u);
    if (opt.scope && ed.scope_) {
      // A few phrases: voice over the threshold, the send ducks, the echo follows after each phrase.
      for (int i = 0; i < DuckScope::kPoints; ++i) {
        const double t = i * 0.03;
        const bool talking = std::fmod(t, 2.0) < 1.1;
        const float voice = talking ? static_cast<float>(0.6 + 0.25 * std::fabs(std::sin(t * 9))) : 0.05f;
        const float send = talking ? 0.25f : static_cast<float>(1.0 - 0.75 * std::exp(-(std::fmod(t, 2.0) - 1.1) * 4));
        const float echo = talking ? 0.1f : static_cast<float>(0.55 * send * std::exp(-(std::fmod(t, 2.0) - 1.1) * 2));
        ed.scope_->push(voice, send, echo);
      }
    }
    ed.updateDisplays();
    auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(Editor::kWidth, Editor::kHeight), 1.0);
    if (!ctx) return false;
    ctx->beginDraw();
    root->draw(ctx);
    ctx->endDraw();
    auto png = VSTGUI::getPlatformFactory().createBitmapMemoryPNGRepresentation(ctx->getBitmap()->getPlatformBitmap());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png.data()),
                                                static_cast<std::streamsize>(png.size()));
    root->removeAll(true);
    ed.resetViewPointers();
    return true;
  }
};

} // namespace FairyTapes::PillowTalk::Ui

int main(int argc, char** argv) {
  using namespace FairyTapes::PillowTalk;
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s out.png [bypass] [sync] [meters] [scope] [wear=off|light|heavy] [seed=<n>] [no-grain]\n",
                 argv[0]);
    return 2;
  }
  VSTGUI::init(nullptr);
  bool grain = true;
  for (int i = 2; i < argc; ++i)
    if (!std::strcmp(argv[i], "no-grain")) grain = false;
#if SMTG_OS_LINUX
  // The bundle's Resources folder is not there: point VSTGUI at the shared assets for the grain tile.
  if (auto* lf = VSTGUI::getPlatformFactory().asLinuxFactory()) lf->setResourcePath(grain ? PILLOW_TALK_ASSETS : "/nonexistent/");
#endif

  Controller ctl;
  ctl.initialize(nullptr);
  Ui::EditorPreview::Options opt;
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "bypass")) ctl.setParamNormalized(kBypass, 1.0);
    else if (!std::strcmp(argv[i], "sync")) {
      ctl.setParamNormalized(kTimeSync, 1.0);
      ctl.setParamNormalized(kHoldSync, 1.0);
    } else if (!std::strcmp(argv[i], "scope")) opt.scope = true;
    else if (!std::strcmp(argv[i], "meters")) {
      const double v[][2] = {{kDispInL, .8}, {kDispInR, .62}, {kDispOutL, .7}, {kDispOutR, .55},
                             {kDispSend, .4}, {kDispClipIn, 1.0}, {kDispClipOut, 0.0}};
      for (const auto& e : v) ctl.setParamNormalized(static_cast<Steinberg::Vst::ParamID>(e[0]), e[1]);
    } else if (!std::strncmp(argv[i], "wear=", 5)) {
      opt.customWear = true;
      const char* w = argv[i] + 5;
      opt.wear = !std::strcmp(w, "off")     ? FairyTapes::Ui::WearLevel::Off
                 : !std::strcmp(w, "heavy") ? FairyTapes::Ui::WearLevel::Heavy
                                            : FairyTapes::Ui::WearLevel::Light;
    } else if (!std::strncmp(argv[i], "seed=", 5)) {
      Decor d = ctl.decor();
      d.seed = static_cast<uint32_t>(std::strtoul(argv[i] + 5, nullptr, 0));
      ctl.editDecor(d);
    }
  }
  const bool ok = Ui::EditorPreview::render(ctl, argv[1], opt);
  ctl.terminate();
  return ok ? 0 : 1;
}
