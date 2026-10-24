/**
 * Fairy Tapes · Deep Throat — tests for the pure editor layout (ui/editor_size.h, ui/layout.h): no VSTGUI.
 * Build target: deep_throat_ui_tests.
 */
#include "ui/editor_size.h"
#include "ui/layout.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace FairyTapes::DeepThroat;
using namespace FairyTapes::DeepThroat::Ui;

namespace {
int g_failed = 0, g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)
#define RUN_TEST(fn) do { const int before = g_failed; fn(); if (g_failed == before) ++g_passed; } while(0)

bool inside(const Rect& in, const Rect& out) {
  return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}
bool overlap(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
const Rect kPanel{0, 0, static_cast<float>(kEditorWidth), static_cast<float>(kEditorHeight)};
constexpr int kBoxCount = static_cast<int>(sizeof(kBoxes) / sizeof(kBoxes[0]));
constexpr int kControlCount = static_cast<int>(sizeof(kControls) / sizeof(kControls[0]));

constexpr ParamID kInIds[6] = {kRiderIn, kBreathIn, kEqIn, kDeEssIn, kCompIn, kToneIn};

float cx(const ControlSpec& c) { return c.rect.x + c.rect.w / 2; }
float cy(const ControlSpec& c) { return c.rect.y + c.rect.h / 2; }

// Everything the editor places in a box: control views, the scope, meter bars and clip LEDs.
struct Placed {
  Box box;
  Rect rect;
  std::string what;
};
std::vector<Placed> placedItems() {
  std::vector<Placed> out;
  for (const ControlSpec& c : kControls) out.push_back({c.box, viewRect(c), c.name ? c.name : "choice"});
  out.push_back({Box::Scope, kScopeRect, "scope"});
  for (const MeterRow& m : kMeterRows) out.push_back({Box::Level, meterBarRect(m), "meter"});
  for (int i = 0; i < kClipCount; ++i) out.push_back({Box::Level, clipRect(kClips[i]), kClips[i].label});
  return out;
}

void test_editor_is_1000_by_614() { ASSERT_TRUE(kEditorWidth == 1000 && kEditorHeight == 614); }

void test_layout_binds_every_param_once() {
  ASSERT_TRUE(kControlCount == kNumParams && kNumParams == 25);
  for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) {
    int n = 0;
    for (const ControlSpec& c : kControls) n += c.id == id ? 1 : 0;
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(controlOf(static_cast<ParamID>(id)) != nullptr);
  }
  for (const ControlSpec& c : kControls) ASSERT_TRUE(c.id < kNumParams);   // no display parameter
  ASSERT_TRUE(controlOf(static_cast<ParamID>(kNumParams)) == nullptr);
  ASSERT_TRUE(controlOf(static_cast<ParamID>(kDispInL)) == nullptr);
}

void test_control_kinds_leds_and_scales() {
  int knobs = 0, toggles = 0, cycles = 0, choices = 0;
  for (const ControlSpec& c : kControls) {
    switch (c.kind) {
    case Kind::Knob:
      ++knobs;
      ASSERT_TRUE(c.name && *c.name && c.lo && *c.lo && c.hi && *c.hi && c.capRadius > 0 && c.led == Led::None);
      break;
    case Kind::Toggle:
      ++toggles;
      ASSERT_TRUE(c.name && *c.name && c.led == Led::Red);   // on/off: red LED (DESIGN_SYSTEM.md)
      break;
    case Kind::Cycle:
      ++cycles;
      ASSERT_TRUE(c.name && *c.name && c.led == Led::Yellow);
      break;
    case Kind::Choice:
      ++choices;
      ASSERT_TRUE(c.led == Led::None);   // exclusive choice: name on the face, no LED
      break;
    }
  }
  ASSERT_TRUE(knobs == 12 && toggles == 9 && cycles == 3 && choices == 1);
  // choice counts follow params.h
  ASSERT_TRUE(controlOf(kMode)->kind == Kind::Choice && controlOf(kMode)->choiceCount == kModeCount);
  ASSERT_TRUE(controlOf(kCompRatio)->kind == Kind::Cycle && controlOf(kCompRatio)->choiceCount == kCompRatioCount);
  ASSERT_TRUE(controlOf(kCompDist)->kind == Kind::Cycle && controlOf(kCompDist)->choiceCount == kCompDistCount);
  ASSERT_TRUE(controlOf(kCompHpf)->kind == Kind::Cycle && controlOf(kCompHpf)->choiceCount == kCompHpfCount);
  for (ParamID id : {kMode, kCompRatio, kCompDist, kCompHpf}) {
    const ControlSpec& c = *controlOf(id);
    ASSERT_TRUE(c.choiceCount > 0 && c.choiceCount <= 8);
    for (int i = 0; i < c.choiceCount; ++i) ASSERT_TRUE(c.choices[i] && *c.choices[i]);
  }
  ASSERT_TRUE(!std::strcmp(controlOf(kMode)->choices[0], "SPEAK") && !std::strcmp(controlOf(kMode)->choices[2], "PUKE"));
  ASSERT_TRUE(!std::strcmp(controlOf(kCompRatio)->choices[0], "AUTO") && !std::strcmp(controlOf(kCompRatio)->choices[7], "20:1"));
  ASSERT_TRUE(!std::strcmp(controlOf(kCompDist)->choices[3], "DIST 3"));
  ASSERT_TRUE(!std::strcmp(controlOf(kCompHpf)->choices[2], "150 Hz"));
  // Toggles by parameter: the nine on/off controls
  for (ParamID id : {kBypass, kRiderIn, kBreathIn, kEqIn, kDeEssIn, kCompIn, kToneIn, kBreathListen, kDeEssListen})
    ASSERT_TRUE(controlOf(id)->kind == Kind::Toggle);
}

void test_layout_rects_inside() {
  ASSERT_TRUE(kBoxCount == 11);
  for (int i = 0; i < kBoxCount; ++i) {
    ASSERT_TRUE(inside(kBoxes[i].rect, kPanel));
    for (int j = i + 1; j < kBoxCount; ++j) ASSERT_TRUE(!overlap(kBoxes[i].rect, kBoxes[j].rect));
  }
  for (const Placed& p : placedItems()) {
    if (!inside(p.rect, kPanel) || !inside(p.rect, boxOf(p.box).rect)) std::fprintf(stderr, "  outside: %s\n", p.what.c_str());
    ASSERT_TRUE(inside(p.rect, kPanel));
    ASSERT_TRUE(inside(p.rect, boxOf(p.box).rect));
  }
  ASSERT_TRUE(inside(kScopeRect, boxOf(Box::Scope).rect));
}

void test_layout_no_control_overlap() {
  const auto items = placedItems();
  for (size_t i = 0; i < items.size(); ++i)
    for (size_t j = i + 1; j < items.size(); ++j) {
      if (overlap(items[i].rect, items[j].rect)) std::fprintf(stderr, "  overlap: %s / %s\n", items[i].what.c_str(), items[j].what.c_str());
      ASSERT_TRUE(!overlap(items[i].rect, items[j].rect));
    }
}

void test_layout_in_buttons_top_right() {
  for (int m = 0; m < 6; ++m) {
    const ControlSpec& in = *controlOf(kInIds[m]);
    ASSERT_TRUE(in.kind == Kind::Toggle && !std::strcmp(in.name, "IN"));
    const Rect& b = boxOf(in.box).rect;
    ASSERT_TRUE(cy(in) == b.y + 38);                // 38 px under the top border
    ASSERT_TRUE(cx(in) > b.x + b.w / 2);            // right half
    const float inset = (b.x + b.w) - cx(in);
    const float expect = kInIds[m] == kBreathIn ? 34.f : kInIds[m] == kDeEssIn ? 36.f : 24.f;   // preview modBox arguments
    ASSERT_TRUE(inset == expect);
  }
  ASSERT_TRUE(controlOf(kBypass)->box == Box::Output);
}

void test_layout_titles_bottom() {
  for (const BoxSpec& b : kBoxes) {
    ASSERT_TRUE(b.title && *b.title);
    for (const char* p = b.title; *p; ++p) ASSERT_TRUE(!std::islower(static_cast<unsigned char>(*p)));
  }
  ASSERT_TRUE(!std::strcmp(boxOf(Box::DeEss).title, "DE-ESS") && !std::strcmp(boxOf(Box::Eq).title, "EQ"));
}

void test_dimming_rules() {
  DimState all{};
  for (bool& b : all.in) b = true;
  for (const ControlSpec& c : kControls) ASSERT_TRUE(!controlDimmed(c.id, all));   // nothing dimmed at rest
  // Global bypass: dims everything but itself.
  DimState bp = all;
  bp.bypass = true;
  for (const ControlSpec& c : kControls) ASSERT_TRUE(controlDimmed(c.id, bp) == (c.id != kBypass));
  // Module IN off: the module's controls (not its IN, not its Listen), nothing else.
  struct Case { int in; Module mod; };
  for (const Case& k : {Case{0, Module::Rider}, Case{1, Module::Breath}, Case{2, Module::Eq}, Case{3, Module::DeEss},
                        Case{4, Module::Comp}, Case{5, Module::Tone}}) {
    DimState s = all;
    s.in[k.in] = false;
    for (const ControlSpec& c : kControls) {
      const bool expected = moduleOf(c.id) == k.mod;
      if (controlDimmed(c.id, s) != expected) std::fprintf(stderr, "  dimming: param %u\n", (unsigned)c.id);
      ASSERT_TRUE(controlDimmed(c.id, s) == expected);
    }
    ASSERT_TRUE(!controlDimmed(kInIds[k.in], s));
  }
  ASSERT_TRUE(moduleOf(kRiderTarget) == Module::Rider && moduleOf(kRiderRange) == Module::Rider);
  ASSERT_TRUE(moduleOf(kBreathAmount) == Module::Breath && moduleOf(kBreathSens) == Module::Breath);
  ASSERT_TRUE(moduleOf(kEqAmount) == Module::Eq);
  ASSERT_TRUE(moduleOf(kDeEssAmount) == Module::DeEss && moduleOf(kDeEssFreq) == Module::DeEss);
  for (ParamID id : {kCompAmount, kCompMix, kCompRatio, kCompDist, kCompHpf}) ASSERT_TRUE(moduleOf(id) == Module::Comp);
  ASSERT_TRUE(moduleOf(kTone) == Module::Tone);
  for (ParamID id : {kBypass, kMode, kInput, kOutput, kBreathListen, kDeEssListen, kRiderIn, kToneIn})
    ASSERT_TRUE(moduleOf(id) == Module::None);
  // Listen: out of the module's attenuation, but dimmed by the bypass.
  DimState off = all;
  off.in[1] = off.in[3] = false;
  ASSERT_TRUE(!controlDimmed(kBreathListen, off) && !controlDimmed(kDeEssListen, off));
  ASSERT_TRUE(controlDimmed(kBreathListen, bp));
}

bool hasBrand(const char* s) {
  std::string t = s ? s : "";
  for (char& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return t.find("distressor") != std::string::npos;
}

void test_silkscreen_items_cover_controls() {
  using FairyTapes::Ui::PrintKind;
  const auto items = silkscreenItems();
  int expected = 0;
  for (const ControlSpec& c : kControls) expected += c.kind == Kind::Choice ? 0 : 1;
  ASSERT_TRUE(static_cast<int>(items.size()) == expected && expected == 24);
  for (const ControlSpec& c : kControls) {
    int n = 0;
    for (const auto& it : items) n += it.id == static_cast<int>(c.id) ? 1 : 0;
    ASSERT_TRUE(n == (c.kind == Kind::Choice ? 0 : 1));
  }
  for (const auto& it : items) {
    const ControlSpec& c = *controlOf(static_cast<ParamID>(it.id));
    switch (c.kind) {
    case Kind::Knob:
      ASSERT_TRUE(it.kind == PrintKind::Knob && it.cx == cx(c) && it.cy == c.rect.y + kKnobCentreOffsetY);
      ASSERT_TRUE(it.capRadius == c.capRadius && it.name == c.name && it.lo == c.lo && it.hi == c.hi);
      break;
    case Kind::Toggle:
      ASSERT_TRUE(it.kind == PrintKind::Button && it.cx == cx(c) && it.cy == cy(c) && it.name == c.name);
      break;
    case Kind::Cycle:
      ASSERT_TRUE(it.kind == PrintKind::Cycle && it.cx == cx(c) && it.cy == cy(c) && it.name == c.name);
      break;
    case Kind::Choice: ASSERT_TRUE(false);
    }
    ASSERT_TRUE(!hasBrand(it.name.c_str()) && !hasBrand(it.lo.c_str()) && !hasBrand(it.hi.c_str()));
  }
  for (const ControlSpec& c : kControls) {
    ASSERT_TRUE(!hasBrand(c.name) && !hasBrand(c.lo) && !hasBrand(c.hi));
    for (int i = 0; i < c.choiceCount; ++i) ASSERT_TRUE(!hasBrand(c.choices[i]));
  }
  for (const PanelText& t : kPanelTexts) ASSERT_TRUE(t.text && *t.text && !hasBrand(t.text));
  for (const BoxSpec& b : kBoxes) ASSERT_TRUE(!hasBrand(b.title));
}

// Anchor coordinates read from docs/mockups/deep_throat_rack.html (modBox content: preview y - 12).
void test_preview_anchor_positions() {
  const ControlSpec& in = *controlOf(kInput);
  ASSERT_TRUE(in.box == Box::Input && cx(in) == 79 && in.rect.y + kKnobCentreOffsetY == 150 && in.capRadius == 22);
  ASSERT_TRUE(!std::strcmp(in.name, "GAIN") && !std::strcmp(in.lo, "-12") && !std::strcmp(in.hi, "+12"));
  const ControlSpec& out = *controlOf(kOutput);
  ASSERT_TRUE(out.box == Box::Output && cx(out) == 868 && out.rect.y + kKnobCentreOffsetY == 150 && out.capRadius == 22);
  const ControlSpec& by = *controlOf(kBypass);
  ASSERT_TRUE(cx(by) == 946 && cy(by) == 130 && by.rect.w == 24 && by.rect.h == 22 && !std::strcmp(by.name, "BYPASS"));
  // MODE: three faces 56 x 20, centres (89, 302 + 26 i)
  const ControlSpec& mode = *controlOf(kMode);
  ASSERT_TRUE(mode.box == Box::Mode && mode.rect.x == 61 && mode.rect.w == 56 && mode.rect.y == 292 && mode.rect.h == 72);
  ASSERT_TRUE(modeFaceRect(0).y == 292 && modeFaceRect(1).y == 318 && modeFaceRect(2).y == 344 && modeFaceRect(2).h == 20);
  // RIDER (box 160,238): knobs at (222|310, 322 - 12), IN at (370, 276)
  const ControlSpec& tg = *controlOf(kRiderTarget);
  ASSERT_TRUE(cx(tg) == 222 && tg.rect.y + kKnobCentreOffsetY == 310 && !std::strcmp(tg.lo, "-30") && !std::strcmp(tg.hi, "-10"));
  ASSERT_TRUE(cx(*controlOf(kRiderRange)) == 310);
  const ControlSpec& rin = *controlOf(kRiderIn);
  ASSERT_TRUE(cx(rin) == 370 && cy(rin) == 276);
  // BREATH: IN with 34 px, LISTEN at (718, 352)
  ASSERT_TRUE(cx(*controlOf(kBreathIn)) == 721 && cy(*controlOf(kBreathIn)) == 276);
  ASSERT_TRUE(cx(*controlOf(kBreathListen)) == 718 && cy(*controlOf(kBreathListen)) == 352);
  ASSERT_TRUE(cx(*controlOf(kBreathSens)) == 550 && !std::strcmp(controlOf(kBreathSens)->name, "SENSITIVITY"));
  // EQ, DE-ESS, COMP, TONE
  ASSERT_TRUE(cx(*controlOf(kEqIn)) == 952 && cy(*controlOf(kEqIn)) == 276 && cx(*controlOf(kEqAmount)) == 812);
  ASSERT_TRUE(cx(*controlOf(kDeEssIn)) == 225 && cy(*controlOf(kDeEssIn)) == 462);
  ASSERT_TRUE(cx(*controlOf(kDeEssListen)) == 225 && cy(*controlOf(kDeEssListen)) == 536);
  ASSERT_TRUE(cx(*controlOf(kDeEssFreq)) == 156 && controlOf(kDeEssFreq)->rect.y + kKnobCentreOffsetY == 510);
  ASSERT_TRUE(cx(*controlOf(kCompIn)) == 646 && cy(*controlOf(kCompIn)) == 462);
  ASSERT_TRUE(cy(*controlOf(kCompRatio)) == 532 && cx(*controlOf(kCompRatio)) == 488);
  ASSERT_TRUE(cx(*controlOf(kCompDist)) == 552 && cy(*controlOf(kCompDist)) == 532);
  ASSERT_TRUE(cx(*controlOf(kCompHpf)) == 616 && cy(*controlOf(kCompHpf)) == 532);
  ASSERT_TRUE(!std::strcmp(controlOf(kCompAmount)->lo, "-6") && !std::strcmp(controlOf(kCompAmount)->hi, "-30"));
  const ControlSpec& tone = *controlOf(kTone);
  ASSERT_TRUE(cx(tone) == 750 && tone.rect.y + kKnobCentreOffsetY == 510 && tone.capRadius == 30);
  ASSERT_TRUE(cx(*controlOf(kToneIn)) == 952 && cy(*controlOf(kToneIn)) == 462);
  // boxes
  ASSERT_TRUE(boxOf(Box::Input).rect.x == 24 && boxOf(Box::Input).rect.y == 96 && boxOf(Box::Input).rect.w == 110);
  ASSERT_TRUE(boxOf(Box::Comp).rect.x == 267 && boxOf(Box::Comp).rect.w == 403 && boxOf(Box::Comp).rect.h == 166);
  ASSERT_TRUE(boxOf(Box::Tone).rect.x == 676 && boxOf(Box::Tone).rect.y == 424);
  // scope and LEVEL
  ASSERT_TRUE(kScopeRect.x == 156 && kScopeRect.y == 106 && kScopeRect.w == 292 && kScopeRect.h == 84);
  ASSERT_TRUE(kMeterFirstX == 528 && kMeterPitch == 18 && kMeterLeds == 15);
  ASSERT_TRUE(kMeterRowCount == 5 && kMeterRows[0].id == kDispInL && kMeterRows[0].cy == 110);
  ASSERT_TRUE(kMeterRows[1].id == kDispInR && kMeterRows[1].cy == 122 && kMeterRows[2].id == kDispOutL && kMeterRows[2].cy == 148);
  ASSERT_TRUE(kMeterRows[3].id == kDispOutR && kMeterRows[3].cy == 160 && kMeterRows[4].id == kDispGr && kMeterRows[4].cy == 184);
  ASSERT_TRUE(kClips[0].id == kDispClipIn && kClips[0].cx == 532 && kClips[1].id == kDispClipOut && kClips[1].cx == 576);
  ASSERT_TRUE(kClipRowY == 210);
}

void test_meters_and_clips() {
  for (int i = 0; i < kMeterRowCount; ++i) {
    ASSERT_TRUE(kMeterRows[i].id >= kDispInL && kMeterRows[i].id <= kDispGr);
    ASSERT_TRUE(kMeterRows[i].gainReduction == (kMeterRows[i].id == kDispGr));
    const Rect r = meterBarRect(kMeterRows[i]);
    ASSERT_TRUE(r.w == 15 * 18 && r.h == 8 && r.x == 528 - 9);
  }
  ASSERT_TRUE(kClipCount == 2);
}

void test_panel_texts_and_decor_tables() {
  bool title = false, brand = false, tagline = false;
  for (const PanelText& t : kPanelTexts) {
    ASSERT_TRUE(t.x > 0 && t.x < kEditorWidth && t.y > 0 && t.y < kEditorHeight);
    title = title || (!std::strcmp(t.text, "DEEP THROAT") && t.style == TextStyle::Title && t.x == 34 && t.y == 58);
    brand = brand || (!std::strcmp(t.text, "FAIRY TAPES") && t.style == TextStyle::Brand);
    tagline = tagline || (t.style == TextStyle::Tagline && t.align == FairyTapes::Ui::TextAlign::Right && t.x == 966);
  }
  ASSERT_TRUE(title && brand && tagline);
  ASSERT_TRUE(kNumTapeSlots == 6);
  for (const TapeSlot& s : kTapeSlots) ASSERT_TRUE(s.cx > 0 && s.cx < kEditorWidth && s.cy > 0 && s.cy < kEditorHeight);
  for (const auto& s : kScrewPos) ASSERT_TRUE(s[0] > 0 && s[0] < kEditorWidth && s[1] > 0 && s[1] < kEditorHeight);
  ASSERT_TRUE(kScrewPos[2][1] == 596 && kScrewPos[3][0] == 982);   // corners of the 614-high panel
}

// A tape (up to 96 x 26, turned up to 9 degrees, torn ends) around any position the seed may pick must stay clear of
// every control, the scope, the meter rows and the clip LEDs, and of the other tapes (ui/decor tests will go further).
void test_tape_slots_clear_of_controls() {
  constexpr float kHalfW = 50.f, kHalfH = 22.f;   // bounding half extents of the turned tape
  std::vector<Rect> reach;
  for (const TapeSlot& s : kTapeSlots)
    reach.push_back({s.cx - s.jx - kHalfW, s.cy - s.jy - kHalfH, 2 * (s.jx + kHalfW), 2 * (s.jy + kHalfH)});
  for (const Rect& r : reach) ASSERT_TRUE(inside(r, kPanel));
  for (size_t i = 0; i < reach.size(); ++i) {
    for (const Placed& p : placedItems()) {
      if (overlap(reach[i], p.rect)) std::fprintf(stderr, "  tape %zu covers %s\n", i, p.what.c_str());
      ASSERT_TRUE(!overlap(reach[i], p.rect));
    }
    for (size_t j = i + 1; j < reach.size(); ++j) ASSERT_TRUE(!overlap(reach[i], reach[j]));
  }
  // screws (radius 7) clear of controls
  for (const auto& s : kScrewPos)
    for (const Placed& p : placedItems()) ASSERT_TRUE(!overlap(Rect{s[0] - 7, s[1] - 7, 14, 14}, p.rect));
}

} // namespace

int main() {
  RUN_TEST(test_editor_is_1000_by_614);
  RUN_TEST(test_layout_binds_every_param_once);
  RUN_TEST(test_control_kinds_leds_and_scales);
  RUN_TEST(test_layout_rects_inside);
  RUN_TEST(test_layout_no_control_overlap);
  RUN_TEST(test_layout_in_buttons_top_right);
  RUN_TEST(test_layout_titles_bottom);
  RUN_TEST(test_dimming_rules);
  RUN_TEST(test_silkscreen_items_cover_controls);
  RUN_TEST(test_preview_anchor_positions);
  RUN_TEST(test_meters_and_clips);
  RUN_TEST(test_panel_texts_and_decor_tables);
  RUN_TEST(test_tape_slots_clear_of_controls);
  std::printf("deep_throat_ui_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
