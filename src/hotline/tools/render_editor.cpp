// Developer tool: draws the Hotline editor offscreen into a PNG, no host or window needed.
//   hotline_render out.png [bypass] [characterin=off] [device=<0..5>] [mono] [meters] [clips] [no-grain]
//                  [wear=off|light|heavy] [seed=<N>] [tape<slot>=<text>] [screw<i>=<degrees>] [tapefont=<0..2>]
//                  [back=settings|devices|routing|credits] [flip=<0..1>] [set=<key>:<index>]
// set= applies a SETTINGS choice (1 = WEAR 0..2, 2 = TAPE FONT 0..2) as a click on the back would, which also WRITES the
// user's preferences file: point XDG_CONFIG_HOME at a scratch folder when using it.
// back= draws the back of the panel on that tab instead of the front; flip=<p> draws the turn from the front to the back
// (on the tab given by back=, SETTINGS by default) at progress p.
// The wear level and tape font come from the options (default Light, Zeyada), never from the user's preferences file; the
// seed, screw rotations and tape texts go through the controller's decor, as an editor edit would.
// Used to check the look of the code-drawn GUI against docs/mockups/hotline_rack.html on a machine without a host.
#include "controller.h"
#include "state.h"
#include "ui/back_settings.h"
#include "ui/editor.h"
#include "ui/tapes.h"

#include "fairy_ui/back_view.h"
#include "fairy_ui/flip_view.h"

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
  struct Options {
    int back = -1;       // tab of the back to draw (index into the tabs), -1: the front
    double flip = -1;    // >= 0: the turn from the front to the back at this progress
    int setKey = 0, setIndex = 0;   // set=<key>:<index>: a choice made on the SETTINGS tab (writes the preferences file!)
  };

  static VSTGUI::SharedPointer<VSTGUI::CBitmap> snap(VSTGUI::CView* v) {
    auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(Editor::kWidth, Editor::kHeight), 1.0);
    if (!ctx) return nullptr;
    ctx->beginDraw();
    v->draw(ctx);
    ctx->endDraw();
    return VSTGUI::SharedPointer<VSTGUI::CBitmap>(ctx->getBitmap());
  }

  static bool render(Steinberg::Vst::EditController& ctl, const char* path, FairyTapes::Ui::WearLevel wear,
                     FairyTapes::Ui::TapeFontId font, const Options& opt) {
    Editor ed(&ctl);
    ed.wearLevel_ = wear;
    ed.tapeFont_ = font;
    ed.prefs_.wear = wear;   // what the SETTINGS tab shows
    ed.prefs_.tapeFont = font == FairyTapes::Ui::TapeFontId::RockSalt        ? TapeFont::RockSalt
                         : font == FairyTapes::Ui::TapeFontId::HomemadeApple ? TapeFont::HomemadeApple
                                                                             : TapeFont::Zeyada;
    ed.refreshDecor();   // seed, screws and tape texts of the controller's decor (open() does the same)
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    auto* front = new VSTGUI::CViewContainer(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(front);
    root->addView(front);
    ed.buildBack(root);
    for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) ed.syncFromController(id);
    for (int i = 0; i < 4; ++i) ed.updateDisplays();   // the LED rows show the display parameters
    if (opt.setKey) {   // the same call as a click on the back: stored in the preferences, applied live
      ed.changeSetting(opt.setKey, opt.setIndex);
      std::printf("set %d:%d -> wear %d, tape font %d, prefs file %s\n", opt.setKey, opt.setIndex,
                  static_cast<int>(ed.wearLevel()), static_cast<int>(ed.tapeFont()), prefsPath().string().c_str());
    }
    if (ed.back_) ed.back_->setTab(opt.back < 0 ? 0 : opt.back);
    auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(Editor::kWidth, Editor::kHeight), 1.0);
    if (!ctx) return false;
    ctx->beginDraw();
    if (opt.flip >= 0 && ed.back_) {
      auto turn = VSTGUI::makeOwned<FairyTapes::Ui::FlipView>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight), snap(front),
                                                             snap(ed.back_));
      turn->setProgress(opt.flip);
      turn->draw(ctx);
    } else if (opt.back >= 0 && ed.back_) {
      ed.back_->draw(ctx);
    } else {
      root->draw(ctx);   // the back is hidden: only the front draws
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

} // namespace FairyTapes::Hotline::Ui

int main(int argc, char** argv) {
  using namespace FairyTapes::Hotline;
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: %s out.png [bypass] [characterin=off] [device=<0..5>] [mono] [meters] [clips] [no-grain]\n"
                 "       [wear=off|light|heavy] [seed=<N>] [tape<slot>=<text>] [screw<i>=<degrees>] [tapefont=<0..2>]\n"
                 "       [back=settings|devices|routing|credits] [flip=<0..1>] [set=<key>:<index>]\n",
                 argv[0]);
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
  FairyTapes::Ui::WearLevel wear = FairyTapes::Ui::WearLevel::Light;
  FairyTapes::Ui::TapeFontId font = FairyTapes::Ui::TapeFontId::Zeyada;
  Decor decor = ctl.decor();
  for (int i = 2; i < argc; ++i) {
    const char* a = argv[i];
    if (!std::strcmp(a, "wear=off")) wear = FairyTapes::Ui::WearLevel::Off;
    else if (!std::strcmp(a, "wear=light")) wear = FairyTapes::Ui::WearLevel::Light;
    else if (!std::strcmp(a, "wear=heavy")) wear = FairyTapes::Ui::WearLevel::Heavy;
    else if (!std::strncmp(a, "seed=", 5)) decor.seed = static_cast<uint32_t>(std::strtoul(a + 5, nullptr, 0));
    else if (!std::strncmp(a, "tapefont=", 9)) {
      const int f = std::atoi(a + 9);
      font = f == 1 ? FairyTapes::Ui::TapeFontId::RockSalt : f == 2 ? FairyTapes::Ui::TapeFontId::HomemadeApple
                                                                    : FairyTapes::Ui::TapeFontId::Zeyada;
    } else if (!std::strncmp(a, "tape", 4) && a[4] >= '0' && a[4] <= '9' && a[5] == '=')
      decor = Ui::withTapeText(decor, a[4] - '0', a + 6);
    else if (!std::strncmp(a, "screw", 5) && a[5] >= '0' && a[5] <= '3' && a[6] == '=')
      decor.screwAngles[a[5] - '0'] = static_cast<float>(std::atof(a + 7));
  }
  Ui::EditorPreview::Options opt;
  for (int i = 2; i < argc; ++i) {
    const char* a = argv[i];
    if (!std::strcmp(a, "back=settings")) opt.back = 0;
    else if (!std::strcmp(a, "back=devices")) opt.back = Ui::kDevicesTab;
    else if (!std::strcmp(a, "back=routing")) opt.back = 2;
    else if (!std::strcmp(a, "back=credits")) opt.back = 3;
    else if (!std::strncmp(a, "flip=", 5)) opt.flip = std::atof(a + 5);
    else if (!std::strncmp(a, "set=", 4)) std::sscanf(a + 4, "%d:%d", &opt.setKey, &opt.setIndex);
  }
  ctl.editDecor(decor);
  const bool ok = Ui::EditorPreview::render(ctl, argv[1], wear, font, opt);
  ctl.terminate();
  return ok ? 0 : 1;
}
