// Developer tool: draws the Deep Throat editor offscreen into PNG files, no host or window needed.
//   deep_throat_render <out-dir> [bypass] [mode=speak|sing|puke] [no-grain] [module-off=rider|breath|eq|deess|comp|tone] [meters] [scope]
//                     [wear=off|light|heavy] [seed=<n>] [text=<s>] [text1..text6=<s>] [font=zeyada|rocksalt|homemade]
//                     [values=standard|tape] [screw1..screw4=<deg>] [back=settings|modes|routing|credits|all] [cycles=<n>]
//                     [latency=mix|live]
// Writes <out-dir>/front.png (1000 x 614); back= also writes the back of the panel on that tab (or on every tab with
// all) as <out-dir>/back_<tab>.png, with the editor's own BackView (mode= picks the yellow column of MODES).
// bypass sets the global Bypass, module-off= turns a module's IN off (both through the
// controller, picked up by the editor's timer work); no-grain draws the panel without the grain tile. meters sets proof
// values of the display parameters (LEVEL rows, GR, IN clip lit, BREATH lamp lit); scope fills the SCOPE history with a
// simulated voice (phrases, a breath, a pause, the Rider gain following), one timer run per point. The seed, the tape
// texts and the screw rotations go through the controller's decor (as a stored state would): text= writes on every tape
// slot, textN= on slot N-1 (only the slots the seed and Wear show are drawn; the tool prints them), screwN= is the user's
// rotation of screw N-1. latency= sets BEHAVIOUR > LATENCY of the instance decor the same way (LIVE: the amber
// "LEVEL DETECTOR . LIVE" in MODE, LIVE selected on SETTINGS). wear=, font= and values= go through
// Editor::setWearLevel / setTapeFont / setValuesFont; without them the user preferences file decides, as when the
// editor opens. cycles=<n> builds
// and removes the editor's views n times first (for leak checkers). Used to check the code-drawn GUI against
// docs/mockups/deep_throat_rack.html on a machine without a host.
#include "controller.h"
#include "params.h"
#include "ui/back_settings.h"
#include "ui/editor.h"
#include "ui/layout.h"
#include "ui/panel_view.h"
#include "ui/scope_view.h"
#include "ui/tape_view.h"
#include "ui/tapes.h"

#include "fairy_ui/back_view.h"
#include "fairy_ui/screw_view.h"
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

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

// The SDK's VSTGUI init hook asks the module for its factory; there is none in this tool.
extern "C" Steinberg::IPluginFactory* GetPluginFactory() { return nullptr; }
void* moduleHandle = nullptr;   // normally set by the platform module entry point

namespace FairyTapes::DeepThroat::Ui {

struct EditorPreview {
  struct Options {
    bool scope = false;
    int wear = -1;     // -1: from the preferences, else WearLevel
    int font = -1;     // -1: from the preferences, else TapeFont
    int values = -1;   // -1: from the preferences, 0 standard, 1 tape font
    int back = -1;     // -1: front only, 0..3 one tab of the back, 4 every tab
  };
  static constexpr const char* kTabNames[] = {"settings", "modes", "routing", "credits"};

  // Builds and removes the editor's views as open()/close() do, without a window.
  static void cycle(Steinberg::Vst::EditController& ctl, int n) {
    for (int i = 0; i < n; ++i) {
      Editor ed(&ctl);
      auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
      ed.createSides(root);
      ed.onTimer();
      ed.flip_.reset();
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

  // A voice in the shape of the preview's simulation: 4.6 s of phrases, 0.8 s of breath, 1 s of silence; the Rider gain
  // (+-6 dB) slews toward a -27 dB target while the voice speaks.
  static void simulateScope(Steinberg::Vst::EditController& ctl, Editor& ed) {
    double smooth = -40.0, gain = 0.0;
    for (int k = 0; k < ScopeView::kPoints; ++k) {
      const double t = 0.033 * (k + 1), p = std::fmod(t, 6.4);
      double db = -62.0, breath = 0.05;
      bool voice = false;
      if (p < 4.6) {
        db = -38.0 + (std::max(0.0, std::sin(p * 7.3)) * 0.7 + std::max(0.0, std::sin(p * 3.1 + 1)) * 0.3) * 24.0;
        voice = true;
      } else if (p < 5.4) {
        db = -46.0 + std::sin(p * 70.0) * 2.0;
        breath = 0.9;
      }
      smooth += (db - smooth) * 0.07;
      const double want = voice ? std::clamp(-27.0 - smooth, -6.0, 6.0) : gain;
      gain += std::clamp(want - gain, -0.26, 0.26);
      ctl.setParamNormalized(kDispVoice, std::clamp((db + 60.0) / 66.0, 0.0, 1.0));
      ctl.setParamNormalized(kDispRider, std::clamp(0.5 + gain / 12.0, 0.0, 1.0));
      ctl.setParamNormalized(kDispBreath, breath);
      ed.onTimer();
    }
  }

  static bool render(Steinberg::Vst::EditController& ctl, const std::string& dir, const Options& opt) {
    Editor ed(&ctl);
    std::filesystem::path prefsFile;
    try {
      prefsFile = prefsPath();
    } catch (...) {
    }
    ed.loadAppearance(prefsFile);   // what open() does
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createSides(root);   // front shown, back hidden: root->draw() shows the front only
    if (opt.wear >= 0) ed.setWearLevel(static_cast<WearLevel>(opt.wear));
    if (opt.font >= 0) ed.setTapeFont(static_cast<TapeFont>(opt.font));
    if (opt.values >= 0) ed.setValuesFont(opt.values == 1);
    for (const TapeView* t : ed.tapes_) {
      const VSTGUI::CRect r = t->tapeRect(), v = t->getViewSize();
      std::printf("tape slot %d at (%.0f, %.0f) %.0f x %.0f, box (%.0f, %.0f)-(%.0f, %.0f): \"%s\"\n", t->slot(),
                  r.getCenter().x, r.getCenter().y, r.getWidth(), r.getHeight(), v.left, v.top, v.right, v.bottom,
                  t->text().c_str());
    }
    for (const auto* sv : ed.screws_)
      if (sv)
        std::printf("screw at (%.0f, %.0f): %s, %s, shown %.1f deg (seeded %.0f)\n", sv->spec().x, sv->spec().y,
                    sv->spec().cross ? "cross" : "slot", sv->spec().missing ? "missing" : "present", sv->angle(),
                    sv->spec().initialAngle);
    std::printf("seed %u, wear %d, %zu marks, values %s\n", ed.wearSeed(), static_cast<int>(ed.wearLevel()),
                ed.wear_ ? ed.wear_->marks().size() : 0u, ed.valuesHandwritten() ? "tape font" : "standard");
    if (opt.scope) {
      const double breath = ctl.getParamNormalized(kDispBreath);   // the lamp of `meters`, if set
      simulateScope(ctl, ed);
      ctl.setParamNormalized(kDispBreath, breath);
    }
    ed.onTimer();   // dimming and display values from the controller's parameters
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
      std::printf("%s: %d x %d, %zu printed texts (%d dimmed), %d scope points\n", path.c_str(), Editor::kWidth,
                  Editor::kHeight, ed.panel_ ? ed.panel_->drawnTexts().size() : 0u, dimmed,
                  ed.scope_ ? ed.scope_->history().size() : 0);
    }
    if (opt.back >= 0 && ed.back_) {
      ed.back_->setVisible(true);
      for (int tab = 0; tab < 4; ++tab) {
        if (opt.back != 4 && opt.back != tab) continue;
        ed.back_->setTab(tab);
        auto bctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(Editor::kWidth, Editor::kHeight), 1.0);
        if (!bctx) {
          ok = false;
          continue;
        }
        bctx->beginDraw();
        ed.back_->draw(bctx);
        bctx->endDraw();
        const std::string path = dir + "/back_" + kTabNames[tab] + ".png";
        const bool written = writePng(*bctx, path);
        if (!written) std::fprintf(stderr, "cannot write %s\n", path.c_str());
        ok = ok && written;
        std::printf("%s: tab %s\n", path.c_str(), ed.back_->content().tabs[static_cast<std::size_t>(tab)].label.c_str());
      }
      ed.back_->setVisible(false);
    }
    ed.flip_.reset();
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
                 "usage: %s <out-dir> [bypass] [mode=speak|sing|puke] [no-grain] [module-off=rider|breath|eq|deess|comp|tone] [meters] [scope]\n"
                 "       [wear=off|light|heavy] [seed=<n>] [text=<s>] [text1..text6=<s>] [font=zeyada|rocksalt|homemade]\n"
                 "       [values=standard|tape] [screw1..screw4=<deg>] [back=settings|modes|routing|credits|all] [cycles=<n>]\n"
                 "       [latency=mix|live]\n",
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
  Ui::EditorPreview::Options opt;
  bool customDecor = false;
  Decor decor = ctl.decor();
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "bypass")) {
      ctl.setParamNormalized(kBypass, 1.0);
    } else if (!std::strncmp(argv[i], "mode=", 5)) {
      const char* m = argv[i] + 5;
      ctl.setParamNormalized(kMode, !std::strcmp(m, "puke") ? 1.0 : !std::strcmp(m, "sing") ? 0.5 : 0.0);
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
    } else if (!std::strcmp(argv[i], "meters")) {
      const double v[][2] = {{kDispInL, .8},  {kDispInR, .62},   {kDispOutL, .7},  {kDispOutR, .55},
                             {kDispGr, .85},  {kDispClipIn, 1.0}, {kDispClipOut, 0.0}, {kDispBreath, .9}};
      for (const auto& p : v) ctl.setParamNormalized(static_cast<Steinberg::Vst::ParamID>(p[0]), p[1]);
    } else if (!std::strcmp(argv[i], "scope")) {
      opt.scope = true;
    } else if (!std::strncmp(argv[i], "wear=", 5)) {
      const char* w = argv[i] + 5;
      opt.wear = !std::strcmp(w, "off") ? 0 : !std::strcmp(w, "heavy") ? 2 : 1;
    } else if (!std::strncmp(argv[i], "font=", 5)) {
      const char* f = argv[i] + 5;
      opt.font = !std::strcmp(f, "rocksalt") ? static_cast<int>(Ui::TapeFont::RockSalt)
                 : !std::strcmp(f, "homemade") ? static_cast<int>(Ui::TapeFont::HomemadeApple)
                                               : static_cast<int>(Ui::TapeFont::Zeyada);
    } else if (!std::strncmp(argv[i], "values=", 7)) {
      opt.values = !std::strcmp(argv[i] + 7, "standard") ? 0 : 1;
    } else if (!std::strncmp(argv[i], "seed=", 5)) {
      decor.seed = static_cast<uint32_t>(std::strtoul(argv[i] + 5, nullptr, 0));
      customDecor = true;
    } else if (!std::strncmp(argv[i], "text=", 5)) {
      for (int s = 0; s < Ui::kNumTapeSlots; ++s) decor = Ui::withTapeText(decor, s, argv[i] + 5);
      customDecor = true;
    } else if (!std::strncmp(argv[i], "text", 4) && argv[i][4] >= '1' && argv[i][4] <= '6' && argv[i][5] == '=') {
      decor = Ui::withTapeText(decor, argv[i][4] - '1', argv[i] + 6);
      customDecor = true;
    } else if (!std::strncmp(argv[i], "screw", 5) && argv[i][5] >= '1' && argv[i][5] <= '4' && argv[i][6] == '=') {
      decor.screwAngles[argv[i][5] - '1'] = static_cast<float>(std::atof(argv[i] + 7));
      customDecor = true;
    } else if (!std::strncmp(argv[i], "back=", 5)) {
      const char* b = argv[i] + 5;
      opt.back = !std::strcmp(b, "all") ? 4 : 0;
      for (int t = 0; t < 4; ++t)
        if (!std::strcmp(b, Ui::EditorPreview::kTabNames[t])) opt.back = t;
    } else if (!std::strncmp(argv[i], "latency=", 8)) {
      decor.behaviour.latency = !std::strcmp(argv[i] + 8, "live") ? LatencyMode::Live : LatencyMode::Mix;
      customDecor = true;
    } else if (!std::strncmp(argv[i], "cycles=", 7)) {
      cycles = std::atoi(argv[i] + 7);
    }
  }
  if (cycles > 0) Ui::EditorPreview::cycle(ctl, cycles);
  if (customDecor) ctl.editDecor(decor);
  const bool ok = Ui::EditorPreview::render(ctl, argv[1], opt);
  ctl.terminate();
  VSTGUI::exit();
  return ok ? 0 : 1;
}
