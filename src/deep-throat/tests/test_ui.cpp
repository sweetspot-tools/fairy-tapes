/**
 * Fairy Tapes · Deep Throat — tests for the pure editor layout (ui/editor_size.h, ui/layout.h): no VSTGUI.
 * Build target: deep_throat_ui_tests.
 */
#include "ui/decor.h"
#include "ui/dynamic_text.h"
#include "ui/editor_size.h"
#include "ui/history.h"
#include "ui/layout.h"
#include "ui/prefs.h"
#include "ui/screw_angles.h"
#include "ui/tapes.h"

#include "fairy_ui/knob_math.h"
#include "fairy_ui/rack_geometry.h"
#include "fairy_ui/screws.h"
#include "fairy_ui/tape_paper.h"

#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <system_error>
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

// Printed texts dim like the preview: the global bypass dims only the flagged ones (SCOPE legend, GR row label and scale,
// module texts); the header, the LEVEL IN / OUT / CLIP and L / R tags and the level scale never dim. A module that is out
// dims its own texts and nothing else.
void test_panel_text_dimming_flags() {
  const std::set<std::string> neverDim = {"DEEP THROAT", "FAIRY TAPES", "IN", "OUT", "CLIP", "L", "R"};
  DimState bypass;
  bypass.bypass = true;
  int flagged = 0;
  for (const PanelText& t : kPanelTexts) {
    const std::string s = t.text;
    const bool header = t.style == TextStyle::Title || t.style == TextStyle::Brand || t.style == TextStyle::Tagline;
    const bool levelScale = t.style == TextStyle::Scale && t.y == 138;
    const bool grRow = (s == "GR" && t.style == TextStyle::Section) || (t.style == TextStyle::Scale && t.y == 174);
    const bool legend = t.style == TextStyle::LegendVoice || t.style == TextStyle::LegendRider || t.style == TextStyle::LegendBreath;
    if (header || levelScale || (neverDim.count(s) && t.module == Module::None && !grRow)) ASSERT_TRUE(!t.dimsWithBypass);
    if (grRow || legend || t.module != Module::None) ASSERT_TRUE(t.dimsWithBypass);
    ASSERT_TRUE(panelTextDimmed(t, bypass) == t.dimsWithBypass);
    ASSERT_TRUE(!panelTextDimmed(t, DimState{}));
    flagged += t.dimsWithBypass ? 1 : 0;
  }
  ASSERT_TRUE(flagged == 3 + 1 + 4 + 10);   // legend, GR, GR scale, module texts
  for (int m = 1; m <= 6; ++m) {   // one module out at a time: only its texts dim
    DimState s;
    s.in[m - 1] = false;
    for (const PanelText& t : kPanelTexts) ASSERT_TRUE(panelTextDimmed(t, s) == (t.module == static_cast<Module>(m)));
  }
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
  constexpr float kHalfW = 54.f, kHalfH = 25.f;   // half extents of the turned tape with its torn ends and shadow
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

void test_eq_texts() {
  ASSERT_TRUE(eqHpfText(Mode::Speak, 0.5) == "80 Hz" && eqMudText(Mode::Speak, 0.5) == "-2 dB");
  ASSERT_TRUE(eqHpfText(Mode::Speak, 1.0) == "120 Hz" && eqMudText(Mode::Speak, 1.0) == "-4 dB");
  ASSERT_TRUE(eqHpfText(Mode::Puke, 0.0) == "30 Hz" && eqMudText(Mode::Puke, 0.0) == "0 dB");
  ASSERT_TRUE(eqHpfText(Mode::Sing, 0.5) == "70 Hz" && eqMudText(Mode::Sing, 0.5) == "-3 dB");
  ASSERT_TRUE(eqMudText(Mode::Sing, 0.25) == "-1.5 dB");
  const double nan = std::nan("");
  ASSERT_TRUE(eqHpfText(Mode::Speak, nan) == eqHpfText(Mode::Speak, 0.5));
  ASSERT_TRUE(eqMudText(Mode::Sing, nan) == eqMudText(Mode::Sing, 0.5));
}

void test_tone_gain_texts() {
  ASSERT_TRUE(toneGainText(Mode::Speak, 0, 1.0) == "-2.0 dB");
  ASSERT_TRUE(toneGainText(Mode::Speak, 1, 1.0) == "+3.0 dB");
  ASSERT_TRUE(toneGainText(Mode::Speak, 2, 1.0) == "+2.0 dB");
  for (int b = 0; b < 3; ++b) ASSERT_TRUE(toneGainText(Mode::Speak, b, 0.5) == "0.0 dB");
  ASSERT_TRUE(toneGainText(Mode::Sing, 0, 0.0) == "+1.5 dB");
  ASSERT_TRUE(toneGainText(Mode::Sing, 1, 0.0) == "-2.0 dB");
  ASSERT_TRUE(toneGainText(Mode::Sing, 2, 0.0) == "-4.0 dB");
  for (double t : {0.0, 0.3, 0.5, 0.8, 1.0}) ASSERT_TRUE(toneGainText(Mode::Puke, 2, t) == "0.0 dB");
  ASSERT_TRUE(toneGainText(Mode::Speak, 2, std::nan("")) == "0.0 dB");
  ASSERT_TRUE(toneGainText(Mode::Speak, 1, 0.51) == "+0.1 dB");   // |g| = 0.06 >= 0.05
  ASSERT_TRUE(toneGainText(Mode::Speak, 1, 0.508) == "0.0 dB");   // |g| = 0.048 < 0.05
}

void test_ratio_dist_texts() {
  ASSERT_TRUE(ratioText(0, Mode::Sing) == "AUTO 3:1");
  ASSERT_TRUE(ratioText(0, Mode::Speak) == "AUTO 2:1" && ratioText(0, Mode::Puke) == "AUTO 8:1");
  ASSERT_TRUE(ratioText(7, Mode::Speak) == "20:1" && ratioText(1, Mode::Puke) == "2:1");
  ASSERT_TRUE(ratioText(4, Mode::Sing) == "6:1");
  ASSERT_TRUE(distText(0, Mode::Puke) == "AUTO DIST 3" && distText(0, Mode::Speak) == "AUTO OFF");
  ASSERT_TRUE(distText(0, Mode::Sing) == "AUTO DIST 2");
  ASSERT_TRUE(distText(1, Mode::Sing) == "OFF" && distText(2, Mode::Speak) == "DIST 2" && distText(3, Mode::Speak) == "DIST 3");
  ASSERT_TRUE(hpfChoiceText(0) == "OFF" && hpfChoiceText(1) == "80 Hz" && hpfChoiceText(2) == "150 Hz");
}

void test_mode_tags() {
  ASSERT_TRUE(modeTagLine1(Mode::Speak) == "SPOKEN WORD" && modeTagLine2(Mode::Speak) == "PODCAST, VOICE-OVER");
  ASSERT_TRUE(modeTagLine1(Mode::Sing) == "SUNG VOICE" && modeTagLine2(Mode::Sing).empty());
  ASSERT_TRUE(modeTagLine1(Mode::Puke) == "EXTREME VOICES" && modeTagLine2(Mode::Puke) == "SCREAM, GROWL");
  ASSERT_TRUE(std::strcmp(detectorTag(Mode::Speak), "SPECTRAL") == 0);
  ASSERT_TRUE(std::strcmp(detectorTag(Mode::Sing), "SPECTRAL") == 0);
  ASSERT_TRUE(std::strcmp(detectorTag(Mode::Puke), "LEVEL") == 0);
}

void test_rider_max_text() {
  ASSERT_TRUE(riderMaxText(Mode::Speak) == "MAX \xC2\xB1" "6 dB \xC2\xB7 8 dB/s");
  ASSERT_TRUE(riderMaxText(Mode::Sing) == "MAX \xC2\xB1" "4 dB \xC2\xB7 4 dB/s");
  ASSERT_TRUE(riderMaxText(Mode::Puke) == "MAX \xC2\xB1" "3 dB \xC2\xB7 2 dB/s");
}

void test_de_ess_note_only_in_puke() {
  ASSERT_TRUE(deEssNote(Mode::Speak).empty() && deEssNote(Mode::Sing).empty());
  ASSERT_TRUE(deEssNote(Mode::Puke) == "MOSTLY OFF IN PUKE (X0.15)");
}

void test_texts_follow_the_table() {
  for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke}) {
    const ModeRow& r = modeRow(m);
    ASSERT_TRUE(eqHpfText(m, 0.7) == eqHpfText(r, 0.7) && eqMudText(m, 0.7) == eqMudText(r, 0.7));
    for (int b = 0; b < 3; ++b) ASSERT_TRUE(toneGainText(m, b, 0.9) == toneGainText(r, b, 0.9));
    ASSERT_TRUE(ratioText(0, m) == ratioText(0, r) && distText(0, m) == distText(0, r));
    ASSERT_TRUE(riderMaxText(m) == riderMaxText(r) && modeTagLine1(m) == modeTagLine1(r));
  }
  ModeRow r = modeRow(Mode::Speak);
  r.hpfHz = 100; r.mudCutDb = -6; r.toneMudDb = 4; r.tonePresenceDb = -1; r.toneAirDb = 6;
  r.compRatio = 4; r.compDist = 3; r.riderMaxRangeDb = 9; r.riderSlewDbPerSec = 5; r.levelDetector = true;
  r.description = "Test voice: line two"; r.deEssScale = 0.5; r.name = "TEST";
  ASSERT_TRUE(eqHpfText(r, 0.5) == "100 Hz" && eqMudText(r, 0.5) == "-6 dB");
  ASSERT_TRUE(toneGainText(r, 0, 1.0) == "+4.0 dB" && toneGainText(r, 1, 1.0) == "-1.0 dB" && toneGainText(r, 2, 1.0) == "+6.0 dB");
  ASSERT_TRUE(ratioText(0, r) == "AUTO 4:1" && distText(0, r) == "AUTO DIST 3");
  ASSERT_TRUE(riderMaxText(r) == "MAX \xC2\xB1" "9 dB \xC2\xB7 5 dB/s");
  ASSERT_TRUE(modeTagLine1(r) == "TEST VOICE" && modeTagLine2(r) == "LINE TWO");
  ASSERT_TRUE(std::strcmp(detectorTag(r), "LEVEL") == 0);
  ASSERT_TRUE(deEssNote(r) == "MOSTLY OFF IN TEST (X0.50)");
}

void test_rounding_matches_js_to_fixed() {
  // exact binary ties round away from zero (JS toFixed), not to even
  ASSERT_TRUE(toneGainText(Mode::Speak, 0, 0.8125) == "-1.3 dB");  // g = -1.25
  ASSERT_TRUE(toneGainText(Mode::Speak, 2, 0.8125) == "+1.3 dB");  // g = +1.25
  ASSERT_TRUE(toneGainText(Mode::Speak, 0, 0.1875) == "+1.3 dB");  // g = +1.25
  ASSERT_TRUE(toneGainText(Mode::Speak, 2, 0.1875) == "-1.3 dB");  // g = -1.25
  // near ties: the exact double decides (1.15 is stored below, 1.35 above)
  ModeRow r = modeRow(Mode::Speak);
  r.mudCutDb = -1.15;
  ASSERT_TRUE(eqMudText(r, 0.5) == "-1.1 dB");
  r.mudCutDb = -1.35;
  ASSERT_TRUE(eqMudText(r, 0.5) == "-1.4 dB");
  r.mudCutDb = -1.25;
  ASSERT_TRUE(eqMudText(r, 0.5) == "-1.3 dB");
  r.mudCutDb = 1.25;
  ASSERT_TRUE(eqMudText(r, 0.5) == "+1.3 dB");
}

void test_hpf_rounding_and_infinite_inputs() {
  ASSERT_TRUE(eqHpfText(Mode::Sing, 0.25) == "53 Hz");  // 52.5
  const double inf = std::numeric_limits<double>::infinity();
  for (double v : {inf, -inf}) {
    ASSERT_TRUE(eqHpfText(Mode::Speak, v) == eqHpfText(Mode::Speak, 0.5));
    ASSERT_TRUE(eqMudText(Mode::Sing, v) == eqMudText(Mode::Sing, 0.5));
    for (int b = 0; b < 3; ++b) ASSERT_TRUE(toneGainText(Mode::Sing, b, v) == toneGainText(Mode::Sing, b, 0.5));
  }
}


// ---- Task 6: preferences, decor, screws, tapes, scope history ---------------------------------------------------------
namespace fs = std::filesystem;
using FairyTapes::Ui::WearLevel;

fs::path scratchDir() {
  static fs::path dir;
  if (dir.empty()) {
    std::error_code ec;
    dir = fs::temp_directory_path(ec) / ("deep_throat_ui_test_" + std::to_string(std::rand()) + "_" + std::to_string(std::time(nullptr)));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
  }
  return dir;
}
void writeFile(const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; }
bool isDefaultPrefs(const Prefs& p) {
  const Prefs d;
  return p.wear == d.wear && p.notes == d.notes && p.tapeFont == d.tapeFont && p.handwrittenValues == d.handwrittenValues;
}

void test_controls_are_in_enum_order() {
  for (int i = 0; i < kControlCount; ++i) ASSERT_TRUE(static_cast<int>(kControls[i].id) == i);
}

void test_prefs_parse_defaults_and_bad_values() {
  const Prefs d;
  ASSERT_TRUE(d.wear == WearLevel::Light && d.notes && d.tapeFont == TapeFont::Zeyada && d.handwrittenValues);
  ASSERT_TRUE(isDefaultPrefs(parsePrefs("")));
  ASSERT_TRUE(isDefaultPrefs(parsePrefs("wear\n=Heavy\nnoequals here\n# wear=Off\nunknownKey=1\n")));
  const Prefs p = parsePrefs("wear=Rusty\nnotes=Hide\ntapeFont=Comic\nvaluesFont=Standard\n");
  ASSERT_TRUE(p.wear == WearLevel::Light && !p.notes && p.tapeFont == TapeFont::Zeyada && !p.handwrittenValues);
  const Prefs q = parsePrefs("# only wear\n  wear = Heavy  \r\nnotes=maybe\nvaluesFont=Mono\n");
  ASSERT_TRUE(q.wear == WearLevel::Heavy && q.notes && q.handwrittenValues);
}

void test_prefs_roundtrip() {
  const WearLevel wears[] = {WearLevel::Off, WearLevel::Light, WearLevel::Heavy};
  const TapeFont fonts[] = {TapeFont::Zeyada, TapeFont::RockSalt, TapeFont::HomemadeApple};
  for (WearLevel w : wears)
    for (TapeFont f : fonts)
      for (int b = 0; b < 4; ++b) {
        Prefs p;
        p.wear = w;
        p.tapeFont = f;
        p.notes = (b & 1) != 0;
        p.handwrittenValues = (b & 2) != 0;
        const Prefs q = parsePrefs(serializePrefs(p));
        ASSERT_TRUE(q.wear == w && q.tapeFont == f && q.notes == p.notes && q.handwrittenValues == p.handwrittenValues);
      }
}

void test_prefs_file_missing_or_oversized_or_binary() {
  const fs::path file = scratchDir() / "cfg" / "FairyTapes" / "DeepThroat.prefs";   // no environment changes: explicit paths
  ASSERT_TRUE(isDefaultPrefs(loadPrefs(file)));   // absent
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  writeFile(file, "wear=Heavy\n" + std::string(kMaxPrefsBytes, '#'));   // oversized, with a valid line in it
  ASSERT_TRUE(isDefaultPrefs(loadPrefs(file)));
  writeFile(file, std::string("wear=Off\n\0\0\0binary", 18));   // binary
  ASSERT_TRUE(isDefaultPrefs(loadPrefs(file)));
  writeFile(file, std::string("wear=Heavy\n") + std::string(kMaxPrefsBytes - 20, '#'));   // large but allowed
  ASSERT_TRUE(loadPrefs(file).wear == WearLevel::Heavy);
  fs::remove(file, ec);
  Prefs p;
  p.wear = WearLevel::Off;
  p.notes = false;
  p.tapeFont = TapeFont::RockSalt;
  p.handwrittenValues = false;
  ASSERT_TRUE(savePrefs(file, p));
  const Prefs q = loadPrefs(file);
  ASSERT_TRUE(q.wear == WearLevel::Off && !q.notes && q.tapeFont == TapeFont::RockSalt && !q.handwrittenValues);
  ASSERT_TRUE(!fs::exists(fs::path(file) += ".tmp", ec));
  int files = 0;
  for (const auto& e : fs::directory_iterator(file.parent_path(), ec)) { (void)e; ++files; }
  ASSERT_TRUE(files == 1);   // only DeepThroat.prefs
  writeFile(scratchDir() / "blocker", "x");   // a file where the folder should be, and a folder as target: both fail cleanly
  ASSERT_TRUE(!savePrefs(scratchDir() / "blocker" / "DeepThroat.prefs", p));
  ASSERT_TRUE(!savePrefs(fs::path(), p));
  ASSERT_TRUE(!savePrefs(file.parent_path(), p));
  ASSERT_TRUE(isDefaultPrefs(loadPrefs(fs::path())) && isDefaultPrefs(loadPrefs(file.parent_path())));
}

void test_prefs_path_per_os() {
#if defined(_WIN32)
  ASSERT_TRUE(prefsPathFor("C:/Users/u", "", "C:/Users/u/AppData/Roaming") ==
              fs::path("C:/Users/u/AppData/Roaming") / "FairyTapes" / "DeepThroat.prefs");
#elif defined(__APPLE__)
  ASSERT_TRUE(prefsPathFor("/Users/u", "", "") == fs::path("/Users/u/Library/Application Support/FairyTapes/DeepThroat.prefs"));
#else
  ASSERT_TRUE(prefsPathFor("/home/u", "", "") == fs::path("/home/u/.config/FairyTapes/DeepThroat.prefs"));
  ASSERT_TRUE(prefsPathFor("/home/u", "/xdg", "") == fs::path("/xdg/FairyTapes/DeepThroat.prefs"));
  ASSERT_TRUE(prefsPathFor("/home/u", "relative/xdg", "") == fs::path("/home/u/.config/FairyTapes/DeepThroat.prefs"));
#endif
  ASSERT_TRUE(prefsPathFor("", "", "").empty());
  ASSERT_TRUE(prefsPath().empty() || prefsPath().filename() == "DeepThroat.prefs");
}

// ---- decor ------------------------------------------------------------------------------------------------------------
bool tapeInsidePanel(const TapePlacement& t, const Rect& p) {
  const float r = t.rotDeg * 3.14159265f / 180.f, c = std::cos(r), s = std::sin(r);
  for (int i = 0; i < 4; ++i) {
    const float lx = (i & 1 ? 0.5f : -0.5f) * t.w, ly = (i & 2 ? 0.5f : -0.5f) * t.h;
    const float x = t.cx + lx * c - ly * s, y = t.cy + lx * s + ly * c;
    if (x < p.x || x > p.x + p.w || y < p.y || y > p.y + p.h) return false;
  }
  return true;
}

void test_decor_layout_deterministic() {
  const Rect panel = kPanel;
  int sawMissing = 0;
  for (uint32_t seed = 0; seed < 3000; ++seed) {
    for (WearLevel lv : {WearLevel::Off, WearLevel::Light, WearLevel::Heavy}) {
      const WearLayout a = layoutFromSeed(seed, lv, panel), b = layoutFromSeed(seed, lv, panel);
      ASSERT_TRUE(a.tapes.size() == b.tapes.size() && a.screws.size() == 4);
      for (size_t i = 0; i < a.tapes.size(); ++i)
        ASSERT_TRUE(a.tapes[i].cx == b.tapes[i].cx && a.tapes[i].cy == b.tapes[i].cy && a.tapes[i].rotDeg == b.tapes[i].rotDeg &&
                    a.tapes[i].slot == b.tapes[i].slot && tapeInsidePanel(a.tapes[i], panel));
      const size_t n = a.tapes.size();
      if (lv == WearLevel::Off) ASSERT_TRUE(n == 0);
      if (lv == WearLevel::Light) ASSERT_TRUE(n >= 1 && n <= 2);
      if (lv == WearLevel::Heavy) ASSERT_TRUE(n >= 2 && n <= 3);
      int missing = 0;
      for (const auto& s : a.screws) missing += s.missing ? 1 : 0;
      ASSERT_TRUE(missing <= 1);
      sawMissing += missing;
      if (lv != WearLevel::Off) {   // distinct slots, valid index
        std::set<int> slots;
        for (const auto& t : a.tapes) ASSERT_TRUE(t.slot >= 0 && t.slot < kNumTapeSlots && slots.insert(t.slot).second);
      }
    }
  }
  ASSERT_TRUE(sawMissing > 0);
  // Light is a prefix of Heavy; screws do not depend on the level; the seed matters
  for (uint32_t seed = 0; seed < 500; ++seed) {
    const WearLayout l = layoutFromSeed(seed, WearLevel::Light, panel), h = layoutFromSeed(seed, WearLevel::Heavy, panel);
    ASSERT_TRUE(l.tapes.size() <= h.tapes.size());
    for (size_t i = 0; i < l.tapes.size(); ++i) ASSERT_TRUE(l.tapes[i].slot == h.tapes[i].slot && l.tapes[i].cx == h.tapes[i].cx);
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(l.screws[i].angleDeg == h.screws[i].angleDeg && l.screws[i].missing == h.screws[i].missing);
  }
  bool differs = false;
  const WearLayout a = layoutFromSeed(5u, WearLevel::Heavy, panel);
  for (uint32_t s = 6; s < 40 && !differs; ++s) {
    const WearLayout c = layoutFromSeed(s, WearLevel::Heavy, panel);
    differs = c.tapes.size() != a.tapes.size() || c.tapes[0].cx != a.tapes[0].cx || c.screws[0].angleDeg != a.screws[0].angleDeg;
  }
  ASSERT_TRUE(differs);
  // a different panel size scales the placement and keeps the tapes inside
  const Rect small{10, 20, 500, 307};
  for (uint32_t seed = 0; seed < 300; ++seed)
    for (const auto& t : layoutFromSeed(seed, WearLevel::Heavy, small).tapes) ASSERT_TRUE(tapeInsidePanel(t, small));
  // the drawn tape (ragged ends and shadow) stays inside the panel too, at the edge where the clamp acts
  const Rect tight{0, 0, 120, 80};
  for (uint32_t seed = 0; seed < 300; ++seed)
    for (const auto& t : layoutFromSeed(seed, WearLevel::Heavy, tight).tapes) {
      const auto b = FairyTapes::Ui::tapeBounds(t.cx, t.cy, t.w, t.h, t.rotDeg, FairyTapes::Ui::kTapeRaggedX + 1.0);
      ASSERT_TRUE(b.left >= -1e-3 && b.top >= -1e-3 && b.right <= tight.w + 1e-3 && b.bottom <= tight.h + 1e-3);
    }
}

// ---- what a tape must never touch ------------------------------------------------------------------------------------
int codepoints(const char* s) {
  int n = 0;
  for (; *s; ++s) n += (static_cast<unsigned char>(*s) & 0xC0) != 0x80 ? 1 : 0;
  return n;
}
float styleSize(TextStyle s) {
  switch (s) {
  case TextStyle::Title: return 38;
  case TextStyle::Tagline: return 13;
  case TextStyle::Label12: return 12;
  case TextStyle::Brand: case TextStyle::Section: case TextStyle::RowTag: case TextStyle::Label11: return 11;
  case TextStyle::Label10: return 10;
  default: return 9;
  }
}
// Approximate extent of a printed text: width from the character count (title about 270 px, tagline starts near 390).
Rect textRect(const char* text, float x, float baseline, float size, float perCharEm, TextAlign a) {
  const float w = static_cast<float>(codepoints(text)) * size * perCharEm;
  const float left = a == TextAlign::Left ? x : a == TextAlign::Right ? x - w : x - w / 2;
  return {left, baseline - size, w, size + 3};
}
Rect panelTextRect(const PanelText& t) {
  const float em = t.style == TextStyle::Title ? 0.65f : t.style == TextStyle::Tagline ? 0.58f : 0.68f;
  return textRect(t.text, t.x, t.y, styleSize(t.style), em, t.align);
}

std::vector<Placed> tapeObstacles() {
  std::vector<Placed> out = placedItems();
  for (const PanelText& t : kPanelTexts) out.push_back({Box::Input, panelTextRect(t), t.text});
  for (const BoxSpec& b : kBoxes) {   // the title printed in a gap of the bottom border (14 px bold)
    const float w = static_cast<float>(codepoints(b.title)) * 14 * 0.8f + 12;
    out.push_back({b.box, {b.rect.x + b.rect.w / 2 - w / 2, b.rect.y + b.rect.h - 10, w, 20}, b.title});
  }
  for (const Readout& r : kReadouts) {   // up to 22 characters ("MAX +-12 dB . 18 dB/s")
    out.push_back({Box::Input, textRect("0123456789012345678901", r.x, r.y, r.size, 0.68f, r.align), "readout"});
  }
  {   // faint 7 px block tags: the longest text each can show (the mode lines over all modes; the fixed ones as printed)
    size_t modeName = 0, modeInfo = 0;
    for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke}) {
      modeName = std::max<size_t>(modeName, modeTagLine1(m).size());
      modeInfo = std::max<size_t>(modeInfo, modeTagLine2(m).size());
    }
    const size_t len[6] = {modeName, modeInfo, 8, 22, 27, 52};   // SPECTRAL, FOLLOWS MODE . AMOUNT, MOSTLY OFF IN PUKE (X0.15), THRESHOLD SET BY ...
    for (int i = 0; i < 6; ++i)
      out.push_back({Box::Input, textRect(std::string(len[i], 'W').c_str(), static_cast<float>(kTags[i].tag.x), static_cast<float>(kTags[i].tag.y), 7, 0.62f, TextAlign::Left), "tag"});
  }
  using namespace FairyTapes::Ui::Rack;
  for (const ControlSpec& c : kControls) {
    if (c.kind == Kind::Knob) {   // cell is wider and taller than the view: scale ring, end values, name, value text
      const float cxk = knobCentreX(c), cyk = knobCentreY(c), r = c.capRadius;
      const float e = (r + static_cast<float>(kScaleGap)) * 0.7071f;
      const float lenLo = static_cast<float>(std::strlen(c.lo)), lenHi = static_cast<float>(std::strlen(c.hi));
      const float halfW = std::max({r + 18.f, e + 6.f + 6.f * lenLo, e + 6.f + 6.f * lenHi});
      const float bottom = cyk + static_cast<float>(valueBaseline(0, r)) + 3.f;
      out.push_back({c.box, {cxk - halfW, cyk - (r + 18.f), 2 * halfW, bottom - (cyk - (r + 18.f))}, c.name});
    } else if (c.kind == Kind::Toggle || c.kind == Kind::Cycle) {   // label printed above the face
      const float w = 7.f * static_cast<float>(std::strlen(c.name)) + 4.f, fy = c.rect.y + c.rect.h / 2;
      out.push_back({c.box, {c.rect.x + c.rect.w / 2 - w / 2, fy - 28.f, w, 13.f}, c.name});
    }
  }
  return out;
}
bool overlapsBox(const FairyTapes::Ui::TapeBox& b, const Rect& r) {
  return b.left < r.x + r.w && b.right > r.x && b.top < r.y + r.h && b.bottom > r.y;
}
FairyTapes::Ui::TapeBox drawnBox(const TapePlacement& t) {   // rotated rectangle plus ragged ends and shadow
  return FairyTapes::Ui::tapeBounds(t.cx, t.cy, t.w, t.h, t.rotDeg, FairyTapes::Ui::kTapeRaggedX + 1.0);
}
Rect asRect(const FairyTapes::Ui::TapeBox& b) {
  return {static_cast<float>(b.left), static_cast<float>(b.top), static_cast<float>(b.right - b.left), static_cast<float>(b.bottom - b.top)};
}

void test_decor_tapes_never_cover_controls() {
  const std::vector<Placed> obstacles = tapeObstacles();
  std::set<std::string> clashes;   // "slot N covers X", all of them listed before failing
  for (uint32_t seed = 0; seed < 200; ++seed) {
    for (WearLevel lv : {WearLevel::Light, WearLevel::Heavy, WearLevel::Off}) {
      const WearLayout l = layoutFromSeed(seed, lv, kPanel);
      for (size_t i = 0; i < l.tapes.size(); ++i) {
        const auto b = drawnBox(l.tapes[i]);
        const std::string slot = "slot " + std::to_string(l.tapes[i].slot) + " covers ";
        for (const Placed& o : obstacles)
          if (overlapsBox(b, o.rect)) clashes.insert(slot + o.what);
        for (const ScrewPlacement& s : l.screws)
          if (overlapsBox(b, Rect{s.x - 7, s.y - 7, 14, 14})) clashes.insert(slot + "a screw");
        for (size_t j = i + 1; j < l.tapes.size(); ++j)
          if (overlapsBox(b, asRect(drawnBox(l.tapes[j])))) clashes.insert(slot + "tape " + std::to_string(l.tapes[j].slot));
      }
    }
  }
  for (const std::string& c : clashes) std::fprintf(stderr, "  %s\n", c.c_str());
  ASSERT_TRUE(clashes.empty());
}

void test_text_model_matches_preview_extents() {
  Rect title{}, tag{};
  for (const PanelText& t : kPanelTexts) {
    if (t.style == TextStyle::Title) title = panelTextRect(t);
    if (t.style == TextStyle::Tagline) tag = panelTextRect(t);
  }
  ASSERT_TRUE(title.x + title.w < 310.f && tag.x > 380.f && tag.x < 400.f);   // the preview: title ends ~300, tagline starts ~390
}

void test_screw_look_matches_shared_generator() {
  const Rect panel = kPanel;
  for (uint32_t k = 0; k < 5000; ++k) {
    const uint32_t seed = k * 2654435761u;
    const auto shared = FairyTapes::Ui::generateScrews(seed);
    const WearLayout l = layoutFromSeed(seed, WearLevel::Light, panel);
    ASSERT_TRUE(l.screws.size() == shared.size());
    for (size_t i = 0; i < shared.size(); ++i) {
      ASSERT_TRUE(l.screws[i].cross == shared[i].cross && l.screws[i].missing == shared[i].missing &&
                  l.screws[i].angleDeg == static_cast<float>(shared[i].initialAngle));
      ASSERT_TRUE(l.screws[i].x == kScrewPos[i][0] && l.screws[i].y == kScrewPos[i][1]);
    }
    const auto specs = screwSpecs(seed);   // screw_angles.h: the same look at the kScrewPos
    for (size_t i = 0; i < shared.size(); ++i)
      ASSERT_TRUE(specs[i].x == kScrewPos[i][0] && specs[i].y == kScrewPos[i][1] && specs[i].cross == shared[i].cross &&
                  specs[i].missing == shared[i].missing && specs[i].initialAngle == shared[i].initialAngle);
  }
}

bool nearD(double a, double b) { return std::fabs(a - b) < 1e-4; }

void test_screw_angles() {
  ASSERT_TRUE(nearD(shownScrewAngle(30.0, 0.f), 30) && nearD(shownScrewAngle(30.0, 45.5f), 75.5) && nearD(shownScrewAngle(170.0, 300.f), 110));
  ASSERT_TRUE(nearD(shownScrewAngle(30.0, std::nanf("")), 30) && nearD(shownScrewAngle(30.0, INFINITY), 30));
  ASSERT_TRUE(nearD(userScrewAngle(75.5, 30.0), 45.5) && nearD(userScrewAngle(10, 30.0), 340) && userScrewAngle(30, 30.0) == 0.f);
  for (double initial : {0.0, 8.0, 91.0, 180.0})
    for (double shown : {0.0, 0.25, 33.3, 179.9, 359.75}) {
      const float user = userScrewAngle(shown, initial);
      ASSERT_TRUE(user >= 0.f && user < 360.f);
      const double back = shownScrewAngle(initial, user);
      ASSERT_TRUE(nearD(back, shown) || nearD(std::fabs(back - shown), 360));
    }
  FairyTapes::DeepThroat::Decor d;
  d.seed = 77;
  d.screwAngles[1] = 12.f;
  d.tapeTexts = {"VOX"};
  const auto e = withScrewAngle(d, 2, 400.f);
  ASSERT_TRUE(e.seed == 77 && e.tapeTexts == d.tapeTexts && e.screwAngles[0] == 0.f && e.screwAngles[1] == 12.f &&
              e.screwAngles[2] == 40.f && e.screwAngles[3] == 0.f);
  ASSERT_TRUE(withScrewAngle(d, -1, 5.f).screwAngles[0] == 0.f && withScrewAngle(d, 4, 5.f).screwAngles[3] == 0.f);
  ASSERT_TRUE(withScrewAngle(d, 0, std::nanf("")).screwAngles[0] == 0.f);
}

void test_tape_texts_and_ink() {
  using FairyTapes::Ui::TapeFontId;
  ASSERT_TRUE(tapeFontIdOf(TapeFont::Zeyada) == TapeFontId::Zeyada && tapeFontIdOf(TapeFont::RockSalt) == TapeFontId::RockSalt &&
              tapeFontIdOf(TapeFont::HomemadeApple) == TapeFontId::HomemadeApple);
  FairyTapes::DeepThroat::Decor d;
  ASSERT_TRUE(tapeTextFor(d, 0).empty());
  d.tapeTexts = {"VOX", "", "keep!"};
  ASSERT_TRUE(tapeTextFor(d, 0) == "VOX" && tapeTextFor(d, 2) == "keep!" && tapeTextFor(d, 3).empty() && tapeTextFor(d, -1).empty());
  FairyTapes::DeepThroat::Decor e = withTapeText(FairyTapes::DeepThroat::Decor{}, 4, "  TAKE 3  ");
  ASSERT_TRUE(e.tapeTexts.size() == 5 && e.tapeTexts[4] == "TAKE 3");
  e = withTapeText(e, 4, "a very long text indeed");
  ASSERT_TRUE(e.tapeTexts[4] == "a very long te");
  ASSERT_TRUE(withTapeText(e, kNumTapeSlots, "x").tapeTexts == e.tapeTexts && withTapeText(e, -1, "x").tapeTexts == e.tapeTexts);
  const TapeLook a = tapeLook(5, 2), b = tapeLook(5, 2), c = tapeLook(5, 3);
  ASSERT_TRUE(a.paperSeed == b.paperSeed && a.inkSeed == b.inkSeed && a.paperSeed != c.paperSeed);
  for (uint32_t s = 0; s < 200; ++s)
    for (int slot = 0; slot < kNumTapeSlots; ++slot) {
      const TapeLook l = tapeLook(s, slot);
      ASSERT_TRUE(l.textSize >= 11 && l.textSize <= 14 && std::fabs(l.textRotDeg) <= 3);
    }
  for (TapeFontId f : {TapeFontId::Zeyada, TapeFontId::RockSalt, TapeFontId::HomemadeApple})
    for (const char* t : {"VOX", "WWWWWWWWWWWWWW", "@@@@@@@@@@@@@@", "7/12 keep!"})
      for (double w : {58.0, 70.0, 96.0})
        for (uint32_t seed = 0; seed < 20; ++seed) {
          const TapeLook look = tapeLook(seed, static_cast<int>(seed % kNumTapeSlots));
          const TapeInk ink = tapeInk(f, t, w, look);
          ASSERT_TRUE(ink.emPx > 0);
          double lo = 0, hi = 0;
          for (const auto& p : FairyTapes::Ui::layoutText(f, ink.text, ink.emPx, look.inkSeed, 1.0)) {
            lo = std::min(lo, p.x);
            hi = std::max(hi, p.x + p.glyph->advance / 1000.0 * ink.emPx * p.scale);
          }
          ASSERT_TRUE(hi - lo <= w - 2 * kTapeTextEdge);
        }
  ASSERT_TRUE(tapeInk(TapeFontId::Zeyada, "", 96, tapeLook(1, 0)).text.empty());
}

// ---- scope history ----------------------------------------------------------------------------------------------------
float normOfDb(double db) { return static_cast<float>(FairyTapes::Ui::meterNormFromDb(db, -60.0, 6.0)); }
float riderNorm(double db) { return static_cast<float>(0.5 + db / 12.0); }

void test_history_ring_and_columns() {
  History<4> h;
  ASSERT_TRUE(h.size() == 0 && h.capacity() == 4);
  for (int i = 1; i <= 10; ++i) h.push({static_cast<float>(i) / 10.f, 0.5f, 0.f});
  ASSERT_TRUE(h.size() == 4);
  ASSERT_TRUE(nearD(h.at(0).voice, 0.7) && nearD(h.at(3).voice, 1.0));   // 0 = the oldest
  ASSERT_TRUE(h.at(-1).voice == 0.f && h.at(4).voice == 0.f);              // out of range: neutral point
  // exactly N and N+1 pushes
  History<4> w;
  for (int i = 1; i <= 4; ++i) w.push({static_cast<float>(i) / 10.f, 0.5f, 0.f});
  ASSERT_TRUE(w.size() == 4 && nearD(w.at(0).voice, 0.1) && nearD(w.at(3).voice, 0.4));
  w.push({0.5f, 0.5f, 0.f});
  ASSERT_TRUE(w.size() == 4 && nearD(w.at(0).voice, 0.2) && nearD(w.at(3).voice, 0.5));
  h.clear();
  ASSERT_TRUE(h.size() == 0);
  h.push({0.25f, 0.5f, 0.f});
  ASSERT_TRUE(h.size() == 1 && nearD(h.at(0).voice, 0.25));

  const Rect r{156, 106, 292, 84};
  History<8> s;
  s.push({normOfDb(-60), riderNorm(6), 0.6f});    // silence, +6 dB, breath on
  s.push({normOfDb(0), riderNorm(-6), 0.5f});     // 0 dB, -6 dB, exactly 0.5: not above
  s.push({normOfDb(-30), riderNorm(0), 0.4f});
  s.push({normOfDb(6), riderNorm(0), 1.0f});      // above 0 dB: stays at the top
  const std::vector<ScopeColumn> cols = scopeColumns(s, r);
  ASSERT_TRUE(cols.size() == 4);
  ASSERT_TRUE(nearD(cols[0].voiceTop, r.y + r.h) && nearD(cols[1].voiceTop, r.y) && nearD(cols[2].voiceTop, r.y + r.h / 2) &&
              nearD(cols[3].voiceTop, r.y));
  ASSERT_TRUE(nearD(cols[0].riderY, r.y) && nearD(cols[1].riderY, r.y + r.h) && nearD(cols[2].riderY, r.y + r.h / 2));
  ASSERT_TRUE(cols[0].breath && !cols[1].breath && !cols[2].breath && cols[3].breath);
  // bad values are limited into the rectangle (NaN: silence, centre, no breath)
  History<8> bad;
  bad.push({std::nanf(""), std::nanf(""), std::nanf("")});
  bad.push({-3.f, -3.f, -3.f});
  bad.push({9.f, 9.f, 9.f});
  bad.push({INFINITY, -INFINITY, INFINITY});
  for (const ScopeColumn& c : scopeColumns(bad, r)) ASSERT_TRUE(c.voiceTop >= r.y && c.voiceTop <= r.y + r.h && c.riderY >= r.y && c.riderY <= r.y + r.h);
  const auto bc = scopeColumns(bad, r);
  ASSERT_TRUE(nearD(bc[0].voiceTop, r.y + r.h) && nearD(bc[0].riderY, r.y + r.h / 2) && !bc[0].breath);
  ASSERT_TRUE(nearD(bc[1].voiceTop, r.y + r.h) && nearD(bc[1].riderY, r.y + r.h) && !bc[1].breath);
  ASSERT_TRUE(nearD(bc[2].voiceTop, r.y) && nearD(bc[2].riderY, r.y) && bc[2].breath);
  ASSERT_TRUE(scopeColumns(History<8>{}, r).empty());
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
  RUN_TEST(test_panel_text_dimming_flags);
  RUN_TEST(test_tape_slots_clear_of_controls);
  RUN_TEST(test_eq_texts);
  RUN_TEST(test_tone_gain_texts);
  RUN_TEST(test_ratio_dist_texts);
  RUN_TEST(test_mode_tags);
  RUN_TEST(test_rider_max_text);
  RUN_TEST(test_de_ess_note_only_in_puke);
  RUN_TEST(test_texts_follow_the_table);
  RUN_TEST(test_rounding_matches_js_to_fixed);
  RUN_TEST(test_hpf_rounding_and_infinite_inputs);
  RUN_TEST(test_controls_are_in_enum_order);
  RUN_TEST(test_prefs_parse_defaults_and_bad_values);
  RUN_TEST(test_prefs_roundtrip);
  RUN_TEST(test_prefs_file_missing_or_oversized_or_binary);
  RUN_TEST(test_prefs_path_per_os);
  RUN_TEST(test_decor_layout_deterministic);
  RUN_TEST(test_decor_tapes_never_cover_controls);
  RUN_TEST(test_text_model_matches_preview_extents);
  RUN_TEST(test_screw_look_matches_shared_generator);
  RUN_TEST(test_screw_angles);
  RUN_TEST(test_tape_texts_and_ink);
  RUN_TEST(test_history_ring_and_columns);
  { std::error_code ec; fs::remove_all(scratchDir(), ec); }
  std::printf("deep_throat_ui_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
