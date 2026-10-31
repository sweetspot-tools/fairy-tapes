/**
 * Fairy Tapes · Happy Ending — editor behaviour without a window (build target: happy_ending_editor_tests).
 * A real Controller is the host; EditorTest (a friend of Editor) builds the views in a container instead of a frame,
 * runs the UI timer's work by hand and reaches the views; FakePlugFrame stands for the host's IPlugFrame and records the
 * resize requests. Drawing goes to an offscreen context: the pixels checked here are the panel colour, the white box
 * borders, the gap of the box titles on the bottom border and the closed border of the screen; the exact colours and the
 * fonts as the host renders them stay a manual check (tools/render_editor.cpp).
 * The preferences folder is a scratch one ($XDG_CONFIG_HOME), never the user's: an editor made here reads no SIZE unless
 * a test writes one.
 */
#include "controller.h"
#include "params.h"
#include "ui/editor.h"
#include "ui/layout.h"
#include "ui/panel_view.h"
#include "ui/prefs.h"

#include "fairy_ui/controls.h"
#include "fairy_ui/silkscreen.h"
#include "fairy_ui/silkscreen_view.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cfont.h"
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
#include <iterator>
#include <string>
#include <vector>

// The SDK's VSTGUI init hook asks the module for its factory; there is none in this test.
extern "C" Steinberg::IPluginFactory* GetPluginFactory() { return nullptr; }
void* moduleHandle = nullptr;   // normally set by the platform module entry point

namespace FairyTapes::HappyEnding::Ui {

// What the tests may reach inside the editor. Later tasks add what their views need (controls, meters, screen, wear,
// tapes, screws, the back); keep each accessor a plain read of one editor member or one private step.
struct EditorTest {
  // Builds the editor's views in a container, as open() does in the frame.
  static VSTGUI::SharedPointer<VSTGUI::CViewContainer> build(Editor& ed) {
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(root);
    return root;
  }
  // Removes the views, as close() does.
  static void destroy(Editor& ed, VSTGUI::CViewContainer* root) {
    root->removeAll(true);
    ed.resetViewPointers();
  }
  // One run of the UI timer's work.
  static void tick(Editor& ed) { ed.onTimer(); }
  static PanelView* panel(const Editor& ed) { return ed.panel_; }
  // The control view of a parameter; for LISTEN pass the bell (0..2). nullptr when there is none (yet).
  static FairyTapes::Ui::NormalizedControl* control(const Editor& ed, VstParamID id, int choiceIndex = -1) {
    for (int i = 0; i < kNumControls; ++i)
      if (kControls[i].id == id && (choiceIndex < 0 || kControls[i].choiceIndex == choiceIndex))
        return ed.controls_[static_cast<std::size_t>(i)];
    return nullptr;
  }
  // The silkscreen's model (printed names, values, dimming); nullptr while there is no silkscreen view.
  static const FairyTapes::Ui::SilkscreenModel* silk(const Editor& ed) { return ed.silk_ ? &ed.silk_->model() : nullptr; }
  // The appearance from a preferences file instead of the user's (what open() does with prefsPath()).
  static void loadAppearance(Editor& ed, const std::filesystem::path& file) { ed.loadAppearance(file); }
  static const Prefs& prefs(const Editor& ed) { return ed.prefs_; }
  static bool timerRunning(const Editor& ed) { return ed.timer_ != nullptr; }
  // Gives the editor the host's IPlugFrame (nullptr takes it away), as the host does through IPlugView::setFrame.
  static Steinberg::tresult setPlugFrame(Editor& ed, Steinberg::IPlugFrame* frame) {
    return static_cast<Steinberg::IPlugView&>(ed).setFrame(frame);
  }
};

} // namespace FairyTapes::HappyEnding::Ui

using namespace FairyTapes::HappyEnding;
using namespace FairyTapes::HappyEnding::Ui;

namespace {
int g_failed = 0, g_passed = 0;
Controller* g_ctl = nullptr;
std::filesystem::path g_prefsHome;   // the scratch $XDG_CONFIG_HOME

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)
#define RUN_TEST(fn) do { const int before = g_failed; fn(); if (g_failed == before) ++g_passed; } while(0)

constexpr std::size_t kNumPanelTexts = std::size(kPanelTexts);

void setResourcePath(bool grain) {
#if SMTG_OS_LINUX
  if (auto* lf = VSTGUI::getPlatformFactory().asLinuxFactory()) lf->setResourcePath(grain ? HAPPY_ENDING_ASSETS : "/nonexistent/");
#else
  (void)grain;
#endif
}

// The plug-in's preferences file in the scratch folder.
std::filesystem::path prefsFile() { return g_prefsHome / "FairyTapes" / "HappyEnding.prefs"; }
void writePrefs(const std::filesystem::path& file, const std::string& text) {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

// The host's side of the view: records every resize request and accepts or refuses it.
class FakePlugFrame : public Steinberg::IPlugFrame {
public:
  bool accept = true;
  std::vector<Steinberg::ViewRect> requests;
  int refs = 0;
  Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView*, Steinberg::ViewRect* r) override {
    if (r) requests.push_back(*r);
    return accept ? Steinberg::kResultTrue : Steinberg::kResultFalse;
  }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
    QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, Steinberg::IPlugFrame)
    QUERY_INTERFACE(iid, obj, Steinberg::IPlugFrame::iid, Steinberg::IPlugFrame)
    *obj = nullptr;
    return Steinberg::kNoInterface;
  }
  Steinberg::uint32 PLUGIN_API addRef() override { return static_cast<Steinberg::uint32>(++refs); }
  Steinberg::uint32 PLUGIN_API release() override { return static_cast<Steinberg::uint32>(--refs); }
};

double defaultOf(Steinberg::Vst::EditController* ctl, Steinberg::Vst::ParamID id) {
  const auto* p = ctl->getParameterObject(id);
  return p ? p->getInfo().defaultNormalizedValue : 0.0;
}
void resetParams(Steinberg::Vst::EditController* ctl) {
  for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) ctl->setParamNormalized(id, defaultOf(ctl, id));
}

// Draws `v` into a 1280 x 890 offscreen bitmap at scale 1.
VSTGUI::SharedPointer<VSTGUI::CBitmap> drawOffscreen(VSTGUI::CView* v) {
  auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(Editor::kWidth, Editor::kHeight), 1.0);
  if (!ctx) return nullptr;
  ctx->beginDraw();
  v->draw(ctx);
  ctx->endDraw();
  return VSTGUI::SharedPointer<VSTGUI::CBitmap>(ctx->getBitmap());
}

struct Pixels {
  VSTGUI::SharedPointer<VSTGUI::CBitmapPixelAccess> access;
  VSTGUI::CColor at(int x, int y) const {
    VSTGUI::CColor c;
    if (access && access->setPosition(static_cast<uint32_t>(x), static_cast<uint32_t>(y))) access->getColor(c);
    return c;
  }
};
Pixels pixelsOf(VSTGUI::CBitmap* bmp) { return {VSTGUI::owned(VSTGUI::CBitmapPixelAccess::create(bmp, false))}; }
int brightness(const VSTGUI::CColor& c) { return std::max({c.red, c.green, c.blue}); }
// The brightest of the two pixel rows a 2 px line at `y` covers.
int lineBrightness(const Pixels& p, int x, int y) { return std::max(brightness(p.at(x, y - 1)), brightness(p.at(x, y))); }

// Width of a box title as drawTitledBox measures it. Duplicates private details of shared/ui/src/draw_util.cpp
// (drawTitledBox: title font Arial 14 bold): keep in step with it.
double titleWidth(const char* title) {
  auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(8, 8), 1.0);
  if (!ctx) return 0;
  ctx->beginDraw();
  ctx->setFont(VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 14, VSTGUI::kBoldFace));
  const double w = ctx->getStringWidth(title);
  ctx->endDraw();
  return w;
}

// The UiState of the parameters' defaults: everything in but METALHEAD (off by default, params.h).
UiState defaultState() {
  UiState s;
  s.stageOn[static_cast<int>(Stage::Metal)] = false;
  return s;
}
// The opacity of a printed text in a state: the block-tag lettering at 10 %, the rest opaque, times 35 % when dimmed.
float textAlpha(const PanelText& t, const UiState& s) {
  const float base = t.style == TextStyle::Tag ? kTagOpacity : 1.f;
  return panelTextDimmed(t, s) ? base * PanelView::kDimmedAlpha : base;
}

const PanelView::DrawnTag* drawnTag(const PanelView& panel, Box b) {
  for (const auto& t : panel.drawnTags())
    if (t.box == b) return &t;
  return nullptr;
}

// ---- Tests ----------------------------------------------------------------------------------------------------------

// The panel layer is the bottom view of the editor and covers the whole 1280 x 890 panel; clicks go through it. No
// control view exists yet (the bench answers nullptr for every parameter), nor a silkscreen.
void test_panel_layer_built() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  PanelView* panel = EditorTest::panel(ed);
  ASSERT_TRUE(panel != nullptr);
  ASSERT_TRUE(root->getNbViews() >= 1 && root->getView(0) == panel);
  const VSTGUI::CRect r = panel->getViewSize();
  ASSERT_TRUE(r.left == 0 && r.top == 0 && r.getWidth() == kEditorWidth && r.getHeight() == kEditorHeight);
  ASSERT_TRUE(!panel->getMouseEnabled());
  ASSERT_TRUE(EditorTest::control(ed, kBypass) == nullptr);
  ASSERT_TRUE(EditorTest::silk(ed) == nullptr);
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(EditorTest::panel(ed) == nullptr);
}

// Every kPanelTexts string is drawn once, in table order, at its position with its alignment; the block-tag lettering
// ("SHAME ON YOU") at 10 % like the tags, the rest opaque; at the defaults only METALHEAD's text is dimmed (its IN is off).
void test_panel_texts_from_layout() {
  resetParams(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  PanelView* panel = EditorTest::panel(ed);
  ASSERT_TRUE(panel != nullptr);
  ASSERT_TRUE(drawOffscreen(root) != nullptr);
  const auto& drawn = panel->drawnTexts();
  ASSERT_TRUE(drawn.size() == kNumPanelTexts);
  for (std::size_t i = 0; i < kNumPanelTexts; ++i) {
    const PanelText& t = kPanelTexts[i];
    const PanelView::DrawnText& d = drawn[i];
    ASSERT_TRUE(d.text && !std::strcmp(d.text, t.text));
    ASSERT_TRUE(d.width > 0 && d.y == t.y);
    const double anchor = t.align == FairyTapes::Ui::TextAlign::Left    ? d.left
                          : t.align == FairyTapes::Ui::TextAlign::Right ? d.left + d.width
                                                                        : d.left + d.width / 2;
    ASSERT_TRUE(std::fabs(anchor - t.x) < 1e-6);
    ASSERT_TRUE(d.alpha == textAlpha(t, defaultState()));
    ASSERT_TRUE((d.alpha < (t.style == TextStyle::Tag ? kTagOpacity : 1.f)) == (t.stage == Stage::Metal));
  }
  ASSERT_TRUE(panel->drawnLines().size() == std::size(kHeaderLines));
  EditorTest::destroy(ed, root);
}

// The engine tags: one per box that has one (FINE EQ, VIBE EQ, GLUE, CLIP, FINAL), at tagPosition, 10 % opacity; none for
// LIMIT, METALHEAD, the service boxes and the screen. FINAL reads INFL-Z for INFLATE and C-ONLY2 for CLIP, from the
// controller on the next timer run.
void test_engine_tags() {
  resetParams(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  PanelView* panel = EditorTest::panel(ed);
  ASSERT_TRUE(panel != nullptr);
  ASSERT_TRUE(drawOffscreen(root) != nullptr);
  ASSERT_TRUE(panel->drawnTags().size() == 5);
  for (const BoxSpec& b : kBoxes) {
    const PanelView::DrawnTag* t = drawnTag(*panel, b.box);
    const char* want = engineTag(b.box, 0);
    ASSERT_TRUE((t != nullptr) == (want != nullptr));
    if (!t) continue;
    ASSERT_TRUE(!std::strcmp(t->text, want));
    ASSERT_TRUE(t->x == tagPosition(b.box).x && t->y == tagPosition(b.box).y);
    ASSERT_TRUE(t->alpha == kTagOpacity);
  }
  ASSERT_TRUE(!std::strcmp(drawnTag(*panel, Box::Final)->text, "INFL-Z"));
  g_ctl->setParamNormalized(kFinalMode, normFromIndex(kFinalMode, 1));
  drawOffscreen(root);
  const bool unchangedBeforeTick = !std::strcmp(drawnTag(*panel, Box::Final)->text, "INFL-Z");
  EditorTest::tick(ed);
  drawOffscreen(root);
  const bool clip = !std::strcmp(drawnTag(*panel, Box::Final)->text, "C-ONLY2");
  resetParams(g_ctl);
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(unchangedBeforeTick && clip);
}

// BYPASS, a stage IN and a FINE EQ band IN changed on the controller dim (35 %) the printed parts that belong to them on
// the next timer run, as panelTextDimmed / headerLineDimmed / tagDimmed say; the header, LEVEL and LUFS texts never.
void test_tick_applies_controller_state() {
  resetParams(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  PanelView* panel = EditorTest::panel(ed);
  ASSERT_TRUE(panel != nullptr);
  auto alphasMatch = [&](const UiState& s) {
    drawOffscreen(root);
    const auto& texts = panel->drawnTexts();
    if (texts.size() != kNumPanelTexts) return false;
    for (std::size_t i = 0; i < kNumPanelTexts; ++i)
      if (texts[i].alpha != textAlpha(kPanelTexts[i], s)) return false;
    const auto& lines = panel->drawnLines();
    if (lines.size() != std::size(kHeaderLines)) return false;
    for (std::size_t i = 0; i < lines.size(); ++i)
      if (lines[i].alpha != (headerLineDimmed(kHeaderLines[i], s) ? PanelView::kDimmedAlpha : 1.f)) return false;
    for (const auto& t : panel->drawnTags())
      if (t.alpha != (tagDimmed(t.box, s) ? kTagOpacity * PanelView::kDimmedAlpha : kTagOpacity)) return false;
    return true;
  };
  const UiState live = defaultState();
  ASSERT_TRUE(alphasMatch(live));
  g_ctl->setParamNormalized(kBypass, 1.0);
  const bool sameBeforeTick = alphasMatch(live);
  EditorTest::tick(ed);
  UiState bypass = defaultState();
  bypass.bypass = true;
  const bool bypassed = alphasMatch(bypass) && panel->uiState().bypass;
  g_ctl->setParamNormalized(kBypass, 0.0);
  g_ctl->setParamNormalized(kClipOn, 0.0);
  g_ctl->setParamNormalized(kHpOn, 0.0);
  EditorTest::tick(ed);
  UiState parts = defaultState();
  parts.stageOn[static_cast<int>(Stage::Clip)] = false;
  parts.bandOn[0] = false;
  const bool partsDimmed = alphasMatch(parts);
  int dimmedTexts = 0;
  for (const auto& t : panel->drawnTexts()) dimmedTexts += t.alpha < 1.f && std::strcmp(t.text, "SHAME ON YOU") ? 1 : 0;
  resetParams(g_ctl);
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(sameBeforeTick && bypassed && partsDimmed);
  ASSERT_TRUE(dimmedTexts == 3);   // "level", "matched" (CLIP) and "HI PASS" (its band)
}

// Offscreen render of the panel without the grain tile: panel colour in the left margin, the white border of every box,
// the gap of the title on the bottom border (the border goes on outside it) and no gap on the top border; the screen's
// border is closed on both edges.
void test_render_boxes_and_bottom_titles() {
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto bmp = drawOffscreen(root);
  ASSERT_TRUE(bmp != nullptr);
  ASSERT_TRUE(bmp->getWidth() == kEditorWidth && bmp->getHeight() == kEditorHeight);
  const Pixels px = pixelsOf(bmp);
  ASSERT_TRUE(px.access != nullptr);
  const VSTGUI::CColor margin = px.at(10, 420);   // left of FINE EQ: tint #161617 with the soft light
  ASSERT_TRUE(margin.alpha == 255 && brightness(margin) >= 0x08 && brightness(margin) <= 0x30);
  ASSERT_TRUE(std::abs(margin.red - margin.blue) <= 4);
  for (const BoxSpec& b : kBoxes) {
    const int left = static_cast<int>(b.rect.x), midY = static_cast<int>(b.rect.y + b.rect.h / 2);
    const int top = static_cast<int>(b.rect.y), bottom = static_cast<int>(b.rect.y + b.rect.h);
    const int cx = static_cast<int>(b.rect.x + b.rect.w / 2);
    ASSERT_TRUE(lineBrightness(px, left, midY) > 0xc0 || lineBrightness(px, left + 1, midY) > 0xc0);   // left edge
    ASSERT_TRUE(lineBrightness(px, left - 3, midY) < 0x40);   // outside it (the box on the left ends 6 px before)
    ASSERT_TRUE(lineBrightness(px, cx, top) > 0xc0);          // the top border has no gap
    if (!b.title) {   // the screen: closed border, nothing in the middle of the bottom edge
      ASSERT_TRUE(lineBrightness(px, cx, bottom) > 0xc0);
      continue;
    }
    const double half = titleWidth(b.title) / 2;
    ASSERT_TRUE(half > 4);
    // 13 = kTitleGap of shared/ui/src/draw_util.cpp (blank run on each side of a box title): keep in step with it.
    const int inGap = static_cast<int>(cx - half - 13.0 / 2), besideGap = static_cast<int>(cx - half - 13 - 8);
    ASSERT_TRUE(lineBrightness(px, inGap, bottom) < 0x60);       // the blank run before the title
    ASSERT_TRUE(lineBrightness(px, besideGap, bottom) > 0xc0);   // the bottom border goes on outside the gap
    ASSERT_TRUE(lineBrightness(px, besideGap, top) > 0xc0);
  }
  EditorTest::destroy(ed, root);
}

// The texts are really drawn, not only recorded: the white bold italic "HAPPY ENDING" (x 34, baseline 58, 38 px) lights
// many pixels of its block well above the dark panel.
void test_title_text_is_drawn() {
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto bmp = drawOffscreen(root);
  ASSERT_TRUE(bmp != nullptr);
  const Pixels px = pixelsOf(bmp);
  const PanelText& title = kPanelTexts[0];
  ASSERT_TRUE(!std::strcmp(title.text, "HAPPY ENDING"));
  int bright = 0, dark = 0;
  for (int y = static_cast<int>(title.y) - 24; y <= static_cast<int>(title.y) - 2; ++y)
    for (int x = static_cast<int>(title.x); x <= static_cast<int>(title.x) + 260; ++x) {
      const int b = brightness(px.at(x, y));
      bright += b > 0xc0 ? 1 : 0;
      dark += b < 0x40 ? 1 : 0;
    }
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(bright > 300);   // letter strokes
  ASSERT_TRUE(dark > 300);     // and the panel between them
}

// The grain tile is drawn when the asset is found (pixels of a blank strip vary) and the panel is plain when it is
// missing (the soft light is vertical only: the strip is one colour).
void test_panel_grain_when_asset_found() {
  auto spread = [](bool grain) {
    setResourcePath(grain);
    Editor ed(g_ctl);
    auto root = EditorTest::build(ed);
    auto bmp = drawOffscreen(root);
    int lo = 255, hi = 0;
    if (bmp) {
      const Pixels px = pixelsOf(bmp);
      for (int x = 6; x <= 20; ++x)
        for (int y = 400; y <= 440; y += 10) {
          const int v = px.at(x, y).red;
          lo = std::min(lo, v);
          hi = std::max(hi, v);
        }
    }
    EditorTest::destroy(ed, root);
    return bmp ? hi - lo : -1;
  };
  const int plain = spread(false), grain = spread(true);
  setResourcePath(false);
  ASSERT_TRUE(plain >= 0 && plain <= 2);
  ASSERT_TRUE(grain > plain + 2);
}

// The window is 1280 x 890 x the host factor x the SIZE of the preferences: SIZE 90 gives 1152 x 801 (890 x 0.9 = 801),
// times a host factor of 2 gives 2304 x 1602; the size constraint answers only that size. A missing file gives SIZE 100.
void test_window_size_from_prefs_and_host_factor() {
  const auto file = g_prefsHome / "size90" / "HappyEnding.prefs";
  writePrefs(file, "size=90\n");
  Editor ed(g_ctl);
  EditorTest::loadAppearance(ed, file);
  ASSERT_TRUE(EditorTest::prefs(ed).size == EditorSize::S90);
  Steinberg::ViewRect r{};
  ASSERT_TRUE(ed.getSize(&r) == Steinberg::kResultOk);
  ASSERT_TRUE(r.getWidth() == 1152 && r.getHeight() == 801);
  ASSERT_TRUE(ed.setContentScaleFactor(2.0f) == Steinberg::kResultTrue);
  ASSERT_TRUE(ed.getSize(&r) == Steinberg::kResultOk);
  ASSERT_TRUE(r.getWidth() == 2304 && r.getHeight() == 1602);
  Steinberg::ViewRect asked(0, 0, 1280, 890);
  ASSERT_TRUE(ed.checkSizeConstraint(&asked) == Steinberg::kResultTrue);
  ASSERT_TRUE(asked.getWidth() == 2304 && asked.getHeight() == 1602);
  EditorTest::loadAppearance(ed, g_prefsHome / "missing" / "HappyEnding.prefs");
  ASSERT_TRUE(EditorTest::prefs(ed).size == EditorSize::S100);
  ASSERT_TRUE(ed.getSize(&r) == Steinberg::kResultOk);
  ASSERT_TRUE(r.getWidth() == 2560 && r.getHeight() == 1780);
}

// The editor made through the controller reads the SIZE of the user's preferences file (here the scratch one).
void test_created_view_reads_size() {
  writePrefs(prefsFile(), "size=125\n");
  Steinberg::IPlugView* view = g_ctl->createView(Steinberg::Vst::ViewType::kEditor);
  std::filesystem::remove(prefsFile());
  ASSERT_TRUE(view != nullptr);
  Steinberg::ViewRect r{};
  const bool ok = view->getSize(&r) == Steinberg::kResultOk;
  const int refs = static_cast<int>(view->release());
  ASSERT_TRUE(ok && r.getWidth() == 1600 && r.getHeight() == 1113);   // 890 x 1.25 = 1112.5, rounded
  ASSERT_TRUE(refs == 0);
}

// A new host factor while the views are shown asks the host (IPlugFrame::resizeView) once for the new window; before the
// views exist only the reported size changes (the host reads it when it opens the window). The same factor asks nothing.
void test_content_scale_asks_host_when_shown() {
  FakePlugFrame host;
  {
    Editor ed(g_ctl);
    ASSERT_TRUE(EditorTest::setPlugFrame(ed, &host) == Steinberg::kResultTrue);
    ASSERT_TRUE(ed.setContentScaleFactor(1.5f) == Steinberg::kResultTrue);
    ASSERT_TRUE(host.requests.empty());
    auto root = EditorTest::build(ed);
    ASSERT_TRUE(ed.setContentScaleFactor(2.0f) == Steinberg::kResultTrue);
    ASSERT_TRUE(ed.setContentScaleFactor(2.0f) == Steinberg::kResultTrue);
    const bool one = host.requests.size() == 1;
    const bool size = one && host.requests[0].getWidth() == 2560 && host.requests[0].getHeight() == 1780;
    EditorTest::destroy(ed, root);
    ASSERT_TRUE(one && size);
    ASSERT_TRUE(EditorTest::setPlugFrame(ed, nullptr) == Steinberg::kResultTrue);
  }
  ASSERT_TRUE(host.refs == 0);
}

// Built, ticked and removed 50 times through the controller's view: the panel layer is let go by its container every
// time, the timer never runs without a window, and the view goes away with the host's release.
void test_editor_build_destroy_repeatedly() {
  for (int i = 0; i < 50; ++i) {
    Steinberg::IPlugView* view = g_ctl->createView(Steinberg::Vst::ViewType::kEditor);
    ASSERT_TRUE(view != nullptr);
    auto* ed = dynamic_cast<Editor*>(view);
    ASSERT_TRUE(ed != nullptr);
    auto root = EditorTest::build(*ed);
    PanelView* panel = EditorTest::panel(*ed);
    ASSERT_TRUE(panel != nullptr);
    panel->remember();
    EditorTest::tick(*ed);
    g_ctl->setParamNormalized(kBypass, (i & 1) ? 1.0 : 0.0);
    EditorTest::tick(*ed);
    ASSERT_TRUE(!EditorTest::timerRunning(*ed));
    EditorTest::destroy(*ed, root);
    EditorTest::tick(*ed);   // safe with no views
    const auto left = panel->getNbReference();
    panel->forget();
    ASSERT_TRUE(left == 1);   // only the test's reference: the container let it go
    ASSERT_TRUE(root->getNbReference() == 1);
    ASSERT_TRUE(view->release() == 0);
  }
  g_ctl->setParamNormalized(kBypass, 0.0);
}

} // namespace

int main() {
  // The editor reads its SIZE from the user's preferences: a scratch folder instead of the real one.
  char scratch[] = "/tmp/happy_ending_editor_XXXXXX";
  if (!mkdtemp(scratch)) {
    std::fprintf(stderr, "mkdtemp failed\n");
    return 1;
  }
  g_prefsHome = scratch;
  setenv("XDG_CONFIG_HOME", scratch, 1);

  VSTGUI::init(nullptr);
  setResourcePath(false);
  int failed = 0, passed = 0;
  {
    Controller ctl;
    ctl.initialize(nullptr);
    g_ctl = &ctl;
    RUN_TEST(test_panel_layer_built);
    RUN_TEST(test_panel_texts_from_layout);
    RUN_TEST(test_engine_tags);
    RUN_TEST(test_tick_applies_controller_state);
    RUN_TEST(test_render_boxes_and_bottom_titles);
    RUN_TEST(test_title_text_is_drawn);
    RUN_TEST(test_panel_grain_when_asset_found);
    RUN_TEST(test_window_size_from_prefs_and_host_factor);
    RUN_TEST(test_created_view_reads_size);
    RUN_TEST(test_content_scale_asks_host_when_shown);
    RUN_TEST(test_editor_build_destroy_repeatedly);
    g_ctl = nullptr;
    ctl.terminate();
    failed = g_failed;
    passed = g_passed;
  }
  VSTGUI::exit();
  std::error_code ec;
  std::filesystem::remove_all(scratch, ec);
  std::printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
