/**
 * Fairy Tapes · Happy Ending — editor behaviour without a window (build target: happy_ending_editor_tests).
 * A real Controller is the host; EditorTest (a friend of Editor) builds the views in a container instead of a frame,
 * runs the UI timer's work by hand and reaches the views; FakePlugFrame stands for the host's IPlugFrame and records the
 * resize requests. Drawing goes to an offscreen context: the pixels checked here are the panel colour, the white box
 * borders, the gap of the box titles on the bottom border and the closed border of the screen; the exact colours and the
 * fonts as the host renders them stay a manual check (tools/render_editor.cpp).
 * The preferences folder is a scratch one ($XDG_CONFIG_HOME), never the user's: an editor made here reads no SIZE unless
 * a test writes one. CountingHandler stands for the host's IComponentHandler and counts every beginEdit / performEdit /
 * endEdit the editor sends through the controller.
 */
#include "controller.h"
#include "params.h"
#include "ui/back_layout.h"
#include "ui/back_settings.h"
#include "ui/buttons_text.h"
#include "ui/credits.h"
#include "ui/editor.h"
#include "ui/layout.h"
#include "display.h"
#include "ui/level_views.h"
#include "ui/panel_view.h"
#include "ui/prefs.h"
#include "ui/readouts.h"
#include "ui/routing_diagram.h"
#include "ui/screen_model.h"
#include "ui/screen_view.h"
#include "ui/screw_angles.h"
#include "ui/tape_view.h"
#include "ui/tapes.h"
#include "ui/value_text.h"

#include "fairy_ui/back_view.h"
#include "fairy_ui/back_widgets.h"
#include "fairy_ui/controls.h"
#include "fairy_ui/flip_view.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/silkscreen.h"
#include "fairy_ui/silkscreen_view.h"
#include "fairy_ui/screw_view.h"
#include "fairy_ui/draw_util.h"
#include "fairy_ui/tape_draw.h"
#include "fairy_ui/tape_font.h"
#include "fairy_ui/wear.h"
#include "fairy_ui/wear_view.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/vstguiinit.h"
#if SMTG_OS_LINUX
#include "vstgui/lib/platform/linux/linuxfactory.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
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
  // Removes the views, as close() does (the parameters are no longer watched; a typed value left open is dropped). The
  // session parameters are not switched off here: close() itself does that (test_close_turns_listen_and_set_off).
  static void destroy(Editor& ed, VSTGUI::CViewContainer* root) {
    ed.unwatchParameters();
    ed.closeValueEdit(false);
    ed.closeTapeEdit(false);
    ed.flip_.reset();
    root->removeAll(true);
    ed.resetViewPointers();
  }
  // Both sides of the panel in a container, as open() does in the frame: the front (a container with the views of build()),
  // the hidden back and the turn between them.
  static VSTGUI::SharedPointer<VSTGUI::CViewContainer> buildSides(Editor& ed) {
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createSides(root);
    return root;
  }
  static VSTGUI::CViewContainer* front(const Editor& ed) { return ed.front_; }
  static FairyTapes::Ui::BackView* back(const Editor& ed) { return ed.back_; }
  static FairyTapes::Ui::TextLink* backLink(const Editor& ed) { return ed.backLink_; }
  static FairyTapes::Ui::PanelFlip& flip(Editor& ed) { return ed.flip_; }
  static VSTGUI::CView* oversamplingNote(const Editor& ed) { return ed.oversamplingNote_; }
  // The size the window has now (the preference may be ahead of it when the host refused).
  static EditorSize appliedSize(const Editor& ed) { return ed.size_; }
  // A frame that was never opened (no window): the editor zooms it as it zooms the real one.
  static void setFrame(Editor& ed, VSTGUI::CFrame* frame) { ed.frame = frame; }
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
  // The view of kControls[i].
  static FairyTapes::Ui::NormalizedControl* controlAt(const Editor& ed, int i) { return ed.controls_[static_cast<std::size_t>(i)]; }
  // MATCH and the column of the screen view buttons (one exclusive choice).
  static FairyTapes::Ui::NormalizedControl* match(const Editor& ed) { return ed.match_; }
  static FairyTapes::Ui::NormalizedControl* views(const Editor& ed) { return ed.views_; }
  // LEVEL: the peak rows (IN L, IN R, OUT L, OUT R), the GR rows (G C L F M), the clip LEDs (IN ... OUT), the GR tags, the
  // LUFS screen and the ACT LEDs (LOW, AIR); nullptr while the views do not exist.
  static LevelBar* meter(const Editor& ed, int i) { return ed.meters_[static_cast<std::size_t>(i)]; }
  static LevelBar* grBar(const Editor& ed, int i) { return ed.grBars_[static_cast<std::size_t>(i)]; }
  static FairyTapes::Ui::ClipLed* clip(const Editor& ed, int i) { return ed.clips_[static_cast<std::size_t>(i)]; }
  static GrTags* grTags(const Editor& ed) { return ed.grTags_; }
  static LufsView* lufs(const Editor& ed) { return ed.lufs_; }
  static ActLed* act(const Editor& ed, int i) { return ed.actLeds_[static_cast<std::size_t>(i)]; }
  // The shared screen (nullptr while the views do not exist).
  static ScreenDisplayView* screen(const Editor& ed) { return ed.screen_; }
  // Wear, tapes and screws (Task 12): the wear layer, the tape views (layoutFromSeed order), the screw views (kScrewPos order;
  // nullptr while the decor is not ready), the open tape text field, and the fonts as applied.
  static FairyTapes::Ui::WearView* wear(const Editor& ed) { return ed.wear_; }
  static const std::vector<TapeView*>& tapes(const Editor& ed) { return ed.tapes_; }
  static FairyTapes::Ui::ScrewView* screw(const Editor& ed, int i) { return ed.screws_[static_cast<std::size_t>(i)]; }
  static VSTGUI::CTextEdit* tapeEdit(const Editor& ed) { return ed.tapeEdit_; }
  static void closeTapeEdit(Editor& ed, bool confirm) { ed.closeTapeEdit(confirm); }
  static FairyTapes::Ui::TapeFontId valuesFont(const Editor& ed) { return ed.valuesFont_; }
  static FairyTapes::Ui::SilkscreenView* silkView(const Editor& ed) { return ed.silk_; }
  // The text field of a typed value (nullptr when none is open) and its closing (confirm: what was typed is kept).
  static VSTGUI::CTextEdit* valueEdit(const Editor& ed) { return ed.valueEdit_; }
  static void closeValueEdit(Editor& ed, bool confirm) { ed.closeValueEdit(confirm); }
  // The silkscreen's model (printed names, values, dimming); nullptr while there is no silkscreen view.
  static const FairyTapes::Ui::SilkscreenModel* silk(const Editor& ed) { return ed.silk_ ? &ed.silk_->model() : nullptr; }
  // The printed value of the first silkscreen item with key `id` ("" when none).
  static std::string silkValue(const Editor& ed, int id) {
    if (const auto* m = silk(ed))
      for (std::size_t i = 0; i < m->size(); ++i)
        if (m->item(i).id == id) return m->value(i);
    return {};
  }
  // Whether the silkscreen has items with key `id` and all of them are dimmed (hidden).
  static bool silkDimmed(const Editor& ed, int id) { return silkAll(ed, id, [](const auto& m, std::size_t i) { return m.dimmed(i); }); }
  static bool silkHidden(const Editor& ed, int id) { return silkAll(ed, id, [](const auto& m, std::size_t i) { return m.hidden(i); }); }
  template <typename F>
  static bool silkAll(const Editor& ed, int id, F&& f) {
    const auto* m = silk(ed);
    int n = 0;
    if (m)
      for (std::size_t i = 0; i < m->size(); ++i)
        if (m->item(i).id == id) {
          if (!f(*m, i)) return false;
          ++n;
        }
    return n > 0;
  }
  // The appearance from a preferences file instead of the user's (what open() does with prefsPath()).
  static void loadAppearance(Editor& ed, const std::filesystem::path& file) { ed.loadAppearance(file); }
  static const Prefs& prefs(const Editor& ed) { return ed.prefs_; }
  static bool timerRunning(const Editor& ed) { return ed.timer_ != nullptr; }
  // The UI timer as open() starts it (a test has no window, so no run loop: it never fires here).
  static void startTimer(Editor& ed) { ed.startTimer(); }
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

bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

// The host side of the controller: counts what the editor sends (beginEdit / performEdit / endEdit per parameter).
class CountingHandler : public Steinberg::Vst::IComponentHandler {
public:
  std::map<Steinberg::Vst::ParamID, int> begins, performs, ends;
  std::map<Steinberg::Vst::ParamID, double> last;
  int latencyRestarts = 0;   // restartComponent calls that asked the host to read the latency again

  void clear() {
    latencyRestarts = 0;
    begins.clear();
    performs.clear();
    ends.clear();
    last.clear();
  }
  int total() const {
    int n = 0;
    for (const auto* m : {&begins, &performs, &ends})
      for (const auto& kv : *m) n += kv.second;
    return n;
  }
  // Exactly one beginEdit, performEdit and endEdit of `id`.
  bool once(Steinberg::Vst::ParamID id) const { return count(begins, id) == 1 && count(performs, id) == 1 && count(ends, id) == 1; }
  static int count(const std::map<Steinberg::Vst::ParamID, int>& m, Steinberg::Vst::ParamID id) {
    const auto it = m.find(id);
    return it == m.end() ? 0 : it->second;
  }
  Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override {
    ++begins[id];
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue v) override {
    ++performs[id];
    last[id] = v;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override {
    ++ends[id];
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override {
    if (flags & Steinberg::Vst::kLatencyChanged) ++latencyRestarts;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
    QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, Steinberg::Vst::IComponentHandler)
    QUERY_INTERFACE(iid, obj, Steinberg::Vst::IComponentHandler::iid, Steinberg::Vst::IComponentHandler)
    *obj = nullptr;
    return Steinberg::kNoInterface;
  }
  Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
  Steinberg::uint32 PLUGIN_API release() override { return 1000; }
};
CountingHandler g_host;

// The stored table back to its defaults, LISTEN and SET off (host side: no edit is sent).
void resetAll(Steinberg::Vst::EditController* ctl) {
  resetParams(ctl);
  ctl->setParamNormalized(kListenBand, kDefaultListenNorm);
  ctl->setParamNormalized(kSetActive, kDefaultSetNorm);
}
void normsOf(Steinberg::Vst::EditController* ctl, double* n) {
  for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) n[id] = ctl->getParamNormalized(id);
}
// The UiState of the controller's parameters.
UiState stateOf(Steinberg::Vst::EditController* ctl) {
  double n[kNumParams];
  normsOf(ctl, n);
  return uiStateFrom(n, listenFromNorm(ctl->getParamNormalized(kListenBand)), ctl->getParamNormalized(kSetActive) >= 0.5);
}
// The value printed under a knob or a cycling selector: formatValue, but the real ms of LIMIT RELEASE (releaseText) and
// the siglas of the M/S selectors.
std::string expectedValue(Steinberg::Vst::EditController* ctl, Steinberg::Vst::ParamID id) {
  double n[kNumParams];
  normsOf(ctl, n);
  if (id == kLimitRelease) return releaseText(indexFromNorm(kLimitStyle, n[kLimitStyle]), indexFromNorm(kLimitRelease, n[kLimitRelease]));
  if (id == kHpMs || id == kLsMs || id == kLmMs || id == kMidMs || id == kHmMs || id == kHsMs) {
    static const char* const siglas[3] = {"ST", "MID", "SIDE"};
    return siglas[indexFromNorm(static_cast<ParamID>(id), n[id])];
  }
  return formatValue(static_cast<ParamID>(id), n[id], n);
}
int indexOf(VstParamID id, int choiceIndex = -1) {
  for (int i = 0; i < kNumControls; ++i)
    if (kControls[i].id == id && kControls[i].choiceIndex == choiceIndex) return i;
  return -1;
}
// Every control, its printed parts, MATCH and the view buttons as the UiState says: dimmed, locked (no clicks), shown;
// the LISTEN LEDs show the bell really heard (listenedBand).
bool controlsMatchState(const Editor& ed, const UiState& s) {
  for (int i = 0; i < kNumControls; ++i) {
    const ControlSpec& c = kControls[i];
    const FairyTapes::Ui::NormalizedControl* v = EditorTest::controlAt(ed, i);
    if (!v) return false;
    const bool dimmed = controlDimmed(c, s), locked = controlLocked(c, s), visible = controlVisible(c, s);
    bool ok = v->isDimmed() == dimmed && v->getMouseEnabled() == !locked && v->isVisible() == visible;
    if (c.kind != Kind::LitChoice && c.kind != Kind::HeadToggle)
      ok = ok && EditorTest::silkDimmed(ed, printId(c)) == dimmed && EditorTest::silkHidden(ed, printId(c)) == !visible;
    if (c.id == kListenBand) ok = ok && near(v->getValueNormalized(), listenedBand(s) == c.choiceIndex + 2 ? 1.0 : 0.0);
    if (!ok) {
      std::fprintf(stderr, "  control %d (param %u) does not follow the state\n", i, static_cast<unsigned>(c.id));
      return false;
    }
  }
  for (const auto* v : {EditorTest::match(ed), EditorTest::views(ed)})
    if (!v || v->isDimmed() != s.bypass || v->getMouseEnabled() == s.bypass) return false;
  return EditorTest::silkDimmed(ed, actionPrintId(Action::Match)) == s.bypass;
}
VSTGUI::CButtonState leftClick() { return VSTGUI::CButtonState(VSTGUI::kLButton); }
void clickAt(FairyTapes::Ui::NormalizedControl* v, Point p) {
  VSTGUI::CPoint at(p.x, p.y);
  v->onMouseDown(at, leftClick());
  v->onMouseUp(at, leftClick());
}

const PanelView::DrawnTag* drawnTag(const PanelView& panel, Box b) {
  for (const auto& t : panel.drawnTags())
    if (t.box == b) return &t;
  return nullptr;
}

// ---- Tests ----------------------------------------------------------------------------------------------------------

// The panel layer is the bottom view of the editor and covers the whole 1280 x 890 panel; clicks go through it. The
// silkscreen lies right over it and every control of kControls has its view (above both), MATCH and the view buttons too.
void test_panel_layer_built() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  PanelView* panel = EditorTest::panel(ed);
  ASSERT_TRUE(panel != nullptr);
  ASSERT_TRUE(root->getNbViews() >= 1 && root->getView(0) == panel);
  const VSTGUI::CRect r = panel->getViewSize();
  ASSERT_TRUE(r.left == 0 && r.top == 0 && r.getWidth() == kEditorWidth && r.getHeight() == kEditorHeight);
  ASSERT_TRUE(!panel->getMouseEnabled());
  ASSERT_TRUE(EditorTest::silk(ed) != nullptr && root->getView(1) != nullptr && !root->getView(1)->getMouseEnabled());
  for (int i = 0; i < kNumControls; ++i) {
    FairyTapes::Ui::NormalizedControl* v = EditorTest::controlAt(ed, i);
    ASSERT_TRUE(v != nullptr && root->isChild(v));
    const Rect want = viewRect(kControls[i]);
    const VSTGUI::CRect r = v->getViewSize();
    ASSERT_TRUE(r.left == want.x && r.top == want.y && r.getWidth() == want.w && r.getHeight() == want.h);
    const bool kindOk = kControls[i].kind == Kind::Knob        ? dynamic_cast<FairyTapes::Ui::RackKnob*>(v) != nullptr
                        : kControls[i].kind == Kind::Cycle     ? dynamic_cast<FairyTapes::Ui::RackCycle*>(v) != nullptr
                        : kControls[i].kind == Kind::LitChoice ? dynamic_cast<FairyTapes::Ui::RackLitChoice*>(v) != nullptr
                        : kControls[i].kind == Kind::Toggle    ? dynamic_cast<FairyTapes::Ui::RackToggle*>(v) != nullptr
                                                               : dynamic_cast<FairyTapes::Ui::ToggleButton*>(v) != nullptr;
    ASSERT_TRUE(kindOk);
  }
  ASSERT_TRUE(EditorTest::match(ed) != nullptr && EditorTest::views(ed) != nullptr);
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(EditorTest::panel(ed) == nullptr && EditorTest::controlAt(ed, 0) == nullptr && EditorTest::silk(ed) == nullptr);
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


// A host change (automation, preset, undo) on any parameter with a front control, LISTEN and SET included, reaches its
// control on the next timer run and not before (update() only flags), with its printed value; nothing goes back to the
// host. A stepped value off the grid (an automation ramp) shows the step the DSP uses (indexFromNorm).
void test_editor_controls_follow_host() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  g_host.clear();
  for (int i = 0; i < kNumControls; ++i) {
    const ControlSpec& c = kControls[i];
    resetAll(g_ctl);
    EditorTest::tick(ed);
    FairyTapes::Ui::NormalizedControl* v = EditorTest::controlAt(ed, i);
    ASSERT_TRUE(v != nullptr);
    double target = 1.0, want = 1.0;
    if (c.id == kListenBand) {
      target = normFromListen(c.choiceIndex);
    } else if (c.id != kSetActive) {
      const auto id = static_cast<ParamID>(c.id);
      const int steps = kParams[id].steps, def = kParams[id].defaultIndex;
      const int idx = c.kind == Kind::Knob ? (def < steps / 2 ? steps - 1 : 0) : (def + 1) % steps;
      target = want = normFromIndex(id, idx);
    }
    const float before = v->getValueNormalized();
    ASSERT_TRUE(!near(before, want));
    g_ctl->setParamNormalized(c.id, target);
    ASSERT_TRUE(v->getValueNormalized() == before);   // only flagged: the timer applies it
    EditorTest::tick(ed);
    if (!near(v->getValueNormalized(), want)) std::fprintf(stderr, "  control %d (param %u) did not follow\n", i, (unsigned)c.id);
    ASSERT_TRUE(near(v->getValueNormalized(), want));
    if (c.kind == Kind::Knob || c.kind == Kind::Cycle) ASSERT_TRUE(EditorTest::silkValue(ed, printId(c)) == expectedValue(g_ctl, c.id));
  }
  // off the grid: GLUE THRESH 0.52 is step 10 of 21 (floor(0.52 * 21)), drawn at 10 / 20
  resetAll(g_ctl);
  g_ctl->setParamNormalized(kGlueThresh, 0.52);
  EditorTest::tick(ed);
  ASSERT_TRUE(near(EditorTest::control(ed, kGlueThresh)->getValueNormalized(), 0.5));
  ASSERT_TRUE(EditorTest::silkValue(ed, kGlueThresh) == "-10 dB");
  ASSERT_TRUE(g_host.total() == 0);   // no echo
  resetAll(g_ctl);
  EditorTest::tick(ed);
  ASSERT_TRUE(controlsMatchState(ed, stateOf(g_ctl)));
  ASSERT_TRUE(g_host.total() == 0);
  g_ctl->setComponentHandler(nullptr);
  EditorTest::destroy(ed, root);
}

// A user gesture (begin, one value, end) reaches the host as exactly one beginEdit / performEdit / endEdit, on the grid
// of the parameter's steps, and the timer runs that follow do not send it back: a knob, an on/off button, a cycling
// selector, a face-lit choice, DYN and SET.
void test_user_edit_reaches_host_once() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  struct Case {
    VstParamID id;
    float value;   // what the control reports
    double grid;   // what the host gets
  };
  const Case cases[] = {
      {kInputGain, 0.3f, normFromIndex(kInputGain, indexFromNorm(kInputGain, 0.3f))},
      {kGlueWarm, 1.f, 1.0},
      {kGlueRatio, 1.f, 1.0},
      {kLimitStyle, 0.5f, 0.5},
      {kDynLow, 1.f, 1.0},
      {kSetActive, 1.f, 1.0},
  };
  for (const Case& k : cases) {
    g_host.clear();
    FairyTapes::Ui::NormalizedControl* v = EditorTest::control(ed, k.id);
    ASSERT_TRUE(v != nullptr);
    v->beginEdit();
    v->setValueNormalized(k.value);
    v->valueChanged();
    v->endEdit();
    if (!g_host.once(k.id)) std::fprintf(stderr, "  param %u: not one edit\n", (unsigned)k.id);
    ASSERT_TRUE(g_host.once(k.id) && g_host.total() == 3);
    ASSERT_TRUE(near(g_host.last[k.id], k.grid) && near(g_ctl->getParamNormalized(k.id), k.grid));
    for (int t = 0; t < 3; ++t) EditorTest::tick(ed);
    ASSERT_TRUE(g_host.total() == 3);   // no echo
    ASSERT_TRUE(near(v->getValueNormalized(), k.grid));
  }
  ASSERT_TRUE(EditorTest::silkValue(ed, kInputGain) == expectedValue(g_ctl, kInputGain) && EditorTest::silkValue(ed, kInputGain) == "-5.0 dB");
  g_ctl->setComponentHandler(nullptr);
  resetAll(g_ctl);
  EditorTest::destroy(ed, root);
}

// A double click on a knob brings it back to its default as one undoable edit, with its printed value.
void test_double_click_resets_default() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  int knobs = 0;
  for (const ControlSpec& c : kControls) {
    if (c.kind != Kind::Knob) continue;
    ++knobs;
    auto* knob = dynamic_cast<FairyTapes::Ui::RackKnob*>(EditorTest::control(ed, c.id));
    ASSERT_TRUE(knob != nullptr);
    const double def = defaultOf(g_ctl, c.id);
    const auto id = static_cast<ParamID>(c.id);
    const double away = normFromIndex(id, def < 0.5 ? kParams[id].steps - 1 : 0);
    g_ctl->setParamNormalized(c.id, away);
    EditorTest::tick(ed);
    ASSERT_TRUE(near(knob->getValueNormalized(), away));
    g_host.clear();
    VSTGUI::CPoint at = knob->center();
    knob->onMouseDown(at, VSTGUI::CButtonState(VSTGUI::kLButton | VSTGUI::kDoubleClick));
    ASSERT_TRUE(g_host.once(c.id) && g_host.total() == 3);
    ASSERT_TRUE(near(g_ctl->getParamNormalized(c.id), def) && near(knob->getValueNormalized(), def));
    ASSERT_TRUE(EditorTest::silkValue(ed, printId(c)) == expectedValue(g_ctl, c.id));
    EditorTest::tick(ed);
    ASSERT_TRUE(g_host.total() == 3);
  }
  ASSERT_TRUE(knobs == 35);
  g_ctl->setComponentHandler(nullptr);
  resetAll(g_ctl);
  EditorTest::destroy(ed, root);
}

// The three LISTEN buttons write one parameter: a click on MID listens to MID (one edit); while MID is heard the other
// bells are locked, so a click on HI MID through the panel does nothing; HI MID set on its own button moves LISTEN there
// (one lit); a click on the lit HI MID switches LISTEN off.
void test_listen_exclusive_and_toggle() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  g_host.clear();
  auto* mid = EditorTest::control(ed, kListenBand, 1);
  auto* hiMid = EditorTest::control(ed, kListenBand, 2);
  auto* loMid = EditorTest::control(ed, kListenBand, 0);
  ASSERT_TRUE(mid && hiMid && loMid);
  auto lit = [&] { return std::vector<bool>{loMid->getValueNormalized() >= 0.5f, mid->getValueNormalized() >= 0.5f, hiMid->getValueNormalized() >= 0.5f}; };
  clickAt(mid, faceCentre(*controlOf(kListenBand, 1)));
  ASSERT_TRUE(g_host.once(kListenBand) && near(g_host.last[kListenBand], normFromListen(1)));
  ASSERT_TRUE(listenFromNorm(g_ctl->getParamNormalized(kListenBand)) == 1);
  ASSERT_TRUE((lit() == std::vector<bool>{false, true, false}));
  EditorTest::tick(ed);
  ASSERT_TRUE((lit() == std::vector<bool>{false, true, false}) && g_host.total() == 3);
  // HI MID is locked while MID is heard: the panel does not hand it the click
  ASSERT_TRUE(!hiMid->getMouseEnabled() && hiMid->isDimmed());
  const Point hm = faceCentre(*controlOf(kListenBand, 2));
  VSTGUI::CPoint at(hm.x, hm.y);
  root->onMouseDown(at, leftClick());
  ASSERT_TRUE(listenFromNorm(g_ctl->getParamNormalized(kListenBand)) == 1 && g_host.total() == 3);
  // its own button: LISTEN moves to HI MID, one lit
  clickAt(hiMid, hm);
  ASSERT_TRUE(CountingHandler::count(g_host.performs, kListenBand) == 2 && near(g_host.last[kListenBand], normFromListen(2)));
  ASSERT_TRUE((lit() == std::vector<bool>{false, false, true}));
  // a click on the lit one: off
  clickAt(hiMid, hm);
  ASSERT_TRUE(CountingHandler::count(g_host.performs, kListenBand) == 3 && near(g_host.last[kListenBand], normFromListen(-1)));
  ASSERT_TRUE(listenFromNorm(g_ctl->getParamNormalized(kListenBand)) == -1);
  ASSERT_TRUE((lit() == std::vector<bool>{false, false, false}));
  ASSERT_TRUE(g_host.total() == 9);
  EditorTest::tick(ed);
  ASSERT_TRUE(controlsMatchState(ed, stateOf(g_ctl)));
  g_ctl->setComponentHandler(nullptr);
  resetAll(g_ctl);
  EditorTest::destroy(ed, root);
}

// The LISTEN LEDs show the bell really heard, as the engine: LISTEN on MID with MID's band off, or with FINE EQ off,
// lights nothing (the parameter keeps MID); back on, MID lights again. Each bell's printed label dims on its own.
void test_listen_led_shows_heard_band() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto* mid = EditorTest::control(ed, kListenBand, 1);
  ASSERT_TRUE(mid != nullptr);
  g_ctl->setParamNormalized(kListenBand, normFromListen(1));
  EditorTest::tick(ed);
  ASSERT_TRUE(mid->getValueNormalized() == 1.f && controlsMatchState(ed, stateOf(g_ctl)));
  ASSERT_TRUE(EditorTest::silkDimmed(ed, printId(*controlOf(kListenBand, 0))) && !EditorTest::silkDimmed(ed, printId(*controlOf(kListenBand, 1))));
  g_ctl->setParamNormalized(kMidOn, 0.0);
  EditorTest::tick(ed);
  const bool offWithBand = mid->getValueNormalized() == 0.f && controlsMatchState(ed, stateOf(g_ctl));
  g_ctl->setParamNormalized(kMidOn, 1.0);
  g_ctl->setParamNormalized(kFineOn, 0.0);
  EditorTest::tick(ed);
  const bool offWithStage = mid->getValueNormalized() == 0.f && controlsMatchState(ed, stateOf(g_ctl));
  g_ctl->setParamNormalized(kFineOn, 1.0);
  EditorTest::tick(ed);
  const bool back = mid->getValueNormalized() == 1.f;
  resetAll(g_ctl);
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(offWithBand && offWithStage && back);
}

// When the editor closes, LISTEN and SET are switched off with one host edit each (beginEdit / performEdit 0 / endEdit);
// already off, closing sends nothing.
void test_close_turns_listen_and_set_off() {
  resetAll(g_ctl);
  g_ctl->setComponentHandler(&g_host);
  {
    Editor ed(g_ctl);
    auto root = EditorTest::build(ed);
    g_ctl->setParamNormalized(kListenBand, normFromListen(1));
    g_ctl->setParamNormalized(kSetActive, 1.0);
    EditorTest::tick(ed);
    g_host.clear();
    ed.close();
    const bool listenOff = g_host.once(kListenBand) && near(g_host.last[kListenBand], 0.0) &&
                           listenFromNorm(g_ctl->getParamNormalized(kListenBand)) == -1;
    const bool setOff = g_host.once(kSetActive) && near(g_host.last[kSetActive], 0.0) && g_ctl->getParamNormalized(kSetActive) < 0.5;
    const int total = g_host.total();
    EditorTest::destroy(ed, root);
    ASSERT_TRUE(listenOff);
    ASSERT_TRUE(setOff);
    ASSERT_TRUE(total == 6);
  }
  {
    Editor ed(g_ctl);
    auto root = EditorTest::build(ed);
    EditorTest::tick(ed);
    g_host.clear();
    ed.close();
    const int total = g_host.total();
    EditorTest::destroy(ed, root);
    ASSERT_TRUE(total == 0);
  }
  g_ctl->setComponentHandler(nullptr);
  resetAll(g_ctl);
}

// SET (with LIMIT in) dims and locks LIMIT but RELEASE and SET itself; with LIMIT out SET is dimmed and locked like the
// rest of the stage and only the IN stays live; SET off frees everything. BYPASS dims and locks all but itself, MATCH and
// the view buttons included.
void test_set_locks_limit_except_release() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto live = [&](VstParamID id) { auto* v = EditorTest::control(ed, id); return v && !v->isDimmed() && v->getMouseEnabled(); };
  auto locked = [&](VstParamID id) { auto* v = EditorTest::control(ed, id); return v && v->isDimmed() && !v->getMouseEnabled(); };
  ASSERT_TRUE(controlsMatchState(ed, stateOf(g_ctl)));
  g_ctl->setParamNormalized(kSetActive, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(controlsMatchState(ed, stateOf(g_ctl)));
  ASSERT_TRUE(locked(kLimitThresh) && locked(kLimitStyle) && locked(kLimitOn));
  ASSERT_TRUE(live(kLimitRelease) && live(kSetActive) && live(kGlueThresh));
  ASSERT_TRUE(EditorTest::silkDimmed(ed, kLimitThresh) && !EditorTest::silkDimmed(ed, kLimitRelease));
  g_ctl->setParamNormalized(kLimitOn, 0.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(controlsMatchState(ed, stateOf(g_ctl)));
  ASSERT_TRUE(live(kLimitOn) && locked(kSetActive) && locked(kLimitRelease));
  g_ctl->setParamNormalized(kLimitOn, 1.0);
  g_ctl->setParamNormalized(kSetActive, 0.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(live(kLimitThresh) && live(kLimitStyle) && live(kLimitOn) && live(kLimitRelease) && live(kSetActive));
  g_ctl->setParamNormalized(kBypass, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(controlsMatchState(ed, stateOf(g_ctl)));
  ASSERT_TRUE(live(kBypass) && locked(kInputGain) && !EditorTest::match(ed)->getMouseEnabled());
  resetAll(g_ctl);
  EditorTest::tick(ed);
  ASSERT_TRUE(controlsMatchState(ed, stateOf(g_ctl)));
  EditorTest::destroy(ed, root);
}

// FINAL shows only the controls of its mode: EFFECT and CURVE for INFLATE, DRIVE for CLIP (views and printed parts),
// from the host on the next timer run and at once from a click on the mode's face.
void test_final_mode_shows_its_controls() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto shown = [&](VstParamID id) { return EditorTest::control(ed, id)->isVisible() && !EditorTest::silkHidden(ed, id); };
  auto hidden = [&](VstParamID id) { return !EditorTest::control(ed, id)->isVisible() && EditorTest::silkHidden(ed, id); };
  ASSERT_TRUE(shown(kFinalEffect) && shown(kFinalCurve) && hidden(kFinalDrive));
  g_ctl->setParamNormalized(kFinalMode, 1.0);
  ASSERT_TRUE(shown(kFinalEffect));   // only flagged
  EditorTest::tick(ed);
  ASSERT_TRUE(hidden(kFinalEffect) && hidden(kFinalCurve) && shown(kFinalDrive));
  ASSERT_TRUE(controlsMatchState(ed, stateOf(g_ctl)));
  // the user's click on INFLATE: one edit, the controls swap at once
  g_ctl->setComponentHandler(&g_host);
  g_host.clear();
  const LitChoiceSpec& l = *litChoiceOf(kFinalMode);
  clickAt(EditorTest::control(ed, kFinalMode), {l.cx, l.firstCy});
  ASSERT_TRUE(g_host.once(kFinalMode) && near(g_host.last[kFinalMode], 0.0));
  ASSERT_TRUE(shown(kFinalEffect) && shown(kFinalCurve) && hidden(kFinalDrive));
  g_ctl->setComponentHandler(nullptr);
  resetAll(g_ctl);
  EditorTest::destroy(ed, root);
}

// HI PASS on SIDE (from the host) reads the mono-bass scale: the printed value follows (index 2: 25 Hz, on SIDE 80 Hz) and
// so does the printed scale (end value 40 or 200: one of the two prints of FREQ is shown).
void test_hp_scale_follows_ms() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto shownPrint = [&]() -> const FairyTapes::Ui::PrintedItem* {
    const auto* m = EditorTest::silk(ed);
    const FairyTapes::Ui::PrintedItem* found = nullptr;
    for (std::size_t i = 0; m && i < m->size(); ++i) {
      const int id = m->item(i).id;
      if ((id == kHpFreq || id == kHpSidePrintId) && !m->hidden(i)) {
        if (found) return nullptr;   // never both
        found = &m->item(i);
      }
    }
    return found;
  };
  auto value = [&]() { const auto* it = shownPrint(); return it ? EditorTest::silkValue(ed, it->id) : std::string(); };
  g_ctl->setParamNormalized(kHpFreq, normFromIndex(kHpFreq, 2));
  EditorTest::tick(ed);
  ASSERT_TRUE(shownPrint() && shownPrint()->hi == "40" && value() == "25 Hz");
  g_ctl->setParamNormalized(kHpMs, normFromIndex(kHpMs, 2));
  ASSERT_TRUE(shownPrint()->hi == "40");   // only flagged
  EditorTest::tick(ed);
  ASSERT_TRUE(shownPrint() && shownPrint()->hi == "200" && value() == "80 Hz");
  g_ctl->setParamNormalized(kHpMs, normFromIndex(kHpMs, 1));
  EditorTest::tick(ed);
  ASSERT_TRUE(shownPrint() && shownPrint()->hi == "40" && value() == "25 Hz");
  resetAll(g_ctl);
  EditorTest::destroy(ed, root);
}

// MATCH asks the controller (doMatch): with a short-term reading of -20 LUFS and the TARGET at -14 LOUDNESS goes up as
// one host edit, and nothing else is edited; MATCH does not stay lit. Without a reading it does nothing.
void test_match_calls_controller() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  auto* match = EditorTest::match(ed);
  ASSERT_TRUE(match != nullptr);
  const ActionSpec& m = actionOf(Action::Match);
  const Point at{m.rect.x + m.rect.w / 2, m.rect.y + m.rect.h / 2};
  g_ctl->setParamNormalized(kDispLufsShort, 0.0);   // no measurement
  g_host.clear();
  clickAt(match, at);
  ASSERT_TRUE(g_host.total() == 0);
  g_ctl->setParamNormalized(kDispLufsShort, normFromLufs(-20.0));
  g_host.clear();
  clickAt(match, at);
  ASSERT_TRUE(g_host.once(kLoudness) && g_host.total() == 3);
  ASSERT_TRUE(indexFromNorm(kLoudness, g_ctl->getParamNormalized(kLoudness)) > 0);
  ASSERT_TRUE(match->getValueNormalized() == 0.f);
  EditorTest::tick(ed);
  ASSERT_TRUE(near(EditorTest::control(ed, kLoudness)->getValueNormalized(), g_ctl->getParamNormalized(kLoudness)));
  ASSERT_TRUE(g_host.total() == 3);
  g_ctl->setParamNormalized(kDispLufsShort, 0.0);
  g_ctl->setComponentHandler(nullptr);
  resetAll(g_ctl);
  EditorTest::destroy(ed, root);
}

// The five view buttons right of the screen are one exclusive choice (one lit), EQ at first; a click picks a view
// without any host edit, and the controller keeps it for the next editor of the instance (never in the state).
void test_view_buttons_exclusive() {
  resetAll(g_ctl);
  auto* owner = dynamic_cast<Controller*>(g_ctl);
  ASSERT_TRUE(owner && owner->screenView() == ScreenView::Eq);
  g_ctl->setComponentHandler(&g_host);
  g_host.clear();
  int reopened = -1;
  {
    Editor ed(g_ctl);
    auto root = EditorTest::build(ed);
    auto* views = dynamic_cast<FairyTapes::Ui::ChoiceButtons*>(EditorTest::views(ed));
    ASSERT_TRUE(views != nullptr && views->count() == 5 && views->selectedIndex() == 0);
    for (int i = 1; i <= 5; ++i) {   // each face picks its view
      const ActionSpec& a = kActions[i];
      clickAt(views, {a.rect.x + a.rect.w / 2, a.rect.y + a.rect.h / 2});
      ASSERT_TRUE(views->selectedIndex() == i - 1 && owner->screenView() == static_cast<ScreenView>(i - 1));
    }
    const ActionSpec& glue = actionOf(Action::ViewGlue);
    clickAt(views, {glue.rect.x + glue.rect.w / 2, glue.rect.y + glue.rect.h / 2});
    ASSERT_TRUE(views->selectedIndex() == 1 && owner->screenView() == ScreenView::Glue);
    EditorTest::tick(ed);
    ASSERT_TRUE(views->selectedIndex() == 1);
    EditorTest::destroy(ed, root);
  }
  {
    Editor ed(g_ctl);
    auto root = EditorTest::build(ed);
    reopened = dynamic_cast<FairyTapes::Ui::ChoiceButtons*>(EditorTest::views(ed))->selectedIndex();
    EditorTest::destroy(ed, root);
  }
  const int edits = g_host.total();
  owner->setScreenView(ScreenView::Eq);
  g_ctl->setComponentHandler(nullptr);
  ASSERT_TRUE(reopened == 1);
  ASSERT_TRUE(edits == 0);
}

// The value under a knob, clicked, opens a text field with the value: Enter reads it through parseValue (a bare number
// picks the nearest step) as one edit; a text that is no value changes nothing; Escape changes nothing.
void test_typed_value_goes_through_parse() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  auto* knob = dynamic_cast<FairyTapes::Ui::RackKnob*>(EditorTest::control(ed, kLimitThresh));
  ASSERT_TRUE(knob != nullptr);
  auto open = [&]() {
    VSTGUI::CPoint at = knob->valueArea().getCenter();
    knob->onMouseDown(at, leftClick());
    return EditorTest::valueEdit(ed);
  };
  g_host.clear();
  VSTGUI::CTextEdit* field = open();
  ASSERT_TRUE(field != nullptr && root->isChild(field));
  ASSERT_TRUE(field->getText() == "-2.0 dB");
  ASSERT_TRUE(g_host.total() == 0);   // the click did not start a drag
  ASSERT_TRUE(open() == field);       // one field at a time
  field->setText("-3.2");
  ed.valueChanged(field);   // Enter
  EditorTest::closeValueEdit(ed, true);
  ASSERT_TRUE(EditorTest::valueEdit(ed) == nullptr && !root->isChild(field));
  ASSERT_TRUE(g_host.once(kLimitThresh) && g_host.total() == 3);
  ASSERT_TRUE(indexFromNorm(kLimitThresh, g_ctl->getParamNormalized(kLimitThresh)) == 6);   // -3.0 dB
  ASSERT_TRUE(near(knob->getValueNormalized(), normFromIndex(kLimitThresh, 6)) && EditorTest::silkValue(ed, kLimitThresh) == "-3.0 dB");
  // not a value: nothing
  field = open();
  ASSERT_TRUE(field != nullptr && field->getText() == "-3.0 dB");
  field->setText("loud");
  ed.valueChanged(field);
  EditorTest::closeValueEdit(ed, true);
  ASSERT_TRUE(g_host.total() == 3 && indexFromNorm(kLimitThresh, g_ctl->getParamNormalized(kLimitThresh)) == 6);
  // Escape: nothing is read
  field = open();
  ASSERT_TRUE(field != nullptr);
  field->setText("0");
  VSTGUI::KeyboardEvent esc;
  esc.type = VSTGUI::EventType::KeyDown;
  esc.virt = VSTGUI::VirtualKey::Escape;
  field->onKeyboardEvent(esc);
  ed.valueChanged(field);
  EditorTest::closeValueEdit(ed, false);
  ASSERT_TRUE(g_host.total() == 3 && indexFromNorm(kLimitThresh, g_ctl->getParamNormalized(kLimitThresh)) == 6);
  // HI PASS on SIDE: a typed 60 is the 60 Hz step of the mono-bass scale
  g_ctl->setParamNormalized(kHpMs, normFromIndex(kHpMs, 2));
  EditorTest::tick(ed);
  knob = dynamic_cast<FairyTapes::Ui::RackKnob*>(EditorTest::control(ed, kHpFreq));
  field = open();
  ASSERT_TRUE(field != nullptr);
  field->setText("60");
  ed.valueChanged(field);
  EditorTest::closeValueEdit(ed, true);
  ASSERT_TRUE(indexFromNorm(kHpFreq, g_ctl->getParamNormalized(kHpFreq)) == 1 && EditorTest::silkValue(ed, kHpSidePrintId) == "60 Hz");
  g_ctl->setComponentHandler(nullptr);
  resetAll(g_ctl);
  EditorTest::destroy(ed, root);
}

// The value under LIMIT RELEASE is the real time in ms of the style (releaseText), and it follows the style.
void test_limit_release_shows_ms() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  const std::string modern = EditorTest::silkValue(ed, kLimitRelease);
  ASSERT_TRUE(modern == releaseText(0, 4) && modern.find(" ms") != std::string::npos);
  g_ctl->setParamNormalized(kLimitStyle, 1.0);
  EditorTest::tick(ed);
  const std::string dyn = EditorTest::silkValue(ed, kLimitRelease);
  g_ctl->setParamNormalized(kLimitRelease, normFromIndex(kLimitRelease, 7));
  EditorTest::tick(ed);
  const std::string slower = EditorTest::silkValue(ed, kLimitRelease);
  resetAll(g_ctl);
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(dyn == releaseText(2, 4) && dyn != modern);
  ASSERT_TRUE(slower == releaseText(2, 7));
}

// The six M/S selectors print ST / MID / SIDE under the button (the preview and spec §5.1), not the host's Stereo / Mid /
// Side; the host text stays what formatValue says.
void test_ms_prints_sigla() {
  resetAll(g_ctl);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  const VstParamID ids[6] = {kHpMs, kLsMs, kLmMs, kMidMs, kHmMs, kHsMs};
  const char* const want[3] = {"ST", "MID", "SIDE"};
  const char* const host[3] = {"Stereo", "Mid", "Side"};
  bool ok = true;
  for (VstParamID id : ids)
    for (int i = 0; i < 3; ++i) {
      g_ctl->setParamNormalized(id, normFromIndex(static_cast<ParamID>(id), i));
      EditorTest::tick(ed);
      ok = ok && EditorTest::silkValue(ed, static_cast<int>(id)) == want[i];
      double n[kNumParams];
      normsOf(g_ctl, n);
      ok = ok && formatValue(static_cast<ParamID>(id), n[id], n) == host[i];
    }
  resetAll(g_ctl);
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(ok);
}

// The value under a knob is printed as in the preview, its baseline kKnobValueBaselineBelowCap (36) under the cap centre
// plus the radius, not at the shared silkscreen's R + 39.5 (where it ran into the button labels under the lower FINE EQ
// knobs): amber pixels of INPUT GAIN's "+0.0 dB" (cap 89, 188, R 22: baseline 246) reach the baseline and nothing amber
// lies 2-4 px below it.
void test_knob_values_at_preview_offset() {
  resetAll(g_ctl);
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto bmp = drawOffscreen(root);
  ASSERT_TRUE(bmp != nullptr);
  const Pixels px = pixelsOf(bmp);
  auto amberRow = [&](int y) {
    int n = 0;
    for (int x = 60; x <= 118; ++x) {
      const VSTGUI::CColor c = px.at(x, y);
      n += c.red > 150 && c.green > 90 && c.green < 210 && c.blue < 100 ? 1 : 0;
    }
    return n;
  };
  const ControlSpec& gain = *controlOf(kInputGain);
  const int baseline = static_cast<int>(knobCentre(gain).y + gain.capRadius + kKnobValueBaselineBelowCap);
  ASSERT_TRUE(baseline == 246);
  int above = 0, below = 0;
  for (int y = baseline - 6; y <= baseline; ++y) above += amberRow(y);
  for (int y = baseline + 2; y <= baseline + 4; ++y) below += amberRow(y);
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(above > 20);
  ASSERT_TRUE(below == 0);
}

// ---- LEVEL, LUFS and the ACT LEDs (Task 10) ---------------------------------------------------------------------------
constexpr double kBarLow = -60.0, kBarSpan = 66.0;   // the peak rows span -60..+6 dB over 15 LEDs, as the printed scale
// The display value of a peak in dB (the processor's normFromPeakDb, display.h).
double peakNorm(double db) { return normFromPeakDb(db); }

// Every display parameter back to 0 (host side, no edit sent).
void resetDisplays() {
  for (Steinberg::Vst::ParamID id = kDisplayBase; id < kNumDisplayIDs; ++id)
    if (g_ctl->getParameterObject(id)) g_ctl->setParamNormalized(id, 0.0);
}
void setStage(Stage s, bool on) { g_ctl->setParamNormalized(stageInParam(s), normFromIndex(static_cast<ParamID>(stageInParam(s)), on ? 1 : 0)); }
// The brightest channel of the pixel at layout point (x, y) of a drawn editor.
int brightAt(const Pixels& px, double x, double y) { return brightness(px.at(static_cast<int>(x), static_cast<int>(y))); }
// The brightest pixel of the layout rect r.
int brightIn(const Pixels& px, const Rect& r) {
  int best = 0;
  for (int y = static_cast<int>(r.y); y < static_cast<int>(r.y + r.h); ++y)
    for (int x = static_cast<int>(r.x); x < static_cast<int>(r.x + r.w); ++x) best = std::max(best, brightness(px.at(x, y)));
  return best;
}
// A click of the left (or right) button delivered to the container as the frame does, at a layout point: the container
// finds the view under it, so this also proves the routing (CViewContainer::onMouseDown itself never reaches the children).
void rootClick(VSTGUI::CViewContainer* root, double x, double y, bool left = true) {
  const VSTGUI::MouseEventButtonState button(left ? VSTGUI::MouseButton::Left : VSTGUI::MouseButton::Right);
  VSTGUI::MouseDownEvent down(VSTGUI::CPoint(x, y), button);
  root->dispatchEvent(down);
  VSTGUI::MouseUpEvent up(VSTGUI::CPoint(x, y), button);
  root->dispatchEvent(up);
}

// The peak rows and the GR rows follow the display parameters: IN L at half scale (-30 dB) lights about half of the 15
// LEDs of its row only (the LEDs span -60..+6 dB like the printed scale, so 7), GR 2 (LIMIT) at 0.5 half of its row; the
// right channel and the other rows stay dark; LED colours: green low, amber for GR; a row at full scale lights up to the
// 0 dB LED.
void test_meters_follow_display_params() {
  resetAll(g_ctl);
  resetDisplays();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  EditorTest::tick(ed);
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(EditorTest::meter(ed, i) != nullptr && EditorTest::meter(ed, i)->level() == 0.0);
  for (int i = 0; i < 5; ++i) ASSERT_TRUE(EditorTest::grBar(ed, i) != nullptr && EditorTest::grBar(ed, i)->level() == 0.0);
  g_ctl->setParamNormalized(kDispInL, peakNorm(-30.0));
  g_ctl->setParamNormalized(kDispOutR, peakNorm(0.0));
  g_ctl->setParamNormalized(kDispInR, peakNorm(6.0));
  g_ctl->setParamNormalized(kDispOutL, peakNorm(12.0));   // above the scale: all 15, no more
  g_ctl->setParamNormalized(kDispGr2, 0.5);
  EditorTest::tick(ed);
  using FairyTapes::Ui::ledsLit;
  ASSERT_TRUE(near(EditorTest::meter(ed, 0)->level(), (-30.0 - kBarLow) / kBarSpan));
  ASSERT_TRUE(ledsLit(EditorTest::meter(ed, 0)->level(), kMeterLeds) == 7);
  ASSERT_TRUE(near(EditorTest::meter(ed, 3)->level(), (0.0 - kBarLow) / kBarSpan));
  ASSERT_TRUE(ledsLit(EditorTest::meter(ed, 3)->level(), kMeterLeds) == 14);   // 0 dB: up to the LED of the 0 dB mark
  ASSERT_TRUE(near(EditorTest::meter(ed, 1)->level(), 1.0) && ledsLit(EditorTest::meter(ed, 1)->level(), kMeterLeds) == 15);   // +6 dB: all 15
  ASSERT_TRUE(near(EditorTest::meter(ed, 2)->level(), 1.0));
  ASSERT_TRUE(near(EditorTest::grBar(ed, 2)->level(), 0.5));
  ASSERT_TRUE(ledsLit(EditorTest::grBar(ed, 2)->level(), kMeterLeds) == 8);
  for (int i : {0, 1, 3, 4}) ASSERT_TRUE(EditorTest::grBar(ed, i)->level() == 0.0);
  // The drawn LEDs: IN L's first LED green, the eighth (index 7) dark; GR 2's first LED amber, its last dark
  auto bmp = drawOffscreen(root);
  ASSERT_TRUE(bmp != nullptr);
  const Pixels px = pixelsOf(bmp);
  auto ledX = [](int k) { return kMeterFirstX + kMeterPitch * static_cast<float>(k); };
  const VSTGUI::CColor first = px.at(static_cast<int>(ledX(0)), static_cast<int>(kMeterRows[0].cy));
  ASSERT_TRUE(first.green > 180 && first.red < 100);                                           // 0x37d04a
  ASSERT_TRUE(brightAt(px, ledX(7), kMeterRows[0].cy) < 60);                                    // 0x2a2a2c
  const VSTGUI::CColor top = px.at(static_cast<int>(ledX(14)), static_cast<int>(kMeterRows[1].cy));   // IN R at +6 dB: the 15th LED, red
  ASSERT_TRUE(top.red > 200 && top.green < 100);
  const VSTGUI::CColor gr = px.at(static_cast<int>(ledX(0)), static_cast<int>(kGrRows[2].cy));
  ASSERT_TRUE(gr.red > 200 && gr.green > 150 && gr.blue < 80);                                  // amber 0xf0b429
  ASSERT_TRUE(brightAt(px, ledX(14), kGrRows[2].cy) < 60);
  // the other direction: the display values back to 0 and the rows go dark again
  resetDisplays();
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::meter(ed, 0)->level() == 0.0 && EditorTest::grBar(ed, 2)->level() == 0.0);
  EditorTest::destroy(ed, root);
  resetAll(g_ctl);
}

// A click on any of the nine clip LEDs (the LED or its label) writes the clear trigger once (kClearClips = 1, begin /
// perform / end) and nothing else, dimmed stage LEDs included; the LEDs light and go out with the display parameters.
void test_clip_click_clears_via_trigger() {
  resetAll(g_ctl);
  resetDisplays();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  g_ctl->setParamNormalized(kDispClip0, 1.0);
  g_ctl->setParamNormalized(kDispClip4, 1.0);
  EditorTest::tick(ed);
  for (int i = 0; i < 9; ++i) ASSERT_TRUE(EditorTest::clip(ed, i) != nullptr);
  ASSERT_TRUE(EditorTest::clip(ed, 0)->isLit() && EditorTest::clip(ed, 4)->isLit());
  for (int i : {1, 2, 3, 5, 6, 7, 8}) ASSERT_TRUE(!EditorTest::clip(ed, i)->isLit());
  for (int pass = 0; pass < 2; ++pass) {   // the second pass with BYPASS: the stage LEDs are dimmed and still clickable
    if (pass == 1) {
      g_ctl->setParamNormalized(kBypass, 1.0);
      EditorTest::tick(ed);
    }
    for (int i = 0; i < 9; ++i) {
      const Rect r = clipLedRect(kClipLeds[i]);
      for (double fx : {0.12, 0.8}) {   // on the LED, on its label
        g_host.clear();
        g_ctl->setParamNormalized(kClearClips, 0.0);
        rootClick(root, r.x + r.w * fx, r.y + r.h / 2);
        ASSERT_TRUE(g_host.once(kClearClips) && near(g_host.last[kClearClips], 1.0) && g_host.total() == 3);
        ASSERT_TRUE(g_ctl->getParamNormalized(kClearClips) == 1.0);
      }
    }
  }
  // a click beside the LED row does nothing
  g_host.clear();
  rootClick(root, kClipLeds[0].cx - 30, kClipRowY);
  ASSERT_TRUE(g_host.total() == 0);
  // the processor clears and answers: the LEDs follow the display parameters
  g_ctl->setParamNormalized(kClearClips, 0.0);
  g_ctl->setParamNormalized(kDispClip0, 0.0);
  g_ctl->setParamNormalized(kDispClip4, 0.0);
  EditorTest::tick(ed);
  for (int i = 0; i < 9; ++i) ASSERT_TRUE(!EditorTest::clip(ed, i)->isLit());
  g_ctl->setComponentHandler(nullptr);
  g_ctl->setParamNormalized(kBypass, 0.0);
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// A click on the integrated loudness (its zone, kLufsResetRect) writes the reset trigger once (kResetIntegrated = 1) and
// nothing else; a click elsewhere on the screen, a right click, or a click under BYPASS (the screen stays live) behaves
// accordingly: only the first and the BYPASS one reset.
void test_lufs_click_resets_integrated() {
  resetAll(g_ctl);
  resetDisplays();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  const double cx = kLufsResetRect.x + kLufsResetRect.w / 2, cy = kLufsResetRect.y + kLufsResetRect.h / 2;
  g_host.clear();
  rootClick(root, cx, cy);
  ASSERT_TRUE(g_host.once(kResetIntegrated) && near(g_host.last[kResetIntegrated], 1.0) && g_host.total() == 3);
  ASSERT_TRUE(g_ctl->getParamNormalized(kResetIntegrated) == 1.0);
  g_ctl->setParamNormalized(kResetIntegrated, 0.0);   // the processor's answer
  g_host.clear();
  rootClick(root, cx, kLufsScreenRect.y + 14);                 // the title line
  rootClick(root, cx, kLufsScreenRect.y + kLufsScreenRect.h - 8);   // TO TARGET
  rootClick(root, kLufsResetRect.x + 2, kLufsResetRect.y - 4);   // just above the zone
  rootClick(root, cx, cy, false);                                // right button
  ASSERT_TRUE(g_host.total() == 0);
  g_ctl->setParamNormalized(kBypass, 1.0);
  EditorTest::tick(ed);
  rootClick(root, kLufsResetRect.x + kLufsResetRect.w - 3, kLufsResetRect.y + kLufsResetRect.h - 3);
  ASSERT_TRUE(g_host.once(kResetIntegrated) && g_host.total() == 3);
  g_ctl->setParamNormalized(kResetIntegrated, 0.0);
  g_ctl->setComponentHandler(nullptr);
  EditorTest::destroy(ed, root);
  resetAll(g_ctl);
}

// The LUFS screen shows the display parameters as readouts.h says (typographic minus, em dash for no measurement), with
// TO TARGET against the TARGET chosen on the panel and the green LED within 0.5 LU; under BYPASS it stays live. Texts
// are checked as literals, not through readouts.h.
void test_lufs_texts_and_target_led() {
  resetAll(g_ctl);
  resetDisplays();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  LufsView* v = EditorTest::lufs(ed);
  ASSERT_TRUE(v != nullptr);
  const std::string dash = "\xE2\x80\x94";
  EditorTest::tick(ed);   // no measurement
  ASSERT_TRUE(v->shortText() == dash && v->integratedText() == dash && v->targetGapText() == dash && !v->targetLit());
  g_ctl->setParamNormalized(kDispLufsShort, normFromLufs(-14.2));
  g_ctl->setParamNormalized(kDispLufsInt, normFromLufs(-15.6));
  EditorTest::tick(ed);   // TARGET -14
  ASSERT_TRUE(v->shortText() == "\xE2\x88\x92" "14.2" && v->integratedText() == "\xE2\x88\x92" "15.6");
  ASSERT_TRUE(v->targetGapText() == "\xE2\x88\x92" "0.2" && v->targetLit());
  g_ctl->setParamNormalized(kTarget, normFromIndex(kTarget, 2));   // -9
  EditorTest::tick(ed);
  ASSERT_TRUE(v->shortText() == "\xE2\x88\x92" "14.2" && v->targetGapText() == "\xE2\x88\x92" "5.2" && !v->targetLit());
  g_ctl->setParamNormalized(kTarget, normFromIndex(kTarget, 1));   // -11
  g_ctl->setParamNormalized(kDispLufsShort, normFromLufs(-10.3));
  EditorTest::tick(ed);
  ASSERT_TRUE(v->targetGapText() == "+0.7" && !v->targetLit());
  g_ctl->setParamNormalized(kDispLufsShort, normFromLufs(-11.4));
  EditorTest::tick(ed);
  ASSERT_TRUE(v->targetGapText() == "\xE2\x88\x92" "0.4" && v->targetLit());
  // live under BYPASS
  g_ctl->setParamNormalized(kBypass, 1.0);
  g_ctl->setParamNormalized(kDispLufsShort, normFromLufs(-12.0));
  EditorTest::tick(ed);
  ASSERT_TRUE(v->shortText() == "\xE2\x88\x92" "12.0" && v->targetGapText() == "\xE2\x88\x92" "1.0" && !v->targetLit());
  g_ctl->setParamNormalized(kBypass, 0.0);
  // the green LED is drawn: lit at -11.4, dark without a measurement
  g_ctl->setParamNormalized(kDispLufsShort, normFromLufs(-11.4));
  EditorTest::tick(ed);
  auto lit = drawOffscreen(root);
  g_ctl->setParamNormalized(kDispLufsShort, 0.0);
  EditorTest::tick(ed);
  auto dark = drawOffscreen(root);
  ASSERT_TRUE(lit != nullptr && dark != nullptr);
  const VSTGUI::CColor on = pixelsOf(lit).at(static_cast<int>(kTargetLed.cx), static_cast<int>(kTargetLed.cy));
  const VSTGUI::CColor off = pixelsOf(dark).at(static_cast<int>(kTargetLed.cx), static_cast<int>(kTargetLed.cy));
  ASSERT_TRUE(on.green > 180 && on.red < 100 && brightness(off) < 60);
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// The GR rows and their tags dim with their stage's IN and with BYPASS, and show nothing while dimmed (as the preview);
// the peak rows never dim. The bars say it themselves (LevelBar::isDimmed), the tags are read from the pixels (a dimmed tag
// is far darker).
void test_gr_rows_dim_with_stage_and_bypass() {
  resetAll(g_ctl);
  resetDisplays();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  GrTags* tags = EditorTest::grTags(ed);
  ASSERT_TRUE(tags != nullptr);
  for (int i = 0; i < 5; ++i) g_ctl->setParamNormalized(kDispGr0 + static_cast<Steinberg::Vst::ParamID>(i), 0.6);
  g_ctl->setParamNormalized(kDispInL, peakNorm(-30.0));
  EditorTest::tick(ed);
  // defaults: METALHEAD is off, the other four stages on
  ASSERT_TRUE(!tags->dimmed(0) && !tags->dimmed(1) && !tags->dimmed(2) && !tags->dimmed(3) && tags->dimmed(4));
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(near(EditorTest::grBar(ed, i)->level(), 0.6));
  ASSERT_TRUE(EditorTest::grBar(ed, 4)->level() == 0.0);
  auto tagBright = [&](int row) {
    auto bmp = drawOffscreen(root);
    const Pixels px = pixelsOf(bmp);
    return brightIn(px, {kRowTagX - 12, kGrRows[row].cy - 6, 14, 12});
  };
  const int tagOn = tagBright(0), tagOff = tagBright(4);
  ASSERT_TRUE(tagOn > 200 && tagOff < 140);
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(!EditorTest::grBar(ed, i)->isDimmed());
  ASSERT_TRUE(EditorTest::grBar(ed, 4)->isDimmed());
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(!EditorTest::meter(ed, i)->isDimmed());
  // GLUE's IN off: its row alone goes empty and dim; METALHEAD on: its row wakes
  setStage(Stage::Glue, false);
  setStage(Stage::Metal, true);
  EditorTest::tick(ed);
  ASSERT_TRUE(tags->dimmed(0) && !tags->dimmed(1) && !tags->dimmed(2) && !tags->dimmed(3) && !tags->dimmed(4));
  ASSERT_TRUE(EditorTest::grBar(ed, 0)->isDimmed() && !EditorTest::grBar(ed, 1)->isDimmed() && !EditorTest::grBar(ed, 4)->isDimmed());
  ASSERT_TRUE(EditorTest::grBar(ed, 0)->level() == 0.0 && near(EditorTest::grBar(ed, 1)->level(), 0.6) && near(EditorTest::grBar(ed, 4)->level(), 0.6));
  setStage(Stage::Glue, true);
  EditorTest::tick(ed);
  ASSERT_TRUE(!tags->dimmed(0) && near(EditorTest::grBar(ed, 0)->level(), 0.6));
  // BYPASS: every row dims and empties, the peak row keeps its level and its brightness
  g_ctl->setParamNormalized(kBypass, 1.0);
  EditorTest::tick(ed);
  for (int i = 0; i < 5; ++i) ASSERT_TRUE(tags->dimmed(i) && EditorTest::grBar(ed, i)->isDimmed() && EditorTest::grBar(ed, i)->level() == 0.0);
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(!EditorTest::meter(ed, i)->isDimmed());
  ASSERT_TRUE(near(EditorTest::meter(ed, 0)->level(), (-30.0 - kBarLow) / kBarSpan));
  {
    auto bmp = drawOffscreen(root);
    const Pixels px = pixelsOf(bmp);
    ASSERT_TRUE(brightAt(px, kMeterFirstX, kMeterRows[0].cy) > 180);   // IN L's lit LED at full brightness
    ASSERT_TRUE(brightAt(px, kMeterFirstX + kMeterPitch * 14, kMeterRows[0].cy) >= 38);   // and an unlit one undimmed
  }
  g_ctl->setParamNormalized(kBypass, 0.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(!tags->dimmed(0) && !EditorTest::grBar(ed, 0)->isDimmed() && near(EditorTest::grBar(ed, 0)->level(), 0.6));
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// The stage clip LEDs dim with BYPASS and with their IN; IN and OUT never do (a lit LED is red at full brightness, at
// 35 % it is much darker).
void test_clip_leds_dim_with_stage_and_bypass() {
  resetAll(g_ctl);
  resetDisplays();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  for (int i = 0; i < 9; ++i) g_ctl->setParamNormalized(kDispClip0 + static_cast<Steinberg::Vst::ParamID>(i), 1.0);
  EditorTest::tick(ed);
  auto levels = [&] {
    auto bmp = drawOffscreen(root);
    const Pixels px = pixelsOf(bmp);
    std::vector<int> out;
    for (const FairyTapes::HappyEnding::Ui::ClipLed& c : kClipLeds) out.push_back(brightAt(px, c.cx, kClipRowY));
    return out;
  };
  auto bright = [](int b) { return b > 200; };
  auto dim = [](int b) { return b < 150; };
  std::vector<int> v = levels();
  for (int i = 0; i < 9; ++i) ASSERT_TRUE(i == 7 ? dim(v[7]) : bright(v[static_cast<std::size_t>(i)]));   // METALHEAD is off by default
  setStage(Stage::Clip, false);
  setStage(Stage::Metal, true);
  EditorTest::tick(ed);
  v = levels();
  for (int i = 0; i < 9; ++i) ASSERT_TRUE(i == 4 ? dim(v[4]) : bright(v[static_cast<std::size_t>(i)]));
  setStage(Stage::Clip, true);
  g_ctl->setParamNormalized(kBypass, 1.0);
  EditorTest::tick(ed);
  v = levels();
  for (int i = 0; i < 9; ++i) ASSERT_TRUE((i == 0 || i == 8) ? bright(v[static_cast<std::size_t>(i)]) : dim(v[static_cast<std::size_t>(i)]));
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// The ACT LEDs of the DYN buttons light as much as the boost is pulled back (kDispDynLow / kDispDynAir, dark amber at 0, amber
// at full), each from its own parameter, and dim with BYPASS and with VIBE EQ's IN.
void test_act_leds_follow_dyn() {
  resetAll(g_ctl);
  resetDisplays();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ASSERT_TRUE(EditorTest::act(ed, 0) != nullptr && EditorTest::act(ed, 1) != nullptr);
  auto shade = [&](int i) {
    auto bmp = drawOffscreen(root);
    return brightAt(pixelsOf(bmp), kActLeds[i].cx, kActLeds[i].cy);
  };
  EditorTest::tick(ed);
  const int idleLow = shade(0), idleAir = shade(1);
  ASSERT_TRUE(idleLow >= 45 && idleLow <= 60 && idleAir >= 45 && idleAir <= 60);   // 0x33260a
  g_ctl->setParamNormalized(kDispDynLow, 0.4);
  g_ctl->setParamNormalized(kDispDynAir, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(near(EditorTest::act(ed, 0)->level(), 0.4) && near(EditorTest::act(ed, 1)->level(), 1.0));
  const int low = shade(0), air = shade(1);
  ASSERT_TRUE(low > 130 && low < 190);   // mixed 56 % towards amber
  ASSERT_TRUE(air > 225);
  g_ctl->setParamNormalized(kDispDynLow, 0.0);
  g_ctl->setParamNormalized(kDispDynAir, 0.4);
  EditorTest::tick(ed);
  ASSERT_TRUE(near(EditorTest::act(ed, 0)->level(), 0.0) && near(EditorTest::act(ed, 1)->level(), 0.4));
  ASSERT_TRUE(shade(0) == idleLow && shade(1) == low);
  // dimmed by BYPASS and by the stage's IN
  g_ctl->setParamNormalized(kDispDynAir, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(!EditorTest::act(ed, 1)->isDimmed());
  g_ctl->setParamNormalized(kBypass, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::act(ed, 0)->isDimmed() && EditorTest::act(ed, 1)->isDimmed() && shade(1) < 130);
  g_ctl->setParamNormalized(kBypass, 0.0);
  setStage(Stage::Vibe, false);
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::act(ed, 1)->isDimmed());
  setStage(Stage::Vibe, true);
  EditorTest::tick(ed);
  ASSERT_TRUE(!EditorTest::act(ed, 0)->isDimmed() && !EditorTest::act(ed, 1)->isDimmed() && shade(1) > 225);
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// The LUFS screen's dark box is on the panel layer, under the labels: dark inside (7, 7, 8) where the panel is not, the
// "SHORT" label still bright over it.
void test_lufs_screen_box_under_labels() {
  resetAll(g_ctl);
  resetDisplays();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  EditorTest::tick(ed);
  auto bmp = drawOffscreen(root);
  ASSERT_TRUE(bmp != nullptr);
  const Pixels px = pixelsOf(bmp);
  const int inside = brightAt(px, kLufsScreenRect.x + 100, kLufsScreenRect.y + 30);
  const int beside = brightAt(px, kLufsScreenRect.x - 8, kLufsScreenRect.y + 60);
  const int label = brightIn(px, {652, 139, 28, 8});   // SHORT, 8 px bold dim white
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(inside <= 12);
  ASSERT_TRUE(beside > inside);
  ASSERT_TRUE(label > 120);
}

// ---- Wear, paper tapes, screws and preferences (Task 12) --------------------------------------------------------------
using FairyTapes::Ui::WearLevel;

const Rect kPanelRect{0.f, 0.f, static_cast<float>(kEditorWidth), static_cast<float>(kEditorHeight)};

// A controller of its own: the decor tests start from seed 0 or from a decor they choose, without touching the shared one
// (which the other tests build editors on; a seeded decor there would put wear under their pixel checks).
struct OwnController {
  Controller ctl;
  OwnController() { ctl.initialize(nullptr); }
  ~OwnController() { ctl.terminate(); }
};
// A decor with this seed, blank tapes, screws unturned.
Decor seededDecor(std::uint32_t seed) {
  Decor d;
  d.seed = seed;
  return d;
}
// No preferences file in the scratch folder: the editors made next open with the defaults.
void noPrefs() { std::filesystem::remove(prefsFile()); }

// Index of `v` among the children of `root` (-1 when it is not one).
int childIndex(VSTGUI::CViewContainer* root, const VSTGUI::CView* v) {
  for (uint32_t i = 0; i < root->getNbViews(); ++i)
    if (root->getView(i) == v) return static_cast<int>(i);
  return -1;
}
template <class T>
int countChildren(VSTGUI::CViewContainer* root) {
  int n = 0;
  for (uint32_t i = 0; i < root->getNbViews(); ++i) n += dynamic_cast<T*>(root->getView(i)) ? 1 : 0;
  return n;
}
// The drag of a screw as the frame delivers it: down, moves, up, all through the container.
void rootDrag(VSTGUI::CViewContainer* root, VSTGUI::CPoint from, VSTGUI::CPoint to, int steps, int* lastMoveResult = nullptr) {
  const VSTGUI::MouseEventButtonState left(VSTGUI::MouseButton::Left);
  VSTGUI::MouseDownEvent down(from, left);
  root->dispatchEvent(down);
  for (int k = 1; k <= steps; ++k) {
    VSTGUI::MouseMoveEvent move(VSTGUI::CPoint(from.x + (to.x - from.x) * k / steps, from.y + (to.y - from.y) * k / steps), left);
    root->dispatchEvent(move);
    if (lastMoveResult) *lastMoveResult = move.consumed ? 1 : 0;
  }
  VSTGUI::MouseUpEvent up(to, left);
  root->dispatchEvent(up);
}

// Light (the default) shows marks and 1-2 tapes; Off: no wear mark and no tape view (the four screws stay).
void test_wear_off_has_no_marks_and_no_tapes() {
  noPrefs();
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0x1234ABCDu));
  Editor ed(&oc.ctl);
  auto root = EditorTest::build(ed);
  ASSERT_TRUE(ed.wearLevel() == WearLevel::Light && ed.wearSeed() == 0x1234ABCDu);
  auto* wear = EditorTest::wear(ed);
  ASSERT_TRUE(wear != nullptr && root->isChild(wear) && !wear->getMouseEnabled());   // wear lets the clicks through
  ASSERT_TRUE(!wear->marks().empty());
  const std::size_t light = layoutFromSeed(ed.wearSeed(), WearLevel::Light, kPanelRect).tapes.size();
  ASSERT_TRUE(light >= 1 && EditorTest::tapes(ed).size() == light && countChildren<TapeView>(root) == static_cast<int>(light));
  ed.setWearLevel(WearLevel::Off);
  ASSERT_TRUE(EditorTest::wear(ed) == wear && wear->marks().empty());
  ASSERT_TRUE(EditorTest::tapes(ed).empty() && countChildren<TapeView>(root) == 0);
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(EditorTest::screw(ed, i) != nullptr && root->isChild(EditorTest::screw(ed, i)));
  EditorTest::destroy(ed, root);
}

// Wear Light -> Heavy -> Light: the same views (panel, wear, controls, screws), new marks and tapes for the level, tapes
// drawn in the 4 px box of the decor model, every decor layer between the silkscreen and the screen and the controls.
void test_wear_level_change_redraws_without_rebuild() {
  noPrefs();
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0x0BADF00Du));
  Editor ed(&oc.ctl);
  auto root = EditorTest::build(ed);
  auto* wear = EditorTest::wear(ed);
  ASSERT_TRUE(wear != nullptr);
  auto stable = [&]() {
    std::vector<VSTGUI::CView*> v;
    for (int i = 0; i < kNumControls; ++i) v.push_back(EditorTest::controlAt(ed, i));
    for (int i = 0; i < 4; ++i) v.push_back(EditorTest::screw(ed, i));
    v.push_back(EditorTest::panel(ed));
    v.push_back(EditorTest::silkView(ed));
    v.push_back(EditorTest::screen(ed));
    v.push_back(wear);
    return v;
  };
  const auto before = stable();
  const std::uint32_t seed = ed.wearSeed();
  for (WearLevel level : {WearLevel::Heavy, WearLevel::Light, WearLevel::Heavy}) {
    ed.setWearLevel(level);
    ASSERT_TRUE(ed.wearLevel() == level && EditorTest::wear(ed) == wear);
    ASSERT_TRUE(wear->marks().size() == FairyTapes::Ui::generateWear(seed, level, kEditorWidth, kEditorHeight).size());
    ASSERT_TRUE(stable() == before);
    const WearLayout layout = layoutFromSeed(seed, level, kPanelRect);
    const auto& tapes = EditorTest::tapes(ed);
    ASSERT_TRUE(tapes.size() == layout.tapes.size() && countChildren<TapeView>(root) == static_cast<int>(tapes.size()));
    for (std::size_t k = 0; k < tapes.size(); ++k) {
      const TapePlacement& p = layout.tapes[k];
      const auto b = FairyTapes::Ui::tapeBounds(p.cx, p.cy, p.w, p.h, p.rotDeg, FairyTapes::Ui::kTapeRaggedX + 1);
      const VSTGUI::CRect v = tapes[k]->getViewSize();
      ASSERT_TRUE(tapes[k]->slot() == p.slot);
      ASSERT_TRUE(std::fabs(v.left - b.left) < 1e-6 && std::fabs(v.right - b.right) < 1e-6 &&
                  std::fabs(v.top - b.top) < 1e-6 && std::fabs(v.bottom - b.bottom) < 1e-6);
    }
    // Layers: panel < silkscreen < wear < tapes < screws < the screen < the controls, MATCH, the LEVEL views.
    const int iWear = childIndex(root, wear);
    ASSERT_TRUE(childIndex(root, EditorTest::panel(ed)) < childIndex(root, EditorTest::silkView(ed)) &&
                childIndex(root, EditorTest::silkView(ed)) < iWear);
    int lastTape = iWear;
    for (TapeView* t : tapes) {
      ASSERT_TRUE(childIndex(root, t) > iWear);
      lastTape = std::max(lastTape, childIndex(root, t));
    }
    const int iScreen = childIndex(root, EditorTest::screen(ed));
    for (int i = 0; i < 4; ++i) {
      const int sc = childIndex(root, EditorTest::screw(ed, i));
      ASSERT_TRUE(sc > lastTape && sc < iScreen);
    }
    for (int i = 0; i < kNumControls; ++i) ASSERT_TRUE(childIndex(root, EditorTest::controlAt(ed, i)) > iScreen);
    ASSERT_TRUE(childIndex(root, EditorTest::match(ed)) > iScreen && childIndex(root, EditorTest::lufs(ed)) > iScreen);
  }
  EditorTest::destroy(ed, root);
}

// A click on a tape (delivered through the container, over the wear) opens a field; 40 typed characters (some not ASCII)
// are stored as the 14-character printable ASCII text through one editDecor; Escape stores nothing; leaving the field
// confirms; the same text again stores nothing.
void test_tape_text_edit_goes_to_decor() {
  noPrefs();
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0x5EEDBEEFu));
  Editor ed(&oc.ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(WearLevel::Heavy);
  ASSERT_TRUE(!EditorTest::tapes(ed).empty());
  TapeView* tape = EditorTest::tapes(ed)[0];
  const int slot = tape->slot();
  const VSTGUI::CPoint at = tape->tapeRect().getCenter();
  const std::uint32_t rev0 = oc.ctl.decorRevision();
  rootClick(root, at.x, at.y);
  VSTGUI::CTextEdit* field = EditorTest::tapeEdit(ed);
  ASSERT_TRUE(field != nullptr && root->isChild(field));
  const std::string typed = "  Lead vox \xC3\xA9 take 3 - keep it, sure!!!!";   // 40 bytes, two of them not ASCII
  ASSERT_TRUE(typed.size() == 40);
  field->setText(typed.c_str());
  ed.valueChanged(field);   // Enter
  EditorTest::closeTapeEdit(ed, true);
  ASSERT_TRUE(EditorTest::tapeEdit(ed) == nullptr && !root->isChild(field));
  ASSERT_TRUE(oc.ctl.decorRevision() == rev0 + 1);   // one editDecor
  const std::string stored = tapeTextFor(oc.ctl.decor(), slot);
  ASSERT_TRUE(stored == sanitizeTapeText(typed) && stored.size() == 14);
  for (char c : stored) ASSERT_TRUE(c >= 0x20 && c <= 0x7E);
  ASSERT_TRUE(tape->text() == stored);
  // Escape: nothing stored
  rootClick(root, at.x, at.y);
  field = EditorTest::tapeEdit(ed);
  ASSERT_TRUE(field != nullptr && field->getText() == stored.c_str());
  field->setText("other");
  VSTGUI::KeyboardEvent esc;
  esc.type = VSTGUI::EventType::KeyDown;
  esc.virt = VSTGUI::VirtualKey::Escape;
  field->onKeyboardEvent(esc);
  ed.valueChanged(field);
  EditorTest::closeTapeEdit(ed, false);
  ASSERT_TRUE(oc.ctl.decorRevision() == rev0 + 1 && tapeTextFor(oc.ctl.decor(), slot) == stored && tape->text() == stored);
  // leaving the field (no Enter) confirms what was typed
  rootClick(root, at.x, at.y);
  field = EditorTest::tapeEdit(ed);
  ASSERT_TRUE(field != nullptr);
  field->setText("MASTER v3");
  EditorTest::closeTapeEdit(ed, true);
  ASSERT_TRUE(oc.ctl.decorRevision() == rev0 + 2 && tapeTextFor(oc.ctl.decor(), slot) == "MASTER v3" && tape->text() == "MASTER v3");
  // the same text again: no edit, no dirty mark
  rootClick(root, at.x, at.y);
  field = EditorTest::tapeEdit(ed);
  ASSERT_TRUE(field != nullptr);
  field->setText("  MASTER v3 ");
  ed.valueChanged(field);
  EditorTest::closeTapeEdit(ed, true);
  ASSERT_TRUE(oc.ctl.decorRevision() == rev0 + 2);
  // the seed is the decor's own, not touched by writing on a tape
  ASSERT_TRUE(oc.ctl.decor().seed == 0x5EEDBEEFu);
  EditorTest::destroy(ed, root);
}

// A drag of a screw (delivered through the container): no decor edit while it moves, exactly one at the end with the
// user's rotation; the timer keeps the shown angle.
void test_screw_drag_stores_angle_once() {
  noPrefs();
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0x0DDBA11u));
  Editor ed(&oc.ctl);
  auto root = EditorTest::build(ed);
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(EditorTest::screw(ed, i) != nullptr);
  int index = -1;
  for (int i = 0; i < 4 && index < 0; ++i)
    if (!EditorTest::screw(ed, i)->spec().missing) index = i;
  ASSERT_TRUE(index >= 0);
  FairyTapes::Ui::ScrewView* screw = EditorTest::screw(ed, index);
  const double initial = screw->spec().initialAngle;
  ASSERT_TRUE(std::fabs(screw->angle() - FairyTapes::Ui::normalizeScrewAngle(initial)) < 1e-6);
  const std::uint32_t rev0 = oc.ctl.decorRevision();
  const VSTGUI::MouseEventButtonState left(VSTGUI::MouseButton::Left);
  const VSTGUI::CPoint p(screw->spec().x, screw->spec().y);
  VSTGUI::MouseDownEvent down(p, left);
  root->dispatchEvent(down);
  ASSERT_TRUE(down.consumed && screw->dragging());
  for (int k = 1; k <= 4; ++k) {
    VSTGUI::MouseMoveEvent move(VSTGUI::CPoint(p.x + 8 * k, p.y - 4 * k), left);
    root->dispatchEvent(move);
    ASSERT_TRUE(oc.ctl.decorRevision() == rev0);
  }
  VSTGUI::MouseUpEvent up(VSTGUI::CPoint(p.x + 32, p.y - 16), left);
  root->dispatchEvent(up);
  ASSERT_TRUE(!screw->dragging() && oc.ctl.decorRevision() == rev0 + 1);
  const double shown = screw->angle();
  ASSERT_TRUE(std::fabs(shown - FairyTapes::Ui::normalizeScrewAngle(initial)) > 1.0);   // it turned
  const float stored = oc.ctl.decor().screwAngles[index];
  ASSERT_TRUE(std::fabs(stored - userScrewAngle(shown, initial)) < 1e-4 && stored != 0.f);
  for (int i = 0; i < 4; ++i)
    if (i != index) ASSERT_TRUE(oc.ctl.decor().screwAngles[i] == 0.f);
  EditorTest::tick(ed);
  ASSERT_TRUE(std::fabs(screw->angle() - shown) < 1e-4 && oc.ctl.decorRevision() == rev0 + 1);
  // a click without a turn stores nothing
  rootClick(root, p.x, p.y);
  ASSERT_TRUE(oc.ctl.decorRevision() == rev0 + 1);
  EditorTest::destroy(ed, root);
}

// RE-ROLL: a new non-zero seed in the decor, rotations back to 0, tapes blank; wear, tapes and screws follow the new seed.
void test_reroll_changes_seed_and_clears_decor() {
  noPrefs();
  OwnController oc;
  Decor d = seededDecor(0xFACE0FFu);
  d.screwAngles[0] = 45.f;
  d.screwAngles[3] = 90.f;
  d.tapeTexts.assign(kNumTapeSlots, "WRITTEN");
  oc.ctl.editDecor(d);
  Editor ed(&oc.ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(WearLevel::Heavy);
  const std::uint32_t seed0 = ed.wearSeed();
  ASSERT_TRUE(seed0 == 0xFACE0FFu && seed0 == oc.ctl.decor().seed);
  ASSERT_TRUE(!EditorTest::tapes(ed).empty() && EditorTest::tapes(ed)[0]->text() == "WRITTEN");
  const std::uint32_t rev0 = oc.ctl.decorRevision();
  ed.reroll();
  ASSERT_TRUE(oc.ctl.decorRevision() == rev0 + 1);
  const Decor r = oc.ctl.decor();
  ASSERT_TRUE(r.seed != 0 && r.seed != seed0 && ed.wearSeed() == r.seed);
  for (float a : r.screwAngles) ASSERT_TRUE(a == 0.f);
  for (const std::string& t : r.tapeTexts) ASSERT_TRUE(t.empty());
  const auto& tapes = EditorTest::tapes(ed);
  const WearLayout layout = layoutFromSeed(r.seed, WearLevel::Heavy, kPanelRect);
  ASSERT_TRUE(tapes.size() == layout.tapes.size());
  for (std::size_t k = 0; k < tapes.size(); ++k) ASSERT_TRUE(tapes[k]->text().empty() && tapes[k]->slot() == layout.tapes[k].slot);
  const auto specs = screwSpecs(r.seed);
  for (int i = 0; i < 4; ++i) {
    const auto* s = EditorTest::screw(ed, i);
    ASSERT_TRUE(s != nullptr);
    ASSERT_TRUE(s->spec().cross == specs[static_cast<std::size_t>(i)].cross && s->spec().missing == specs[static_cast<std::size_t>(i)].missing &&
                s->spec().initialAngle == specs[static_cast<std::size_t>(i)].initialAngle);
    ASSERT_TRUE(std::fabs(s->angle() - FairyTapes::Ui::normalizeScrewAngle(specs[static_cast<std::size_t>(i)].initialAngle)) < 1e-6);
  }
  ASSERT_TRUE(EditorTest::wear(ed) && EditorTest::wear(ed)->marks().size() ==
              FairyTapes::Ui::generateWear(r.seed, WearLevel::Heavy, kEditorWidth, kEditorHeight).size());
  EditorTest::destroy(ed, root);
}

// The timer applies a decor changed elsewhere (state recall, undo) on its next run, and only then; with the same
// revision nothing is rebuilt; a new seed rebuilds the wear, the tapes and the screws.
void test_decor_change_applied_on_tick() {
  noPrefs();
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0xAB12CD34u));
  Editor ed(&oc.ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(WearLevel::Heavy);
  const std::vector<TapeView*> tapes = EditorTest::tapes(ed);
  ASSERT_TRUE(!tapes.empty());
  for (int i = 0; i < 5; ++i) EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::tapes(ed) == tapes);   // same revision: the same views
  Decor d = oc.ctl.decor();
  d = withTapeText(d, tapes[0]->slot(), "FROM HOST");
  d.screwAngles[1] = 30.f;
  oc.ctl.editDecor(d);
  ASSERT_TRUE(tapes[0]->text().empty());   // not before the timer
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::tapes(ed) == tapes && tapes[0]->text() == "FROM HOST");   // same seed: texts only
  const auto* s = EditorTest::screw(ed, 1);
  ASSERT_TRUE(s != nullptr && std::fabs(s->angle() - shownScrewAngle(s->spec().initialAngle, 30.f)) < 1e-4);
  // another seed (a state recalled): the tapes are made again, the wear and the screws follow
  Decor e = seededDecor(0x13572468u);
  oc.ctl.editDecor(e);
  EditorTest::tick(ed);
  ASSERT_TRUE(ed.wearSeed() == 0x13572468u);
  ASSERT_TRUE(EditorTest::wear(ed)->marks().size() ==
              FairyTapes::Ui::generateWear(0x13572468u, WearLevel::Heavy, kEditorWidth, kEditorHeight).size());
  ASSERT_TRUE(EditorTest::tapes(ed).size() == layoutFromSeed(0x13572468u, WearLevel::Heavy, kPanelRect).tapes.size());
  for (TapeView* t : EditorTest::tapes(ed)) ASSERT_TRUE(t->text().empty());
  const auto specs = screwSpecs(0x13572468u);
  for (int i = 0; i < 4; ++i)
    ASSERT_TRUE(EditorTest::screw(ed, i)->spec().initialAngle == specs[static_cast<std::size_t>(i)].initialAngle);
  EditorTest::destroy(ed, root);
}

// Seed 0 is "decor not ready" (the controller has no seed until the processor, a state or an edit gives one): nothing
// seeded is drawn, whatever the Wear level and however many timer runs pass; the revision that brings a seed makes the wear,
// the tapes and the screws on the next run, in their layers, and the run after changes nothing.
void test_seed_zero_waits_for_decor() {
  noPrefs();
  OwnController oc;
  ASSERT_TRUE(oc.ctl.decor().seed == 0);
  Editor ed(&oc.ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(WearLevel::Heavy);
  auto none = [&]() {
    bool ok = ed.wearSeed() == 0 && EditorTest::wear(ed) != nullptr && EditorTest::wear(ed)->marks().empty();
    ok = ok && EditorTest::tapes(ed).empty() && countChildren<TapeView>(root) == 0;
    ok = ok && countChildren<FairyTapes::Ui::ScrewView>(root) == 0;
    for (int i = 0; i < 4; ++i) ok = ok && EditorTest::screw(ed, i) == nullptr;
    return ok;
  };
  ASSERT_TRUE(none());
  for (int i = 0; i < 5; ++i) EditorTest::tick(ed);
  ASSERT_TRUE(none() && drawOffscreen(root) != nullptr);
  ed.setWearLevel(WearLevel::Light);
  ed.setWearLevel(WearLevel::Heavy);
  ASSERT_TRUE(none());
  // the decor arrives
  const std::uint32_t seed = 0x600DD0Cu;
  oc.ctl.editDecor(seededDecor(seed));
  ASSERT_TRUE(none());   // the timer has not run yet
  EditorTest::tick(ed);
  ASSERT_TRUE(ed.wearSeed() == seed);
  ASSERT_TRUE(EditorTest::wear(ed)->marks().size() == FairyTapes::Ui::generateWear(seed, WearLevel::Heavy, kEditorWidth, kEditorHeight).size());
  ASSERT_TRUE(!EditorTest::wear(ed)->marks().empty());
  const auto tapes = EditorTest::tapes(ed);
  ASSERT_TRUE(tapes.size() == layoutFromSeed(seed, WearLevel::Heavy, kPanelRect).tapes.size() && !tapes.empty());
  ASSERT_TRUE(countChildren<FairyTapes::Ui::ScrewView>(root) == 4);
  const int iScreen = childIndex(root, EditorTest::screen(ed));
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(EditorTest::screw(ed, i) != nullptr && childIndex(root, EditorTest::screw(ed, i)) < iScreen);
    for (TapeView* t : tapes) ASSERT_TRUE(childIndex(root, t) < childIndex(root, EditorTest::screw(ed, i)));
  }
  for (TapeView* t : tapes) ASSERT_TRUE(childIndex(root, t) > childIndex(root, EditorTest::wear(ed)));
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::tapes(ed) == tapes);
  EditorTest::destroy(ed, root);
}

// Preferences: a valid file is applied (Heavy, Rock Salt, plain values); a missing folder or a corrupt file gives the
// defaults (Light, Zeyada, values in the tape font) and the editor still has all its views.
void test_prefs_missing_file_opens_with_defaults() {
  namespace fs = std::filesystem;
  noPrefs();
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0xC0FFEE11u));
  const fs::path dir = g_prefsHome / "prefs_missing_file";
  const fs::path file = dir / "FairyTapes" / "HappyEnding.prefs";
  Editor ed(&oc.ctl);
  auto root = EditorTest::build(ed);
  auto isDefault = [&]() {
    return ed.wearLevel() == WearLevel::Light && ed.tapeFont() == TapeFont::Zeyada && ed.valuesHandwritten() &&
           EditorTest::valuesFont(ed) == FairyTapes::Ui::TapeFontId::Zeyada &&
           EditorTest::tapes(ed).size() == layoutFromSeed(ed.wearSeed(), WearLevel::Light, kPanelRect).tapes.size() &&
           EditorTest::prefs(ed).size == EditorSize::S100;
  };
  auto applyCustom = [&]() {
    Prefs p;
    p.wear = WearLevel::Heavy;
    p.tapeFont = TapeFont::RockSalt;
    p.handwrittenValues = false;
    p.size = EditorSize::S90;
    if (!savePrefs(file, p)) return false;
    EditorTest::loadAppearance(ed, file);
    return ed.wearLevel() == WearLevel::Heavy && ed.tapeFont() == TapeFont::RockSalt && !ed.valuesHandwritten() &&
           EditorTest::valuesFont(ed) == FairyTapes::Ui::TapeFontId::RockSalt &&
           EditorTest::prefs(ed).size == EditorSize::S90 &&
           EditorTest::wear(ed) && EditorTest::wear(ed)->marks().size() ==
               FairyTapes::Ui::generateWear(ed.wearSeed(), WearLevel::Heavy, kEditorWidth, kEditorHeight).size() &&
           EditorTest::tapes(ed).size() == layoutFromSeed(ed.wearSeed(), WearLevel::Heavy, kPanelRect).tapes.size();
  };
  ASSERT_TRUE(applyCustom());
  EditorTest::loadAppearance(ed, dir / "missing" / "FairyTapes" / "HappyEnding.prefs");   // no such folder
  ASSERT_TRUE(isDefault());
  ASSERT_TRUE(applyCustom());
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const char junk[] = {'\x00', '\xff', '\x13', 'w', 'e', 'a', 'r', '\x00', '=', '\x7f', '\xfe'};
    out.write(junk, sizeof junk);
  }
  EditorTest::loadAppearance(ed, file);
  ASSERT_TRUE(isDefault());
  for (int i = 0; i < kNumControls; ++i) ASSERT_TRUE(EditorTest::controlAt(ed, i) != nullptr);
  EditorTest::destroy(ed, root);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// An editor made while the user's file holds Heavy / Home-made Apple / plain values opens with them (the constructor reads the
// file; the views are made from it).
void test_prefs_applied_when_editor_made() {
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0xDEC0DE01u));
  writePrefs(prefsFile(), "wear=Heavy\ntapeFont=HomemadeApple\nvaluesFont=Standard\n");
  Editor ed(&oc.ctl);
  noPrefs();
  auto root = EditorTest::build(ed);
  const bool ok = ed.wearLevel() == WearLevel::Heavy && ed.tapeFont() == TapeFont::HomemadeApple && !ed.valuesHandwritten() &&
                  EditorTest::valuesFont(ed) == FairyTapes::Ui::TapeFontId::HomemadeApple &&
                  EditorTest::tapes(ed).size() == layoutFromSeed(ed.wearSeed(), WearLevel::Heavy, kPanelRect).tapes.size() &&
                  !EditorTest::wear(ed)->marks().empty();
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(ok);
}

// No grain asset: the editor with its wear, tapes and screws draws without crashing and, with Wear Off, the panel is
// plain (a blank row of the margin is one colour).
void test_missing_grain_asset_draws_plain_panel() {
  noPrefs();
  setResourcePath(false);
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0x7A11B0DDu));
  Editor ed(&oc.ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(WearLevel::Heavy);
  ASSERT_TRUE(drawOffscreen(root) != nullptr);
  ed.setWearLevel(WearLevel::Off);
  auto bmp = drawOffscreen(root);
  ASSERT_TRUE(bmp != nullptr);
  const Pixels px = pixelsOf(bmp);
  int lo = 255, hi = 0;
  for (int x = 6; x <= 20; ++x)
    for (int y = 290; y <= 320; y += 10) {
      lo = std::min<int>(lo, px.at(x, y).red);
      hi = std::max<int>(hi, px.at(x, y).red);
    }
  ASSERT_TRUE(hi - lo <= 2 && brightness(px.at(10, 300)) >= 0x08);
  EditorTest::destroy(ed, root);
}

// ---- Values font (carried note of the Task 9 review) ------------------------------------------------------------------
// The amber ink of the silkscreen view alone inside a layout rect: the number of pixels and their vertical extent.
struct Ink {
  int pixels = 0, top = 1 << 30, bottom = -1;
  int height() const { return bottom < top ? 0 : bottom - top + 1; }
};
Ink amberInk(const Pixels& px, int x0, int x1, int y0, int y1) {
  Ink ink;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const VSTGUI::CColor c = px.at(x, y);   // the bitmap holds premultiplied colours: judge the hue by ratios
      if (c.alpha >= 100 && c.red >= 60 && c.green * 100 >= c.red * 60 && c.green * 100 <= c.red * 88 && c.blue * 100 <= c.red * 30) {
        ++ink.pixels;
        ink.top = std::min(ink.top, y);
        ink.bottom = std::max(ink.bottom, y);
      }
    }
  return ink;
}

// With handwritten values (the default) the value under a knob is written in the tape font, with the digit height of the
// standard text (Arial bold, 0.716 em at the silkscreen's value size) times 1.3, whichever of the three fonts is chosen: the
// ink equals that of the shared drawTapeText at fontSizeForDigitHeight (the oracle, drawn on its own) and is about the digit
// height of the standard text times 1.3; plain values are the Arial digits; switching changes the drawing.
void test_values_in_tape_font_with_normalized_digits() {
  noPrefs();
  setResourcePath(false);
  OwnController oc;   // the knobs at their defaults: LOW prints a bare digit; no seed yet (the fixed variation seed)
  Editor ed(&oc.ctl);
  auto root = EditorTest::build(ed);
  ASSERT_TRUE(ed.valuesHandwritten() && EditorTest::valuesFont(ed) == FairyTapes::Ui::TapeFontId::Zeyada);
  const ControlSpec& low = *controlOf(kLow);
  const std::string text = EditorTest::silkValue(ed, printId(low));
  ASSERT_TRUE(!text.empty());
  for (char c : text) ASSERT_TRUE(std::isdigit(static_cast<unsigned char>(c)));   // digits only: the ink is the digit height
  const int cx = static_cast<int>(knobCentre(low).x), baseline = static_cast<int>(knobCentre(low).y + low.capRadius + kKnobValueBaselineBelowCap);
  auto inkOf = [&](VSTGUI::CBitmap* bmp) { return bmp ? amberInk(pixelsOf(bmp), cx - 30, cx + 30, baseline - 16, baseline + 8) : Ink{}; };
  auto ink = [&]() {
    auto bmp = drawOffscreen(EditorTest::silkView(ed));
    return inkOf(bmp);
  };
  // The oracle: the same text written by the shared kit at the normalized size, on the knob's centre and baseline.
  auto oracle = [&](FairyTapes::Ui::TapeFontId font) {
    auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(Editor::kWidth, Editor::kHeight), 1.0);
    if (!ctx) return Ink{};
    ctx->beginDraw();
    FairyTapes::Ui::drawTapeText(ctx, font, text, VSTGUI::CPoint(knobCentre(low).x, baseline),
                                 FairyTapes::Ui::normalizedFontSize(font, FairyTapes::Ui::kSilkValueSize), FairyTapes::Ui::kAmber,
                                 FairyTapes::Ui::kDefaultWearSeed + static_cast<std::uint32_t>(printId(low)) * 131u, 0.5,
                                 FairyTapes::Ui::TapeAlign::Center);
    ctx->endDraw();
    return inkOf(ctx->getBitmap());
  };
  const double want = FairyTapes::Ui::kStandardDigitHeight * FairyTapes::Ui::kSilkValueSize * FairyTapes::Ui::kLegibilityFactor;   // 9.3 px
  ed.setValuesFont(false);
  const Ink standard = ink();
  ASSERT_TRUE(standard.pixels > 10);
  ed.setValuesFont(true);
  std::vector<int> seen;
  for (TapeFont f : {TapeFont::Zeyada, TapeFont::RockSalt, TapeFont::HomemadeApple}) {
    ed.setTapeFont(f);
    const Ink h = ink();
    const Ink o = oracle(tapeFontIdOf(f));
    ASSERT_TRUE(h.pixels > 10 && o.pixels > 10);
    ASSERT_TRUE(h.pixels == o.pixels && h.top == o.top && h.bottom == o.bottom);   // the kit at the normalized size
    ASSERT_TRUE(h.height() >= want - 1.5 && h.height() <= want + 2.5);             // and that is the standard digit height x 1.3
    seen.push_back(h.pixels);
  }
  ASSERT_TRUE(seen[0] != seen[1] && seen[1] != seen[2] && seen[0] != standard.pixels);   // three hands, none the standard text
  EditorTest::destroy(ed, root);
}

// Every character a value can print is covered by the three tape fonts (printable ASCII only, no U+2212 / U+2014 / ellipsis
// among the knob and selector values: those belong to the LUFS screen, which stays in the standard font), so no value
// draws a blank where a letter should be.
void test_value_texts_covered_by_tape_fonts() {
  using FairyTapes::Ui::TapeFontId;
  resetAll(g_ctl);
  double all[kNumParams];
  normsOf(g_ctl, all);
  int checked = 0;
  for (int hp = 0; hp < 3; ++hp)
    for (int style = 0; style < 3; ++style) {
      all[kHpMs] = normFromIndex(kHpMs, hp);
      all[kLimitStyle] = normFromIndex(kLimitStyle, style);
      for (int k = 0; k < static_cast<int>(kNumParams); ++k) {
        const auto id = static_cast<ParamID>(k);
        for (int i = 0; i < kParams[id].steps; ++i) {
          std::vector<std::string> texts{formatValue(id, normFromIndex(id, i), all)};
          if (FairyTapes::HappyEnding::Ui::detail::isMsParam(id)) texts.push_back(FairyTapes::HappyEnding::Ui::detail::msSigla(i));
          if (id == kLimitRelease) texts.push_back(releaseText(style, i));
          for (const std::string& t : texts)
            for (unsigned char c : t) {
              ++checked;
              ASSERT_TRUE(c >= 0x20 && c <= 0x7E);
              for (TapeFontId f : {TapeFontId::Zeyada, TapeFontId::RockSalt, TapeFontId::HomemadeApple})
                ASSERT_TRUE(FairyTapes::Ui::findGlyph(f, c) != nullptr);
            }
        }
      }
    }
  ASSERT_TRUE(checked > 1000);
  // the LUFS screen, with its typographic characters, is not written by hand: it stays in the standard font
  ASSERT_TRUE(lufsText(-14.2).find(kMinusSign) != std::string::npos);
}

// The editor's views are built and removed 50 times with timer work in between and the UI timer started and closed each
// time: close() stops the timer and lets the views go (the LEVEL views, the LUFS screen and the ACT LEDs included), a tick
// after it is safe, and nothing is left referenced.
void test_editor_timer_stops_on_close() {
  resetAll(g_ctl);
  resetDisplays();
  for (int i = 0; i < 50; ++i) {
    Steinberg::IPlugView* view = g_ctl->createView(Steinberg::Vst::ViewType::kEditor);
    ASSERT_TRUE(view != nullptr);
    auto* ed = dynamic_cast<Editor*>(view);
    ASSERT_TRUE(ed != nullptr);
    auto root = EditorTest::build(*ed);
    ASSERT_TRUE(EditorTest::lufs(*ed) != nullptr && EditorTest::meter(*ed, 0) != nullptr && EditorTest::act(*ed, 1) != nullptr);
    EditorTest::startTimer(*ed);
    ASSERT_TRUE(EditorTest::timerRunning(*ed));
    g_ctl->setParamNormalized(kDispInL, (i % 10) / 10.0);
    g_ctl->setParamNormalized(kDispLufsShort, normFromLufs(-30.0 + i % 20));
    EditorTest::tick(*ed);
    ed->close();
    ASSERT_TRUE(!EditorTest::timerRunning(*ed));
    ASSERT_TRUE(EditorTest::lufs(*ed) == nullptr && EditorTest::meter(*ed, 0) == nullptr && EditorTest::clip(*ed, 3) == nullptr &&
                EditorTest::act(*ed, 0) == nullptr && EditorTest::grTags(*ed) == nullptr);
    EditorTest::tick(*ed);   // no views, no timer: safe
    root->removeAll(true);
    ASSERT_TRUE(root->getNbReference() == 1);
    ASSERT_TRUE(view->release() == 0);
  }
  resetDisplays();
  resetAll(g_ctl);
}

// ---- The shared screen (Task 11) --------------------------------------------------------------------------------------
// The screen view alone drawn offscreen (layout coordinates, so a pixel is read at its layout point).
VSTGUI::SharedPointer<VSTGUI::CBitmap> drawScreen(ScreenDisplayView* screen) { return drawOffscreen(screen); }
// The layout rect of `n` pixels around a layout point.
Rect around(const Point& p, float n) { return Rect{p.x - n, p.y - n, 2 * n + 1, 2 * n + 1}; }
// Everything back to the defaults, the display parameters at 0 and the controller's screen view on EQ (a test that failed
// halfway leaves its view behind).
void resetScreenTest() {
  resetAll(g_ctl);
  resetDisplays();
  if (auto* owner = dynamic_cast<Controller*>(g_ctl)) owner->setScreenView(ScreenView::Eq);
}
// The table of the controller (kNumParams normalized values).
void tableOf(double* n) { normsOf(g_ctl, n); }
// The value of position `index` of a parameter's table.
double tableValueOf(ParamID id, int index) {
  const ValueTable t = valueTable(id);
  return t.data[std::clamp(index, 0, static_cast<int>(t.size) - 1)];
}

// The five view buttons pick what the screen draws: each click makes that view the one drawn (a counter per view in the
// bench), the others stay as they were; a view made later starts from the controller's view.
void test_screen_view_switch() {
  resetScreenTest();
  auto* owner = dynamic_cast<Controller*>(g_ctl);
  ASSERT_TRUE(owner != nullptr);
  owner->setScreenView(ScreenView::Eq);
  {
    Editor ed(g_ctl);
    auto root = EditorTest::build(ed);
    auto* screen = EditorTest::screen(ed);
    ASSERT_TRUE(screen != nullptr && screen->view() == ScreenView::Eq);
    for (int pass = 0; pass < 2; ++pass) {   // forwards, then back to front
      for (int k = 0; k < 5; ++k) {
        const int i = pass == 0 ? k : 4 - k;
        const ActionSpec& a = kActions[i + 1];
        rootClick(root, a.rect.x + a.rect.w / 2, a.rect.y + a.rect.h / 2);
        ASSERT_TRUE(screen->view() == static_cast<ScreenView>(i));
        int before[5];
        for (int v = 0; v < 5; ++v) before[v] = screen->drawCount(static_cast<ScreenView>(v));
        ASSERT_TRUE(drawOffscreen(root) != nullptr);
        for (int v = 0; v < 5; ++v) ASSERT_TRUE(screen->drawCount(static_cast<ScreenView>(v)) == before[v] + (v == i ? 1 : 0));
      }
    }
    // the views differ on the screen: the EQ view has the 0 dB line across (a bright line), the others do not
    owner->setScreenView(ScreenView::Eq);
    screen->setView(ScreenView::Eq);
    auto eq = drawScreen(screen);
    const int zeroLine = brightAt(pixelsOf(eq), kScreenRect.x + 6, eqY(0.0));
    screen->setView(ScreenView::Clip);
    auto clip = drawScreen(screen);
    ASSERT_TRUE(zeroLine >= 50);
    ASSERT_TRUE(brightAt(pixelsOf(clip), kScreenRect.x + 6, eqY(0.0)) < 30);
    EditorTest::destroy(ed, root);
  }
  owner->setScreenView(ScreenView::Limit);
  {
    Editor ed(g_ctl);
    auto root = EditorTest::build(ed);
    const bool limit = EditorTest::screen(ed)->view() == ScreenView::Limit;
    EditorTest::destroy(ed, root);
    ASSERT_TRUE(limit);
  }
  owner->setScreenView(ScreenView::Eq);
  resetDisplays();
  resetAll(g_ctl);
}

// The EQ shapes are cached: a hundred timer runs and redraws without a change recompute nothing; a change of GAIN
// recomputes the FINE curves once (and nothing else); a parameter that feeds no EQ shape recomputes nothing; a VIBE
// parameter recomputes the VIBE curves; a new published DYN pull-back only the VIBE curve that follows it.
void test_eq_curves_recomputed_only_on_change() {
  resetScreenTest();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto* screen = EditorTest::screen(ed);
  ASSERT_TRUE(screen != nullptr && screen->view() == ScreenView::Eq);
  setStage(Stage::Vibe, true);
  EditorTest::tick(ed);
  drawScreen(screen);
  const int base = screen->recomputes();
  ASSERT_TRUE(base >= 2);   // the first draw built the curves
  for (int i = 0; i < 100; ++i) {
    EditorTest::tick(ed);
    drawScreen(screen);
  }
  ASSERT_TRUE(screen->recomputes() == base);
  g_ctl->setParamNormalized(kMidGain, normFromIndex(kMidGain, 12));
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(screen->recomputes() == base + 1);
  for (int i = 0; i < 20; ++i) {
    EditorTest::tick(ed);
    drawScreen(screen);
  }
  ASSERT_TRUE(screen->recomputes() == base + 1);
  g_ctl->setParamNormalized(kGlueThresh, normFromIndex(kGlueThresh, 3));   // no EQ shape
  g_ctl->setParamNormalized(kBypass, 1.0);
  EditorTest::tick(ed);
  drawScreen(screen);
  g_ctl->setParamNormalized(kBypass, 0.0);
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(screen->recomputes() == base + 1);
  g_ctl->setParamNormalized(kTilt, normFromIndex(kTilt, 0));   // VIBE EQ
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(screen->recomputes() == base + 3);   // its static curve and the one with the pull-back
  g_ctl->setParamNormalized(kDispDynLow, 0.4);   // the published pull-back
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(screen->recomputes() == base + 4);
  for (int i = 0; i < 10; ++i) {   // the same value again
    EditorTest::tick(ed);
    drawScreen(screen);
  }
  ASSERT_TRUE(screen->recomputes() == base + 4);
  // the curves really move with the parameters: the amber VIBE curve is there, and dashed under the pull-back
  ASSERT_TRUE(screen->vibeDashedDrawn());
  g_ctl->setParamNormalized(kDispDynLow, 0.0);
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(!screen->vibeDashedDrawn());
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// CLIP and FINAL cache their curves the same way: a change of DRIVE (or LOUDNESS) recomputes the CLIP curve once, a
// change of CEILING the FINAL one; nothing else does.
void test_transfer_curves_recomputed_only_on_change() {
  resetScreenTest();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto* screen = EditorTest::screen(ed);
  screen->setView(ScreenView::Clip);
  EditorTest::tick(ed);
  drawScreen(screen);
  const int base = screen->recomputes();
  ASSERT_TRUE(base == 1);
  for (int i = 0; i < 50; ++i) {
    g_ctl->setParamNormalized(kDispClipIn, (i % 7) / 10.0);   // the dot moves, the curve does not
    EditorTest::tick(ed);
    drawScreen(screen);
  }
  ASSERT_TRUE(screen->recomputes() == base);
  g_ctl->setParamNormalized(kClipDrive, normFromIndex(kClipDrive, 10));
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(screen->recomputes() == base + 1);
  g_ctl->setParamNormalized(kCeiling, normFromIndex(kCeiling, 3));   // not the CLIP curve
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(screen->recomputes() == base + 1);
  screen->setView(ScreenView::Final);
  drawScreen(screen);
  ASSERT_TRUE(screen->recomputes() == base + 2);   // built when first shown, with the new ceiling
  for (int i = 0; i < 20; ++i) {
    EditorTest::tick(ed);
    drawScreen(screen);
  }
  ASSERT_TRUE(screen->recomputes() == base + 2);
  g_ctl->setParamNormalized(kCeiling, normFromIndex(kCeiling, 0));
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(screen->recomputes() == base + 3);
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// The GLUE and LIMIT histories get a point on every timer run, whatever view is shown, up to kHistoryPoints; the oldest
// one then falls off. Each point carries the display values (and for GLUE the net reduction after MIX).
void test_scroll_history_grows() {
  resetScreenTest();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto* screen = EditorTest::screen(ed);
  ASSERT_TRUE(screen != nullptr && screen->view() == ScreenView::Eq);   // not a history view: it grows all the same
  const std::size_t n = kHistoryPoints;
  ASSERT_TRUE(screen->historySize(ScreenView::Glue) < 5 && screen->historySize(ScreenView::Limit) < 5);
  for (std::size_t i = 0; i < 10; ++i) {
    const std::size_t g = screen->historySize(ScreenView::Glue), l = screen->historySize(ScreenView::Limit);
    EditorTest::tick(ed);
    ASSERT_TRUE(screen->historySize(ScreenView::Glue) == g + 1 && screen->historySize(ScreenView::Limit) == l + 1);
  }
  // a known stream: the level of tick k is a different value each time
  const int total = static_cast<int>(n) + 50;
  auto levelNorm = [](int k) { return (k % 40) / 40.0; };
  for (int k = 0; k < total; ++k) {
    g_ctl->setParamNormalized(kDispGlueIn, levelNorm(k));
    EditorTest::tick(ed);
  }
  ASSERT_TRUE(screen->historySize(ScreenView::Glue) == n && screen->historySize(ScreenView::Limit) == n);
  const auto& h = screen->history(ScreenView::Glue);
  ASSERT_TRUE(h.at(n - 1).levelDb == static_cast<float>(stageDbFromNorm(levelNorm(total - 1))));
  ASSERT_TRUE(h.at(0).levelDb == static_cast<float>(stageDbFromNorm(levelNorm(total - n))));   // the 51st tick is the oldest left
  ASSERT_TRUE(h.at(1).levelDb == static_cast<float>(stageDbFromNorm(levelNorm(total - n + 1))));
  // what a point carries: GLUE level, GR and the net reduction at MIX 50 %; LIMIT level and GR (the L row)
  g_ctl->setParamNormalized(kDispGlueIn, normFromStageDb(-12.0));
  g_ctl->setParamNormalized(kDispGlueGr, normFromGrDb(6.0));
  g_ctl->setParamNormalized(kDispLimitIn, normFromStageDb(-5.0));
  g_ctl->setParamNormalized(kDispGr2, normFromGrDb(3.0));
  EditorTest::tick(ed);
  const ScrollPoint gp = screen->newest(ScreenView::Glue), lp = screen->newest(ScreenView::Limit);
  ASSERT_TRUE(std::fabs(gp.levelDb - (-12.0f)) < 0.1f && std::fabs(gp.grDb - 6.0f) < 0.01f);
  const double net = -20.0 * std::log10(0.5 + 0.5 * std::pow(10.0, -6.0 / 20.0));
  ASSERT_TRUE(std::fabs(gp.netGrDb - net) < 0.01);
  ASSERT_TRUE(std::fabs(gp.threshDb - tableValueOf(kGlueThresh, kParams[kGlueThresh].defaultIndex)) < 1e-4);
  ASSERT_TRUE(std::fabs(lp.levelDb - (-5.0f)) < 0.1f && std::fabs(lp.grDb - 3.0f) < 0.01f);
  ASSERT_TRUE(std::fabs(lp.threshDb - tableValueOf(kCeiling, kParams[kCeiling].defaultIndex)) < 1e-4);   // the line LIMIT draws
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// The screen dims with BYPASS and with nothing else: not with a stage out, LISTEN, SET or a stage's IN off; BYPASS dims
// the curve to 35 % and back.
void test_screen_dims_only_on_bypass() {
  resetScreenTest();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto* screen = EditorTest::screen(ed);
  screen->setView(ScreenView::Clip);
  double norm[kNumParams];
  tableOf(norm);
  const std::vector<Point> curve = curvePath(ScreenView::Clip, norm, ceilingLinear(norm));
  ASSERT_TRUE(curve.size() > 60);
  const Point on = curve[60];
  auto curveLevel = [&] {
    auto bmp = drawScreen(screen);
    return brightIn(pixelsOf(bmp), around(on, 1));
  };
  EditorTest::tick(ed);
  ASSERT_TRUE(!screen->isDimmed());
  const int live = curveLevel();
  ASSERT_TRUE(live > 200);
  for (int s = 0; s < kNumStages; ++s) {   // a stage out dims nothing
    setStage(static_cast<Stage>(s), s == static_cast<int>(Stage::Metal));   // all of them flipped from their default
    EditorTest::tick(ed);
    ASSERT_TRUE(!screen->isDimmed());
    setStage(static_cast<Stage>(s), s != static_cast<int>(Stage::Metal));
  }
  resetAll(g_ctl);
  g_ctl->setParamNormalized(kListenBand, normFromListen(1));
  g_ctl->setParamNormalized(kSetActive, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(!screen->isDimmed());
  g_ctl->setParamNormalized(kListenBand, kDefaultListenNorm);
  g_ctl->setParamNormalized(kSetActive, kDefaultSetNorm);
  setStage(Stage::Clip, false);
  EditorTest::tick(ed);
  ASSERT_TRUE(!screen->isDimmed());
  setStage(Stage::Clip, true);
  g_ctl->setParamNormalized(kBypass, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(screen->isDimmed());
  for (ScreenView v : {ScreenView::Eq, ScreenView::Glue, ScreenView::Clip, ScreenView::Limit, ScreenView::Final}) {
    screen->setView(v);
    ASSERT_TRUE(screen->isDimmed());
  }
  screen->setView(ScreenView::Clip);
  const int dimmed = curveLevel();
  ASSERT_TRUE(dimmed < 130 && dimmed > 40);   // 35 % of the amber over the dark screen
  g_ctl->setParamNormalized(kBypass, 0.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(!screen->isDimmed());
  ASSERT_TRUE(curveLevel() == live);
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// While LISTEN hears a bell (and its band and FINE EQ are in) the EQ view draws the zone it lets through; without LISTEN,
// with the band or the stage out, or in another view, there is none.
void test_listen_zone_drawn() {
  resetScreenTest();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto* screen = EditorTest::screen(ed);
  g_ctl->setParamNormalized(kFineOn, 1.0);
  g_ctl->setParamNormalized(kMidOn, 1.0);
  EditorTest::tick(ed);
  auto zone = [&] {
    auto bmp = drawScreen(screen);
    return screen->listenZoneDrawn();
  };
  ASSERT_TRUE(!zone());
  // a pixel under the peak of the MID bell (400 Hz .. 3.15 kHz, default 1 kHz index 4): amber tinted when the zone is there
  const double mid = tableValueOf(kMidFreq, indexFromNorm(kMidFreq, g_ctl->getParamNormalized(kMidFreq)));
  const Point under{eqX(mid) + 3.0f, kScreenRect.y + kScreenRect.h - 44.0f};
  auto shade = [&] {
    auto bmp = drawScreen(screen);
    const VSTGUI::CColor c = pixelsOf(bmp).at(static_cast<int>(under.x), static_cast<int>(under.y));
    return c.red - c.blue;   // amber leans red, the grid and the dark screen do not
  };
  const int without = shade();
  g_ctl->setParamNormalized(kListenBand, normFromListen(1));
  EditorTest::tick(ed);
  ASSERT_TRUE(zone());
  ASSERT_TRUE(shade() >= without + 15);
  g_ctl->setParamNormalized(kListenBand, normFromListen(0));   // LO MID, which is in by default too
  EditorTest::tick(ed);
  ASSERT_TRUE(zone());
  g_ctl->setParamNormalized(kMidOn, 0.0);
  g_ctl->setParamNormalized(kListenBand, normFromListen(1));   // its band is out: LISTEN counts as off
  EditorTest::tick(ed);
  ASSERT_TRUE(!zone());
  g_ctl->setParamNormalized(kMidOn, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(zone());
  g_ctl->setParamNormalized(kFineOn, 0.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(!zone());
  g_ctl->setParamNormalized(kFineOn, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(zone());
  screen->setView(ScreenView::Glue);
  ASSERT_TRUE(!zone());
  screen->setView(ScreenView::Eq);
  g_ctl->setParamNormalized(kListenBand, kDefaultListenNorm);
  EditorTest::tick(ed);
  ASSERT_TRUE(!zone());
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// The dot of CLIP and FINAL follows the stage's own input (kDispClipIn / kDispFinalIn) on the curve of the stage, and the
// FINAL view draws the ceiling line.
void test_transfer_dot_follows_stage_input() {
  resetScreenTest();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto* screen = EditorTest::screen(ed);
  double norm[kNumParams];
  for (double db : {-20.0, -6.0, 0.0}) {
    tableOf(norm);
    g_ctl->setParamNormalized(kDispClipIn, normFromStageDb(db));
    g_ctl->setParamNormalized(kDispFinalIn, normFromStageDb(db + 3.0));   // a different value: the views must not mix them up
    EditorTest::tick(ed);
    const double lin = std::pow(10.0, stageDbFromNorm(normFromStageDb(db)) / 20.0);
    screen->setView(ScreenView::Clip);
    drawScreen(screen);
    ASSERT_TRUE(std::fabs(screen->dot().x - clipX(lin)) < 0.01);
    ASSERT_TRUE(std::fabs(screen->dot().y - clipY(std::fabs(clipTransfer(lin, norm)))) < 0.01);
    const double linF = std::pow(10.0, stageDbFromNorm(normFromStageDb(db + 3.0)) / 20.0);
    screen->setView(ScreenView::Final);
    drawScreen(screen);
    ASSERT_TRUE(std::fabs(screen->dot().x - finalX(linF)) < 0.01);
    ASSERT_TRUE(std::fabs(screen->dot().y - finalY(std::fabs(finalTransfer(linF, norm, ceilingLinear(norm))))) < 0.01);
  }
  // the dot is a drawn ring: its centre is dark, its rim light, at the dot's place
  screen->setView(ScreenView::Clip);
  g_ctl->setParamNormalized(kDispClipIn, normFromStageDb(-6.0));
  EditorTest::tick(ed);
  auto bmp = drawScreen(screen);
  const Pixels px = pixelsOf(bmp);
  const Point d = screen->dot();
  ASSERT_TRUE(brightIn(px, around(Point{d.x + 3.6f, d.y}, 1)) > 150);
  // FINAL: the ceiling line across the screen, at the CEILING of the table
  screen->setView(ScreenView::Final);
  g_ctl->setParamNormalized(kCeiling, normFromIndex(kCeiling, 3));   // -1 dB
  g_ctl->setParamNormalized(kDispFinalIn, 0.0);
  EditorTest::tick(ed);
  tableOf(norm);
  const float cy = finalCeilingY(ceilingLinear(norm));
  auto bmp2 = drawScreen(screen);
  const Pixels px2 = pixelsOf(bmp2);
  int lit = 0;
  // left of where the curve gets there (its low part is far below the ceiling), so only the line can light these columns
  for (int x = static_cast<int>(finalX(0.05)); x < static_cast<int>(finalX(0.5)); ++x)
    lit += std::max(brightness(px2.at(x, static_cast<int>(cy) - 1)), brightness(px2.at(x, static_cast<int>(cy)))) > 70 ? 1 : 0;
  ASSERT_TRUE(lit > 40);   // a dashed line: about half of the columns
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}

// LIMIT follows its own state: with LIMIT out the reduction it keeps is flat (the L row's stale value is not taken) and the
// effective-threshold line, published by the engine even then, is not drawn; with LIMIT in both are live.
void test_limit_view_follows_limit_state() {
  resetScreenTest();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto* screen = EditorTest::screen(ed);
  screen->setView(ScreenView::Limit);
  g_ctl->setParamNormalized(kDispLimitThresh, normFromThreshDb(-3.0));
  g_ctl->setParamNormalized(kDispGr2, normFromGrDb(4.0));
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(screen->limitThreshDrawn());
  ASSERT_TRUE(std::fabs(screen->newest(ScreenView::Limit).grDb - 4.0f) < 0.01f);
  setStage(Stage::Limit, false);
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(!screen->limitThreshDrawn());
  ASSERT_TRUE(screen->newest(ScreenView::Limit).grDb == 0.0f);
  ASSERT_TRUE(!screen->isDimmed());
  setStage(Stage::Limit, true);
  EditorTest::tick(ed);
  drawScreen(screen);
  ASSERT_TRUE(screen->limitThreshDrawn() && std::fabs(screen->newest(ScreenView::Limit).grDb - 4.0f) < 0.01f);
  screen->setView(ScreenView::Glue);   // GLUE has no such line, and a threshold line of its own
  drawScreen(screen);
  ASSERT_TRUE(!screen->limitThreshDrawn());
  EditorTest::destroy(ed, root);
  resetDisplays();
  resetAll(g_ctl);
}
// ---- Back of the panel (Task 13) ---------------------------------------------------------------------------------------
VSTGUI::CRect boxRect(const FairyTapes::Ui::BackBox& b) { return VSTGUI::CRect(b.x, b.y, b.x + b.w, b.y + b.h); }

// The child of `c` of type T whose rectangle is `r` (nullptr when there is none).
template <class T>
T* childAt(VSTGUI::CViewContainer* c, const VSTGUI::CRect& r) {
  T* found = nullptr;
  c->forEachChild([&](VSTGUI::CView* v) {
    if (auto* t = dynamic_cast<T*>(v); t && !found && v->getViewSize() == r) found = t;
  });
  return found;
}

bool leftClick(VSTGUI::CView* v, VSTGUI::CPoint p) {
  return v && v->onMouseDown(p, VSTGUI::CButtonState(VSTGUI::kLButton)) == VSTGUI::kMouseEventHandled;
}

// The widget of SETTINGS item `key` on the back (nullptr when there is none).
template <class T>
T* settingWidget(FairyTapes::Ui::BackView* back, int key) {
  const auto& items = back->content().items;
  const int i = FairyTapes::Ui::findItem(back->content(), key);
  if (i < 0) return nullptr;
  const auto box = FairyTapes::Ui::itemControlBox(items[static_cast<std::size_t>(i)],
                                                  FairyTapes::Ui::itemRow(items, static_cast<std::size_t>(i)), back->layout());
  return childAt<T>(back, boxRect(box));
}

// A click on option `index` of a SETTINGS choice row.
bool chooseOption(FairyTapes::Ui::BackView* back, int key, int index) {
  auto* row = settingWidget<FairyTapes::Ui::SegmentRow>(back, key);
  if (!row) return false;
  const VSTGUI::CRect r = row->getViewSize();
  const int n = static_cast<int>(back->content().items[static_cast<std::size_t>(FairyTapes::Ui::findItem(back->content(), key))].options.size());
  return leftClick(row, VSTGUI::CPoint(r.left + r.getWidth() * (index + 0.5) / n, r.getCenter().y));
}

std::filesystem::path scratchPrefsDir(const char* name) { return g_prefsHome / name; }

std::string fileText(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The window size the editor reports to the host.
Steinberg::ViewRect windowOf(Editor& ed) {
  Steinberg::ViewRect r{};
  ed.getSize(&r);
  return r;
}

// "BACK ->" in the header turns the panel (the timer runs the turn, about 0.8 s); a text field open on a tape is confirmed
// first; "<- BACK" turns it again. The back has the geometry of Happy Ending's own panel (1280 x 890).
void test_flip_roundtrip_and_text_edit_confirmed_first() {
  noPrefs();
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0x7A11CAFEu));
  Editor ed(&oc.ctl);
  auto root = EditorTest::buildSides(ed);
  ed.setWearLevel(WearLevel::Heavy);
  VSTGUI::CViewContainer* front = EditorTest::front(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(front != nullptr && back != nullptr && root->isChild(front) && root->isChild(back));
  ASSERT_TRUE(front->isVisible() && !back->isVisible());
  ASSERT_TRUE(EditorTest::panel(ed) != nullptr && front->isChild(EditorTest::panel(ed)));   // the front views are in the front side
  ASSERT_TRUE(back->getViewSize() == VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
  ASSERT_TRUE(back->layout().width == 1280 && back->layout().height == 890 && back->layout().headerRight == 1246);
  FairyTapes::Ui::TextLink* link = EditorTest::backLink(ed);
  ASSERT_TRUE(link != nullptr && front->isChild(link) && link->getViewSize() == boxRect(FairyTapes::Ui::frontBackLinkBox(kBackLayout)));
  ASSERT_TRUE(link->getViewSize().right == 1246);   // right-aligned at the preview's x
  // a tape field left open with a text typed in
  ASSERT_TRUE(!EditorTest::tapes(ed).empty());
  TapeView* tape = EditorTest::tapes(ed)[0];
  const int slot = tape->slot();
  const std::uint32_t rev0 = oc.ctl.decorRevision();
  ASSERT_TRUE(leftClick(tape, tape->tapeRect().getCenter()));
  VSTGUI::CTextEdit* field = EditorTest::tapeEdit(ed);
  ASSERT_TRUE(field != nullptr);
  field->setText("TAKE 2");
  ASSERT_TRUE(leftClick(link, link->getViewSize().getCenter()));
  ASSERT_TRUE(EditorTest::tapeEdit(ed) == nullptr && !front->isChild(field) && !root->isChild(field));
  ASSERT_TRUE(oc.ctl.decorRevision() == rev0 + 1 && tapeTextFor(oc.ctl.decor(), slot) == "TAKE 2" && tape->text() == "TAKE 2");
  // the turn runs on the timer and ends with the back shown
  ASSERT_TRUE(EditorTest::flip(ed).running() && EditorTest::flip(ed).showingBack());
  ASSERT_TRUE(!front->isVisible() && !back->isVisible());
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::flip(ed).running());   // not over after one timer run
  std::this_thread::sleep_for(std::chrono::milliseconds(900));
  EditorTest::tick(ed);
  ASSERT_TRUE(!EditorTest::flip(ed).running() && back->isVisible() && !front->isVisible());
  // "<- BACK" at the top right of the back
  auto* home = childAt<FairyTapes::Ui::TextLink>(back, boxRect(FairyTapes::Ui::backLinkBox(kBackLayout)));
  ASSERT_TRUE(home != nullptr && home->getViewSize().right == 1246 && leftClick(home, home->getViewSize().getCenter()));
  ASSERT_TRUE(EditorTest::flip(ed).running() && !EditorTest::flip(ed).showingBack());
  EditorTest::flip(ed).finish();
  ASSERT_TRUE(front->isVisible() && !back->isVisible());
  ASSERT_TRUE(oc.ctl.decorRevision() == rev0 + 1);   // nothing else written
  EditorTest::destroy(ed, root);
}

// WEAR on the back: the front changes at once (marks and tapes for the level, no rebuild), the choice is saved in the
// preferences file without touching the other fields (SIZE included), and the back shows it.
void test_settings_apply_wear_at_once() {
  namespace fs = std::filesystem;
  const fs::path dir = scratchPrefsDir("he_settings_wear");
  const fs::path file = dir / "FairyTapes" / "HappyEnding.prefs";
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0x3333AAAAu));
  Prefs start;
  start.tapeFont = TapeFont::HomemadeApple;
  start.notes = false;
  start.size = EditorSize::S75;
  ASSERT_TRUE(savePrefs(file, start));
  Editor ed(&oc.ctl);
  EditorTest::loadAppearance(ed, file);   // as open() does, before the views
  auto root = EditorTest::buildSides(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(back != nullptr && back->tab() == kTabSettings);
  ASSERT_TRUE(back->selected(kSetWear) == 1 && back->selected(kSetTapeFont) == 2 && back->selected(kSetValuesFont) == 1 &&
              back->selected(kSetSize) == 0);
  auto* wear = EditorTest::wear(ed);
  const std::uint32_t seed = ed.wearSeed();
  for (int level : {2, 0, 1}) {
    ASSERT_TRUE(chooseOption(back, kSetWear, level));
    const auto w = static_cast<WearLevel>(level);
    ASSERT_TRUE(ed.wearLevel() == w && EditorTest::wear(ed) == wear);
    ASSERT_TRUE(wear->marks().size() == FairyTapes::Ui::generateWear(seed, w, kEditorWidth, kEditorHeight).size());
    ASSERT_TRUE(EditorTest::tapes(ed).size() == layoutFromSeed(seed, w, kPanelRect).tapes.size());
    ASSERT_TRUE(back->selected(kSetWear) == level);
    const Prefs saved = loadPrefs(file);
    ASSERT_TRUE(saved.wear == w && saved.tapeFont == TapeFont::HomemadeApple && saved.handwrittenValues && !saved.notes &&
                saved.size == EditorSize::S75);
  }
  ASSERT_TRUE(ed.wearSeed() == seed);   // the seed belongs to the instance: not touched by WEAR
  EditorTest::destroy(ed, root);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// TAPE FONT, VALUES FONT and RE-ROLL on the back: applied at once, the fonts saved, RE-ROLL a new seed for the instance.
void test_settings_fonts_and_reroll_apply() {
  namespace fs = std::filesystem;
  const fs::path dir = scratchPrefsDir("he_settings_fonts");
  const fs::path file = dir / "FairyTapes" / "HappyEnding.prefs";
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0x4444BBBBu));
  Editor ed(&oc.ctl);
  EditorTest::loadAppearance(ed, file);   // no file yet: the defaults
  auto root = EditorTest::buildSides(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(back != nullptr);
  auto* stepper = settingWidget<FairyTapes::Ui::OptionStepper>(back, kSetTapeFont);
  ASSERT_TRUE(stepper != nullptr);
  const VSTGUI::CRect r = stepper->getViewSize();
  ASSERT_TRUE(leftClick(stepper, VSTGUI::CPoint(r.right - 4, r.getCenter().y)));   // ">"
  ASSERT_TRUE(ed.tapeFont() == TapeFont::RockSalt && EditorTest::valuesFont(ed) == FairyTapes::Ui::TapeFontId::RockSalt);
  ASSERT_TRUE(loadPrefs(file).tapeFont == TapeFont::RockSalt && back->selected(kSetTapeFont) == 1);
  ASSERT_TRUE(leftClick(stepper, VSTGUI::CPoint(r.left + 4, r.getCenter().y)));    // "<"
  ASSERT_TRUE(leftClick(stepper, VSTGUI::CPoint(r.left + 4, r.getCenter().y)));    // "<" wraps around
  ASSERT_TRUE(ed.tapeFont() == TapeFont::HomemadeApple && loadPrefs(file).tapeFont == TapeFont::HomemadeApple);
  ASSERT_TRUE(chooseOption(back, kSetValuesFont, 0));
  ASSERT_TRUE(!ed.valuesHandwritten() && !loadPrefs(file).handwrittenValues && loadPrefs(file).tapeFont == TapeFont::HomemadeApple);
  ASSERT_TRUE(chooseOption(back, kSetValuesFont, 1));
  ASSERT_TRUE(ed.valuesHandwritten() && loadPrefs(file).handwrittenValues);
  const std::uint32_t seed0 = ed.wearSeed();
  auto* reroll = settingWidget<FairyTapes::Ui::PushButton>(back, kSetLook);
  ASSERT_TRUE(reroll != nullptr && leftClick(reroll, reroll->getViewSize().getCenter()));
  ASSERT_TRUE(ed.wearSeed() != seed0 && ed.wearSeed() == oc.ctl.decor().seed);
  ASSERT_TRUE(loadPrefs(file).tapeFont == TapeFont::HomemadeApple);   // RE-ROLL is not a preference
  EditorTest::destroy(ed, root);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// The preferences folder cannot be written (a file where a folder should be): the editor goes on, the choice applies for the
// session and the back shows it.
void test_settings_failed_write_is_ignored() {
  namespace fs = std::filesystem;
  const fs::path dir = scratchPrefsDir("he_settings_ro");
  fs::create_directories(dir);
  { std::ofstream(dir / "blocker") << "x"; }
  const fs::path file = dir / "blocker" / "FairyTapes" / "HappyEnding.prefs";
  ASSERT_TRUE(!savePrefs(file, Prefs{}));   // the write really fails
  OwnController oc;
  oc.ctl.editDecor(seededDecor(0x5555CCCCu));
  Editor ed(&oc.ctl);
  EditorTest::loadAppearance(ed, file);
  auto root = EditorTest::buildSides(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(back != nullptr && ed.wearLevel() == WearLevel::Light);
  ASSERT_TRUE(chooseOption(back, kSetWear, 2));
  ASSERT_TRUE(ed.wearLevel() == WearLevel::Heavy && back->selected(kSetWear) == 2 && EditorTest::prefs(ed).wear == WearLevel::Heavy);
  ASSERT_TRUE(EditorTest::wear(ed)->marks().size() ==
              FairyTapes::Ui::generateWear(ed.wearSeed(), WearLevel::Heavy, kEditorWidth, kEditorHeight).size());
  ASSERT_TRUE(chooseOption(back, kSetValuesFont, 0));
  ASSERT_TRUE(!ed.valuesHandwritten() && ed.wearLevel() == WearLevel::Heavy);   // the earlier choice stays
  ASSERT_TRUE(chooseOption(back, kSetSize, 1));   // SIZE too: no window, no request, the choice is kept for the session
  ASSERT_TRUE(EditorTest::prefs(ed).size == EditorSize::S90 && back->selected(kSetSize) == 1);
  ASSERT_TRUE(!fs::exists(file) && fs::is_regular_file(dir / "blocker"));
  EditorTest::tick(ed);
  ASSERT_TRUE(ed.wearLevel() == WearLevel::Heavy && !ed.valuesHandwritten());
  EditorTest::destroy(ed, root);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// The tab links, left to right: SETTINGS, BUTTONS, ROUTING, CREDITS (at 880 / 970 / 1060 / 1150); a click shows the tab (its
// name is the big title) and the editor remembers it while it lives; the SETTINGS widgets show only on SETTINGS (the
// OVERSAMPLING line draws only there: test_back_settings_drawn_at_the_preview_geometry).
void test_tab_links_order() {
  noPrefs();
  Editor ed(g_ctl);
  auto root = EditorTest::buildSides(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(back != nullptr);
  const auto& c = back->content();
  ASSERT_TRUE(c.headerLine == "HAPPY ENDING  \xC2\xB7  MASTERING VST3");
  const char* labels[] = {"SETTINGS", "BUTTONS", "ROUTING", "CREDITS"};
  const FairyTapes::Ui::BackTabKind kinds[] = {FairyTapes::Ui::BackTabKind::Settings, FairyTapes::Ui::BackTabKind::Custom,
                                               FairyTapes::Ui::BackTabKind::Routing, FairyTapes::Ui::BackTabKind::Credits};
  const double right[] = {880, 970, 1060, 1150};
  ASSERT_TRUE(c.tabs.size() == 4);
  double lastX = -1;
  auto* wearRow = settingWidget<FairyTapes::Ui::SegmentRow>(back, kSetWear);
  VSTGUI::CView* note = EditorTest::oversamplingNote(ed);
  ASSERT_TRUE(wearRow != nullptr && note != nullptr && back->isChild(note) && !note->getMouseEnabled());   // clicks go through
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(c.tabs[static_cast<std::size_t>(i)].label == labels[i] && c.tabs[static_cast<std::size_t>(i)].kind == kinds[i]);
    const auto box = FairyTapes::Ui::tabLinkBox(i, 4, kBackLayout);
    ASSERT_TRUE(box.x > lastX && box.x + box.w == right[i]);
    lastX = box.x;
    auto* link = childAt<FairyTapes::Ui::TextLink>(back, boxRect(box));
    ASSERT_TRUE(link != nullptr && leftClick(link, link->getViewSize().getCenter()));
    ASSERT_TRUE(back->tab() == i && link->active());
    ASSERT_TRUE(wearRow->isVisible() == (i == kTabSettings));
  }
  EditorTest::destroy(ed, root);
  root = EditorTest::buildSides(ed);   // the same editor opened again: the last tab
  ASSERT_TRUE(EditorTest::back(ed) != nullptr && EditorTest::back(ed)->tab() == kTabCredits);
  EditorTest::destroy(ed, root);
}

// SETTINGS drawn: the boxes of the preview's geometry (APPEARANCE and BEHAVIOUR side by side up to x 1256, NOTES under them)
// and the line under OVERSAMPLING, which only the SETTINGS tab shows.
void test_back_settings_drawn_at_the_preview_geometry() {
  noPrefs();
  Editor ed(g_ctl);
  ed.setWearLevel(WearLevel::Off);
  auto root = EditorTest::buildSides(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(back != nullptr);
  back->setVisible(true);
  back->setTab(kTabSettings);
  auto bmp = drawOffscreen(back);
  ASSERT_TRUE(bmp != nullptr);
  const Pixels p = pixelsOf(bmp);
  auto vline = [&](int x, int y) { return std::max(brightness(p.at(x - 1, y)), brightness(p.at(x, y))); };
  ASSERT_TRUE(lineBrightness(p, 560, 102) > 150 && lineBrightness(p, 1200, 102) > 150);   // top borders of both columns
  ASSERT_TRUE(lineBrightness(p, 300, 514) > 150 && lineBrightness(p, 1200, 532) > 150);   // APPEARANCE bottom, NOTES top
  ASSERT_TRUE(vline(654, 300) > 150 && vline(1256, 300) > 150);                           // BEHAVIOUR left and right edges
  ASSERT_TRUE(vline(976, 300) < 60 && vline(1270, 300) < 60);                              // not the 1000 px template's edges
  auto textPixels = [&](const Pixels& px) {   // light pixels in the band of the OVERSAMPLING line
    int n = 0;
    for (int y = 187; y <= 199; ++y)   // above the bottom border of the CREDITS plug-in box (y 202)
      for (int x = 680; x <= 1120; ++x) n += brightness(px.at(x, y)) > 90 ? 1 : 0;
    return n;
  };
  ASSERT_TRUE(textPixels(p) > 120);
  back->setTab(kTabCredits);   // a tab whose boxes and text keep clear of that band (ROUTING and BUTTONS draw there)
  auto other = drawOffscreen(back);
  ASSERT_TRUE(other != nullptr && textPixels(pixelsOf(other)) == 0);
  back->setVisible(false);
  EditorTest::destroy(ed, root);
  ed.setWearLevel(WearLevel::Light);
}

// Width of a text in the back's font (Arial bold, the template's `bold(size)`).
double backTextWidth(const std::string& text, double size) {
  auto ctx = VSTGUI::COffscreenContext::create(VSTGUI::CPoint(8, 8), 1.0);
  if (!ctx) return 1e9;
  ctx->beginDraw();
  ctx->setFont(VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", size, VSTGUI::kBoldFace));
  const double w = ctx->getStringWidth(text.c_str());
  ctx->endDraw();
  return w;
}

// BUTTONS, ROUTING and CREDITS with the real font: every line of BUTTONS ends inside the box and a long name breaks inside its
// column; the credit names stay out of the author column and the authors out of the license column; every label of the
// diagram stays inside SIGNAL FLOW (its anchor and its real width); the legend's note inside LEGEND.
void test_back_tabs_fit_with_the_real_font() {
  namespace T = ButtonsTab;
  const auto& L = kBackLayout;
  const auto rows = buttonsRows();
  const auto& entries = buttonsEntries();
  ASSERT_TRUE(rows.size() == 14 && entries.size() == 14);
  for (std::size_t i = 0; i < entries.size(); ++i) {
    for (const std::string& l : entries[i].what) ASSERT_TRUE(T::kTextX + backTextWidth(l, T::kWhatSize) <= T::kRight);
    for (const std::string& l : entries[i].details) ASSERT_TRUE(T::kTextX + backTextWidth(l, T::kDetailSize) <= T::kRight);
    const auto name = FairyTapes::Ui::wrapText(entries[i].name, T::kNameWidth,
                                               [](const std::string& t) { return backTextWidth(t, T::kNameSize); });
    ASSERT_TRUE(!name.empty() && name.size() <= entries[i].what.size() + entries[i].details.size());
    for (const std::string& l : name) ASSERT_TRUE(T::kNameX + backTextWidth(l, T::kNameSize) < T::kTextX - 8);
  }
  ASSERT_TRUE(rows.back().lastBaseline + 4 < T::kBox.y + T::kBox.h - 8);
  // CREDITS
  const auto cr = credits();
  const int n = static_cast<int>(cr.size());
  const bool compact = FairyTapes::Ui::creditRowStep(n, L) < 24;   // BackView::drawCredits: 13 / 12 px, else 14 / 13
  for (const auto& e : cr) {
    ASSERT_TRUE(L.creditNameX + backTextWidth(e.name, compact ? 13 : 14) + 8 <= L.creditAuthorX);
    ASSERT_TRUE(L.creditAuthorX + backTextWidth(e.author, compact ? 12 : 13) + 8 <= L.creditLicenseRight - backTextWidth(e.license, compact ? 12 : 13));
  }
  const auto c = backCredits();
  ASSERT_TRUE(L.creditNameX + backTextWidth(c.versionLine, 14) < L.creditsTitle.x + L.creditsTitle.w - 20);
  for (const auto& l : c.licenseLines) ASSERT_TRUE(L.creditNameX + backTextWidth(l, 12) < L.creditsTitle.x + L.creditsTitle.w - 20);
  for (const auto& l : c.notices) ASSERT_TRUE(L.creditNameX + backTextWidth(l, 12) < L.notices.x + L.notices.w - 20);
  // ROUTING
  for (const FairyTapes::Ui::RoutingPrim& p : happyEndingRouting()) {
    if (p.kind != FairyTapes::Ui::RoutingPrim::Kind::Text) continue;
    const double w = backTextWidth(p.text, std::floor(p.size + 0.5));
    const double left = p.align == FairyTapes::Ui::RoutingAlign::Start ? p.x : p.align == FairyTapes::Ui::RoutingAlign::End ? p.x - w : p.x - w / 2;
    if (left < L.flow.x + 4 || left + w > L.flow.x + L.flow.w - 4) std::fprintf(stderr, "  \"%s\" leaves SIGNAL FLOW\n", p.text.c_str());
    ASSERT_TRUE(left >= L.flow.x + 4 && left + w <= L.flow.x + L.flow.w - 4);
  }
  for (const FairyTapes::Ui::RoutingPrim& p : FairyTapes::Ui::legendDiagram(happyEndingLegend(), L.legendSymbolY, L.legendNoteBaseline)) {
    if (p.kind != FairyTapes::Ui::RoutingPrim::Kind::Text) continue;
    ASSERT_TRUE(p.x + backTextWidth(p.text, std::floor(p.size + 0.5)) <= L.legend.x + L.legend.w - 10);   // all start at the left
  }
}

// The three tabs drawn: BUTTONS in its box (first and last entry, nothing past the box's right edge), ROUTING (the blocks and
// the LEGEND box), CREDITS (the license column in amber, the rows under one another).
void test_back_tabs_drawn() {
  noPrefs();
  Editor ed(g_ctl);
  ed.setWearLevel(WearLevel::Off);
  auto root = EditorTest::buildSides(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(back != nullptr);
  back->setVisible(true);
  auto lit = [](const Pixels& p, int x0, int y0, int x1, int y1, int threshold) {
    int count = 0;
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) count += brightness(p.at(x, y)) > threshold ? 1 : 0;
    return count;
  };
  // BUTTONS
  back->setTab(kTabButtons);
  auto bmp = drawOffscreen(back);
  ASSERT_TRUE(bmp != nullptr);
  Pixels p = pixelsOf(bmp);
  ASSERT_TRUE(lineBrightness(p, 300, 102) > 150 && lineBrightness(p, 300, 802) > 150);                 // the box 24, 102, 1232 x 700
  ASSERT_TRUE(lit(p, 50, 130, 120, 144, 150) > 20 && lit(p, 270, 130, 400, 144, 150) > 40);         // BYPASS and its first line
  const auto rows = buttonsRows();
  const int last = static_cast<int>(rows.back().lastBaseline);
  ASSERT_TRUE(lit(p, 270, last - 9, 900, last + 2, 120) > 150);                                       // the last line of SET
  ASSERT_TRUE(lit(p, 1236, 112, 1250, 790, 60) == 0 && lit(p, 30, 112, 44, 790, 60) == 0);            // nothing at the box's edges
  ASSERT_TRUE(lit(p, 270, last + 6, 1230, 796, 60) == 0);                                              // nothing under the last line
  // ROUTING
  back->setTab(kTabRouting);
  bmp = drawOffscreen(back);
  ASSERT_TRUE(bmp != nullptr);
  p = pixelsOf(bmp);
  ASSERT_TRUE(lineBrightness(p, 300, 102) > 150 && lineBrightness(p, 300, 774) > 150 && lineBrightness(p, 300, 790) > 150);   // SIGNAL FLOW, LEGEND
  ASSERT_TRUE(lineBrightness(p, 345, 218) > 150 && lineBrightness(p, 345, 282) > 150);                // the FINE EQ block (270 .. 420, 64 high)
  ASSERT_TRUE(lit(p, 860, 226, 980, 238, 150) > 20 || lit(p, 860, 220, 980, 250, 150) > 20);        // GLUE's name
  ASSERT_TRUE(lit(p, 300, 190, 430, 280, 200) > 30);                                                 // the amber / white text of the block
  // CREDITS
  back->setTab(kTabCredits);
  bmp = drawOffscreen(back);
  ASSERT_TRUE(bmp != nullptr);
  p = pixelsOf(bmp);
  const auto cr = credits();
  const int count = static_cast<int>(cr.size());
  for (int i = 0; i < count; ++i) {
    const int y = static_cast<int>(FairyTapes::Ui::creditBaseline(i, count, kBackLayout));
    ASSERT_TRUE(lit(p, 1150, y - 9, 1232, y + 2, 150) > 15);   // the license in amber, right-aligned at 1230
    ASSERT_TRUE(lit(p, 50, y - 9, 300, y + 2, 150) > 25);      // the name
  }
  back->setVisible(false);
  EditorTest::destroy(ed, root);
  ed.setWearLevel(WearLevel::Light);
}

// SIZE on the back reaches the window at once: the host is asked (IPlugFrame::resizeView) for the new size (1152 x 801 for
// 90 % at host factor 1, with a host factor on top), the frame's zoom follows, and size=90 is written to the plug-in's own file.
void test_size_change_resizes_window() {
  namespace fs = std::filesystem;
  const fs::path dir = scratchPrefsDir("he_size_ok");
  const fs::path file = dir / "FairyTapes" / "HappyEnding.prefs";
  fs::remove_all(dir);
  FakePlugFrame host;
  {
    Editor ed(g_ctl);
    EditorTest::loadAppearance(ed, file);   // no file: 100 %
    ASSERT_TRUE(EditorTest::setPlugFrame(ed, &host) == Steinberg::kResultTrue);
    auto root = EditorTest::buildSides(ed);
    auto* frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, 1280, 890), &ed);
    EditorTest::setFrame(ed, frame);
    FairyTapes::Ui::BackView* back = EditorTest::back(ed);
    ASSERT_TRUE(back != nullptr && back->selected(kSetSize) == 2 && windowOf(ed).getWidth() == 1280);
    ASSERT_TRUE(chooseOption(back, kSetSize, 1));   // 90 %
    ASSERT_TRUE(host.requests.size() == 1 && host.requests[0].getWidth() == 1152 && host.requests[0].getHeight() == 801);
    ASSERT_TRUE(windowOf(ed).getWidth() == 1152 && windowOf(ed).getHeight() == 801);
    Steinberg::ViewRect asked{};
    ASSERT_TRUE(ed.checkSizeConstraint(&asked) == Steinberg::kResultTrue && asked.getWidth() == 1152 && asked.getHeight() == 801);
    ASSERT_TRUE(std::fabs(frame->getTransform().m11 - 0.9) < 1e-9 && std::fabs(frame->getWidth() - 1152) < 1e-6 &&
                std::fabs(frame->getHeight() - 801) < 1e-6);
    ASSERT_TRUE(EditorTest::appliedSize(ed) == EditorSize::S90 && back->selected(kSetSize) == 1);
    ASSERT_TRUE(loadPrefs(file).size == EditorSize::S90 && fileText(file).find("size=90") != std::string::npos);
    // the host factor and SIZE multiply
    ASSERT_TRUE(ed.setContentScaleFactor(1.5f) == Steinberg::kResultTrue);
    ASSERT_TRUE(host.requests.size() == 2 && host.requests[1].getWidth() == 1728 && host.requests[1].getHeight() == 1202);
    ASSERT_TRUE(chooseOption(back, kSetSize, 2));   // 100 %
    ASSERT_TRUE(host.requests.size() == 3 && host.requests[2].getWidth() == 1920 && host.requests[2].getHeight() == 1335);
    ASSERT_TRUE(std::fabs(frame->getTransform().m11 - 1.5) < 1e-9 && windowOf(ed).getWidth() == 1920);
    ASSERT_TRUE(fileText(file).find("size=100") != std::string::npos);
    // the same choice again asks nothing
    ASSERT_TRUE(chooseOption(back, kSetSize, 2) && host.requests.size() == 3);
    EditorTest::destroy(ed, root);
    EditorTest::setFrame(ed, nullptr);
    frame->forget();
    ASSERT_TRUE(EditorTest::setPlugFrame(ed, nullptr) == Steinberg::kResultTrue);
  }
  ASSERT_TRUE(host.refs == 0);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// The host refuses the new size: the window keeps its size and its frame zoom, nothing crashes, the preference is written all
// the same and the next editor opens with it. A later request the host accepts goes through.
void test_size_change_refused_by_host() {
  namespace fs = std::filesystem;
  const fs::path dir = scratchPrefsDir("he_size_refused");
  const fs::path file = dir / "FairyTapes" / "HappyEnding.prefs";
  fs::remove_all(dir);
  FakePlugFrame host;
  host.accept = false;
  {
    Editor ed(g_ctl);
    EditorTest::loadAppearance(ed, file);
    ASSERT_TRUE(EditorTest::setPlugFrame(ed, &host) == Steinberg::kResultTrue);
    auto root = EditorTest::buildSides(ed);
    auto* frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, 1280, 890), &ed);
    EditorTest::setFrame(ed, frame);
    FairyTapes::Ui::BackView* back = EditorTest::back(ed);
    ASSERT_TRUE(back != nullptr);
    ASSERT_TRUE(chooseOption(back, kSetSize, 3));   // 125 %: the host says no
    ASSERT_TRUE(host.requests.size() == 1 && host.requests[0].getWidth() == 1600 && host.requests[0].getHeight() == 1113);
    ASSERT_TRUE(windowOf(ed).getWidth() == 1280 && windowOf(ed).getHeight() == 890);
    Steinberg::ViewRect asked{};
    ASSERT_TRUE(ed.checkSizeConstraint(&asked) == Steinberg::kResultTrue && asked.getWidth() == 1280 && asked.getHeight() == 890);
    ASSERT_TRUE(std::fabs(frame->getTransform().m11 - 1.0) < 1e-9 && std::fabs(frame->getWidth() - 1280) < 1e-6 &&
                std::fabs(frame->getHeight() - 890) < 1e-6);
    ASSERT_TRUE(EditorTest::appliedSize(ed) == EditorSize::S100);
    ASSERT_TRUE(EditorTest::prefs(ed).size == EditorSize::S125 && back->selected(kSetSize) == 3);   // the choice stands
    ASSERT_TRUE(loadPrefs(file).size == EditorSize::S125 && fileText(file).find("size=125") != std::string::npos);
    EditorTest::tick(ed);   // the timer goes on
    // a host factor the host refuses leaves the window alone too
    ASSERT_TRUE(ed.setContentScaleFactor(2.0f) == Steinberg::kResultTrue);
    ASSERT_TRUE(host.requests.size() == 2 && windowOf(ed).getWidth() == 1280 && std::fabs(frame->getTransform().m11 - 1.0) < 1e-9);
    // the next editor opens with the saved size
    Editor next(g_ctl);
    EditorTest::loadAppearance(next, file);
    ASSERT_TRUE(windowOf(next).getWidth() == 1600 && windowOf(next).getHeight() == 1113 && EditorTest::appliedSize(next) == EditorSize::S125);
    // the host accepts again: the next choice goes through from the window as it is
    host.accept = true;
    ASSERT_TRUE(chooseOption(back, kSetSize, 1));   // 90 %
    ASSERT_TRUE(host.requests.size() == 3 && host.requests[2].getWidth() == 1152);
    ASSERT_TRUE(windowOf(ed).getWidth() == 1152 && std::fabs(frame->getTransform().m11 - 0.9) < 1e-9 && loadPrefs(file).size == EditorSize::S90);
    EditorTest::destroy(ed, root);
    EditorTest::setFrame(ed, nullptr);
    frame->forget();
    ASSERT_TRUE(EditorTest::setPlugFrame(ed, nullptr) == Steinberg::kResultTrue);
  }
  ASSERT_TRUE(host.refs == 0);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// OVERSAMPLING on the back is the stored parameter: one click is exactly one beginEdit / performEdit / endEdit of kOversampling
// on the step, the controller asks the host to read the latency once, no preference is written; a change that comes from the host
// moves the row on the next tick without an echo.
void test_oversampling_from_back_is_one_edit() {
  namespace fs = std::filesystem;
  const fs::path file = scratchPrefsDir("he_oversampling") / "FairyTapes" / "HappyEnding.prefs";
  fs::remove_all(file.parent_path().parent_path());
  resetAll(g_ctl);
  g_ctl->setParamNormalized(kOversampling, normFromIndex(kOversampling, 1));
  g_ctl->setComponentHandler(&g_host);
  g_host.clear();
  Editor ed(g_ctl);
  EditorTest::loadAppearance(ed, file);
  auto root = EditorTest::buildSides(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(back != nullptr && back->selected(kSetOversampling) == 1);   // the parameter's default: 2x
  ASSERT_TRUE(chooseOption(back, kSetOversampling, 2));
  ASSERT_TRUE(g_host.once(kOversampling) && g_host.total() == 3);
  ASSERT_TRUE(near(g_host.last[kOversampling], normFromIndex(kOversampling, 2)));
  ASSERT_TRUE(indexFromNorm(kOversampling, g_ctl->getParamNormalized(kOversampling)) == 2 && g_host.latencyRestarts == 1);
  ASSERT_TRUE(back->selected(kSetOversampling) == 2 && !fs::exists(file));   // a project setting, not a preference
  EditorTest::tick(ed);
  ASSERT_TRUE(g_host.total() == 3 && g_host.latencyRestarts == 1 && back->selected(kSetOversampling) == 2);
  ASSERT_TRUE(chooseOption(back, kSetOversampling, 2) && g_host.total() == 3);   // the same choice again: nothing
  ASSERT_TRUE(chooseOption(back, kSetOversampling, 0));
  ASSERT_TRUE(g_host.performs[kOversampling] == 2 && near(g_host.last[kOversampling], normFromIndex(kOversampling, 0)) && g_host.latencyRestarts == 2);
  // from the host (automation is off for it, but a state recall or the generic editor can write it): the row follows
  g_ctl->setParamNormalized(kOversampling, normFromIndex(kOversampling, 2));
  ASSERT_TRUE(g_host.latencyRestarts == 3 && back->selected(kSetOversampling) == 0);   // only flagged
  const int total = g_host.total();
  EditorTest::tick(ed);
  ASSERT_TRUE(back->selected(kSetOversampling) == 2 && g_host.total() == total);       // no echo
  // none of the front controls moved
  for (int i = 0; i < kNumControls; ++i) ASSERT_TRUE(EditorTest::controlAt(ed, i) != nullptr);
  g_ctl->setParamNormalized(kOversampling, normFromIndex(kOversampling, 1));
  g_ctl->setComponentHandler(nullptr);
  g_host.clear();
  EditorTest::destroy(ed, root);
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
    RUN_TEST(test_editor_controls_follow_host);
    RUN_TEST(test_user_edit_reaches_host_once);
    RUN_TEST(test_double_click_resets_default);
    RUN_TEST(test_listen_exclusive_and_toggle);
    RUN_TEST(test_listen_led_shows_heard_band);
    RUN_TEST(test_close_turns_listen_and_set_off);
    RUN_TEST(test_set_locks_limit_except_release);
    RUN_TEST(test_final_mode_shows_its_controls);
    RUN_TEST(test_hp_scale_follows_ms);
    RUN_TEST(test_match_calls_controller);
    RUN_TEST(test_view_buttons_exclusive);
    RUN_TEST(test_typed_value_goes_through_parse);
    RUN_TEST(test_limit_release_shows_ms);
    RUN_TEST(test_ms_prints_sigla);
    RUN_TEST(test_knob_values_at_preview_offset);
    RUN_TEST(test_meters_follow_display_params);
    RUN_TEST(test_clip_click_clears_via_trigger);
    RUN_TEST(test_lufs_click_resets_integrated);
    RUN_TEST(test_lufs_texts_and_target_led);
    RUN_TEST(test_gr_rows_dim_with_stage_and_bypass);
    RUN_TEST(test_clip_leds_dim_with_stage_and_bypass);
    RUN_TEST(test_act_leds_follow_dyn);
    RUN_TEST(test_lufs_screen_box_under_labels);
    RUN_TEST(test_screen_view_switch);
    RUN_TEST(test_eq_curves_recomputed_only_on_change);
    RUN_TEST(test_transfer_curves_recomputed_only_on_change);
    RUN_TEST(test_scroll_history_grows);
    RUN_TEST(test_screen_dims_only_on_bypass);
    RUN_TEST(test_listen_zone_drawn);
    RUN_TEST(test_transfer_dot_follows_stage_input);
    RUN_TEST(test_limit_view_follows_limit_state);
    RUN_TEST(test_wear_off_has_no_marks_and_no_tapes);
    RUN_TEST(test_wear_level_change_redraws_without_rebuild);
    RUN_TEST(test_tape_text_edit_goes_to_decor);
    RUN_TEST(test_screw_drag_stores_angle_once);
    RUN_TEST(test_reroll_changes_seed_and_clears_decor);
    RUN_TEST(test_decor_change_applied_on_tick);
    RUN_TEST(test_seed_zero_waits_for_decor);
    RUN_TEST(test_prefs_missing_file_opens_with_defaults);
    RUN_TEST(test_prefs_applied_when_editor_made);
    RUN_TEST(test_missing_grain_asset_draws_plain_panel);
    RUN_TEST(test_values_in_tape_font_with_normalized_digits);
    RUN_TEST(test_value_texts_covered_by_tape_fonts);
    RUN_TEST(test_flip_roundtrip_and_text_edit_confirmed_first);
    RUN_TEST(test_settings_apply_wear_at_once);
    RUN_TEST(test_settings_fonts_and_reroll_apply);
    RUN_TEST(test_settings_failed_write_is_ignored);
    RUN_TEST(test_tab_links_order);
    RUN_TEST(test_back_settings_drawn_at_the_preview_geometry);
    RUN_TEST(test_back_tabs_fit_with_the_real_font);
    RUN_TEST(test_back_tabs_drawn);
    RUN_TEST(test_size_change_resizes_window);
    RUN_TEST(test_size_change_refused_by_host);
    RUN_TEST(test_oversampling_from_back_is_one_edit);
    RUN_TEST(test_editor_timer_stops_on_close);
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
