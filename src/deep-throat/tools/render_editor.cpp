// Developer tool: draws the Deep Throat editor offscreen into PNG files, no host or window needed.
//   deep_throat_render <out-dir> [bypass] [no-grain] [module-off=rider|breath|eq|deess|comp|tone] [cycles=<n>]
// Writes <out-dir>/front.png (1000 x 614). The back of the panel (SETTINGS, MODES, ROUTING, CREDITS) is drawn here too
// once it exists (Task 11). bypass sets the global Bypass, module-off= turns a module's IN off (both through the
// controller, picked up by the editor's timer work); no-grain draws the panel without the grain tile. cycles=<n> builds
// and removes the editor's views n times first (for leak checkers). Used to check the code-drawn GUI against
// docs/mockups/deep_throat_rack.html on a machine without a host.
#include "controller.h"
#include "params.h"
#include "ui/editor.h"
#include "ui/layout.h"
#include "ui/panel_view.h"

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
#include <filesystem>
#include <fstream>
#include <string>

// The SDK's VSTGUI init hook asks the module for its factory; there is none in this tool.
extern "C" Steinberg::IPluginFactory* GetPluginFactory() { return nullptr; }
void* moduleHandle = nullptr;   // normally set by the platform module entry point

namespace FairyTapes::DeepThroat::Ui {

struct EditorPreview {
  // Builds and removes the editor's views as open()/close() do, without a window.
  static void cycle(Steinberg::Vst::EditController& ctl, int n) {
    for (int i = 0; i < n; ++i) {
      Editor ed(&ctl);
      auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
      ed.createViews(root);
      ed.onTimer();
      root->removeAll(true);
      ed.resetViewPointers();
    }
    std::printf("%d editor build/remove cycles\n", n);
  }

  static bool writePng(VSTGUI::COffscreenContext& ctx, const std::string& path) {
    auto png = VSTGUI::getPlatformFactory().createBitmapMemoryPNGRepresentation(ctx.getBitmap()->getPlatformBitmap());
    if (png.empty()) return false;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
  }

  static bool render(Steinberg::Vst::EditController& ctl, const std::string& dir) {
    Editor ed(&ctl);
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(root);
    ed.onTimer();   // dimming from the controller's parameters
    auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(Editor::kWidth, Editor::kHeight), 1.0);
    bool ok = false;
    if (ctx) {
      ctx->beginDraw();
      root->draw(ctx);
      ctx->endDraw();
      const std::string path = dir + "/front.png";
      ok = writePng(*ctx, path);
      if (!ok) std::fprintf(stderr, "cannot write %s\n", path.c_str());
      int dimmed = 0;
      if (ed.panel_)
        for (const auto& t : ed.panel_->drawnTexts()) dimmed += t.alpha < 1.f ? 1 : 0;
      std::printf("%s: %d x %d, %zu printed texts (%d dimmed)\n", path.c_str(), Editor::kWidth, Editor::kHeight,
                  ed.panel_ ? ed.panel_->drawnTexts().size() : 0u, dimmed);
    }
    root->removeAll(true);
    ed.resetViewPointers();
    return ok;
  }
};

} // namespace FairyTapes::DeepThroat::Ui

int main(int argc, char** argv) {
  using namespace FairyTapes::DeepThroat;
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: %s <out-dir> [bypass] [no-grain] [module-off=rider|breath|eq|deess|comp|tone] [cycles=<n>]\n",
                 argv[0]);
    return 2;
  }
  std::error_code ec;
  std::filesystem::create_directories(argv[1], ec);
  if (ec) {
    std::fprintf(stderr, "cannot create %s: %s\n", argv[1], ec.message().c_str());
    return 1;
  }
  VSTGUI::init(nullptr);
  bool grain = true;
  for (int i = 2; i < argc; ++i)
    if (!std::strcmp(argv[i], "no-grain")) grain = false;
#if SMTG_OS_LINUX
  // The bundle's Resources folder is not there: point VSTGUI at the shared assets for the grain tile.
  if (auto* lf = VSTGUI::getPlatformFactory().asLinuxFactory()) lf->setResourcePath(grain ? DEEP_THROAT_ASSETS : "/nonexistent/");
#else
  (void)grain;
#endif

  Controller ctl;
  ctl.initialize(nullptr);
  int cycles = 0;
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "bypass")) {
      ctl.setParamNormalized(kBypass, 1.0);
    } else if (!std::strncmp(argv[i], "module-off=", 11)) {
      const char* m = argv[i] + 11;
      const Ui::Module mod = !std::strcmp(m, "rider")    ? Ui::Module::Rider
                             : !std::strcmp(m, "breath") ? Ui::Module::Breath
                             : !std::strcmp(m, "eq")     ? Ui::Module::Eq
                             : !std::strcmp(m, "deess")  ? Ui::Module::DeEss
                             : !std::strcmp(m, "comp")   ? Ui::Module::Comp
                             : !std::strcmp(m, "tone")   ? Ui::Module::Tone
                                                         : Ui::Module::None;
      if (mod != Ui::Module::None) ctl.setParamNormalized(Ui::inParamOf(mod), 0.0);
    } else if (!std::strncmp(argv[i], "cycles=", 7)) {
      cycles = std::atoi(argv[i] + 7);
    }
  }
  if (cycles > 0) Ui::EditorPreview::cycle(ctl, cycles);
  const bool ok = Ui::EditorPreview::render(ctl, argv[1]);
  ctl.terminate();
  VSTGUI::exit();
  return ok ? 0 : 1;
}
