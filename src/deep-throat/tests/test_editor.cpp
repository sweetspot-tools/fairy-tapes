/**
 * Fairy Tapes · Deep Throat — editor behaviour without a window (build target: deep_throat_editor_tests).
 * A real Controller is the host; EditorTest (a friend of Editor) builds the views in a container instead of a frame,
 * runs the UI timer's work by hand and reaches the views. Drawing goes to an offscreen context: the pixels checked here
 * are the panel colour, the white box borders, the gap of the box titles, the LED / lamp colour families and the SCOPE
 * breath band; the exact colours, the fonts as the host
 * renders them and the 3D flip stay a manual check.
 */
#include "controller.h"
#include "params.h"
#include "ui/back_modes.h"
#include "ui/back_settings.h"
#include "ui/editor.h"
#include "ui/layout.h"
#include "ui/dynamic_text.h"
#include "ui/panel_view.h"
#include "ui/decor.h"
#include "ui/prefs.h"
#include "ui/readout_view.h"
#include "ui/scope_view.h"
#include "ui/screw_angles.h"
#include "ui/tape_view.h"
#include "ui/tapes.h"
#include "ui/value_text.h"

#include "fairy_ui/back_view.h"
#include "fairy_ui/back_widgets.h"
#include "fairy_ui/controls.h"
#include "fairy_ui/flip_view.h"
#include "fairy_ui/meter_math.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/silkscreen.h"
#include "fairy_ui/screw_view.h"
#include "fairy_ui/silkscreen_view.h"
#include "fairy_ui/tape_paper.h"
#include "fairy_ui/wear_view.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/events.h"
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
#include <limits>
#include <string>
#include <thread>
#include <chrono>
#include <ctime>
#include <vector>

// The SDK's VSTGUI init hook asks the module for its factory; there is none in this test.
extern "C" Steinberg::IPluginFactory* GetPluginFactory() { return nullptr; }
void* moduleHandle = nullptr;   // normally set by the platform module entry point

namespace FairyTapes::DeepThroat::Ui {

// What the tests may reach inside the editor (the later tasks add controls, meters, tapes, screws and the back).
struct EditorTest {
  // Builds the editor's views in a container, as open() does in the frame.
  static VSTGUI::SharedPointer<VSTGUI::CViewContainer> build(Editor& ed) {
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createViews(root);
    return root;
  }
  // Removes the views, as close() does (the parameters are no longer watched).
  static void destroy(Editor& ed, VSTGUI::CViewContainer* root) {
    ed.unwatchParameters();
    ed.closeValueEdit(false);
    ed.closeTapeEdit(false);
    ed.flip_.reset();
    root->removeAll(true);
    ed.resetViewPointers();
  }
  // Both sides of the panel in a container, as open() does in the frame: the front (a container with the views of
  // build()), the hidden back and the turn between them.
  static VSTGUI::SharedPointer<VSTGUI::CViewContainer> buildSides(Editor& ed) {
    auto root = VSTGUI::makeOwned<VSTGUI::CViewContainer>(VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
    ed.createSides(root);
    return root;
  }
  static VSTGUI::CViewContainer* front(const Editor& ed) { return ed.front_; }
  static FairyTapes::Ui::BackView* back(const Editor& ed) { return ed.back_; }
  static FairyTapes::Ui::TextLink* backLink(const Editor& ed) { return ed.backLink_; }
  static FairyTapes::Ui::PanelFlip& flip(Editor& ed) { return ed.flip_; }
  static const Prefs& prefs(const Editor& ed) { return ed.prefs_; }
  // One run of the UI timer's work.
  static void tick(Editor& ed) { ed.onTimer(); }
  static PanelView* panel(const Editor& ed) { return ed.panel_; }
  // The control view bound to a parameter (nullptr when there is none).
  static FairyTapes::Ui::NormalizedControl* control(const Editor& ed, ParamID id) {
    return id < kNumParams ? ed.controls_[id] : nullptr;
  }
  static const FairyTapes::Ui::SilkscreenModel* silk(const Editor& ed) { return ed.silk_ ? &ed.silk_->model() : nullptr; }
  // The printed value of the first silkscreen item of `id` ("" when none).
  static std::string silkValue(const Editor& ed, int id) {
    if (const auto* m = silk(ed))
      for (std::size_t i = 0; i < m->size(); ++i)
        if (m->item(i).id == id) return m->value(i);
    return {};
  }
  // Whether every silkscreen item of `id` is dimmed (false when there is none).
  static bool silkDimmed(const Editor& ed, int id) {
    bool any = false, all = true;
    if (const auto* m = silk(ed))
      for (std::size_t i = 0; i < m->size(); ++i)
        if (m->item(i).id == id) {
          any = true;
          all = all && m->dimmed(i);
        }
    return any && all;
  }
  static const FairyTapes::Ui::SilkscreenModel::Tag* tag(const Editor& ed, int key) {
    if (const auto* m = silk(ed))
      for (const auto& t : m->tags())
        if (t.slot.key == key) return &t;
    return nullptr;
  }
  static ReadoutView* readouts(const Editor& ed) { return ed.readouts_; }
  static ReadoutView* lamp(const Editor& ed) { return ed.lamp_; }
  static VSTGUI::CTextEdit* valueEdit(const Editor& ed) { return ed.valueEdit_; }
  static void closeValueEdit(Editor& ed, bool confirm) { ed.closeValueEdit(confirm); }
  static bool handwritten(const Editor& ed) { return ed.valuesHandwritten_; }
  // The LEVEL rows (kMeterRows order), the clip LEDs (kClips order) and the SCOPE.
  static FairyTapes::Ui::MeterBar* bar(const Editor& ed, int i) { return ed.bars_[static_cast<std::size_t>(i)]; }
  static FairyTapes::Ui::ClipLed* clip(const Editor& ed, int i) { return ed.clips_[static_cast<std::size_t>(i)]; }
  static ScopeView* scope(const Editor& ed) { return ed.scope_; }
  static bool timerRunning(const Editor& ed) { return ed.timer_ != nullptr; }
  static void setHandwritten(Editor& ed, bool on) { ed.valuesHandwritten_ = on; }
  // Wear, tapes and screws (Task 10).
  static FairyTapes::Ui::WearView* wear(const Editor& ed) { return ed.wear_; }
  static const std::vector<TapeView*>& tapes(const Editor& ed) { return ed.tapes_; }
  static FairyTapes::Ui::ScrewView* screw(const Editor& ed, int i) { return ed.screws_[static_cast<std::size_t>(i)]; }
  static VSTGUI::CTextEdit* tapeEdit(const Editor& ed) { return ed.tapeEdit_; }
  static void closeTapeEdit(Editor& ed, bool confirm) { ed.closeTapeEdit(confirm); }
  static void loadAppearance(Editor& ed, const std::filesystem::path& file) { ed.loadAppearance(file); }
  static FairyTapes::Ui::TapeFontId valuesFont(const Editor& ed) { return ed.valuesFont_; }
};

} // namespace FairyTapes::DeepThroat::Ui

using namespace FairyTapes::DeepThroat;
using namespace FairyTapes::DeepThroat::Ui;

namespace {
int g_failed = 0, g_passed = 0;
Controller* g_ctl = nullptr;

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
  if (auto* lf = VSTGUI::getPlatformFactory().asLinuxFactory()) lf->setResourcePath(grain ? DEEP_THROAT_ASSETS : "/nonexistent/");
#else
  (void)grain;
#endif
}

// Draws `v` into a 1000 x 614 offscreen bitmap at scale 1.
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
Pixels pixelsOf(VSTGUI::CBitmap* bmp) {
  return {VSTGUI::owned(VSTGUI::CBitmapPixelAccess::create(bmp, false))};
}
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

// The host side of the controller: counts what the editor sends (beginEdit / performEdit / endEdit per parameter).
class CountingHandler : public Steinberg::Vst::IComponentHandler {
public:
  int begins[kNumParams] = {}, performs[kNumParams] = {}, ends[kNumParams] = {};
  double last[kNumParams] = {};
  int others = 0;   // calls for ids outside the 25 parameters (kClearClips included)
  int clearBegins = 0, clearPerforms = 0, clearEnds = 0;   // the clip-clear trigger
  double clearLast = -1;

  void clear() { *this = CountingHandler(); }
  int total() const {
    int n = others;
    for (int i = 0; i < kNumParams; ++i) n += begins[i] + performs[i] + ends[i];
    return n;
  }
  Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override {
    if (id == kClearClips) ++clearBegins;
    (id < kNumParams ? begins[id] : others) += 1;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue v) override {
    if (id < kNumParams) {
      ++performs[id];
      last[id] = v;
    } else {
      if (id == kClearClips) {
        ++clearPerforms;
        clearLast = v;
      }
      ++others;
    }
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override {
    if (id == kClearClips) ++clearEnds;
    (id < kNumParams ? ends[id] : others) += 1;
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

// A controller that counts the texts it is asked to read (the typed values must go through it).
class SpyController : public Controller {
public:
  int parsed = 0;
  Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID id, Steinberg::Vst::TChar* string,
                                                      Steinberg::Vst::ParamValue& v) override {
    ++parsed;
    return Controller::getParamValueByString(id, string, v);
  }
};

double defaultOf(Steinberg::Vst::EditController* ctl, Steinberg::Vst::ParamID id) {
  const auto* p = ctl->getParameterObject(id);
  return p ? p->getInfo().defaultNormalizedValue : 0.0;
}
void resetParams(Steinberg::Vst::EditController* ctl) {
  for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) ctl->setParamNormalized(id, defaultOf(ctl, id));
}
std::string ascii(const Steinberg::Vst::String128 s) {
  std::string out;
  for (int i = 0; i < 128 && s[i]; ++i) out.push_back(static_cast<char>(s[i]));
  return out;
}
void toStr128(const std::string& in, Steinberg::Vst::String128 out) {
  std::size_t i = 0;
  for (; i < in.size() && i < 127; ++i) out[i] = static_cast<Steinberg::Vst::TChar>(in[i]);
  out[i] = 0;
}
bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }
std::string readout(const Editor& ed, ReadoutId id) {
  const ReadoutView* r = EditorTest::readouts(ed);
  return r ? r->text(id) : std::string("<no view>");
}

// What the dynamic texts must read for the controller's current Mode, EQ Amount, Tone and Comp choices.
bool dynamicTextsMatch(const Editor& ed) {
  const Mode m = modeFromNorm(g_ctl->getParamNormalized(kMode));
  const double eq = g_ctl->getParamNormalized(kEqAmount), tone = g_ctl->getParamNormalized(kTone);
  const int ratio = compRatioChoiceFromNorm(g_ctl->getParamNormalized(kCompRatio));
  const int dist = compDistChoiceFromNorm(g_ctl->getParamNormalized(kCompDist));
  const int hpf = compHpfChoiceFromNorm(g_ctl->getParamNormalized(kCompHpf));
  auto tagIs = [&](int key, const std::string& want) {
    const auto* t = EditorTest::tag(ed, key);
    if (!t || t->text != want) std::fprintf(stderr, "  tag %d: \"%s\" (expected \"%s\")\n", key, t ? t->text.c_str() : "-", want.c_str());
    return t && t->text == want;
  };
  auto valueIs = [&](int id, const std::string& want) {
    const std::string got = EditorTest::silkValue(ed, id);
    if (got != want) std::fprintf(stderr, "  value %d: \"%s\" (expected \"%s\")\n", id, got.c_str(), want.c_str());
    return got == want;
  };
  auto readoutIs = [&](ReadoutId id, const std::string& want) {
    const std::string got = readout(ed, id);
    if (got != want) std::fprintf(stderr, "  readout %d: \"%s\" (expected \"%s\")\n", (int)id, got.c_str(), want.c_str());
    return got == want;
  };
  return tagIs(kTagModeName, modeTagLine1(m)) && tagIs(kTagModeInfo, modeTagLine2(m)) && tagIs(kTagBreath, detectorTag(m)) &&
         tagIs(kTagDeEss, deEssNote(m)) && readoutIs(ReadoutId::RiderInfo, riderMaxText(m)) &&
         readoutIs(ReadoutId::EqHpf, eqHpfText(m, eq)) && readoutIs(ReadoutId::EqMud, eqMudText(m, eq)) &&
         readoutIs(ReadoutId::ToneMud, toneGainText(m, 0, tone)) && readoutIs(ReadoutId::TonePresence, toneGainText(m, 1, tone)) &&
         readoutIs(ReadoutId::ToneAir, toneGainText(m, 2, tone)) && valueIs(kCompRatio, ratioText(ratio, m)) &&
         valueIs(kCompDist, distText(dist, m)) && valueIs(kCompHpf, hpfChoiceText(hpf));
}

// ---- Tests ----------------------------------------------------------------------------------------------------------

// The panel layer is the bottom view of the editor and covers the whole 1000 x 614 panel; clicks go through it.
void test_panel_layer_built() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  PanelView* panel = EditorTest::panel(ed);
  ASSERT_TRUE(panel != nullptr);
  ASSERT_TRUE(root->getNbViews() >= 1 && root->getView(0) == panel);
  const VSTGUI::CRect r = panel->getViewSize();
  ASSERT_TRUE(r.left == 0 && r.top == 0 && r.getWidth() == kEditorWidth && r.getHeight() == kEditorHeight);
  ASSERT_TRUE(!panel->getMouseEnabled());
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(EditorTest::panel(ed) == nullptr);
}

// Every kPanelTexts string is drawn once, in table order, at its position with its alignment.
void test_panel_texts_from_layout() {
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
  }
  EditorTest::destroy(ed, root);
}

// The global bypass dims (35 %) only the texts flagged dimsWithBypass; a module out dims only its texts.
void test_panel_dims_only_flagged_texts() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  PanelView* panel = EditorTest::panel(ed);
  ASSERT_TRUE(panel != nullptr);
  auto alphas = [&]() { drawOffscreen(root); return panel->drawnTexts(); };
  auto plain = alphas();
  ASSERT_TRUE(plain.size() == kNumPanelTexts);
  for (const auto& d : plain) ASSERT_TRUE(d.alpha == 1.f);
  panel->setDimmed(true);
  auto dimmed = alphas();
  ASSERT_TRUE(dimmed.size() == kNumPanelTexts);
  int dimCount = 0;
  for (std::size_t i = 0; i < kNumPanelTexts; ++i) {
    ASSERT_TRUE(dimmed[i].alpha == (kPanelTexts[i].dimsWithBypass ? 0.35f : 1.f));
    ASSERT_TRUE(panel->textDimmed(i) == kPanelTexts[i].dimsWithBypass);
    dimCount += dimmed[i].alpha < 1.f ? 1 : 0;
  }
  ASSERT_TRUE(dimCount > 0 && dimCount < static_cast<int>(kNumPanelTexts));
  panel->setDimmed(false);
  for (const auto& d : alphas()) ASSERT_TRUE(d.alpha == 1.f);
  DimState eqOut;
  eqOut.in[static_cast<int>(Module::Eq) - 1] = false;
  panel->setDimState(eqOut);
  auto eq = alphas();
  for (std::size_t i = 0; i < kNumPanelTexts; ++i) ASSERT_TRUE(eq[i].alpha == (kPanelTexts[i].module == Module::Eq ? 0.35f : 1.f));
  EditorTest::destroy(ed, root);
}

// Bypass and the six IN parameters changed on the controller dim the panel texts on the next UI timer run (dependents).
void test_tick_applies_controller_dimming() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  PanelView* panel = EditorTest::panel(ed);
  ASSERT_TRUE(panel != nullptr);
  EditorTest::tick(ed);
  ASSERT_TRUE(!panel->dimState().bypass);
  g_ctl->setParamNormalized(kBypass, 1.0);
  g_ctl->setParamNormalized(kToneIn, 0.0);
  ASSERT_TRUE(!panel->dimState().bypass);   // nothing changes before the tick
  EditorTest::tick(ed);
  const bool bypassed = panel->dimState().bypass, toneOut = !panel->dimState().in[static_cast<int>(Module::Tone) - 1];
  int othersIn = 0;
  for (int m = 1; m <= 6; ++m) othersIn += (m != static_cast<int>(Module::Tone) && panel->dimState().in[m - 1]) ? 1 : 0;
  g_ctl->setParamNormalized(kBypass, 0.0);
  g_ctl->setParamNormalized(kToneIn, 1.0);
  ASSERT_TRUE(bypassed && toneOut && othersIn == 5);
  EditorTest::tick(ed);
  ASSERT_TRUE(!panel->dimState().bypass && panel->dimState().in[static_cast<int>(Module::Tone) - 1]);
  EditorTest::destroy(ed, root);
  EditorTest::tick(ed);   // no views: nothing to touch
}

// The editor from createView, its views built and removed with timer runs in between, 50 times: every view and the
// editor itself are freed (reference counts back to the owner's, then to 0).
void test_editor_open_close_repeatedly() {
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
    EditorTest::destroy(*ed, root);
    EditorTest::tick(*ed);
    const auto left = panel->getNbReference();
    panel->forget();
    ASSERT_TRUE(left == 1);   // only the test's reference: the container let it go
    ASSERT_TRUE(root->getNbReference() == 1);
    ASSERT_TRUE(view->release() == 0);
  }
  g_ctl->setParamNormalized(kBypass, 0.0);
}

// Offscreen render of the front without the grain tile: panel colour in the left margin, white box border on an edge,
// the title gap of the bottom border not white while the border next to the gap is.
void test_render_smoke() {
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(FairyTapes::Ui::WearLevel::Off);   // the panel layer alone (no marks, no tapes)
  auto bmp = drawOffscreen(root);
  ASSERT_TRUE(bmp != nullptr);
  ASSERT_TRUE(bmp->getWidth() == kEditorWidth && bmp->getHeight() == kEditorHeight);
  const Pixels px = pixelsOf(bmp);
  ASSERT_TRUE(px.access != nullptr);
  const VSTGUI::CColor margin = px.at(10, 300);   // left of the MODE box: tint #161617 with the soft light
  ASSERT_TRUE(margin.alpha == 255 && brightness(margin) >= 0x08 && brightness(margin) <= 0x30);
  ASSERT_TRUE(std::abs(margin.red - margin.blue) <= 4);
  for (const BoxSpec& b : kBoxes) {
    const int left = static_cast<int>(b.rect.x), midY = static_cast<int>(b.rect.y + b.rect.h / 2);
    const int bottom = static_cast<int>(b.rect.y + b.rect.h), cx = static_cast<int>(b.rect.x + b.rect.w / 2);
    ASSERT_TRUE(lineBrightness(px, left, midY) > 0xc0 || lineBrightness(px, left + 1, midY) > 0xc0);   // left edge
    ASSERT_TRUE(lineBrightness(px, left - 3, midY) < 0x40);   // outside it (the box on the left ends 6 px before)
    const double half = titleWidth(b.title) / 2;
    ASSERT_TRUE(half > 4);
    // 13 = kTitleGap of shared/ui/src/draw_util.cpp (blank run on each side of a box title): keep in step with it.
    const int inGap = static_cast<int>(cx - half - 13.0 / 2), besideGap = static_cast<int>(cx - half - 13 - 8);
    ASSERT_TRUE(lineBrightness(px, inGap, bottom) < 0x60);    // the blank run before the title
    ASSERT_TRUE(lineBrightness(px, besideGap, bottom) > 0xc0);   // the bottom border goes on outside the gap
    ASSERT_TRUE(lineBrightness(px, besideGap, static_cast<int>(b.rect.y)) > 0xc0);   // the top border has no gap
    ASSERT_TRUE(lineBrightness(px, cx, static_cast<int>(b.rect.y)) > 0xc0);
  }
  EditorTest::destroy(ed, root);
}

// The texts are really drawn, not only recorded: the white bold italic "DEEP THROAT" (layout x 34, baseline 58, 38 px)
// lights many pixels of its block well above the dark panel.
void test_title_text_is_drawn() {
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(FairyTapes::Ui::WearLevel::Off);
  auto bmp = drawOffscreen(root);
  ASSERT_TRUE(bmp != nullptr);
  const Pixels px = pixelsOf(bmp);
  const PanelText& title = kPanelTexts[0];
  ASSERT_TRUE(!std::strcmp(title.text, "DEEP THROAT"));
  int bright = 0, dark = 0;
  for (int y = static_cast<int>(title.y) - 24; y <= static_cast<int>(title.y) - 2; ++y)
    for (int x = static_cast<int>(title.x); x <= static_cast<int>(title.x) + 220; ++x) {
      const int b = brightness(px.at(x, y));
      bright += b > 0xc0 ? 1 : 0;
      dark += b < 0x40 ? 1 : 0;
    }
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(bright > 300);   // letter strokes
  ASSERT_TRUE(dark > 300);     // and the panel between them
}

// The grain tile is drawn when the asset is found (pixels of a blank row vary) and the panel is plain when it is
// missing (the soft light is vertical only: the row is one colour).
void test_panel_grain_when_asset_found() {
  auto rowSpread = [](bool grain) {
    setResourcePath(grain);
    Editor ed(g_ctl);
    auto root = EditorTest::build(ed);
    ed.setWearLevel(FairyTapes::Ui::WearLevel::Off);
    auto bmp = drawOffscreen(root);
    int lo = 255, hi = 0;
    if (bmp) {
      const Pixels px = pixelsOf(bmp);
      for (int x = 6; x <= 20; ++x)
        for (int y = 280; y <= 320; y += 10) {
          const int v = px.at(x, y).red;
          lo = std::min(lo, v);
          hi = std::max(hi, v);
        }
    }
    EditorTest::destroy(ed, root);
    return bmp ? hi - lo : -1;
  };
  const int plain = rowSpread(false), grain = rowSpread(true);
  setResourcePath(false);
  ASSERT_TRUE(plain >= 0 && plain <= 2);
  ASSERT_TRUE(grain > plain + 2);
}

// ---- Controls bound to the parameters (Task 8) ----------------------------------------------------------------------

using Kind = FairyTapes::DeepThroat::Ui::Kind;
using FairyTapes::Ui::NormalizedControl;

// The value strings under the knobs are the controller's: getParamStringByValue gives exactly formatKnobValue, its
// parser reads them back like parseKnobValue, and the editor prints the controller's string after a host change.
void test_value_text_matches_controller() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  int knobs = 0;
  for (const ControlSpec& c : kControls) {
    if (c.kind != Kind::Knob) continue;
    ++knobs;
    for (double n : {0.0, 0.25, 0.5, 1.0}) {
      Steinberg::Vst::String128 s{};
      ASSERT_TRUE(g_ctl->getParamStringByValue(c.id, n, s) == Steinberg::kResultOk);
      const std::string host = ascii(s);
      if (host != formatKnobValue(c.id, n)) std::fprintf(stderr, "  param %u at %.2f: host \"%s\" panel \"%s\"\n", (unsigned)c.id, n, host.c_str(), formatKnobValue(c.id, n).c_str());
      ASSERT_TRUE(host == formatKnobValue(c.id, n));
      Steinberg::Vst::ParamValue back = -1;
      double mine = -2;
      ASSERT_TRUE(g_ctl->getParamValueByString(c.id, s, back) == Steinberg::kResultOk && parseKnobValue(c.id, host, mine));
      ASSERT_TRUE(back == mine);
      g_ctl->setParamNormalized(c.id, n);
      EditorTest::tick(ed);
      ASSERT_TRUE(EditorTest::silkValue(ed, static_cast<int>(c.id)) == host);
    }
  }
  ASSERT_TRUE(knobs == 12);
  resetParams(g_ctl);
  EditorTest::destroy(ed, root);
}

// One control view per parameter, of the kind and at the place of its ControlSpec; nothing else in the editor carries a
// parameter tag.
void test_every_param_has_exactly_one_control_view() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  int count[kNumParams] = {};
  for (uint32_t i = 0; i < root->getNbViews(); ++i)
    if (auto* c = dynamic_cast<VSTGUI::CControl*>(root->getView(i)))
      if (c->getTag() >= 0 && c->getTag() < kNumParams) ++count[c->getTag()];
  for (const ControlSpec& spec : kControls) {
    ASSERT_TRUE(count[spec.id] == 1);
    NormalizedControl* v = EditorTest::control(ed, spec.id);
    ASSERT_TRUE(v != nullptr && v->getTag() == static_cast<int32_t>(spec.id) && root->isChild(v));
    switch (spec.kind) {
    case Kind::Knob: ASSERT_TRUE(dynamic_cast<FairyTapes::Ui::RackKnob*>(v) != nullptr); break;
    case Kind::Toggle: ASSERT_TRUE(dynamic_cast<FairyTapes::Ui::RackToggle*>(v) != nullptr); break;
    case Kind::Choice: ASSERT_TRUE(dynamic_cast<FairyTapes::Ui::RackLitChoice*>(v) != nullptr); break;
    case Kind::Cycle: ASSERT_TRUE(dynamic_cast<FairyTapes::Ui::RackCycle*>(v) != nullptr); break;
    }
    const VSTGUI::CRect r = v->getViewSize();
    const Rect want = viewRect(spec);
    ASSERT_TRUE(near(r.left, want.x) && near(r.top, want.y) && near(r.getWidth(), want.w) && near(r.getHeight(), want.h));
    ASSERT_TRUE(near(v->getDefaultValue(), defaultOf(g_ctl, spec.id)));
  }
  EditorTest::destroy(ed, root);
  for (const ControlSpec& spec : kControls) ASSERT_TRUE(EditorTest::control(ed, spec.id) == nullptr);
}

// A host change (automation, preset, undo) on any of the 25 parameters reaches its control on the next timer run and
// not before (update() only flags), and nothing goes back to the host.
void test_editor_controls_follow_host() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  g_host.clear();
  for (const ControlSpec& spec : kControls) {
    NormalizedControl* v = EditorTest::control(ed, spec.id);
    ASSERT_TRUE(v != nullptr);
    const double cur = g_ctl->getParamNormalized(spec.id);
    const double target = spec.kind == Kind::Knob ? (cur < 0.5 ? 0.8 : 0.2) : (cur < 0.5 ? 1.0 : 0.0);
    const float before = v->getValueNormalized();
    ASSERT_TRUE(near(before, cur));
    g_ctl->setParamNormalized(spec.id, target);
    ASSERT_TRUE(v->getValueNormalized() == before);   // only flagged: the timer applies it
    EditorTest::tick(ed);
    if (!near(v->getValueNormalized(), target)) std::fprintf(stderr, "  param %u did not follow\n", (unsigned)spec.id);
    ASSERT_TRUE(near(v->getValueNormalized(), target));
    if (spec.kind == Kind::Knob) ASSERT_TRUE(EditorTest::silkValue(ed, static_cast<int>(spec.id)) == formatKnobValue(spec.id, target));
  }
  ASSERT_TRUE(dynamicTextsMatch(ed));   // Puke, AUTO ratio and dist moved, SC HPF off
  ASSERT_TRUE(g_host.total() == 0);     // no echo
  resetParams(g_ctl);
  EditorTest::tick(ed);
  for (const ControlSpec& spec : kControls) ASSERT_TRUE(near(EditorTest::control(ed, spec.id)->getValueNormalized(), defaultOf(g_ctl, spec.id)));
  ASSERT_TRUE(g_host.total() == 0);
  g_ctl->setComponentHandler(nullptr);
  EditorTest::destroy(ed, root);
}

// A user gesture (begin, one value, end) reaches the host as exactly one beginEdit / performEdit / endEdit, and the
// timer runs that follow do not send it back.
void test_user_edit_reaches_host_once() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  struct Case { ParamID id; float value; };
  for (const Case& k : {Case{kInput, 0.75f}, Case{kRiderIn, 0.f}, Case{kMode, 0.5f}, Case{kCompRatio, 3.f / 7.f}, Case{kTone, 0.25f}}) {
    g_host.clear();
    NormalizedControl* v = EditorTest::control(ed, k.id);
    ASSERT_TRUE(v != nullptr);
    v->beginEdit();
    v->setValueNormalized(k.value);
    v->valueChanged();
    v->endEdit();
    ASSERT_TRUE(g_host.begins[k.id] == 1 && g_host.performs[k.id] == 1 && g_host.ends[k.id] == 1);
    ASSERT_TRUE(near(g_host.last[k.id], k.value) && near(g_ctl->getParamNormalized(k.id), k.value));
    ASSERT_TRUE(g_host.total() == 3);
    for (int t = 0; t < 3; ++t) EditorTest::tick(ed);
    ASSERT_TRUE(g_host.total() == 3);   // no echo
    ASSERT_TRUE(near(v->getValueNormalized(), k.value));
  }
  ASSERT_TRUE(EditorTest::silkValue(ed, kInput) == "+6.0 dB" && EditorTest::silkValue(ed, kTone) == "-50 %");
  ASSERT_TRUE(dynamicTextsMatch(ed));   // Sing and ratio 4:1, from the user's edits
  g_ctl->setComponentHandler(nullptr);
  resetParams(g_ctl);
  EditorTest::destroy(ed, root);
}

// A double click on a knob brings it back to its default as one undoable edit.
void test_double_click_resets_default() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setComponentHandler(&g_host);
  int knobs = 0;
  for (const ControlSpec& spec : kControls) {
    if (spec.kind != Kind::Knob) continue;
    ++knobs;
    auto* knob = dynamic_cast<FairyTapes::Ui::RackKnob*>(EditorTest::control(ed, spec.id));
    ASSERT_TRUE(knob != nullptr);
    const double def = defaultOf(g_ctl, spec.id);
    const double away = def < 0.5 ? 0.9 : 0.1;
    g_ctl->setParamNormalized(spec.id, away);
    EditorTest::tick(ed);
    ASSERT_TRUE(near(knob->getValueNormalized(), away));
    g_host.clear();
    VSTGUI::CPoint at = knob->center();
    knob->onMouseDown(at, VSTGUI::CButtonState(VSTGUI::kLButton | VSTGUI::kDoubleClick));
    ASSERT_TRUE(g_host.begins[spec.id] == 1 && g_host.performs[spec.id] == 1 && g_host.ends[spec.id] == 1 && g_host.total() == 3);
    ASSERT_TRUE(near(g_ctl->getParamNormalized(spec.id), def) && near(knob->getValueNormalized(), def));
    ASSERT_TRUE(EditorTest::silkValue(ed, static_cast<int>(spec.id)) == formatKnobValue(spec.id, def));
    EditorTest::tick(ed);
    ASSERT_TRUE(g_host.total() == 3);
  }
  ASSERT_TRUE(knobs == 12);
  g_ctl->setComponentHandler(nullptr);
  resetParams(g_ctl);
  EditorTest::destroy(ed, root);
}

// Mode from the host: the block tags, the read-outs and the AUTO values of the Comp selectors are the dynamic_text.h
// strings for the new mode (with EQ Amount and Tone away from their defaults).
void test_mode_change_updates_dynamic_texts() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ASSERT_TRUE(dynamicTextsMatch(ed));   // at open
  ASSERT_TRUE(riderMaxText(Mode::Speak) != riderMaxText(Mode::Puke) && detectorTag(Mode::Sing) != detectorTag(Mode::Puke));
  g_ctl->setParamNormalized(kEqAmount, 0.8);
  g_ctl->setParamNormalized(kTone, 0.9);
  EditorTest::tick(ed);
  for (int i : {2, 1, 0}) {
    const Mode m = static_cast<Mode>(i);
    g_ctl->setParamNormalized(kMode, i / 2.0);
    EditorTest::tick(ed);
    ASSERT_TRUE(modeFromNorm(g_ctl->getParamNormalized(kMode)) == m);
    ASSERT_TRUE(dynamicTextsMatch(ed));
    ASSERT_TRUE(EditorTest::silkValue(ed, kCompRatio) == ratioText(0, m));
    ASSERT_TRUE(near(EditorTest::control(ed, kMode)->getValueNormalized(), i / 2.0));
  }
  // the Comp selectors and Tone follow their own parameters too
  g_ctl->setParamNormalized(kCompRatio, 6.0 / 7.0);
  g_ctl->setParamNormalized(kCompDist, 1.0 / 3.0);
  g_ctl->setParamNormalized(kCompHpf, 1.0);
  g_ctl->setParamNormalized(kTone, 0.1);
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::silkValue(ed, kCompRatio) == "10:1" && EditorTest::silkValue(ed, kCompDist) == "OFF" &&
              EditorTest::silkValue(ed, kCompHpf) == "150 Hz");
  ASSERT_TRUE(dynamicTextsMatch(ed));
  // the fixed tags of EQ and COMP
  ASSERT_TRUE(EditorTest::tag(ed, kTagEq) && EditorTest::tag(ed, kTagEq)->text == "FOLLOWS MODE \xC2\xB7 AMOUNT");
  ASSERT_TRUE(EditorTest::tag(ed, kTagComp) &&
              EditorTest::tag(ed, kTagComp)->text == "THRESHOLD SET BY AMOUNT \xC2\xB7 GAIN REDUCTION: SEE LEVEL");
  resetParams(g_ctl);
  EditorTest::destroy(ed, root);
}

// What is dimmed in the editor: controls, silkscreen items, read-outs, tags and panel texts, against the layout rules.
bool dimmingMatches(const Editor& ed, const DimState& s) {
  for (const ControlSpec& spec : kControls) {
    const bool want = controlDimmed(spec.id, s);
    NormalizedControl* v = EditorTest::control(ed, spec.id);
    if (!v || v->isDimmed() != want) {
      std::fprintf(stderr, "  control %u dimmed %d (expected %d)\n", (unsigned)spec.id, v ? (int)v->isDimmed() : -1, (int)want);
      return false;
    }
    if (spec.kind != Kind::Choice && EditorTest::silkDimmed(ed, static_cast<int>(spec.id)) != want) {
      std::fprintf(stderr, "  silkscreen %u dimmed %d (expected %d)\n", (unsigned)spec.id, (int)!want, (int)want);
      return false;
    }
  }
  const ReadoutView* r = EditorTest::readouts(ed);
  if (!r) return false;
  for (const Readout& ro : kReadouts)
    if (r->dimmed(ro.id) != (s.bypass || moduleOut(ro.module, s))) {
      std::fprintf(stderr, "  readout %d\n", (int)ro.id);
      return false;
    }
  const ReadoutView* lamp = EditorTest::lamp(ed);
  if (!lamp || lamp->lampDimmed() != (s.bypass || moduleOut(Module::Breath, s))) return false;
  for (const TagSpec& t : kTags) {
    const auto* tag = EditorTest::tag(ed, t.tag.key);
    const bool want = t.module != Module::None && (s.bypass || moduleOut(t.module, s));   // the MODE tags never dim
    if (!tag || tag->dimmed != want) {
      std::fprintf(stderr, "  tag %d\n", t.tag.key);
      return false;
    }
  }
  const PanelView* p = EditorTest::panel(ed);
  if (!p || p->dimState().bypass != s.bypass) return false;
  for (int m = 0; m < 6; ++m)
    if (p->dimState().in[m] != s.in[m]) return false;
  return true;
}

// A module's IN off dims that module's controls, printed parts, read-outs and tag (and nothing else), after one tick.
void test_in_off_dims_module_controls() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ASSERT_TRUE(dimmingMatches(ed, DimState{}));
  for (int m = 1; m <= 6; ++m) {
    const auto mod = static_cast<Module>(m);
    g_ctl->setParamNormalized(inParamOf(mod), 0.0);
    DimState s{};
    s.in[m - 1] = false;
    ASSERT_TRUE(dimmingMatches(ed, DimState{}));   // not before the tick
    EditorTest::tick(ed);
    int dimmedControls = 0;
    for (const ControlSpec& spec : kControls) dimmedControls += EditorTest::control(ed, spec.id)->isDimmed() ? 1 : 0;
    ASSERT_TRUE(dimmedControls > 0);
    ASSERT_TRUE(!EditorTest::control(ed, inParamOf(mod))->isDimmed());   // its IN stays bright
    ASSERT_TRUE(dimmingMatches(ed, s));
    g_ctl->setParamNormalized(inParamOf(mod), 1.0);
    EditorTest::tick(ed);
    ASSERT_TRUE(dimmingMatches(ed, DimState{}));
  }
  EditorTest::destroy(ed, root);
}

// The two Listen buttons dim with their own module's IN (as in the preview), not with the other one's.
void test_listen_dims_with_its_module() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setParamNormalized(kBreathIn, 0.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::control(ed, kBreathListen)->isDimmed() && EditorTest::silkDimmed(ed, kBreathListen));
  ASSERT_TRUE(!EditorTest::control(ed, kDeEssListen)->isDimmed() && !EditorTest::silkDimmed(ed, kDeEssListen));
  g_ctl->setParamNormalized(kBreathIn, 1.0);
  g_ctl->setParamNormalized(kDeEssIn, 0.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::control(ed, kDeEssListen)->isDimmed() && EditorTest::silkDimmed(ed, kDeEssListen));
  ASSERT_TRUE(!EditorTest::control(ed, kBreathListen)->isDimmed() && !EditorTest::silkDimmed(ed, kBreathListen));
  resetParams(g_ctl);
  EditorTest::tick(ed);
  ASSERT_TRUE(!EditorTest::control(ed, kDeEssListen)->isDimmed());
  EditorTest::destroy(ed, root);
}

// The global bypass dims every control but its own button, every printed part of them, the read-outs and the module
// tags; the MODE tags stay as they are.
void test_bypass_dims_everything_but_itself() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  g_ctl->setParamNormalized(kBypass, 1.0);
  EditorTest::tick(ed);
  DimState s{};
  s.bypass = true;
  ASSERT_TRUE(dimmingMatches(ed, s));
  ASSERT_TRUE(!EditorTest::control(ed, kBypass)->isDimmed() && !EditorTest::silkDimmed(ed, kBypass));
  for (const ControlSpec& spec : kControls)
    if (spec.id != kBypass) ASSERT_TRUE(EditorTest::control(ed, spec.id)->isDimmed());
  // bypass with a module out: still everything (the module's IN included) but the bypass
  g_ctl->setParamNormalized(kCompIn, 0.0);
  EditorTest::tick(ed);
  s.in[static_cast<int>(Module::Comp) - 1] = false;
  ASSERT_TRUE(dimmingMatches(ed, s));
  resetParams(g_ctl);
  EditorTest::tick(ed);
  ASSERT_TRUE(dimmingMatches(ed, DimState{}));
  EditorTest::destroy(ed, root);
}

// A click on a knob's printed value opens a text field with the number; what is typed is read by the controller
// (getParamValueByString) and applied as one edit; a text the controller refuses and Escape change nothing.
void test_typed_value_goes_through_the_controller() {
  SpyController spy;
  spy.initialize(nullptr);
  spy.setComponentHandler(&g_host);
  {
    Editor ed(&spy);
    auto root = EditorTest::build(ed);
    auto* knob = dynamic_cast<FairyTapes::Ui::RackKnob*>(EditorTest::control(ed, kInput));
    ASSERT_TRUE(knob != nullptr);
    auto open = [&]() {
      VSTGUI::CPoint at = knob->valueArea().getCenter();
      knob->onMouseDown(at, VSTGUI::CButtonState(VSTGUI::kLButton));
      return EditorTest::valueEdit(ed);
    };
    g_host.clear();
    VSTGUI::CTextEdit* field = open();
    ASSERT_TRUE(field != nullptr && root->isChild(field));
    ASSERT_TRUE(field->getText() == editFieldText(kInput, 0.5));
    ASSERT_TRUE(g_host.total() == 0);   // the click did not start a drag
    ASSERT_TRUE(open() == field);       // one field at a time
    field->setText("+6");
    ed.valueChanged(field);   // Enter
    EditorTest::closeValueEdit(ed, true);
    ASSERT_TRUE(EditorTest::valueEdit(ed) == nullptr && root->isChild(field) == false);
    ASSERT_TRUE(spy.parsed == 1);
    ASSERT_TRUE(near(spy.getParamNormalized(kInput), 0.75) && near(knob->getValueNormalized(), 0.75));
    ASSERT_TRUE(g_host.begins[kInput] == 1 && g_host.performs[kInput] == 1 && g_host.ends[kInput] == 1 && g_host.total() == 3);
    ASSERT_TRUE(EditorTest::silkValue(ed, kInput) == "+6.0 dB");
    // invalid: refused by the controller, nothing applied
    field = open();
    ASSERT_TRUE(field != nullptr && field->getText() == "+6.0");
    field->setText("loud");
    ed.valueChanged(field);
    EditorTest::closeValueEdit(ed, true);
    ASSERT_TRUE(spy.parsed == 2 && near(spy.getParamNormalized(kInput), 0.75) && g_host.total() == 3);
    // Escape: the field reports what was typed, nothing is read or applied
    field = open();
    ASSERT_TRUE(field != nullptr);
    field->setText("-12");
    VSTGUI::KeyboardEvent esc;
    esc.type = VSTGUI::EventType::KeyDown;
    esc.virt = VSTGUI::VirtualKey::Escape;
    field->onKeyboardEvent(esc);
    ed.valueChanged(field);
    EditorTest::closeValueEdit(ed, false);
    ASSERT_TRUE(spy.parsed == 2 && near(spy.getParamNormalized(kInput), 0.75) && g_host.total() == 3);
    EditorTest::destroy(ed, root);
  }
  spy.setComponentHandler(nullptr);
  spy.terminate();
}

// The values under the knobs are drawn by the handwriting (tape font) by default: the pixels of the Input value differ
// from the plain bold text drawn when the switch is off, and both draw something there.
void test_values_handwritten_by_default() {
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ASSERT_TRUE(EditorTest::handwritten(ed));
  auto region = [&]() {
    std::vector<int> px;
    auto bmp = drawOffscreen(root);
    if (!bmp) return px;
    const Pixels p = pixelsOf(bmp);
    for (int y = 200; y <= 216; ++y)
      for (int x = 52; x <= 106; ++x) px.push_back(brightness(p.at(x, y)));
    return px;
  };
  auto amberCount = [](const std::vector<int>& v) { return std::count_if(v.begin(), v.end(), [](int b) { return b > 0x90; }); };
  const auto hand = region();
  EditorTest::setHandwritten(ed, false);
  const auto plain = region();
  EditorTest::destroy(ed, root);
  ASSERT_TRUE(!hand.empty() && hand.size() == plain.size());
  ASSERT_TRUE(amberCount(hand) > 5 && amberCount(plain) > 5);
  int differing = 0;
  for (std::size_t i = 0; i < hand.size(); ++i) differing += std::abs(hand[i] - plain[i]) > 0x40 ? 1 : 0;
  ASSERT_TRUE(differing > 10);
}

// Off-grid host values of the stepped parameters (automation ramps): the face lit by the control is the step the DSP
// and the SDK display use (stepIndex = floor(n * count)), and the dynamic texts agree with it.
void test_stepped_controls_show_the_dsp_step() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto index = [&](ParamID id) {
    auto* c = dynamic_cast<FairyTapes::Ui::ChoiceButtons*>(EditorTest::control(ed, id));
    return c ? c->selectedIndex() : -1;
  };
  auto setTick = [&](ParamID id, double n) {
    g_ctl->setParamNormalized(id, n);
    EditorTest::tick(ed);
  };
  // Mode 0.3: SPEAK for the DSP (floor 0.9 = 0), not SING (round 0.6 = 1)
  setTick(kMode, 0.3);
  ASSERT_TRUE(index(kMode) == 0 && modeFromNorm(0.3) == Mode::Speak);
  ASSERT_TRUE(dynamicTextsMatch(ed) && EditorTest::tag(ed, kTagModeName)->text == modeTagLine1(Mode::Speak));
  for (double n : {0.26, 0.34, 0.7}) {
    setTick(kMode, n);
    ASSERT_TRUE(index(kMode) == FairyTapes::DeepThroat::detail::stepIndex(n, kModeCount));
    ASSERT_TRUE(dynamicTextsMatch(ed));
  }
  setTick(kCompRatio, 0.1);
  ASSERT_TRUE(index(kCompRatio) == 0 && EditorTest::silkValue(ed, kCompRatio) == ratioText(0, modeFromNorm(g_ctl->getParamNormalized(kMode))));
  for (double n : {0.49, 0.51, 1.0}) {
    setTick(kCompRatio, n);
    ASSERT_TRUE(index(kCompRatio) == FairyTapes::DeepThroat::detail::stepIndex(n, kCompRatioCount));
  }
  ASSERT_TRUE(index(kCompRatio) == 7);
  setTick(kCompDist, 0.2);   // floor 0.8 = 0 (AUTO), round 0.6 + 0.5 = 1
  ASSERT_TRUE(index(kCompDist) == 0 && dynamicTextsMatch(ed));
  setTick(kCompHpf, 0.3);    // floor 0.9 = 0 (OFF), round 1.1 = 1
  ASSERT_TRUE(index(kCompHpf) == 0 && dynamicTextsMatch(ed));
  // every stepped parameter on a fine grid
  struct Stepped { ParamID id; int count; };
  for (const Stepped& st : {Stepped{kMode, kModeCount}, Stepped{kCompRatio, kCompRatioCount}, Stepped{kCompDist, kCompDistCount},
                            Stepped{kCompHpf, kCompHpfCount}}) {
    for (int k = 0; k <= 100; ++k) {
      const double n = k / 100.0;
      setTick(st.id, n);
      const int want = FairyTapes::DeepThroat::detail::stepIndex(n, st.count);
      if (index(st.id) != want) std::fprintf(stderr, "  param %u at %.2f: face %d, DSP step %d\n", (unsigned)st.id, n, index(st.id), want);
      ASSERT_TRUE(index(st.id) == want);
    }
    ASSERT_TRUE(dynamicTextsMatch(ed));
  }
  resetParams(g_ctl);
  EditorTest::destroy(ed, root);
}


// ---- LEVEL meters, clip LEDs, SCOPE (Task 9) ------------------------------------------------------------------------

void resetDisplay(Steinberg::Vst::EditController* ctl) {
  for (Steinberg::Vst::ParamID id = kMeterBase; id < kNumDisplayIDs; ++id) ctl->setParamNormalized(id, defaultOf(ctl, id));
  ctl->setParamNormalized(kClearClips, 0.0);
}
int litLeds(const FairyTapes::Ui::MeterBar* b) { return b ? FairyTapes::Ui::ledsLit(b->level(), kMeterLeds) : -1; }
// Centre pixel of LED `i` of a LEVEL row.
VSTGUI::CColor ledPixel(const Pixels& px, const MeterRow& row, int i) {
  return px.at(static_cast<int>(kMeterFirstX + kMeterPitch * static_cast<float>(i)), static_cast<int>(row.cy));
}
bool isAmber(const VSTGUI::CColor& c) { return c.red > 0xc0 && c.green > 0x90 && c.green < 0xd0 && c.blue < 0x60; }   // #f0b429
bool isRed(const VSTGUI::CColor& c) { return c.red > 0xc0 && c.green < 0x60 && c.blue < 0x60; }                      // #e8392b
bool isGreen(const VSTGUI::CColor& c) { return c.green > 0xb0 && c.red < 0x60; }                                      // #37d04a
bool isOff(const VSTGUI::CColor& c) { return brightness(c) < 0x40; }                                                   // #2a2a2c
// The processor's side of the clip-clear trigger (processor.cpp): clears the clip flags and answers with 0.
void processorAcknowledgesClear() {
  if (g_ctl->getParamNormalized(kClearClips) < 0.5) return;
  g_ctl->setParamNormalized(kDispClipIn, 0.0);
  g_ctl->setParamNormalized(kDispClipOut, 0.0);
  g_ctl->setParamNormalized(kClearClips, 0.0);
}

// Each LEVEL row reads its own display parameter on the timer: distinct values per row, every value (0, 1, in between,
// NaN, out of range) lights the nearest number of the 15 LEDs, nothing moves before the tick.
void test_meters_follow_display_params() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double values[] = {0.0, 1.0, 0.5, 0.27, nan, 2.0, -1.0, 0.93};
  const int want[] = {0, 15, 8, 4, 0, 15, 0, 14};   // round(v * 15); NaN -> 0, out of range clamped by the parameter
  constexpr int kN = static_cast<int>(std::size(values));
  for (int j = 0; j < kN; ++j) {
    for (int r = 0; r < kMeterRowCount; ++r) g_ctl->setParamNormalized(kMeterRows[r].id, values[(j + r) % kN]);
    if (j == 1) ASSERT_TRUE(litLeds(EditorTest::bar(ed, 0)) == want[0]);   // only flagged values: the timer applies them
    EditorTest::tick(ed);
    for (int r = 0; r < kMeterRowCount; ++r) {
      const int got = litLeds(EditorTest::bar(ed, r));
      if (got != want[(j + r) % kN]) std::fprintf(stderr, "  row %d step %d: %d lit (expected %d)\n", r, j, got, want[(j + r) % kN]);
      ASSERT_TRUE(got == want[(j + r) % kN]);
    }
  }
  resetDisplay(g_ctl);
  EditorTest::tick(ed);
  for (int r = 0; r < kMeterRowCount; ++r) ASSERT_TRUE(litLeds(EditorTest::bar(ed, r)) == 0);
  EditorTest::destroy(ed, root);
}

// The GR row is amber with the last four LEDs red (preview: i > 10 red); the level rows go green to red. Pixels.
void test_gr_row_amber_with_red_tail() {
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  const MeterRow& gr = kMeterRows[4];
  ASSERT_TRUE(gr.id == kDispGr && gr.gainReduction);
  g_ctl->setParamNormalized(kDispGr, 1.0);
  g_ctl->setParamNormalized(kDispInL, 1.0);
  EditorTest::tick(ed);
  {
    auto bmp = drawOffscreen(root);
    ASSERT_TRUE(bmp != nullptr);
    const Pixels px = pixelsOf(bmp);
    for (int i = 0; i < kMeterLeds; ++i) {
      const VSTGUI::CColor c = ledPixel(px, gr, i);
      if (i <= 10) ASSERT_TRUE(isAmber(c));
      else ASSERT_TRUE(isRed(c));
    }
    ASSERT_TRUE(isGreen(ledPixel(px, kMeterRows[0], 0)) && isRed(ledPixel(px, kMeterRows[0], 14)));
    ASSERT_TRUE(isOff(ledPixel(px, kMeterRows[1], 0)));   // In R stays at 0
  }
  g_ctl->setParamNormalized(kDispGr, 0.5);   // 10.5 dB: 8 LEDs
  EditorTest::tick(ed);
  {
    auto bmp = drawOffscreen(root);
    const Pixels px = pixelsOf(bmp);
    for (int i = 0; i < kMeterLeds; ++i) {
      const VSTGUI::CColor c = ledPixel(px, gr, i);
      if (i < 8) ASSERT_TRUE(isAmber(c));
      else ASSERT_TRUE(isOff(c));
    }
  }
  resetDisplay(g_ctl);
  EditorTest::destroy(ed, root);
}

// The held peak of a level row stays for the hold time after the level falls, then falls (the timer passes its period).
void test_meter_peak_hold_falls_after_hold() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  FairyTapes::Ui::MeterBar* bar = EditorTest::bar(ed, 2);   // Out L
  ASSERT_TRUE(bar != nullptr && kMeterRows[2].id == kDispOutL);
  g_ctl->setParamNormalized(kDispOutL, 0.9);
  EditorTest::tick(ed);
  g_ctl->setParamNormalized(kDispOutL, 0.1);
  for (int t = 0; t < 40; ++t) EditorTest::tick(ed);   // 1.2 s < 1.5 s hold
  ASSERT_TRUE(near(bar->level(), 0.1) && near(bar->hold(), 0.9));
  for (int t = 0; t < 40; ++t) EditorTest::tick(ed);   // 2.4 s: falling at 20 dB/s
  ASSERT_TRUE(bar->hold() < 0.85 && bar->hold() > 0.1);
  for (int t = 0; t < 200; ++t) EditorTest::tick(ed);
  ASSERT_TRUE(near(bar->hold(), 0.1));
  resetDisplay(g_ctl);
  EditorTest::destroy(ed, root);
}

// Each clip LED follows its own parameter; a left click on either writes kClearClips = 1 through the host handler
// exactly once (begin, perform, end); the processor's answer (flags and trigger back to 0) turns both off.
void test_clip_led_follows_param_and_click_clears_via_trigger() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  FairyTapes::Ui::ClipLed* in = EditorTest::clip(ed, 0);
  FairyTapes::Ui::ClipLed* out = EditorTest::clip(ed, 1);
  ASSERT_TRUE(in && out && kClips[0].id == kDispClipIn && kClips[1].id == kDispClipOut);
  g_ctl->setParamNormalized(kDispClipIn, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(in->isLit() && !out->isLit());
  g_ctl->setParamNormalized(kDispClipIn, 0.0);
  g_ctl->setParamNormalized(kDispClipOut, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(!in->isLit() && out->isLit());
  g_ctl->setParamNormalized(kDispClipIn, 1.0);
  EditorTest::tick(ed);
  ASSERT_TRUE(in->isLit() && out->isLit());

  g_ctl->setComponentHandler(&g_host);
  for (int k = 0; k < 2; ++k) {
    g_host.clear();
    VSTGUI::CPoint where(kClips[k].cx, kClipRowY);
    FairyTapes::Ui::ClipLed* led = k == 0 ? in : out;
    ASSERT_TRUE(led->onMouseDown(where, VSTGUI::CButtonState(VSTGUI::kLButton)) == VSTGUI::kMouseEventHandled);
    ASSERT_TRUE(g_host.clearBegins == 1 && g_host.clearPerforms == 1 && g_host.clearEnds == 1);
    ASSERT_TRUE(g_host.clearLast == 1.0 && g_host.total() == 3);   // nothing else is sent
    ASSERT_TRUE(g_ctl->getParamNormalized(kClearClips) == 1.0);
    ASSERT_TRUE(in->isLit() && out->isLit());   // until the processor answers
    processorAcknowledgesClear();
    EditorTest::tick(ed);
    ASSERT_TRUE(!in->isLit() && !out->isLit());
    ASSERT_TRUE(g_ctl->getParamNormalized(kClearClips) == 0.0);
    ASSERT_TRUE(g_host.total() == 3);   // the answer is not echoed
    g_ctl->setParamNormalized(kDispClipIn, 1.0);
    g_ctl->setParamNormalized(kDispClipOut, 1.0);
    EditorTest::tick(ed);
  }
  g_host.clear();
  VSTGUI::CPoint where(kClips[0].cx, kClipRowY);
  ASSERT_TRUE(in->onMouseDown(where, VSTGUI::CButtonState(VSTGUI::kRButton)) == VSTGUI::kMouseEventNotHandled);
  ASSERT_TRUE(g_host.total() == 0);   // a right click clears nothing
  g_ctl->setComponentHandler(nullptr);
  resetDisplay(g_ctl);
  EditorTest::destroy(ed, root);
}

// One SCOPE point per timer run: the history grows to 292 points, then the oldest goes first (after 400 runs the oldest
// is run 108, the newest run 399).
void test_scope_history_grows_to_292() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ScopeView* scope = EditorTest::scope(ed);
  ASSERT_TRUE(scope != nullptr && ScopeView::kPoints == 292);
  const int start = scope->history().size();   // the runs of build(): none
  ASSERT_TRUE(start == 0);
  auto voiceOf = [](int k) { return static_cast<float>(k / 1000.0); };
  auto riderOf = [](int k) { return static_cast<float>((k % 97) / 100.0); };
  for (int k = 0; k < 400; ++k) {
    g_ctl->setParamNormalized(kDispVoice, k / 1000.0);
    g_ctl->setParamNormalized(kDispRider, (k % 97) / 100.0);
    EditorTest::tick(ed);
    if (k == 99) {
      ASSERT_TRUE(scope->history().size() == 100);
      ASSERT_TRUE(scope->history().at(0).voice == voiceOf(0) && scope->history().at(99).voice == voiceOf(99));
    }
  }
  const auto& h = scope->history();
  ASSERT_TRUE(h.size() == 292);
  ASSERT_TRUE(h.at(0).voice == voiceOf(108) && h.at(0).rider == riderOf(108));
  ASSERT_TRUE(h.at(291).voice == voiceOf(399) && h.at(291).rider == riderOf(399));
  for (int i = 0; i < 292; ++i) ASSERT_TRUE(h.at(i).voice == voiceOf(108 + i));
  resetDisplay(g_ctl);
  EditorTest::destroy(ed, root);
}

// Voice, Rider gain and breath of each run land in their own trace (the newest column, at the right edge); a breath
// above 0.5 paints the red band over the full height of its columns, nothing elsewhere.
void test_scope_columns_follow_display_params() {
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ScopeView* scope = EditorTest::scope(ed);
  ASSERT_TRUE(scope != nullptr);
  const ScopePoint a{0.7f, 0.25f, 0.9f}, b{0.1f, 0.8f, 0.2f};
  for (const ScopePoint& p : {a, b}) {
    g_ctl->setParamNormalized(kDispVoice, p.voice);
    g_ctl->setParamNormalized(kDispRider, p.rider);
    g_ctl->setParamNormalized(kDispBreath, p.breath);
    EditorTest::tick(ed);
  }
  auto cols = scope->columns();
  ASSERT_TRUE(cols.size() == 2);
  const ScopeColumn ca = scopeColumnOf(a, kScopeRect), cb = scopeColumnOf(b, kScopeRect);
  ASSERT_TRUE(cols[0].voiceTop == ca.voiceTop && cols[0].riderY == ca.riderY && cols[0].breath);
  ASSERT_TRUE(cols[1].voiceTop == cb.voiceTop && cols[1].riderY == cb.riderY && !cols[1].breath);
  ASSERT_TRUE(ca.voiceTop != cb.voiceTop && ca.riderY != cb.riderY);
  ASSERT_TRUE(near(scope->columnX(1), kScopeRect.x + kScopeRect.w) && scope->columnX(0) < scope->columnX(1));

  // A full history, breath only in runs 100..149: the band shows there (red over the black), not at run 250.
  for (int k = 0; k < ScopeView::kPoints; ++k) {
    g_ctl->setParamNormalized(kDispVoice, 0.3);
    g_ctl->setParamNormalized(kDispRider, 0.5);
    g_ctl->setParamNormalized(kDispBreath, (k >= 100 && k < 150) ? 0.9 : 0.1);
    EditorTest::tick(ed);
  }
  cols = scope->columns();
  ASSERT_TRUE(static_cast<int>(cols.size()) == ScopeView::kPoints && cols[125].breath && !cols[250].breath && !cols[99].breath);
  auto bmp = drawOffscreen(root);
  ASSERT_TRUE(bmp != nullptr);
  const Pixels px = pixelsOf(bmp);
  const int y = static_cast<int>(kScopeRect.y + 6);   // above the traces (voice -40 dB, gain 0 dB in the middle)
  const VSTGUI::CColor inBand = px.at(static_cast<int>(scope->columnX(125)), y);
  const VSTGUI::CColor outBand = px.at(static_cast<int>(scope->columnX(250)), y);
  ASSERT_TRUE(inBand.red > 0x30 && inBand.red > 2 * inBand.green);
  ASSERT_TRUE(brightness(outBand) < 0x18);
  resetDisplay(g_ctl);
  EditorTest::destroy(ed, root);
}

// The BREATH lamp lights while the latest breath probability is above 0.5 (0.5 itself and NaN: off), and draws red.
void test_breath_lamp_lights_above_half() {
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ReadoutView* r = EditorTest::lamp(ed);
  ASSERT_TRUE(r != nullptr && !r->lampLit());
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const struct { double p; bool lit; } cases[] = {{0.4, false}, {0.6, true}, {0.5, false}, {1.0, true}, {nan, false}, {0.51, true}, {0.0, false}};
  for (const auto& c : cases) {
    g_ctl->setParamNormalized(kDispBreath, c.p);
    EditorTest::tick(ed);
    ASSERT_TRUE(r->lampLit() == c.lit);
  }
  g_ctl->setParamNormalized(kDispBreath, 0.9);
  EditorTest::tick(ed);
  auto lampColour = [&]() {
    auto bmp = drawOffscreen(root);
    return pixelsOf(bmp).at(static_cast<int>(kBreathLamp.cx), static_cast<int>(kBreathLamp.cy));
  };
  ASSERT_TRUE(isRed(lampColour()));
  g_ctl->setParamNormalized(kDispBreath, 0.1);
  EditorTest::tick(ed);
  const VSTGUI::CColor off = lampColour();
  ASSERT_TRUE(off.red < 0x50 && off.green < 0x20);   // #3a1010
  resetDisplay(g_ctl);
  EditorTest::destroy(ed, root);
}

// SCOPE and LEVEL dim only with the global Bypass: the module INs off leave them lit; Bypass dims the scope, the GR row
// and its texts but not the IN / OUT rows nor the clip LEDs.
void test_scope_and_level_dim_only_on_bypass() {
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ScopeView* scope = EditorTest::scope(ed);
  PanelView* panel = EditorTest::panel(ed);
  ASSERT_TRUE(scope && panel);
  for (Steinberg::Vst::ParamID id = kMeterBase; id < kNumDisplayIDs; ++id) g_ctl->setParamNormalized(id, 1.0);
  const ParamID ins[] = {kRiderIn, kBreathIn, kEqIn, kDeEssIn, kCompIn, kToneIn};
  auto check = [&](bool bypass) {
    EditorTest::tick(ed);
    if (scope->dimmed() != bypass) return false;
    auto bmp = drawOffscreen(root);
    if (!bmp) return false;
    const Pixels px = pixelsOf(bmp);
    for (int r = 0; r < kMeterRowCount; ++r) {
      if (bypass && kMeterRows[r].gainReduction) {   // no gain reduction while bypassed: the row is empty
        if (litLeds(EditorTest::bar(ed, r)) != 0) return false;
        continue;
      }
      const bool dim = bypass && !kMeterRows[r].alwaysLit;
      const int lit = brightness(ledPixel(px, kMeterRows[r], 0));
      if (dim ? lit > 0x80 : lit < 0xc0) {
        std::fprintf(stderr, "  row %d brightness %d (bypass %d)\n", r, lit, (int)bypass);
        return false;
      }
    }
    for (const ClipSpec& c : kClips)
      if (!isRed(px.at(static_cast<int>(c.cx), static_cast<int>(kClipRowY)))) return false;
    for (std::size_t i = 0; i < kNumPanelTexts; ++i)
      if (!std::strcmp(kPanelTexts[i].text, "GR") && panel->textDimmed(i) != bypass) return false;
    return true;
  };
  ASSERT_TRUE(kMeterRows[4].id == kDispGr && !kMeterRows[4].alwaysLit);
  ASSERT_TRUE(check(false));
  for (ParamID id : ins) g_ctl->setParamNormalized(id, 0.0);
  ASSERT_TRUE(check(false));   // every IN off: no dimming here
  g_ctl->setParamNormalized(kBypass, 1.0);
  ASSERT_TRUE(check(true));
  for (ParamID id : ins) g_ctl->setParamNormalized(id, 1.0);
  ASSERT_TRUE(check(true));
  g_ctl->setParamNormalized(kBypass, 0.0);
  ASSERT_TRUE(check(false));
  resetParams(g_ctl);
  resetDisplay(g_ctl);
  EditorTest::destroy(ed, root);
}

// As the preview: a breath is shown (lamp, scope band) only while Breath IN is on and the plugin is not bypassed, and
// the GR row is empty while bypassed (Comp IN off already reads 0 from the engine). The display values stay as written.
void test_breath_and_gr_gated_by_in_and_bypass() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ReadoutView* lamp = EditorTest::lamp(ed);
  ScopeView* scope = EditorTest::scope(ed);
  FairyTapes::Ui::MeterBar* gr = EditorTest::bar(ed, 4);
  ASSERT_TRUE(lamp && scope && gr && kMeterRows[4].id == kDispGr);
  g_ctl->setParamNormalized(kDispBreath, 0.9);
  g_ctl->setParamNormalized(kDispGr, 1.0);
  auto state = [&](bool breath, int grLit) {
    EditorTest::tick(ed);
    const auto cols = scope->columns();
    return !cols.empty() && lamp->lampLit() == breath && cols.back().breath == breath &&
           (scope->history().at(scope->history().size() - 1).breath > 0.f) == breath && litLeds(gr) == grLit;
  };
  ASSERT_TRUE(state(true, 15));
  g_ctl->setParamNormalized(kBreathIn, 0.0);
  ASSERT_TRUE(state(false, 15));   // Breath IN off: no breath shown, GR as it is
  g_ctl->setParamNormalized(kBreathIn, 1.0);
  ASSERT_TRUE(state(true, 15));
  g_ctl->setParamNormalized(kBypass, 1.0);
  ASSERT_TRUE(state(false, 0));    // Bypass: no breath, no gain reduction
  g_ctl->setParamNormalized(kCompIn, 0.0);
  g_ctl->setParamNormalized(kBypass, 0.0);
  ASSERT_TRUE(state(true, 15));    // Comp IN off is the engine's job (it reports 0): the editor shows what it gets
  ASSERT_TRUE(near(g_ctl->getParamNormalized(kDispBreath), 0.9) && near(g_ctl->getParamNormalized(kDispGr), 1.0));
  resetParams(g_ctl);
  resetDisplay(g_ctl);
  EditorTest::destroy(ed, root);
}

// Open / close 50 times (the editor from createView) with timer runs in between: after close() no timer is left and a
// timer run touches none of the old views (meters, clips, scope keep their state while the display values move on).
void test_editor_timer_stops_on_close() {
  for (int i = 0; i < 50; ++i) {
    Steinberg::IPlugView* view = g_ctl->createView(Steinberg::Vst::ViewType::kEditor);
    ASSERT_TRUE(view != nullptr);
    auto* ed = dynamic_cast<Editor*>(view);
    ASSERT_TRUE(ed != nullptr);
    auto root = EditorTest::build(*ed);
    auto* bar = EditorTest::bar(*ed, 0);
    auto* clip = EditorTest::clip(*ed, 0);
    ScopeView* scope = EditorTest::scope(*ed);
    ASSERT_TRUE(bar && clip && scope);
    bar->remember();
    clip->remember();
    scope->remember();
    g_ctl->setParamNormalized(kDispInL, (i % 10) / 10.0 + 0.05);
    g_ctl->setParamNormalized(kDispClipIn, (i & 1) ? 1.0 : 0.0);
    EditorTest::tick(*ed);
    EditorTest::tick(*ed);
    const double level = bar->level();
    const bool lit = clip->isLit();
    const int points = scope->history().size();
    ASSERT_TRUE(points == 2 && lit == ((i & 1) != 0));
    EditorTest::destroy(*ed, root);
    ed->close();   // no frame: the timer and the view pointers
    ASSERT_TRUE(!EditorTest::timerRunning(*ed) && !EditorTest::bar(*ed, 0) && !EditorTest::clip(*ed, 0) && !EditorTest::scope(*ed));
    g_ctl->setParamNormalized(kDispInL, 0.99);
    g_ctl->setParamNormalized(kDispClipIn, (i & 1) ? 0.0 : 1.0);
    EditorTest::tick(*ed);
    EditorTest::tick(*ed);
    ASSERT_TRUE(bar->level() == level && clip->isLit() == lit && scope->history().size() == points);
    ASSERT_TRUE(bar->getNbReference() == 1 && clip->getNbReference() == 1 && scope->getNbReference() == 1);
    bar->forget();
    clip->forget();
    scope->forget();
    ASSERT_TRUE(view->release() == 0);
  }
  resetDisplay(g_ctl);
}


// ---- Wear, paper tapes, screws and preferences (Task 10) -----------------------------------------------------------

using FairyTapes::Ui::WearLevel;

const Rect kPanelRect{0.f, 0.f, static_cast<float>(kEditorWidth), static_cast<float>(kEditorHeight)};

// Index of `v` among the children of `root` (-1 when it is not one).
int childIndex(VSTGUI::CViewContainer* root, const VSTGUI::CView* v) {
  for (uint32_t i = 0; i < root->getNbViews(); ++i)
    if (root->getView(i) == v) return static_cast<int>(i);
  return -1;
}
int countTapeChildren(VSTGUI::CViewContainer* root) {
  int n = 0;
  for (uint32_t i = 0; i < root->getNbViews(); ++i) n += dynamic_cast<TapeView*>(root->getView(i)) ? 1 : 0;
  return n;
}
// The decor back to blank tapes and unturned screws (the seed is kept).
void clearDecor() { g_ctl->editDecor(Decor{}); }

// Light (the default) shows marks and 1-2 tapes; Off: no wear mark and no tape view (the four screws stay).
void test_wear_off_has_no_marks_and_no_tapes() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ASSERT_TRUE(ed.wearLevel() == WearLevel::Light);
  auto* wear = EditorTest::wear(ed);
  ASSERT_TRUE(wear != nullptr && root->isChild(wear));
  ASSERT_TRUE(!wear->marks().empty());
  const std::size_t light = layoutFromSeed(ed.wearSeed(), WearLevel::Light, kPanelRect).tapes.size();
  ASSERT_TRUE(light >= 1 && EditorTest::tapes(ed).size() == light && countTapeChildren(root) == static_cast<int>(light));
  ed.setWearLevel(WearLevel::Off);
  ASSERT_TRUE(EditorTest::wear(ed) == wear && wear->marks().empty());
  ASSERT_TRUE(EditorTest::tapes(ed).empty() && countTapeChildren(root) == 0);
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(EditorTest::screw(ed, i) != nullptr && root->isChild(EditorTest::screw(ed, i)));
  EditorTest::destroy(ed, root);
}

// Wear Light -> Heavy -> Light: the same views (panel, wear, controls, screws), new marks and tapes for the level, tapes
// drawn in the 4 px box of the decor model, every decor layer between the silkscreen and the controls.
void test_wear_level_change_redraws_without_rebuild() {
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto* wear = EditorTest::wear(ed);
  ASSERT_TRUE(wear != nullptr);
  std::vector<VSTGUI::CView*> before;
  for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) before.push_back(EditorTest::control(ed, static_cast<ParamID>(id)));
  for (int i = 0; i < 4; ++i) before.push_back(EditorTest::screw(ed, i));
  before.push_back(EditorTest::panel(ed));
  const uint32_t seed = ed.wearSeed();
  for (WearLevel level : {WearLevel::Heavy, WearLevel::Light, WearLevel::Heavy}) {
    ed.setWearLevel(level);
      ASSERT_TRUE(ed.wearLevel() == level && EditorTest::wear(ed) == wear);
    ASSERT_TRUE(wear->marks().size() == FairyTapes::Ui::generateWear(seed, level, kEditorWidth, kEditorHeight).size());
    std::vector<VSTGUI::CView*> now;
    for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) now.push_back(EditorTest::control(ed, static_cast<ParamID>(id)));
    for (int i = 0; i < 4; ++i) now.push_back(EditorTest::screw(ed, i));
    now.push_back(EditorTest::panel(ed));
    ASSERT_TRUE(now == before);
    const WearLayout layout = layoutFromSeed(seed, level, kPanelRect);
    const auto& tapes = EditorTest::tapes(ed);
    ASSERT_TRUE(tapes.size() == layout.tapes.size() && countTapeChildren(root) == static_cast<int>(tapes.size()));
    for (std::size_t k = 0; k < tapes.size(); ++k) {
      const TapePlacement& p = layout.tapes[k];
      const auto b = FairyTapes::Ui::tapeBounds(p.cx, p.cy, p.w, p.h, p.rotDeg, FairyTapes::Ui::kTapeRaggedX + 1);
      const VSTGUI::CRect v = tapes[k]->getViewSize();
      ASSERT_TRUE(tapes[k]->slot() == p.slot);
      ASSERT_TRUE(std::fabs(v.left - b.left) < 1e-6 && std::fabs(v.right - b.right) < 1e-6 &&
                  std::fabs(v.top - b.top) < 1e-6 && std::fabs(v.bottom - b.bottom) < 1e-6);
    }
    // Layers: silkscreen and read-outs < wear < tapes < screws < SCOPE and controls.
    const int iWear = childIndex(root, wear);
    ASSERT_TRUE(childIndex(root, EditorTest::readouts(ed)) < iWear);
    int lastTape = iWear;
    for (TapeView* t : tapes) {
      ASSERT_TRUE(childIndex(root, t) > iWear);
      lastTape = std::max(lastTape, childIndex(root, t));
    }
    int firstControl = 1 << 30;
    for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) firstControl = std::min(firstControl, childIndex(root, EditorTest::control(ed, static_cast<ParamID>(id))));
    for (int i = 0; i < 4; ++i) {
      const int s = childIndex(root, EditorTest::screw(ed, i));
      ASSERT_TRUE(s > lastTape && s < childIndex(root, EditorTest::scope(ed)) && s < firstControl);
      ASSERT_TRUE(s < childIndex(root, EditorTest::lamp(ed)));   // the BREATH lamp is an LED: over the decor
    }
  }
  ed.setWearLevel(WearLevel::Light);
  EditorTest::destroy(ed, root);
}

// A click on a tape opens a field; 40 typed characters (some not ASCII) are stored as the 14-character printable ASCII
// text through one editDecor; Escape stores nothing.
void test_tape_text_edit_goes_to_decor() {
  clearDecor();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(WearLevel::Heavy);
  ASSERT_TRUE(!EditorTest::tapes(ed).empty());
  TapeView* tape = EditorTest::tapes(ed)[0];
  const int slot = tape->slot();
  VSTGUI::CPoint at = tape->tapeRect().getCenter();
  const uint32_t rev0 = g_ctl->decorRevision();
  ASSERT_TRUE(tape->onMouseDown(at, VSTGUI::CButtonState(VSTGUI::kLButton)) == VSTGUI::kMouseEventHandled);
  VSTGUI::CTextEdit* field = EditorTest::tapeEdit(ed);
  ASSERT_TRUE(field != nullptr && root->isChild(field));
  const std::string typed = "  Lead vox \xC3\xA9 take 3 - keep it, sure!!!!";   // 40 bytes, two of them not ASCII
  ASSERT_TRUE(typed.size() == 40);
  field->setText(typed.c_str());
  ed.valueChanged(field);   // Enter
  EditorTest::closeTapeEdit(ed, true);
  ASSERT_TRUE(EditorTest::tapeEdit(ed) == nullptr && !root->isChild(field));
  ASSERT_TRUE(g_ctl->decorRevision() == rev0 + 1);   // one editDecor
  const std::string stored = tapeTextFor(g_ctl->decor(), slot);
  ASSERT_TRUE(stored == sanitizeTapeText(typed) && stored.size() == 14);
  for (char c : stored) ASSERT_TRUE(c >= 0x20 && c <= 0x7E);
  ASSERT_TRUE(tape->text() == stored);
  // Escape: nothing stored
  ASSERT_TRUE(tape->onMouseDown(at, VSTGUI::CButtonState(VSTGUI::kLButton)) == VSTGUI::kMouseEventHandled);
  field = EditorTest::tapeEdit(ed);
  ASSERT_TRUE(field != nullptr && field->getText() == stored.c_str());
  field->setText("other");
  VSTGUI::KeyboardEvent esc;
  esc.type = VSTGUI::EventType::KeyDown;
  esc.virt = VSTGUI::VirtualKey::Escape;
  field->onKeyboardEvent(esc);
  ed.valueChanged(field);
  EditorTest::closeTapeEdit(ed, false);
  ASSERT_TRUE(g_ctl->decorRevision() == rev0 + 1 && tapeTextFor(g_ctl->decor(), slot) == stored && tape->text() == stored);
  EditorTest::destroy(ed, root);
  clearDecor();
}

// A drag of a screw: no decor edit while it moves, exactly one at the end with the user's rotation; the timer keeps the
// shown angle.
void test_screw_drag_stores_angle_once() {
  clearDecor();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(EditorTest::screw(ed, i) != nullptr);
  int index = -1;
  for (int i = 0; i < 4 && index < 0; ++i)
    if (!EditorTest::screw(ed, i)->spec().missing) index = i;
  ASSERT_TRUE(index >= 0);
  FairyTapes::Ui::ScrewView* screw = EditorTest::screw(ed, index);
  const double initial = screw->spec().initialAngle;
  ASSERT_TRUE(std::fabs(screw->angle() - FairyTapes::Ui::normalizeScrewAngle(initial)) < 1e-6);
  const uint32_t rev0 = g_ctl->decorRevision();
  VSTGUI::CPoint p(screw->spec().x, screw->spec().y);
  ASSERT_TRUE(screw->onMouseDown(p, VSTGUI::CButtonState(VSTGUI::kLButton)) == VSTGUI::kMouseEventHandled);
  for (int k = 1; k <= 4; ++k) {
    VSTGUI::CPoint q(screw->spec().x + 8 * k, screw->spec().y - 4 * k);
    screw->onMouseMoved(q, VSTGUI::CButtonState(VSTGUI::kLButton));
    ASSERT_TRUE(g_ctl->decorRevision() == rev0);
  }
  VSTGUI::CPoint end(screw->spec().x + 32, screw->spec().y - 16);
  screw->onMouseUp(end, VSTGUI::CButtonState(VSTGUI::kLButton));
  ASSERT_TRUE(g_ctl->decorRevision() == rev0 + 1);
  const double shown = screw->angle();
  ASSERT_TRUE(std::fabs(shown - FairyTapes::Ui::normalizeScrewAngle(initial)) > 1.0);   // it turned
  const float stored = g_ctl->decor().screwAngles[index];
  ASSERT_TRUE(std::fabs(stored - userScrewAngle(shown, initial)) < 1e-4 && stored != 0.f);
  for (int i = 0; i < 4; ++i)
    if (i != index) ASSERT_TRUE(g_ctl->decor().screwAngles[i] == 0.f);
  EditorTest::tick(ed);
  ASSERT_TRUE(std::fabs(screw->angle() - shown) < 1e-4 && g_ctl->decorRevision() == rev0 + 1);
  EditorTest::destroy(ed, root);
  clearDecor();
}

// RE-ROLL: a new non-zero seed in the decor, rotations back to 0, tapes blank; wear, tapes and screws follow the new seed.
void test_reroll_changes_seed_and_clears_decor() {
  Decor d = g_ctl->decor();
  d.screwAngles[0] = 45.f;
  d.screwAngles[3] = 90.f;
  d.tapeTexts.assign(kNumTapeSlots, "WRITTEN");
  g_ctl->editDecor(d);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(WearLevel::Heavy);
  const uint32_t seed0 = ed.wearSeed();
  ASSERT_TRUE(seed0 != 0 && seed0 == g_ctl->decor().seed);
  ASSERT_TRUE(!EditorTest::tapes(ed).empty() && EditorTest::tapes(ed)[0]->text() == "WRITTEN");
  ed.reroll();
  const Decor r = g_ctl->decor();
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
  ed.setWearLevel(WearLevel::Light);
  EditorTest::destroy(ed, root);
}

// The timer applies a decor changed elsewhere (state recall, undo) on its next run, and only then; with the same
// revision nothing is rebuilt.
void test_decor_change_applied_on_tick() {
  clearDecor();
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(WearLevel::Heavy);
  const std::vector<TapeView*> tapes = EditorTest::tapes(ed);
  ASSERT_TRUE(!tapes.empty());
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::tapes(ed) == tapes);
  Decor d = g_ctl->decor();
  d = withTapeText(d, tapes[0]->slot(), "FROM HOST");
  d.screwAngles[1] = 30.f;
  g_ctl->editDecor(d);
  ASSERT_TRUE(tapes[0]->text().empty());   // not before the timer
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::tapes(ed) == tapes && tapes[0]->text() == "FROM HOST");   // same seed: texts only
  const auto* s = EditorTest::screw(ed, 1);
  ASSERT_TRUE(s != nullptr && std::fabs(s->angle() - shownScrewAngle(s->spec().initialAngle, 30.f)) < 1e-4);
  ed.setWearLevel(WearLevel::Light);
  EditorTest::destroy(ed, root);
  clearDecor();
}

// Preferences: a valid file is applied (Heavy, Rock Salt, plain values); a missing folder or a corrupt file gives the
// defaults (Light, Zeyada, values in the tape font) and the editor still has all its views.
void test_prefs_missing_file_opens_with_defaults() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / ("dt_editor_prefs_" + std::to_string(static_cast<unsigned>(std::rand())));
  const fs::path file = dir / "FairyTapes" / "DeepThroat.prefs";
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  auto isDefault = [&]() {
    return ed.wearLevel() == WearLevel::Light && ed.tapeFont() == TapeFont::Zeyada && ed.valuesHandwritten() &&
           EditorTest::valuesFont(ed) == FairyTapes::Ui::TapeFontId::Zeyada &&
           EditorTest::tapes(ed).size() == layoutFromSeed(ed.wearSeed(), WearLevel::Light, kPanelRect).tapes.size();
  };
  auto applyCustom = [&]() {
    Prefs p;
    p.wear = WearLevel::Heavy;
    p.tapeFont = TapeFont::RockSalt;
    p.handwrittenValues = false;
    if (!savePrefs(file, p)) return false;
    EditorTest::loadAppearance(ed, file);
    return ed.wearLevel() == WearLevel::Heavy && ed.tapeFont() == TapeFont::RockSalt && !ed.valuesHandwritten() &&
           EditorTest::valuesFont(ed) == FairyTapes::Ui::TapeFontId::RockSalt &&
           EditorTest::wear(ed) && EditorTest::wear(ed)->marks().size() ==
               FairyTapes::Ui::generateWear(ed.wearSeed(), WearLevel::Heavy, kEditorWidth, kEditorHeight).size();
  };
  ASSERT_TRUE(applyCustom());
  EditorTest::loadAppearance(ed, dir / "missing" / "FairyTapes" / "DeepThroat.prefs");   // no such folder
  ASSERT_TRUE(isDefault());
  ASSERT_TRUE(applyCustom());
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const char junk[] = {'\x00', '\xff', '\x13', 'w', 'e', 'a', 'r', '\x00', '=', '\x7f', '\xfe'};
    out.write(junk, sizeof junk);
  }
  EditorTest::loadAppearance(ed, file);
  ASSERT_TRUE(isDefault());
  for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) ASSERT_TRUE(EditorTest::control(ed, static_cast<ParamID>(id)) != nullptr);
  EditorTest::destroy(ed, root);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// No grain asset: the editor with its wear, tapes and screws draws without crashing and, with Wear Off, the panel is
// plain (a blank row of the margin is one colour).
void test_missing_grain_asset_draws_plain_panel() {
  setResourcePath(false);
  Editor ed(g_ctl);
  auto root = EditorTest::build(ed);
  ed.setWearLevel(WearLevel::Heavy);
  ASSERT_TRUE(drawOffscreen(root) != nullptr);
  ed.setWearLevel(WearLevel::Off);
  auto bmp = drawOffscreen(root);
  ASSERT_TRUE(bmp != nullptr);
  const Pixels px = pixelsOf(bmp);
  int lo = 255, hi = 0;
  for (int x = 6; x <= 20; ++x)
    for (int y = 280; y <= 320; y += 10) {
      lo = std::min<int>(lo, px.at(x, y).red);
      hi = std::max<int>(hi, px.at(x, y).red);
    }
  ASSERT_TRUE(hi - lo <= 2 && brightness(px.at(10, 300)) >= 0x08);
  ed.setWearLevel(WearLevel::Light);
  EditorTest::destroy(ed, root);
}


// ---- Back of the panel (Task 11) ------------------------------------------------------------------------------------
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

std::filesystem::path scratchPrefsDir(const char* name) {
  return std::filesystem::temp_directory_path() /
         (std::string(name) + "_" + std::to_string(static_cast<unsigned>(std::rand())) + "_" + std::to_string(static_cast<long long>(std::time(nullptr))));
}

// "BACK ->" in the header turns the panel (the timer runs the turn, about 0.8 s); a text field open on a tape is
// confirmed first; "<- BACK" turns it again.
void test_flip_roundtrip_and_text_edit_confirmed_first() {
  clearDecor();
  Editor ed(g_ctl);
  auto root = EditorTest::buildSides(ed);
  ed.setWearLevel(WearLevel::Heavy);
  VSTGUI::CViewContainer* front = EditorTest::front(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(front != nullptr && back != nullptr && root->isChild(front) && root->isChild(back));
  ASSERT_TRUE(front->isVisible() && !back->isVisible());
  ASSERT_TRUE(EditorTest::panel(ed) != nullptr && front->isChild(EditorTest::panel(ed)));   // the front views are in the front side
  ASSERT_TRUE(back->getViewSize() == VSTGUI::CRect(0, 0, Editor::kWidth, Editor::kHeight));
  FairyTapes::Ui::TextLink* link = EditorTest::backLink(ed);
  ASSERT_TRUE(link != nullptr && front->isChild(link) && link->getViewSize() == boxRect(FairyTapes::Ui::frontBackLinkBox()));
  // a tape field left open with a text typed in
  ASSERT_TRUE(!EditorTest::tapes(ed).empty());
  TapeView* tape = EditorTest::tapes(ed)[0];
  const int slot = tape->slot();
  const uint32_t rev0 = g_ctl->decorRevision();
  ASSERT_TRUE(leftClick(tape, tape->tapeRect().getCenter()));
  VSTGUI::CTextEdit* field = EditorTest::tapeEdit(ed);
  ASSERT_TRUE(field != nullptr);
  field->setText("TAKE 2");
  ASSERT_TRUE(leftClick(link, link->getViewSize().getCenter()));
  ASSERT_TRUE(EditorTest::tapeEdit(ed) == nullptr && !front->isChild(field) && !root->isChild(field));
  ASSERT_TRUE(g_ctl->decorRevision() == rev0 + 1 && tapeTextFor(g_ctl->decor(), slot) == "TAKE 2" && tape->text() == "TAKE 2");
  // the turn runs on the timer and ends with the back shown
  ASSERT_TRUE(EditorTest::flip(ed).running() && EditorTest::flip(ed).showingBack());
  ASSERT_TRUE(!front->isVisible() && !back->isVisible());
  EditorTest::tick(ed);
  ASSERT_TRUE(EditorTest::flip(ed).running());   // not over after one timer run
  std::this_thread::sleep_for(std::chrono::milliseconds(900));
  EditorTest::tick(ed);
  ASSERT_TRUE(!EditorTest::flip(ed).running() && back->isVisible() && !front->isVisible());
  // "<- BACK" at the top right of the back
  auto* home = childAt<FairyTapes::Ui::TextLink>(back, boxRect(FairyTapes::Ui::backLinkBox()));
  ASSERT_TRUE(home != nullptr && leftClick(home, home->getViewSize().getCenter()));
  ASSERT_TRUE(EditorTest::flip(ed).running() && !EditorTest::flip(ed).showingBack());
  EditorTest::flip(ed).finish();
  ASSERT_TRUE(front->isVisible() && !back->isVisible());
  ASSERT_TRUE(g_ctl->decorRevision() == rev0 + 1);   // nothing else written
  ed.setWearLevel(WearLevel::Light);
  EditorTest::destroy(ed, root);
  clearDecor();
}

// WEAR on the back: the front changes at once (marks and tapes for the level, no rebuild), the choice is saved in the
// preferences file without touching the other fields, and the back shows it.
void test_settings_apply_wear_at_once() {
  namespace fs = std::filesystem;
  const fs::path dir = scratchPrefsDir("dt_settings_wear");
  const fs::path file = dir / "FairyTapes" / "DeepThroat.prefs";
  Prefs start;
  start.tapeFont = TapeFont::HomemadeApple;
  start.notes = false;
  ASSERT_TRUE(savePrefs(file, start));
  Editor ed(g_ctl);
  EditorTest::loadAppearance(ed, file);   // as open() does, before the views
  auto root = EditorTest::buildSides(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(back != nullptr && back->tab() == kTabSettings);
  ASSERT_TRUE(back->selected(kSetWear) == 1 && back->selected(kSetTapeFont) == 2 && back->selected(kSetValuesFont) == 1);
  auto* wear = EditorTest::wear(ed);
  const uint32_t seed = ed.wearSeed();
  for (int level : {2, 0, 1}) {
    ASSERT_TRUE(chooseOption(back, kSetWear, level));
    const auto w = static_cast<WearLevel>(level);
    ASSERT_TRUE(ed.wearLevel() == w && EditorTest::wear(ed) == wear);
    ASSERT_TRUE(wear->marks().size() == FairyTapes::Ui::generateWear(seed, w, kEditorWidth, kEditorHeight).size());
    ASSERT_TRUE(EditorTest::tapes(ed).size() == layoutFromSeed(seed, w, kPanelRect).tapes.size());
    ASSERT_TRUE(back->selected(kSetWear) == level);
    const Prefs saved = loadPrefs(file);
    ASSERT_TRUE(saved.wear == w && saved.tapeFont == TapeFont::HomemadeApple && saved.handwrittenValues && !saved.notes);
  }
  ASSERT_TRUE(ed.wearSeed() == seed);   // the seed belongs to the instance: not touched by WEAR
  EditorTest::destroy(ed, root);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// TAPE FONT, VALUES FONT and RE-ROLL on the back: applied at once, the fonts saved, RE-ROLL a new seed for the instance.
void test_settings_fonts_and_reroll_apply() {
  namespace fs = std::filesystem;
  const fs::path dir = scratchPrefsDir("dt_settings_fonts");
  const fs::path file = dir / "FairyTapes" / "DeepThroat.prefs";
  Editor ed(g_ctl);
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
  const uint32_t seed0 = ed.wearSeed();
  auto* reroll = settingWidget<FairyTapes::Ui::PushButton>(back, kSetLook);
  ASSERT_TRUE(reroll != nullptr && leftClick(reroll, reroll->getViewSize().getCenter()));
  ASSERT_TRUE(ed.wearSeed() != seed0 && ed.wearSeed() == g_ctl->decor().seed);
  ASSERT_TRUE(loadPrefs(file).tapeFont == TapeFont::HomemadeApple);   // RE-ROLL is not a preference
  EditorTest::destroy(ed, root);
  ed.setTapeFont(TapeFont::Zeyada);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// The preferences folder cannot be written (a file where a folder should be): the editor goes on, the choice applies
// for the session and the back shows it.
void test_settings_failed_write_is_ignored() {
  namespace fs = std::filesystem;
  const fs::path dir = scratchPrefsDir("dt_settings_ro");
  fs::create_directories(dir);
  { std::ofstream(dir / "blocker") << "x"; }
  const fs::path file = dir / "blocker" / "FairyTapes" / "DeepThroat.prefs";
  ASSERT_TRUE(!savePrefs(file, Prefs{}));   // the write really fails
  Editor ed(g_ctl);
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
  ASSERT_TRUE(!fs::exists(file) && fs::is_regular_file(dir / "blocker"));
  EditorTest::tick(ed);
  ASSERT_TRUE(ed.wearLevel() == WearLevel::Heavy && !ed.valuesHandwritten());
  EditorTest::destroy(ed, root);
  ed.setWearLevel(WearLevel::Light);
  ed.setValuesFont(true);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// The tab links, left to right: SETTINGS, MODES, ROUTING, CREDITS; a click shows the tab (its name is the big title)
// and the editor remembers it while it lives; the SETTINGS widgets show only on SETTINGS.
void test_tab_links_order() {
  Editor ed(g_ctl);
  auto root = EditorTest::buildSides(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(back != nullptr);
  const auto& c = back->content();
  ASSERT_TRUE(c.headerLine == "DEEP THROAT  \xC2\xB7  VOCAL CHANNEL STRIP VST3");
  const char* labels[] = {"SETTINGS", "MODES", "ROUTING", "CREDITS"};
  const FairyTapes::Ui::BackTabKind kinds[] = {FairyTapes::Ui::BackTabKind::Settings, FairyTapes::Ui::BackTabKind::Custom,
                                               FairyTapes::Ui::BackTabKind::Routing, FairyTapes::Ui::BackTabKind::Credits};
  ASSERT_TRUE(c.tabs.size() == 4);
  double lastX = -1;
  auto* wearRow = settingWidget<FairyTapes::Ui::SegmentRow>(back, kSetWear);
  ASSERT_TRUE(wearRow != nullptr);
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(c.tabs[static_cast<std::size_t>(i)].label == labels[i] && c.tabs[static_cast<std::size_t>(i)].kind == kinds[i]);
    const auto box = FairyTapes::Ui::tabLinkBox(i, 4);
    ASSERT_TRUE(box.x > lastX);
    lastX = box.x;
    auto* link = childAt<FairyTapes::Ui::TextLink>(back, boxRect(box));
    ASSERT_TRUE(link != nullptr && leftClick(link, link->getViewSize().getCenter()));
    ASSERT_TRUE(back->tab() == i && link->active() && wearRow->isVisible() == (i == kTabSettings));
  }
  EditorTest::destroy(ed, root);
  root = EditorTest::buildSides(ed);   // the same editor opened again: the last tab
  ASSERT_TRUE(EditorTest::back(ed) != nullptr && EditorTest::back(ed)->tab() == kTabCredits);
  EditorTest::destroy(ed, root);
}

// Yellow text pixels (the MODES active column, #f2c21b) in the band of column `j` of the table (heads and rows).
int modesYellowPixels(const VSTGUI::CBitmap* bmp, int j) {
  const Pixels p = pixelsOf(const_cast<VSTGUI::CBitmap*>(bmp));
  const int cx = static_cast<int>(ModesTab::kColumnX[j]);
  int n = 0;
  for (int y = 180; y < 492; ++y)
    for (int x = cx - 70; x <= cx + 70; ++x) {
      const VSTGUI::CColor c = p.at(x, y);
      n += c.red > 190 && c.green > 140 && c.green < 215 && c.blue < 90 && c.red - c.blue > 120 ? 1 : 0;
    }
  return n;
}

// The MODES tab draws the table with the column of the current Mode in yellow, and follows the parameter while the
// back is open; ROUTING and CREDITS come filled (backContent).
void test_modes_tab_follows_mode() {
  Editor ed(g_ctl);
  ed.setWearLevel(WearLevel::Off);
  auto root = EditorTest::buildSides(ed);
  FairyTapes::Ui::BackView* back = EditorTest::back(ed);
  ASSERT_TRUE(back != nullptr);
  ASSERT_TRUE(back->content().credits.entries.size() == 5 && !back->content().routing.flow.empty());
  back->setVisible(true);
  back->setTab(kTabModes);
  for (int i : {1, 2, 0}) {
    g_ctl->setParamNormalized(kMode, i / 2.0);
    EditorTest::tick(ed);
    auto bmp = drawOffscreen(back);
    ASSERT_TRUE(bmp != nullptr);
    int counts[3];
    for (int j = 0; j < 3; ++j) counts[j] = modesYellowPixels(bmp, j);
    if (counts[i] < 200 || counts[(i + 1) % 3] != 0 || counts[(i + 2) % 3] != 0)
      std::fprintf(stderr, "  mode %d: yellow pixels %d %d %d\n", i, counts[0], counts[1], counts[2]);
    ASSERT_TRUE(counts[i] >= 200 && counts[(i + 1) % 3] == 0 && counts[(i + 2) % 3] == 0);
  }
  // the other tabs have no yellow there
  back->setTab(kTabCredits);
  auto credits = drawOffscreen(back);
  ASSERT_TRUE(credits != nullptr && modesYellowPixels(credits, 0) == 0);
  back->setVisible(false);
  EditorTest::destroy(ed, root);
}
} // namespace

int main() {
  VSTGUI::init(nullptr);
  setResourcePath(false);
  Controller ctl;
  ctl.initialize(nullptr);
  {
    Decor d;
    d.seed = 0x51u;   // a fixed instance seed: the same wear, tapes and screws on every run
    ctl.editDecor(d);
  }
  g_ctl = &ctl;
  RUN_TEST(test_panel_layer_built);
  RUN_TEST(test_panel_texts_from_layout);
  RUN_TEST(test_panel_dims_only_flagged_texts);
  RUN_TEST(test_tick_applies_controller_dimming);
  RUN_TEST(test_editor_open_close_repeatedly);
  RUN_TEST(test_render_smoke);
  RUN_TEST(test_title_text_is_drawn);
  RUN_TEST(test_panel_grain_when_asset_found);
  RUN_TEST(test_value_text_matches_controller);
  RUN_TEST(test_every_param_has_exactly_one_control_view);
  RUN_TEST(test_editor_controls_follow_host);
  RUN_TEST(test_user_edit_reaches_host_once);
  RUN_TEST(test_double_click_resets_default);
  RUN_TEST(test_mode_change_updates_dynamic_texts);
  RUN_TEST(test_in_off_dims_module_controls);
  RUN_TEST(test_listen_dims_with_its_module);
  RUN_TEST(test_bypass_dims_everything_but_itself);
  RUN_TEST(test_typed_value_goes_through_the_controller);
  RUN_TEST(test_values_handwritten_by_default);
  RUN_TEST(test_stepped_controls_show_the_dsp_step);
  RUN_TEST(test_meters_follow_display_params);
  RUN_TEST(test_gr_row_amber_with_red_tail);
  RUN_TEST(test_meter_peak_hold_falls_after_hold);
  RUN_TEST(test_clip_led_follows_param_and_click_clears_via_trigger);
  RUN_TEST(test_scope_history_grows_to_292);
  RUN_TEST(test_scope_columns_follow_display_params);
  RUN_TEST(test_breath_lamp_lights_above_half);
  RUN_TEST(test_scope_and_level_dim_only_on_bypass);
  RUN_TEST(test_editor_timer_stops_on_close);
  RUN_TEST(test_breath_and_gr_gated_by_in_and_bypass);
  RUN_TEST(test_wear_off_has_no_marks_and_no_tapes);
  RUN_TEST(test_wear_level_change_redraws_without_rebuild);
  RUN_TEST(test_tape_text_edit_goes_to_decor);
  RUN_TEST(test_screw_drag_stores_angle_once);
  RUN_TEST(test_reroll_changes_seed_and_clears_decor);
  RUN_TEST(test_decor_change_applied_on_tick);
  RUN_TEST(test_prefs_missing_file_opens_with_defaults);
  RUN_TEST(test_missing_grain_asset_draws_plain_panel);
  RUN_TEST(test_flip_roundtrip_and_text_edit_confirmed_first);
  RUN_TEST(test_settings_apply_wear_at_once);
  RUN_TEST(test_settings_fonts_and_reroll_apply);
  RUN_TEST(test_settings_failed_write_is_ignored);
  RUN_TEST(test_tab_links_order);
  RUN_TEST(test_modes_tab_follows_mode);
  ctl.terminate();
  g_ctl = nullptr;
  VSTGUI::exit();
  std::printf("deep_throat_editor_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
