// Developer tool: draws the Hotline editor offscreen into a PNG, no host or window needed.
//   hotline_render out.png [bypass] [characterin=off] [device=<0..5>] [mono] [meters] [clips] [no-grain]
// Used to check the look of the code-drawn GUI against docs/mockups/hotline_rack.html on a machine without a host.
#include "controller.h"
#include "ui/editor.h"

#include "fairy_ui/meters.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/silkscreen_view.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/vstguiinit.h"
#if SMTG_OS_LINUX
#include "vstgui/lib/platform/linux/linuxfactory.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

// The SDK's VSTGUI init hook asks the module for its factory; there is none in this tool.
extern "C" Steinberg::IPluginFactory* GetPluginFactory() { return nullptr; }
void* moduleHandle = nullptr;   // normally set by the platform module entry point

namespace FairyTapes::Hotline::Ui {

struct EditorPreview {
  static bool render(Steinberg::Vst::EditController& ctl, const char* path) {
    Editor ed(&ctl);
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(root);
    for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) ed.syncFromController(id);
    for (int i = 0; i < 4; ++i) ed.updateDisplays();   // the LED rows show the display parameters
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

} // namespace FairyTapes::Hotline::Ui

int main(int argc, char** argv) {
  using namespace FairyTapes::Hotline;
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s out.png [bypass] [characterin=off] [device=<0..5>] [mono] [meters] [clips] [no-grain]\n", argv[0]);
    return 2;
  }
  VSTGUI::init(nullptr);
  bool grain = true;
  for (int i = 2; i < argc; ++i)
    if (!std::strcmp(argv[i], "no-grain")) grain = false;
#if SMTG_OS_LINUX
  // The bundle's Resources folder is not there: point VSTGUI at the shared assets for the grain tile.
  if (auto* lf = VSTGUI::getPlatformFactory().asLinuxFactory()) lf->setResourcePath(grain ? HOTLINE_ASSETS : "/nonexistent/");
#endif

  Controller ctl;
  ctl.initialize(nullptr);
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "bypass")) ctl.setParamNormalized(kBypass, 1.0);
    else if (!std::strcmp(argv[i], "characterin=off")) ctl.setParamNormalized(kCharacterIn, 0.0);
    else if (!std::strcmp(argv[i], "mono")) ctl.setParamNormalized(kMono, 1.0);
    else if (!std::strncmp(argv[i], "device=", 7)) ctl.setParamNormalized(kDevice, normFromDeviceIndex(std::atoi(argv[i] + 7)));
    else if (!std::strcmp(argv[i], "meters")) {
      const double v[][2] = {{kMeterInL, meterNormFromDb(-12)}, {kMeterInR, meterNormFromDb(-30)},
                             {kMeterOutL, meterNormFromDb(-3)}, {kMeterOutR, meterNormFromDb(3)}};
      for (const auto& e : v) ctl.setParamNormalized(static_cast<Steinberg::Vst::ParamID>(e[0]), e[1]);
    } else if (!std::strcmp(argv[i], "clips")) {
      ctl.setParamNormalized(kClipIn, 1.0);
      ctl.setParamNormalized(kClipDrive, 1.0);
    }
  }
  const bool ok = Ui::EditorPreview::render(ctl, argv[1]);
  ctl.terminate();
  return ok ? 0 : 1;
}
