// Developer tool: draws the editor offscreen into a PNG, no host or window needed.
//   the_stripper_render out.png [bypass] [bus] [eq-off] [nam-off] [recall] [moved] [meters] [seed=<n>] [model=<file>] [gpu] [model-error=<text>] [browser] [back-settings|back-buttons|back-routing|back-credits]
// Used to check the look of the code-drawn GUI on a machine without a plugin host.
#include "controller.h"
#include "ui/editor.h"
#include "ui/back_panel.h"
#include "ui/back_content.h"
#include "ui/buttons_tab.h"
#include "fairy_ui/back_view.h"
#include "ui/model_browser.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/vstguiinit.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>

// The SDK's VSTGUI init hook asks the module for its factory; there is none in this tool.
extern "C" Steinberg::IPluginFactory* GetPluginFactory() { return nullptr; }
void* moduleHandle = nullptr;   // normally set by the platform module entry point

namespace FairyTapes::TheStripper::Ui {

struct EditorPreview {
  static void setSeed(Controller& ctl, std::uint32_t seed) { ctl.wearSeed_ = seed; }

  static void setModel(Controller& ctl, int state, const char* name, const char* error) {
    ctl.modelStatus_.state = state;
    ctl.modelStatus_.name = name;
    ctl.modelStatus_.error = error;
  }

  static void setGpu(Controller& ctl) { ctl.modelStatus_.gpu = true; }

  static bool render(Controller& ctl, const char* path, bool browser, int backTab) {
    Editor ed(&ctl);
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(root);
    ed.syncFromController(true);
    ed.updateMeters();
    ed.updateModel();
    if (backTab >= 0) {   // the back of the panel instead of the front
      FairyTapes::Ui::Skin skin;
      Preferences prefs = loadPreferences(preferencesPath("TheStripper"));
      prefs.backTab = static_cast<BackTab>(backTab);
      auto* back = new FairyTapes::Ui::BackView(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight), &skin,
                                                makeBackContent(prefs, ctl.recallMode(), BackActions{}), FairyTapes::Ui::BackView::Callbacks{},
                                                backTabIndex(prefs.backTab));
      back->setCustomDrawer(1, drawButtonsTab);
      back->setWear(FairyTapes::Ui::generateWear(ctl.wearSeed() ^ 0x77u, prefs.wear));
      root->removeAll(true);
      root->addView(back);
    }
    if (browser) {
      auto* popup = new ModelBrowser(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight), ctl.modelPath());
      root->addView(popup);
      for (int i = 0; i < 400; ++i) {   // wait for the folder scan
        popup->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
    auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(Editor::kWidth, Editor::kHeight), 1.0);
    if (!ctx) return false;
    ctx->beginDraw();
    root->draw(ctx);
    ctx->endDraw();
    auto png = VSTGUI::getPlatformFactory().createBitmapMemoryPNGRepresentation(ctx->getBitmap()->getPlatformBitmap());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    root->removeAll(true);
    return true;
  }
};

} // namespace FairyTapes::TheStripper::Ui

int main(int argc, char** argv) {
  using namespace FairyTapes::TheStripper;
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s out.png [bypass] [bus] [eq-off] [nam-off]\n", argv[0]);
    return 2;
  }
  if (argc >= 2 && !std::strcmp(argv[1], "dump-help")) {   // the button help as tab-separated lines (title, what, imposes), for the HTML preview
    for (const auto& e : Ui::helpEntries()) std::printf("%s\t%s\t%s\n", e.title.c_str(), e.what.c_str(), e.imposes.c_str());
    return 0;
  }
  VSTGUI::init(nullptr);
  Controller ctl;
  bool browser = false;
  int backTab = -1;
  ctl.initialize(nullptr);
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "bypass")) ctl.setParamNormalized(kBypass, 1.0);
    else if (!std::strcmp(argv[i], "bus")) ctl.setParamNormalized(kStripMode, 1.0);
    else if (!std::strcmp(argv[i], "nam-off")) { ctl.setParamNormalized(kDriveNamOn, 0.0); ctl.setParamNormalized(kEqSaturation, 0.4); }
    else if (!std::strcmp(argv[i], "eq-off")) ctl.setParamNormalized(kBypassEQ, 1.0);
    else if (!std::strcmp(argv[i], "browser")) browser = true;
    else if (!std::strcmp(argv[i], "back-settings")) backTab = 0;
    else if (!std::strcmp(argv[i], "back-credits")) backTab = 1;
    else if (!std::strcmp(argv[i], "back-buttons")) backTab = 2;
    else if (!std::strcmp(argv[i], "back-routing")) backTab = 3;
    else if (!std::strncmp(argv[i], "seed=", 5)) Ui::EditorPreview::setSeed(ctl, static_cast<std::uint32_t>(std::strtoul(argv[i] + 5, nullptr, 0)));
    else if (!std::strncmp(argv[i], "model=", 6)) Ui::EditorPreview::setModel(ctl, 2, argv[i] + 6, "");
    else if (!std::strcmp(argv[i], "gpu")) Ui::EditorPreview::setGpu(ctl);
    else if (!std::strncmp(argv[i], "model-error=", 12)) Ui::EditorPreview::setModel(ctl, 3, "", argv[i] + 12);
    else if (!std::strcmp(argv[i], "recall")) ctl.markRecallNow();   // marks at the current values: use before "moved"
    else if (!std::strcmp(argv[i], "moved")) {
      ctl.setParamNormalized(kEqLFGain, 0.8);
      ctl.setParamNormalized(kInputGain, 0.2);
      ctl.setParamNormalized(kCompressAmount, 0.7);
      ctl.setParamNormalized(kLimiterPound, 0.9);
    }
    else if (!std::strcmp(argv[i], "meters")) {
      const double v[][2] = {{kMeterInL, .8}, {kMeterInR, .62}, {kMeterOutL, .7}, {kMeterOutR, .55}, {kMeterGrComp, .4},
                             {kMeterGrLimiter, .2}, {kMeterGrGate, .9}, {kClipEq, 1.0}, {kClipOutput, 1.0}};
      for (const auto& e : v) ctl.setParamNormalized(static_cast<Steinberg::Vst::ParamID>(e[0]), e[1]);
    }
  }
  return Ui::EditorPreview::render(ctl, argv[1], browser, backTab) ? 0 : 1;
}
