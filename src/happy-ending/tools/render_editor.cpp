// Developer tool: draws the Happy Ending editor offscreen into PNG files, no host or window needed.
//   happy_ending_render <out-dir> [no-grain] [bypass] [final=inflate|clip] [stage-off=fine|vibe|glue|clip|limit|final|metal]
//                      [band-off=0..5] [listen=0..2] [set] [meters] [cycles=<n>]
//                      [view=eq|glue|clip|limit|final] [scope=<n>] [demo] [ms]
//                      [seed=<n>] [wear=off|light|heavy] [tapefont=zeyada|rocksalt|homemade] [values=tape|standard]
//                      [tape=<slot>:<text>]
// Writes <out-dir>/front.png (1280 x 890, logical size: SIZE and the host factor only scale the window). The back of the
// panel and its tabs come with the editor's back (editor plan, Tasks 13-14): the tool will write back_<tab>.png then.
// bypass, final=, stage-off=, band-off=, listen= and set go through the controller's parameters, picked up by the
// editor's timer work (dimming, the FINAL tag); no-grain draws the panel without the grain tile. meters sets proof
// values of the display parameters (LEVEL rows, GR rows, clip LEDs, LUFS near the default TARGET so the green LED is on,
// DYN, the screen's inputs); the LEVEL views and the screen show them. view= picks the screen's view (the controller's, as
// the view buttons do); scope=<n> runs n timer runs of a synthetic programme (a kick every half second and a hat between) through
// the display parameters of the screen, so the GLUE and LIMIT histories scroll and the dots of CLIP and FINAL move; demo sets
// FINE EQ bells, shelves and the VIBE amounts so the curves are not flat, ms puts two bells on MID and SIDE (the screen then draws
// MID and SIDE). seed=<n> gives the instance decor that seed (default 0xC0FFEE; seed=0 renders the editor as it is before
// the decor is ready: no wear, tape or screw); wear=, tapefont= and values= stand for the user's preferences (the tool does
// not read the real file: the Wear preference defaults to Light, the fonts to Zeyada and handwritten values); tape=<slot>:<text>
// writes on a tape. cycles=<n> builds and removes the editor's views n times first (for leak checkers).
// Used to check the code-drawn GUI against src/happy-ending/docs/mockups/happy_ending_rack.html on a machine without a
// host.
#include "controller.h"
#include "display.h"
#include "params.h"
#include "ui/editor.h"
#include "ui/layout.h"
#include "ui/panel_view.h"
#include "ui/prefs.h"
#include "ui/screen_model.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/vstguiinit.h"
#if SMTG_OS_LINUX
#include "vstgui/lib/platform/linux/linuxfactory.h"
#endif

#include <algorithm>
#include <cmath>
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

  // One tick of the synthetic programme of scope=<n> into the screen's display parameters (30 ms per tick).
  static void feedScreen(Steinberg::Vst::EditController& ctl, int k) {
    const double t = k * 0.03;
    const double kick = std::exp(-std::fmod(t, 0.5) / 0.09), hat = 0.5 * std::exp(-std::fmod(t + 0.25, 0.25) / 0.05);
    const double glueLvl = -22 + 16 * kick + 6 * hat, limitLvl = -9 + 9 * std::min(1.0, kick * 1.6) + 2 * hat;
    const double clipLvl = -16 + 16 * kick + 6 * hat;
    auto set = [&](Steinberg::Vst::ParamID id, double v) { ctl.setParamNormalized(id, v); };
    set(kDispGlueIn, normFromStageDb(glueLvl));
    set(kDispGlueGr, normFromGrDb(std::min(12.0, std::max(0.0, glueLvl + 14.0) * 0.5)));
    set(kDispLimitIn, normFromStageDb(limitLvl));
    set(kDispGr2, normFromGrDb(std::max(0.0, limitLvl + 2.0)));
    set(kDispLimitThresh, normFromThreshDb(-2.0));
    set(kDispClipIn, normFromStageDb(clipLvl));
    set(kDispFinalIn, normFromStageDb(limitLvl));
  }

  struct Look {
    WearLevel wear = WearLevel::Light;
    TapeFont tapeFont = TapeFont::Zeyada;
    bool handwritten = true;
  };

  static bool render(Steinberg::Vst::EditController& ctl, const std::string& dir, int scope, const Look& look) {
    Editor ed(&ctl);   // reads the user's preferences as the plug-in does (SIZE does not change the drawing) ...
    ed.setWearLevel(look.wear);   // ... but not Wear and the fonts: the command line stands for them
    ed.setTapeFont(look.tapeFont);
    ed.setValuesFont(look.handwritten);
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(root);
    ed.onTimer();   // the controller's state: dimming and the FINAL tag
    for (int k = 0; k < scope; ++k) {
      feedScreen(ctl, k);
      ed.onTimer();
    }
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
                 "       [band-off=0..5] [listen=0..2] [set] [meters] [cycles=<n>]\n"
                 "       [view=eq|glue|clip|limit|final] [scope=<n>] [demo] [ms]\n"
                 "       [seed=<n>] [wear=off|light|heavy] [tapefont=zeyada|rocksalt|homemade] [values=tape|standard]\n"
                 "       [tape=<slot>:<text>]\n",
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
    int cycles = 0, scope = 0;
    Ui::EditorPreview::Look look;
    Decor decor;
    decor.seed = 0xC0FFEEu;
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
                               {kDispClip0, 1.0},     {kDispClip4, 1.0},     {kDispLufsShort, .767},   {kDispLufsInt, .75},
                               {kDispDynLow, .4},     {kDispDynAir, .7},     {kDispGlueIn, .7},        {kDispGlueGr, .3},
                               {kDispLimitIn, .75},   {kDispLimitThresh, .6}, {kDispClipIn, .72},      {kDispFinalIn, .68}};
        for (const auto& p : v) ctl.setParamNormalized(static_cast<Steinberg::Vst::ParamID>(p[0]), p[1]);
      } else if (!std::strncmp(a, "cycles=", 7)) {
        cycles = std::atoi(a + 7);
      } else if (!std::strncmp(a, "scope=", 6)) {
        scope = std::atoi(a + 6);
      } else if (!std::strncmp(a, "view=", 5)) {
        static const char* const names[5] = {"eq", "glue", "clip", "limit", "final"};
        for (int v = 0; v < 5; ++v)
          if (!std::strcmp(a + 5, names[v])) ctl.setScreenView(static_cast<Ui::ScreenView>(v));
      } else if (!std::strncmp(a, "seed=", 5)) {
        decor.seed = static_cast<std::uint32_t>(std::strtoul(a + 5, nullptr, 0));
      } else if (!std::strncmp(a, "wear=", 5)) {
        look.wear = !std::strcmp(a + 5, "off") ? Ui::WearLevel::Off : !std::strcmp(a + 5, "heavy") ? Ui::WearLevel::Heavy : Ui::WearLevel::Light;
      } else if (!std::strncmp(a, "tapefont=", 9)) {
        look.tapeFont = !std::strcmp(a + 9, "rocksalt") ? Ui::TapeFont::RockSalt : !std::strcmp(a + 9, "homemade") ? Ui::TapeFont::HomemadeApple : Ui::TapeFont::Zeyada;
      } else if (!std::strncmp(a, "values=", 7)) {
        look.handwritten = std::strcmp(a + 7, "standard") != 0;
      } else if (!std::strncmp(a, "tape=", 5)) {
        const int slot = std::atoi(a + 5);
        const char* colon = std::strchr(a + 5, ':');
        if (slot >= 0 && slot < Ui::kNumTapeSlots && colon) {
          if (decor.tapeTexts.size() <= static_cast<std::size_t>(slot)) decor.tapeTexts.resize(static_cast<std::size_t>(slot) + 1);
          decor.tapeTexts[static_cast<std::size_t>(slot)] = sanitizeTapeText(colon + 1);
        }
      } else if (!std::strcmp(a, "demo")) {
        on(kLsGain, 12);
        on(kMidGain, 11);
        on(kHmGain, 4);
        on(kLow, 5);
        on(kAir, 4);
        on(kTilt, 12);
      } else if (!std::strcmp(a, "ms")) {
        on(kLmMs, 1);   // MID
        on(kHmMs, 2);   // SIDE
      }
    }
    if (decor.seed != 0) ctl.editDecor(decor);   // seed=0 leaves the decor not ready
    if (cycles > 0) Ui::EditorPreview::cycle(ctl, cycles);
    ok = Ui::EditorPreview::render(ctl, argv[1], scope, look);
    ctl.terminate();
  }
  VSTGUI::exit();
  return ok ? 0 : 1;
}
