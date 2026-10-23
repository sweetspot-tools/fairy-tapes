// Developer tool: draws the Pillow Talk editor offscreen into a PNG, no host or window needed.
//   pillow_talk_render out.png [bypass] [sync] [meters] [scope] [wear=off|light|heavy] [seed=<n>] [no-grain]
//                      [text=<s>] [text1..text6=<s>] [font=zeyada|rocksalt|homemade] [screw1..screw4=<deg>]
//                      [values=standard|tape] [window=3|6|12] [peaks=off|1.5|3] [back] [tab=settings|routing|credits]
//                      [flip=<0..1>] [cycles=<n>]
// The seed and the tape texts go through the controller's decor (as a stored state would): text= writes on every tape
// slot, textN= on slot N-1 (only the slots the seed and Wear show are drawn; the tool prints them). The wear level goes
// through Editor::setWearLevel and the font through Editor::setTapeFont; without wear= / font= they come from the user
// preferences file, as when the editor opens. screwN= stores the user's rotation of screw N-1 (added to its seeded angle)
// in the decor; the tool prints each screw's look and shown angle. values= goes through Editor::setHandwrittenValues;
// window= and peaks= are the behaviour settings of the instance (stored in the decor, as the back panel does).
// back draws the back of the panel (tab= SETTINGS, ROUTING or CREDITS) instead of the front; flip=<p> draws the turn from the
// front to the back at progress p. cycles=<n> builds and removes the editor's views n times first (for leak checkers).
// Used to check the look of the code-drawn GUI against docs/mockups/pillow_rack.html on a machine without a host.
#include "controller.h"
#include "ui/back_settings.h"
#include "ui/duck_scope.h"
#include "ui/editor.h"
#include "ui/prefs.h"
#include "ui/screw_angles.h"
#include "ui/tape_view.h"
#include "ui/tapes.h"

#include "fairy_ui/back_view.h"
#include "fairy_ui/flip_view.h"
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
    bool customFont = false;
    FairyTapes::Ui::TapeFontId font = FairyTapes::Ui::TapeFontId::Zeyada;
    int values = -1;   // -1 = from the preferences, 0 = standard, 1 = tape font
    bool back = false;
    int tab = 0;
    double flip = -1;   // < 0: no turn
    int cycles = 0;
  };

  static VSTGUI::SharedPointer<VSTGUI::CBitmap> snap(VSTGUI::CView* v) {
    auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(Editor::kWidth, Editor::kHeight), 1.0);
    if (!ctx) return nullptr;
    ctx->beginDraw();
    v->draw(ctx);
    ctx->endDraw();
    return VSTGUI::SharedPointer<VSTGUI::CBitmap>(ctx->getBitmap());
  }

  // Builds and removes the editor's views (front and back) as open()/close() do, without a window.
  static void cycle(Steinberg::Vst::EditController& ctl, int n) {
    for (int i = 0; i < n; ++i) {
      Editor ed(&ctl);
      ed.refreshDecor();
      auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
      auto* front = new VSTGUI::CViewContainer(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
      ed.createViews(front);
      root->addView(front);
      ed.buildBack(root);
      ed.flip_ = FairyTapes::Ui::PanelFlip(root, front, ed.back_);
      ed.startFlip(true);   // a turn left running when the editor closes
      ed.flip_.reset();
      root->removeAll(true);
      ed.resetViewPointers();
    }
    std::printf("%d editor build/remove cycles\n", n);
  }

  static bool render(Steinberg::Vst::EditController& ctl, const char* path, const Options& opt) {
    Editor ed(&ctl);
    Prefs prefs = loadPrefs(prefsPath());   // what open() does
    if (opt.customWear) prefs.wear = opt.wear;   // as if chosen on the back (the file is not written)
    if (opt.customFont)
      prefs.tapeFont = opt.font == FairyTapes::Ui::TapeFontId::RockSalt        ? TapeFont::RockSalt
                       : opt.font == FairyTapes::Ui::TapeFontId::HomemadeApple ? TapeFont::HomemadeApple
                                                                               : TapeFont::Zeyada;
    if (opt.values >= 0) prefs.handwrittenValues = opt.values == 1;
    ed.wearLevel_ = prefs.wear;
    ed.tapeFont_ = tapeFontIdOf(prefs.tapeFont);
    ed.prefs_ = prefs;
    ed.handwrittenValues_ = prefs.handwrittenValues;
    ed.refreshDecor();
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(root);
    ed.buildBack(root);   // hidden in root: root->draw() shows the front only
    if (opt.values >= 0) ed.setHandwrittenValues(opt.values == 1);
    for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) ed.syncFromController(id);
    if (opt.customWear) ed.setWearLevel(opt.wear);
    if (opt.customFont) ed.setTapeFont(opt.font);
    for (const TapeView* t : ed.tapes_) {
      const VSTGUI::CRect r = t->tapeRect();
      std::printf("tape slot %d at (%.0f, %.0f) %.0f x %.0f: \"%s\"\n", t->slot(), r.getCenter().x, r.getCenter().y,
                  r.getWidth(), r.getHeight(), t->text().c_str());
    }
    for (const auto* sv : ed.screws_)
      if (sv)
        std::printf("screw at (%.0f, %.0f): %s, %s, shown %.1f deg (seeded %.0f)\n", sv->spec().x, sv->spec().y,
                    sv->spec().cross ? "cross" : "slot", sv->spec().missing ? "missing" : "present", sv->angle(),
                    sv->spec().initialAngle);
    std::printf("seed %u, wear %d, %zu marks\n", ed.wearSeed(), static_cast<int>(ed.wearLevel()),
                ed.wear_ ? ed.wear_->marks().size() : 0u);
    if (opt.scope && ed.scope_) {
      // A few phrases: voice over the threshold, the send ducks, the echo follows after each phrase.
      for (int i = 0; i < ed.scope_->points(); ++i) {
        const double t = i * 0.03;
        const bool talking = std::fmod(t, 2.0) < 1.1;
        const float voice = talking ? static_cast<float>(0.6 + 0.25 * std::fabs(std::sin(t * 9))) : 0.05f;
        const float send = talking ? 0.25f : static_cast<float>(1.0 - 0.75 * std::exp(-(std::fmod(t, 2.0) - 1.1) * 4));
        const float echo = talking ? 0.1f : static_cast<float>(0.55 * send * std::exp(-(std::fmod(t, 2.0) - 1.1) * 2));
        ed.scope_->push(voice, send, echo);
      }
    }
    ed.updateDisplays();
    std::printf("values %s, display window %d points, peak hold %.1f s, tempo fallback %.0f BPM\n",
                ed.handwrittenValues() ? "tape font" : "standard", ed.scope_ ? ed.scope_->points() : 0,
                displayPeakHoldSeconds(ed.behaviour_.displayPeaks), ed.behaviour_.tempoFallbackBpm);
    if (ed.back_) ed.back_->setTab(opt.tab);
    auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(Editor::kWidth, Editor::kHeight), 1.0);
    if (!ctx) return false;
    ctx->beginDraw();
    if (opt.flip >= 0 && ed.back_) {
      auto turn = VSTGUI::makeOwned<FairyTapes::Ui::FlipView>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight), snap(root),
                                                             snap(ed.back_));
      turn->setProgress(opt.flip);
      turn->draw(ctx);
    } else if (opt.back && ed.back_) {
      ed.back_->draw(ctx);
    } else {
      root->draw(ctx);
    }
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
    std::fprintf(stderr,
                 "usage: %s out.png [bypass] [sync] [meters] [scope] [wear=off|light|heavy] [seed=<n>] [no-grain]\n"
                 "       [text=<s>] [text1..text6=<s>] [font=zeyada|rocksalt|homemade] [screw1..screw4=<deg>]\n"
                 "       [values=standard|tape] [window=3|6|12] [peaks=off|1.5|3] [back] [tab=settings|routing|credits]\n"
                 "       [flip=<0..1>] [cycles=<n>]\n",
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
    } else if (!std::strncmp(argv[i], "font=", 5)) {
      opt.customFont = true;
      const char* f = argv[i] + 5;
      opt.font = !std::strcmp(f, "rocksalt")   ? FairyTapes::Ui::TapeFontId::RockSalt
                 : !std::strcmp(f, "homemade") ? FairyTapes::Ui::TapeFontId::HomemadeApple
                                               : FairyTapes::Ui::TapeFontId::Zeyada;
    } else if (!std::strncmp(argv[i], "text=", 5)) {
      Decor d = ctl.decor();
      for (int slot = 0; slot < Ui::kNumTapeSlots; ++slot) d = Ui::withTapeText(d, slot, argv[i] + 5);
      ctl.editDecor(d);
    } else if (!std::strncmp(argv[i], "text", 4) && argv[i][4] >= '1' && argv[i][4] <= '6' && argv[i][5] == '=') {
      ctl.editDecor(Ui::withTapeText(ctl.decor(), argv[i][4] - '1', argv[i] + 6));
    } else if (!std::strncmp(argv[i], "screw", 5) && argv[i][5] >= '1' && argv[i][5] <= '4' && argv[i][6] == '=') {
      ctl.editDecor(Ui::withScrewAngle(ctl.decor(), argv[i][5] - '1', std::strtof(argv[i] + 7, nullptr)));
    } else if (!std::strncmp(argv[i], "values=", 7)) {
      opt.values = !std::strcmp(argv[i] + 7, "standard") ? 0 : 1;
    } else if (!std::strncmp(argv[i], "window=", 7) || !std::strncmp(argv[i], "peaks=", 6)) {
      Decor d = ctl.decor();
      const bool window = argv[i][0] == 'w';
      const char* v = argv[i] + (window ? 7 : 6);
      if (window) d.behaviour.displayWindow = !std::strcmp(v, "3") ? 0 : !std::strcmp(v, "12") ? 2 : 1;
      else d.behaviour.displayPeaks = !std::strcmp(v, "off") ? 0 : !std::strcmp(v, "3") ? 2 : 1;
      ctl.editDecor(d);
    } else if (!std::strcmp(argv[i], "back")) {
      opt.back = true;
    } else if (!std::strncmp(argv[i], "tab=", 4)) {
      opt.back = true;
      opt.tab = !std::strcmp(argv[i] + 4, "credits") ? 2 : !std::strcmp(argv[i] + 4, "routing") ? 1 : 0;
    } else if (!std::strncmp(argv[i], "flip=", 5)) {
      opt.flip = std::strtod(argv[i] + 5, nullptr);
    } else if (!std::strncmp(argv[i], "cycles=", 7)) {
      opt.cycles = std::atoi(argv[i] + 7);
    } else if (!std::strncmp(argv[i], "seed=", 5)) {
      Decor d = ctl.decor();
      d.seed = static_cast<uint32_t>(std::strtoul(argv[i] + 5, nullptr, 0));
      ctl.editDecor(d);
    }
  }
  if (opt.cycles > 0) Ui::EditorPreview::cycle(ctl, opt.cycles);
  const bool ok = Ui::EditorPreview::render(ctl, argv[1], opt);
  ctl.terminate();
  return ok ? 0 : 1;
}
