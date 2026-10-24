/**
 * Fairy Tapes · Deep Throat — editor behaviour without a window (build target: deep_throat_editor_tests).
 * A real Controller is the host; EditorTest (a friend of Editor) builds the views in a container instead of a frame,
 * runs the UI timer's work by hand and reaches the views. Drawing goes to an offscreen context: the pixels checked here
 * are the panel colour, the white box borders and the gap of the box titles; the exact colours, the fonts as the host
 * renders them and the 3D flip stay a manual check.
 */
#include "controller.h"
#include "params.h"
#include "ui/editor.h"
#include "ui/layout.h"
#include "ui/dynamic_text.h"
#include "ui/panel_view.h"
#include "ui/readout_view.h"
#include "ui/value_text.h"

#include "fairy_ui/controls.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/silkscreen.h"
#include "fairy_ui/silkscreen_view.h"

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
#include <iterator>
#include <string>

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
    root->removeAll(true);
    ed.resetViewPointers();
  }
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
  static VSTGUI::CTextEdit* valueEdit(const Editor& ed) { return ed.valueEdit_; }
  static void closeValueEdit(Editor& ed, bool confirm) { ed.closeValueEdit(confirm); }
  static bool handwritten(const Editor& ed) { return ed.valuesHandwritten_; }
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
  int others = 0;   // calls for ids outside the 25 parameters

  void clear() { *this = CountingHandler(); }
  int total() const {
    int n = others;
    for (int i = 0; i < kNumParams; ++i) n += begins[i] + performs[i] + ends[i];
    return n;
  }
  Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override {
    (id < kNumParams ? begins[id] : others) += 1;
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue v) override {
    if (id < kNumParams) {
      ++performs[id];
      last[id] = v;
    } else {
      ++others;
    }
    return Steinberg::kResultOk;
  }
  Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override {
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
  if (r->lampDimmed() != (s.bypass || moduleOut(Module::Breath, s))) return false;
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

// The values are drawn in the tape font by default (the preferences are wired in Task 10).
void test_values_handwritten_by_default() {
  Editor ed(g_ctl);
  ASSERT_TRUE(EditorTest::handwritten(ed));
}

} // namespace

int main() {
  VSTGUI::init(nullptr);
  setResourcePath(false);
  Controller ctl;
  ctl.initialize(nullptr);
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
  ctl.terminate();
  g_ctl = nullptr;
  VSTGUI::exit();
  std::printf("deep_throat_editor_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
