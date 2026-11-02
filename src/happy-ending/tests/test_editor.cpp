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
#include "ui/editor.h"
#include "ui/layout.h"
#include "display.h"
#include "ui/panel_view.h"
#include "ui/prefs.h"
#include "ui/readouts.h"
#include "ui/screen_model.h"
#include "ui/value_text.h"

#include "fairy_ui/controls.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/silkscreen.h"
#include "fairy_ui/silkscreen_view.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/events.h"
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
#include <map>
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
  // Removes the views, as close() does (the parameters are no longer watched; a typed value left open is dropped). The
  // session parameters are not switched off here: close() itself does that (test_close_turns_listen_and_set_off).
  static void destroy(Editor& ed, VSTGUI::CViewContainer* root) {
    ed.unwatchParameters();
    ed.closeValueEdit(false);
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
  // The view of kControls[i].
  static FairyTapes::Ui::NormalizedControl* controlAt(const Editor& ed, int i) { return ed.controls_[static_cast<std::size_t>(i)]; }
  // MATCH and the column of the screen view buttons (one exclusive choice).
  static FairyTapes::Ui::NormalizedControl* match(const Editor& ed) { return ed.match_; }
  static FairyTapes::Ui::NormalizedControl* views(const Editor& ed) { return ed.views_; }
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

  void clear() {
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
  Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override { return Steinberg::kResultOk; }
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
// The value printed under a knob or a cycling selector: formatValue, but the real ms of LIMIT RELEASE (releaseText).
std::string expectedValue(Steinberg::Vst::EditController* ctl, Steinberg::Vst::ParamID id) {
  double n[kNumParams];
  normsOf(ctl, n);
  if (id == kLimitRelease) return releaseText(indexFromNorm(kLimitStyle, n[kLimitStyle]), indexFromNorm(kLimitRelease, n[kLimitRelease]));
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
    RUN_TEST(test_knob_values_at_preview_offset);
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
