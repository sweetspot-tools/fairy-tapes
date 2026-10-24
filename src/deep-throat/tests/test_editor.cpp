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
#include "ui/panel_view.h"

#include "fairy_ui/silkscreen.h"

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
#include <iterator>

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
  // Removes the views, as close() does.
  static void destroy(Editor& ed, VSTGUI::CViewContainer* root) {
    root->removeAll(true);
    ed.resetViewPointers();
  }
  // One run of the UI timer's work.
  static void tick(Editor& ed) { ed.onTimer(); }
  static PanelView* panel(const Editor& ed) { return ed.panel_; }
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

// The UI timer reads Bypass and the six IN parameters from the controller and dims the panel texts accordingly.
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
  ctl.terminate();
  g_ctl = nullptr;
  VSTGUI::exit();
  std::printf("deep_throat_editor_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
