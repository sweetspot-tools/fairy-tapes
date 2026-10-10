// Developer tool: draws the editor offscreen into a PNG, no host or window needed.
//   the_stripper_render out.png [bypass] [bus] [eq-off] [mix <name>=<0..1> ...]
// Used to check the look of the code-drawn GUI on a machine without a plugin host.
#include "controller.h"
#include "ui/editor.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/vstguiinit.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

// The SDK's VSTGUI init hook asks the module for its factory; there is none in this tool.
extern "C" Steinberg::IPluginFactory* GetPluginFactory() { return nullptr; }
void* moduleHandle = nullptr;   // normally set by the platform module entry point

namespace FairyTapes::TheStripper::Ui {

struct EditorPreview {
  static bool render(Controller& ctl, const char* path) {
    Editor ed(&ctl);
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(root);
    ed.syncFromController(true);
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
    std::fprintf(stderr, "usage: %s out.png [bypass] [bus] [eq-off]\n", argv[0]);
    return 2;
  }
  VSTGUI::init(nullptr);
  Controller ctl;
  ctl.initialize(nullptr);
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "bypass")) ctl.setParamNormalized(kBypass, 1.0);
    else if (!std::strcmp(argv[i], "bus")) ctl.setParamNormalized(kStripMode, 1.0);
    else if (!std::strcmp(argv[i], "eq-off")) ctl.setParamNormalized(kBypassEQ, 1.0);
  }
  return Ui::EditorPreview::render(ctl, argv[1]) ? 0 : 1;
}
