// Developer tool: draws the Happy Ending editor offscreen into PNG files, no host or window needed.
//   happy_ending_render <out-dir> [no-grain] [bypass] [final=inflate|clip] [stage-off=fine|vibe|glue|clip|limit|final|metal]
//                      [band-off=0..5] [listen=0..2] [set] [meters] [cycles=<n>]
// Writes <out-dir>/front.png (1280 x 890, logical size: SIZE and the host factor only scale the window). The back of the
// panel and its tabs come with the editor's back (editor plan, Tasks 13-14): the tool will write back_<tab>.png then.
// bypass, final=, stage-off=, band-off=, listen= and set go through the controller's parameters, picked up by the
// editor's timer work (dimming, the FINAL tag); no-grain draws the panel without the grain tile. meters sets proof
// values of the display parameters (LEVEL rows, GR rows, clip LEDs, LUFS, DYN, the screen's inputs), shown once their
// views exist (Tasks 10-11). cycles=<n> builds and removes the editor's views n times first (for leak checkers).
// Used to check the code-drawn GUI against src/happy-ending/docs/mockups/happy_ending_rack.html on a machine without a
// host.
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

namespace FairyTapes::HappyEnding::Ui {

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
    Editor ed(&ctl);   // reads the user's preferences as the plug-in does (SIZE does not change the drawing)
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(root);
    ed.onTimer();   // the controller's state: dimming and the FINAL tag
    Steinberg::ViewRect window{};
    ed.getSize(&window);
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
      std::string tags;
      if (ed.panel_) {
        for (const PanelText& t : kPanelTexts) dimmed += panelTextDimmed(t, ed.panel_->uiState()) ? 1 : 0;
        for (const auto& t : ed.panel_->drawnTags()) tags += std::string(tags.empty() ? "" : ", ") + t.text;
      }
      std::printf("%s: %d x %d (window %d x %d at SIZE %s), %zu printed texts (%d dimmed), tags: %s\n", path.c_str(),
                  Editor::kWidth, Editor::kHeight, window.getWidth(), window.getHeight(),
                  FairyTapes::Ui::sizeLabel(ed.prefs_.size), ed.panel_ ? ed.panel_->drawnTexts().size() : 0u, dimmed,
                  tags.c_str());
    }
    root->removeAll(true);
    ed.resetViewPointers();
    return ok;
  }
};

} // namespace FairyTapes::HappyEnding::Ui

int main(int argc, char** argv) {
  using namespace FairyTapes::HappyEnding;
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: %s <out-dir> [no-grain] [bypass] [final=inflate|clip] [stage-off=fine|vibe|glue|clip|limit|final|metal]\n"
                 "       [band-off=0..5] [listen=0..2] [set] [meters] [cycles=<n>]\n",
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
  if (auto* lf = VSTGUI::getPlatformFactory().asLinuxFactory()) lf->setResourcePath(grain ? HAPPY_ENDING_ASSETS : "/nonexistent/");
#else
  (void)grain;
#endif

  bool ok = false;
  {
    Controller ctl;
    ctl.initialize(nullptr);
    int cycles = 0;
    auto on = [&](Steinberg::Vst::ParamID id, int index) { ctl.setParamNormalized(id, normFromIndex(static_cast<ParamID>(id), index)); };
    for (int i = 2; i < argc; ++i) {
      const char* a = argv[i];
      if (!std::strcmp(a, "bypass")) {
        on(kBypass, 1);
      } else if (!std::strncmp(a, "final=", 6)) {
        on(kFinalMode, !std::strcmp(a + 6, "clip") ? 1 : 0);
      } else if (!std::strncmp(a, "stage-off=", 10)) {
        static const char* const names[Ui::kNumStages] = {"fine", "vibe", "glue", "clip", "limit", "final", "metal"};
        for (int s = 0; s < Ui::kNumStages; ++s)
          if (!std::strcmp(a + 10, names[s])) on(Ui::stageInParam(static_cast<Ui::Stage>(s)), 0);
      } else if (!std::strncmp(a, "band-off=", 9)) {
        const int b = std::atoi(a + 9);
        if (b >= 0 && b < Ui::kNumBands) on(Ui::bandInParam(b), 0);
      } else if (!std::strncmp(a, "listen=", 7)) {
        const int bell = std::atoi(a + 7);
        if (bell >= 0 && bell <= 2) ctl.setParamNormalized(kListenBand, normFromListen(bell));
      } else if (!std::strcmp(a, "set")) {
        ctl.setParamNormalized(kSetActive, 1.0);
      } else if (!std::strcmp(a, "meters")) {
        const double v[][2] = {{kDispInL, .8},        {kDispInR, .62},       {kDispOutL, .7},          {kDispOutR, .55},
                               {kDispGr0, .35},       {kDispGr1, .2},        {kDispGr2, .5},           {kDispGr3, .15},
                               {kDispClip0, 1.0},     {kDispClip4, 1.0},     {kDispLufsShort, .62},    {kDispLufsInt, .58},
                               {kDispDynLow, .4},     {kDispDynAir, .7},     {kDispGlueIn, .7},        {kDispGlueGr, .3},
                               {kDispLimitIn, .75},   {kDispLimitThresh, .6}, {kDispClipIn, .72},      {kDispFinalIn, .68}};
        for (const auto& p : v) ctl.setParamNormalized(static_cast<Steinberg::Vst::ParamID>(p[0]), p[1]);
      } else if (!std::strncmp(a, "cycles=", 7)) {
        cycles = std::atoi(a + 7);
      }
    }
    if (cycles > 0) Ui::EditorPreview::cycle(ctl, cycles);
    ok = Ui::EditorPreview::render(ctl, argv[1]);
    ctl.terminate();
  }
  VSTGUI::exit();
  return ok ? 0 : 1;
}
