/**
 * Fairy Tapes · Deep Throat — tests for the pure editor layout (ui/editor_size.h, ui/layout.h): no VSTGUI.
 * Build target: deep_throat_ui_tests.
 */
#include "ui/back_modes.h"
#include "ui/back_settings.h"
#include "ui/credits.h"
#include "ui/decor.h"
#include "ui/dynamic_text.h"
#include "ui/editor_size.h"
#include "ui/history.h"
#include "ui/layout.h"
#include "ui/prefs.h"
#include "ui/routing_diagram.h"
#include "ui/screw_angles.h"
#include "ui/tapes.h"
#include "ui/value_text.h"

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
  // Module IN off: the module's controls (its Listen too, not its IN), nothing else.
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
  for (ParamID id : {kBypass, kMode, kInput, kOutput, kRiderIn, kBreathIn, kEqIn, kDeEssIn, kCompIn, kToneIn})
    ASSERT_TRUE(moduleOf(id) == Module::None);
  // Listen: dimmed with its module (as in the preview) and by the bypass.
  ASSERT_TRUE(moduleOf(kBreathListen) == Module::Breath && moduleOf(kDeEssListen) == Module::DeEss);
  DimState off = all;
  off.in[1] = off.in[3] = false;
  ASSERT_TRUE(controlDimmed(kBreathListen, off) && controlDimmed(kDeEssListen, off));
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

// ---- Values under the knobs (ui/value_text.h, src/value_format.h) ---------------------------------------------------
constexpr ParamID kKnobIds[12] = {kInput, kOutput, kRiderTarget, kRiderRange, kBreathAmount, kBreathSens,
                                  kEqAmount, kDeEssAmount, kDeEssFreq, kCompAmount, kCompMix, kTone};

bool formats(ParamID id, double n, const char* expected) {
  const std::string s = formatKnobValue(id, n);
  if (s != expected) std::fprintf(stderr, "  param %u at %.3f: \"%s\" (expected \"%s\")\n", (unsigned)id, n, s.c_str(), expected);
  return s == expected;
}

// The preview's formats (deep_throat_rack.html: db(12), tgf, pct, brf, dsf, cmf, tonef).
void test_value_text_formats() {
  ASSERT_TRUE(formats(kInput, 0.5, "0.0 dB") && formats(kInput, 1.0, "+12.0 dB") && formats(kInput, 0.0, "-12.0 dB"));
  ASSERT_TRUE(formats(kOutput, 0.75, "+6.0 dB"));
  ASSERT_TRUE(formats(kRiderTarget, 0.5, "-20.0 dBFS") && formats(kRiderTarget, 1.0, "-10.0 dBFS"));
  ASSERT_TRUE(formats(kRiderRange, 0.5, "50 %") && formats(kBreathSens, 0.0, "0 %") && formats(kCompMix, 1.0, "100 %"));
  ASSERT_TRUE(formats(kEqAmount, 0.125, "13 %"));   // Math.round: halves go up
  ASSERT_TRUE(formats(kBreathAmount, 0.5, "6.0 dB") && formats(kBreathAmount, 1.0, "12.0 dB"));
  ASSERT_TRUE(formats(kDeEssFreq, 0.53, "6.5 kHz") && formats(kDeEssFreq, 0.0, "4.0 kHz") && formats(kDeEssFreq, 1.0, "10.0 kHz"));
  ASSERT_TRUE(formats(kCompAmount, 0.5, "-18.0 dB") && formats(kCompAmount, 0.0, "-6.0 dB") && formats(kCompAmount, 1.0, "-30.0 dB"));
  ASSERT_TRUE(formats(kTone, 1.0, "+100 %") && formats(kTone, 0.5, "0 %") && formats(kTone, 0.0, "-100 %"));
  ASSERT_TRUE(formats(kInput, 0.4999, "0.0 dB"));   // never "-0.0"
  ASSERT_TRUE(formats(kInput, std::nan(""), "-12.0 dB") && formats(kTone, 7.0, "+100 %"));   // bad input: limited
  ASSERT_TRUE(formatKnobValue(kBypass, 1.0).empty() && formatKnobValue(kMode, 0.5).empty() && formatKnobValue(kCompRatio, 0).empty());
  ASSERT_TRUE(editFieldText(kInput, 0.75) == "+6.0" && editFieldText(kDeEssFreq, 0.53) == "6.5" && editFieldText(kTone, 0.25) == "-50");
}

// Printed resolution of each knob, in normalized units at the worst point: half a printed step over the slope.
double printedResolution(ParamID id) {
  switch (id) {
  case kInput: case kOutput: return 0.05 / 24.0;
  case kRiderTarget: return 0.05 / 20.0;
  case kBreathAmount: return 0.05 / 12.0;
  case kCompAmount: return 0.05 / 24.0;
  case kDeEssFreq: return 0.05 / (4.0 * std::log(2.5));   // kHz per norm is smallest at the low end
  case kTone: return 0.5 / 200.0;
  default: return 0.5 / 100.0;
  }
}

void test_value_text_parse_roundtrip() {
  for (ParamID id : kKnobIds) {
    for (int k = 0; k <= 200; ++k) {
      const double n = k / 200.0;
      const std::string s = formatKnobValue(id, n);
      double back = -1;
      ASSERT_TRUE(parseKnobValue(id, s, back));
      if (std::fabs(back - n) > printedResolution(id) + 1e-9)
        std::fprintf(stderr, "  param %u: %.4f -> \"%s\" -> %.4f\n", (unsigned)id, n, s.c_str(), back);
      ASSERT_TRUE(std::fabs(back - n) <= printedResolution(id) + 1e-9);
      ASSERT_TRUE(formatKnobValue(id, back) == s);   // the same printed value
      double typed = -1;   // the field text (number without the unit) reads back the same
      ASSERT_TRUE(parseKnobValue(id, editFieldText(id, n), typed) && formatKnobValue(id, typed) == s);
    }
    double v = 0.42;
    for (const char* bad : {"", "  ", "loud", "dB", "+", "1.2.3", "5 apples", "5 dB extra", "nan", "inf", "1e999", "--3"}) {
      if (parseKnobValue(id, bad, v)) std::fprintf(stderr, "  param %u accepted \"%s\"\n", (unsigned)id, bad);
      ASSERT_TRUE(!parseKnobValue(id, bad, v));
    }
    ASSERT_TRUE(v == 0.42);   // left alone
  }
  double n = -1;
  ASSERT_TRUE(parseKnobValue(kInput, "+6", n) && n == 0.75);
  ASSERT_TRUE(parseKnobValue(kInput, " -12 DB ", n) && n == 0.0);
  ASSERT_TRUE(parseKnobValue(kInput, "99", n) && n == 1.0);   // limited
  ASSERT_TRUE(parseKnobValue(kRiderTarget, "-25,0 dBFS", n) && std::fabs(n - 0.25) < 1e-12);
  ASSERT_TRUE(parseKnobValue(kBreathAmount, "50 %", n) && n == 0.5 && parseKnobValue(kBreathAmount, "3 dB", n) && n == 0.25);
  ASSERT_TRUE(parseKnobValue(kCompAmount, "-12", n) && std::fabs(n - 0.25) < 1e-12);
  ASSERT_TRUE(parseKnobValue(kDeEssFreq, "10000 Hz", n) && std::fabs(n - 1.0) < 1e-12);
  ASSERT_TRUE(parseKnobValue(kDeEssFreq, "4k", n) && std::fabs(n) < 1e-12 && parseKnobValue(kDeEssFreq, "6500", n) &&
              formatKnobValue(kDeEssFreq, n) == "6.5 kHz");
  ASSERT_TRUE(!parseKnobValue(kDeEssFreq, "0 kHz", n) && !parseKnobValue(kDeEssFreq, "-3", n));
  ASSERT_TRUE(parseKnobValue(kTone, "+50 %", n) && n == 0.75 && !parseKnobValue(kTone, "5 dB", n));
  // Tone is the one knob whose % is its own signed scale (-100..+100), not a share of the travel
  ASSERT_TRUE(parseKnobValue(kTone, "-100 %", n) && n == 0.0 && parseKnobValue(kTone, "100", n) && n == 1.0);
  ASSERT_TRUE(parseKnobValue(kTone, "50 %", n) && n == 0.75 && parseKnobValue(kTone, "0 %", n) && n == 0.5);
  ASSERT_TRUE(parseKnobValue(kEqAmount, "250", n) && n == 1.0 && !parseKnobValue(kEqAmount, "3 dB", n));
  ASSERT_TRUE(!parseKnobValue(kBypass, "1", n) && !parseKnobValue(kMode, "0.5", n));
}

// Listen dims with its own module (as in the preview); the IN buttons and the rest stay out of every module.
void test_listen_belongs_to_its_module() {
  ASSERT_TRUE(moduleOf(kBreathListen) == Module::Breath && moduleOf(kDeEssListen) == Module::DeEss);
  DimState s{};
  s.in[static_cast<int>(Module::Breath) - 1] = false;
  ASSERT_TRUE(controlDimmed(kBreathListen, s) && !controlDimmed(kDeEssListen, s) && !controlDimmed(kBreathIn, s));
  DimState d{};
  d.in[static_cast<int>(Module::DeEss) - 1] = false;
  ASSERT_TRUE(controlDimmed(kDeEssListen, d) && !controlDimmed(kBreathListen, d) && !controlDimmed(kDeEssIn, d));
}

// The Mode view holds three equal cells, one per face: the shared lit-choice geometry puts every face where the layout
// (the preview) has it.
void test_mode_view_cells_match_faces() {
  const ControlSpec& m = *controlOf(kMode);
  const Rect v = viewRect(m);
  for (int i = 0; i < kModeCount; ++i) {
    const auto cell = FairyTapes::Ui::Rack::litChoiceCell(v.w, v.h, kModeCount, 1, i);
    const auto f = FairyTapes::Ui::Rack::litFace(cell, kModeFaceW, FairyTapes::Ui::Rack::kLitFaceHeight);
    const Rect want = modeFaceRect(i);
    ASSERT_TRUE(std::fabs(v.x + f.left - want.x) < 1e-4 && std::fabs(v.y + f.top - want.y) < 1e-4);
    ASSERT_TRUE(std::fabs(f.right - f.left - want.w) < 1e-4 && std::fabs(f.bottom - f.top - want.h) < 1e-4);
  }
  ASSERT_TRUE(inside(v, boxOf(Box::Mode).rect));
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
// The preview's scope scale (deep_throat_rack.html, renderSim): voice bottom - clamp((dB + 64) / 54) * 94 % of the height
// - 2 px; Rider gain mid - clamp(gain / 6) * 85 % of the half height.
double previewVoiceY(const Rect& r, double db) { return r.y + r.h - std::clamp((db + 64.0) / 54.0, 0.0, 1.0) * r.h * 0.94 - 2.0; }
double previewRiderY(const Rect& r, double gainDb) { return r.y + r.h / 2 - std::clamp(gainDb / 6.0, -1.0, 1.0) * r.h / 2 * 0.85; }

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
  s.push({normOfDb(6), riderNorm(3), 1.0f});      // above -10 dB: stays at the top of the voice range
  const std::vector<ScopeColumn> cols = scopeColumns(s, r);
  ASSERT_TRUE(cols.size() == 4);
  ASSERT_TRUE(nearD(cols[0].voiceTop, previewVoiceY(r, -60)) && nearD(cols[1].voiceTop, previewVoiceY(r, 0)) &&
              nearD(cols[2].voiceTop, previewVoiceY(r, -30)) && nearD(cols[3].voiceTop, previewVoiceY(r, -10)));
  ASSERT_TRUE(nearD(cols[1].voiceTop, r.y + r.h - r.h * 0.94 - 2) && nearD(cols[2].voiceTop, r.y + r.h - 34.0 / 54.0 * r.h * 0.94 - 2));
  ASSERT_TRUE(nearD(cols[0].riderY, previewRiderY(r, 6)) && nearD(cols[1].riderY, previewRiderY(r, -6)) &&
              nearD(cols[2].riderY, r.y + r.h / 2) && nearD(cols[3].riderY, previewRiderY(r, 3)));
  ASSERT_TRUE(nearD(cols[0].riderY, r.y + r.h / 2 - r.h / 2 * 0.85));
  ASSERT_TRUE(cols[0].breath && !cols[1].breath && !cols[2].breath && cols[3].breath);
  // bad values are limited into the rectangle (NaN: silence, centre, no breath)
  History<8> bad;
  bad.push({std::nanf(""), std::nanf(""), std::nanf("")});
  bad.push({-3.f, -3.f, -3.f});
  bad.push({9.f, 9.f, 9.f});
  bad.push({INFINITY, -INFINITY, INFINITY});
  for (const ScopeColumn& c : scopeColumns(bad, r)) ASSERT_TRUE(c.voiceTop >= r.y && c.voiceTop <= r.y + r.h && c.riderY >= r.y && c.riderY <= r.y + r.h);
  const auto bc = scopeColumns(bad, r);
  ASSERT_TRUE(nearD(bc[0].voiceTop, previewVoiceY(r, -60)) && nearD(bc[0].riderY, r.y + r.h / 2) && !bc[0].breath);
  ASSERT_TRUE(nearD(bc[1].voiceTop, previewVoiceY(r, -60)) && nearD(bc[1].riderY, previewRiderY(r, -6)) && !bc[1].breath);
  ASSERT_TRUE(nearD(bc[2].voiceTop, previewVoiceY(r, 0)) && nearD(bc[2].riderY, previewRiderY(r, 6)) && bc[2].breath);
  ASSERT_TRUE(scopeColumns(History<8>{}, r).empty());
}

// The traces never touch the scope border (1.5 px): the Rider gain over its whole +-6 dB range (and beyond, NaN, inf)
// stays at least 6 px inside, the voice at least 2 px above the bottom and 3 px below the top.
void test_scope_traces_stay_inside_the_border() {
  const Rect r{156, 106, 292, 84};
  const float odd[] = {std::nanf(""), INFINITY, -INFINITY, -5.f, 5.f};
  std::vector<float> values;
  for (int i = 0; i <= 1000; ++i) values.push_back(static_cast<float>(i) / 1000.f);
  values.insert(values.end(), std::begin(odd), std::end(odd));
  double minRider = 1e9, maxRider = -1e9, minVoice = 1e9, maxVoice = -1e9;
  for (float v : values) {
    const ScopeColumn c = scopeColumnOf({v, v, 0.f}, r);
    minRider = std::min(minRider, static_cast<double>(c.riderY));
    maxRider = std::max(maxRider, static_cast<double>(c.riderY));
    minVoice = std::min(minVoice, static_cast<double>(c.voiceTop));
    maxVoice = std::max(maxVoice, static_cast<double>(c.voiceTop));
  }
  ASSERT_TRUE(minRider >= r.y + 6 && maxRider <= r.y + r.h - 6);
  ASSERT_TRUE(nearD(minRider, r.y + r.h / 2 - r.h / 2 * 0.85) && nearD(maxRider, r.y + r.h / 2 + r.h / 2 * 0.85));   // +-6 dB used
  ASSERT_TRUE(minVoice >= r.y + 3 && maxVoice <= r.y + r.h - 2);
}


// ---- Back of the panel: SETTINGS (Task 11) -------------------------------------------------------------------------
// Every combination of the preference fields: the items are WEAR, TAPE FONT, VALUES FONT, LOOK in the APPEARANCE column,
// and each one shows the option of its field.
void test_settings_items_reflect_prefs() {
  using FairyTapes::Ui::BackColumn;
  using FairyTapes::Ui::BackItemKind;
  for (int w = 0; w < 3; ++w)
    for (int f = 0; f < 3; ++f)
      for (int v = 0; v < 2; ++v)
        for (int n = 0; n < 2; ++n) {
          Prefs p;
          p.wear = static_cast<WearLevel>(w);
          p.tapeFont = static_cast<TapeFont>(f);
          p.handwrittenValues = v == 1;
          p.notes = n == 1;
          const auto items = settingsItems(p);
          ASSERT_TRUE(items.size() == 4);
          const int keys[] = {kSetWear, kSetTapeFont, kSetValuesFont, kSetLook};
          const char* labels[] = {"WEAR", "TAPE FONT", "VALUES FONT", "LOOK"};
          const BackItemKind kinds[] = {BackItemKind::Choice, BackItemKind::Stepper, BackItemKind::Choice, BackItemKind::Button};
          for (std::size_t i = 0; i < 4; ++i) {
            ASSERT_TRUE(items[i].key == keys[i] && items[i].label == labels[i] && items[i].kind == kinds[i]);
            ASSERT_TRUE(items[i].column == BackColumn::Appearance);
          }
          ASSERT_TRUE(items[0].selected == w);
          ASSERT_TRUE((items[0].options == std::vector<std::string>{"OFF", "LIGHT", "HEAVY"}));
          ASSERT_TRUE(items[1].selected == f && items[1].tapeFontOptions);
          ASSERT_TRUE((items[1].options == std::vector<std::string>{"Zeyada", "Rock Salt", "Homemade Apple"}));
          ASSERT_TRUE(items[2].selected == v);
          ASSERT_TRUE((items[2].options == std::vector<std::string>{"STANDARD", "TAPE FONT"}));
          ASSERT_TRUE(items[3].options == std::vector<std::string>{"RE-ROLL"} && items[3].note == "new wear, dirt and screws");
          for (int key : {kSetWear, kSetTapeFont, kSetValuesFont})
            ASSERT_TRUE(items[static_cast<std::size_t>(key - 1)].selected == selectedFor(key, p));
          // The template accepts the whole back (rows and controls inside APPEARANCE, keys unique, ROUTING and CREDITS
          // complete).
          const auto content = backContent(p);
          const auto problems = FairyTapes::Ui::backContentProblems(content, FairyTapes::Ui::backLayoutFor(kEditorHeight));
          for (const std::string& problem : problems) std::fprintf(stderr, "  back content: %s\n", problem.c_str());
          ASSERT_TRUE(problems.empty());
          ASSERT_TRUE(content.notes.size() == 4 && content.notes == settingsNotes());
        }
  for (const std::string& line : settingsNotes()) ASSERT_TRUE(!line.empty() && line.size() <= 125);   // one line of the box each
}

// Each click gives the expected preferences and leaves the other fields alone; RE-ROLL, an unknown key or an index out
// of range change nothing.
void test_settings_item_to_prefs() {
  auto same = [](const Prefs& a, const Prefs& b) {
    return a.wear == b.wear && a.tapeFont == b.tapeFont && a.handwrittenValues == b.handwrittenValues && a.notes == b.notes;
  };
  for (int w = 0; w < 3; ++w)
    for (int f = 0; f < 3; ++f)
      for (int v = 0; v < 2; ++v)
        for (int n = 0; n < 2; ++n) {
          Prefs base;
          base.wear = static_cast<WearLevel>(w);
          base.tapeFont = static_cast<TapeFont>(f);
          base.handwrittenValues = v == 1;
          base.notes = n == 1;
          for (int i = 0; i < 3; ++i) {
            Prefs p = base, want = base;
            want.wear = static_cast<WearLevel>(i);
            ASSERT_TRUE(applySetting(kSetWear, i, p) && same(p, want));
            p = base;
            want = base;
            want.tapeFont = static_cast<TapeFont>(i);
            ASSERT_TRUE(applySetting(kSetTapeFont, i, p) && same(p, want));
          }
          for (int i = 0; i < 2; ++i) {
            Prefs p = base, want = base;
            want.handwrittenValues = i == 1;
            ASSERT_TRUE(applySetting(kSetValuesFont, i, p) && same(p, want));
            ASSERT_TRUE(selectedFor(kSetValuesFont, p) == i);
          }
          for (const auto& bad : {std::pair<int, int>{kSetWear, -1}, {kSetWear, 3}, {kSetTapeFont, 3}, {kSetTapeFont, -1},
                                  {kSetValuesFont, 2}, {kSetValuesFont, -1}, {kSetLook, 0}, {0, 0}, {99, 1}}) {
            Prefs p = base;
            ASSERT_TRUE(!applySetting(bad.first, bad.second, p) && same(p, base));
          }
        }
}

// ---- Back of the panel: MODES, ROUTING, CREDITS (Task 12) -----------------------------------------------------------
// The nine rows of the preview's MODES table, every cell equal to the preview's value and to the dynamic_text.h string of
// the table row of its mode (the strings the front shows come from the same functions).
void test_modes_rows_from_table() {
  const auto rows = modesRows();
  ASSERT_TRUE(rows.size() == 9);
  const char* labels[9] = {"Breath detector", "Rider max range", "Rider speed", "HPF", "Mud cut (at 300 Hz)", "De-ess",
                           "Comp ratio", "Dist", "Tone curve (mud / presence / air, dB at +100%)"};
  const char* want[9][3] = {
      {"SPECTRAL", "SPECTRAL", "LEVEL"},
      {"\xC2\xB1" "6 dB", "\xC2\xB1" "4 dB", "\xC2\xB1" "3 dB"},
      {"8 dB/s", "4 dB/s", "2 dB/s"},
      {"80 Hz", "70 Hz", "60 Hz"},
      {"-2 dB", "-3 dB", "-4 dB"},
      {"normal", "normal", "x0.15"},
      {"2:1", "3:1", "8:1"},
      {"OFF", "DIST 2", "DIST 3"},
      {"-2 / +3 / +2 dB", "-1.5 / +2 / +4 dB", "-3 / +1 / 0 dB"},
  };
  for (std::size_t i = 0; i < 9; ++i) {
    ASSERT_TRUE(rows[i].label == labels[i]);
    for (int j = 0; j < 3; ++j) {
      if (rows[i].cells[static_cast<std::size_t>(j)] != want[i][j])
        std::fprintf(stderr, "  row %zu mode %d: \"%s\" (want \"%s\")\n", i, j, rows[i].cells[static_cast<std::size_t>(j)].c_str(), want[i][j]);
      ASSERT_TRUE(rows[i].cells[static_cast<std::size_t>(j)] == want[i][j]);
    }
  }
  for (int j = 0; j < 3; ++j) {
    const ModeRow& r = modeRow(static_cast<Mode>(j));
    const std::size_t c = static_cast<std::size_t>(j);
    ASSERT_TRUE(rows[0].cells[c] == detectorTag(r) && rows[1].cells[c] == riderRangeText(r) && rows[2].cells[c] == riderSpeedText(r));
    ASSERT_TRUE(rows[3].cells[c] == eqHpfText(r, 0.5) && rows[4].cells[c] == eqMudText(r, 0.5));
    ASSERT_TRUE(rows[5].cells[c] == deEssScaleText(r) && rows[6].cells[c] == compRatioText(r) && rows[7].cells[c] == distName(r));
    ASSERT_TRUE(rows[8].cells[c] == toneCurveText(r));
    // the front's strings are built from the same pieces
    ASSERT_TRUE(riderMaxText(r) == "MAX " + riderRangeText(r) + " \xC2\xB7 " + riderSpeedText(r));
    ASSERT_TRUE(ratioText(0, r) == "AUTO " + compRatioText(r) && distText(0, r) == "AUTO " + distName(r));
  }
  ASSERT_TRUE((modesHeader() == std::array<std::string, 3>{"SPEAK", "SING", "PUKE"}));
  const auto notes = modesNotes();
  ASSERT_TRUE(notes.size() == 4);
  ASSERT_TRUE(notes[1] == "De-ess x0.15: the DE-ESS amount is multiplied by 0.15 in PUKE, so the stage is almost off.");
  ASSERT_TRUE(notes[2].find("LEVEL (PUKE)") != std::string::npos);
  ASSERT_TRUE(modesIntro().size() == 2);
}

// Rows, header and notes are generated: a local copy of the table with other values gives other cells.
void test_modes_follow_table_edit() {
  ModeRow t[3] = {kModeTable[0], kModeTable[1], kModeTable[2]};
  t[0].name = "TALK";
  t[0].levelDetector = true;
  t[1].riderMaxRangeDb = 9;
  t[1].riderSlewDbPerSec = 12;
  t[2].hpfHz = 95;
  t[2].mudCutDb = -1.5;
  t[0].deEssScale = 0.5;
  t[2].deEssScale = 1.0;
  t[1].compRatio = 5;
  t[2].compDist = 1;
  t[0].toneMudDb = 0;
  t[0].tonePresenceDb = -2.5;
  t[0].toneAirDb = 6;
  const auto rows = modesRows(t);
  ASSERT_TRUE(rows.size() == 9);
  ASSERT_TRUE(rows[0].cells[0] == "LEVEL" && rows[0].cells[1] == "SPECTRAL");
  ASSERT_TRUE(rows[1].cells[1] == "\xC2\xB1" "9 dB" && rows[2].cells[1] == "12 dB/s");
  ASSERT_TRUE(rows[3].cells[2] == "95 Hz" && rows[4].cells[2] == "-1.5 dB");
  ASSERT_TRUE(rows[5].cells[0] == "x0.5" && rows[5].cells[2] == "normal");
  ASSERT_TRUE(rows[6].cells[1] == "5:1" && rows[7].cells[2] == "OFF");
  ASSERT_TRUE(rows[8].cells[0] == "0 / -2.5 / +6 dB");
  ASSERT_TRUE((modesHeader(t) == std::array<std::string, 3>{"TALK", "SING", "PUKE"}));
  const auto notes = modesNotes(t);
  ASSERT_TRUE(notes.size() == 4);
  ASSERT_TRUE(notes[1] == "De-ess x0.5: the DE-ESS amount is multiplied by 0.5 in TALK.");
  ASSERT_TRUE(notes[2].find("LEVEL (TALK, PUKE)") != std::string::npos);
  // no mode scales the de-esser: the note goes away
  t[0].deEssScale = 1.0;
  ASSERT_TRUE(modesNotes(t).size() == 3);
  // the real table is untouched
  ASSERT_TRUE(modesRows()[3].cells[2] == "60 Hz");
}

void test_active_mode_column() {
  ASSERT_TRUE(activeModeColumn(Mode::Speak) == 0 && activeModeColumn(Mode::Sing) == 1 && activeModeColumn(Mode::Puke) == 2);
  ASSERT_TRUE(activeModeColumn(static_cast<Mode>(-3)) == 0 && activeModeColumn(static_cast<Mode>(9)) == 2);
  for (int i = -2; i < 6; ++i) {
    const Mode m = static_cast<Mode>(i);
    ASSERT_TRUE(&kModeTable[activeModeColumn(m)] == &modeRow(m));   // the yellow column is the row the engine reads
  }
  // the table's geometry: columns left to right inside the box, rows above the notes, notes inside the box
  const auto& L = FairyTapes::Ui::backLayoutFor(kEditorHeight);
  ASSERT_TRUE(ModesTab::kBox.x >= L.custom.x && ModesTab::kBox.y >= L.custom.y &&
              ModesTab::kBox.x + ModesTab::kBox.w <= L.custom.x + L.custom.w && ModesTab::kBox.y + ModesTab::kBox.h <= L.custom.y + L.custom.h);
  ASSERT_TRUE(ModesTab::kColumnX[0] < ModesTab::kColumnX[1] && ModesTab::kColumnX[1] < ModesTab::kColumnX[2] &&
              ModesTab::kColumnX[2] < ModesTab::kRight);
  const double lastRow = ModesTab::kFirstRowBaseline + ModesTab::kRowStep * 8;
  ASSERT_TRUE(lastRow < ModesTab::kNoteFirstBaseline - 11);
  ASSERT_TRUE(ModesTab::kNoteFirstBaseline + ModesTab::kNoteStep * 3 < ModesTab::kBox.y + ModesTab::kBox.h - 6);
}

namespace {
using FairyTapes::Ui::RoutingPrim;
const RoutingPrim* findText(const FairyTapes::Ui::RoutingDiagram& d, const std::string& s, double size = 0) {
  for (const RoutingPrim& p : d)
    if (p.kind == RoutingPrim::Kind::Text && p.text == s && (size == 0 || p.size == size)) return &p;
  return nullptr;
}
int countText(const FairyTapes::Ui::RoutingDiagram& d, const std::string& s) {
  int n = 0;
  for (const RoutingPrim& p : d) n += p.kind == RoutingPrim::Kind::Text && p.text == s ? 1 : 0;
  return n;
}
// The module box whose outline contains the point.
const RoutingPrim* boxAround(const FairyTapes::Ui::RoutingDiagram& d, double x, double y) {
  for (const RoutingPrim& p : d)
    if (p.kind == RoutingPrim::Kind::Rect && x > p.x && x < p.x + p.w && y > p.y && y < p.y + p.h) return &p;
  return nullptr;
}
}  // namespace

// The six modules in the order of the engine: Rider, Breath, EQ, De-ess on the first line, Comp and Tone on the second,
// each in its box with an IN switch over it; one global BYPASS from the input side to the output switch; the detection
// paths dashed.
void test_routing_diagram_blocks_in_engine_order() {
  const auto r = deepThroatRouting();
  const auto& d = r.flow;
  const char* names[6] = {"RIDER", "BREATH", "EQ", "DE-ESS", "COMP", "TONE"};
  const RoutingPrim* boxes[6] = {};
  for (int i = 0; i < 6; ++i) {
    ASSERT_TRUE(countText(d, names[i]) == 1);
    const RoutingPrim* t = findText(d, names[i], 11);
    ASSERT_TRUE(t != nullptr);
    boxes[i] = boxAround(d, t->x, t->y - 4);
    ASSERT_TRUE(boxes[i] != nullptr);
  }
  // reading order: line by line, left to right
  for (int i = 1; i < 6; ++i) {
    const RoutingPrim& a = *boxes[i - 1];
    const RoutingPrim& b = *boxes[i];
    const bool sameLine = std::fabs((a.y + a.h / 2) - (b.y + b.h / 2)) < 1;
    ASSERT_TRUE((sameLine && b.x >= a.x + a.w) || (!sameLine && b.y >= a.y + a.h));
  }
  ASSERT_TRUE(boxes[3]->y + boxes[3]->h <= boxes[4]->y);   // the line break is between De-ess and Comp
  // six IN switches, one over each module
  ASSERT_TRUE(countText(d, "IN") == 6);
  for (const RoutingPrim* b : boxes) {
    int over = 0;
    for (const RoutingPrim& p : d)
      if (p.kind == RoutingPrim::Kind::Text && p.text == "IN" && p.x > b->x && p.x < b->x + b->w && p.y < b->y && p.y > b->y - 40) ++over;
    ASSERT_TRUE(over == 1);
  }
  // BYPASS: one label, one line from the input side (left of the INPUT gain stage) to the output switch
  ASSERT_TRUE(countText(d, "BYPASS") == 1);
  const RoutingPrim* outJack = findText(d, "OUT L \xC2\xB7 R");
  const RoutingPrim* inJack = findText(d, "IN L \xC2\xB7 R");
  ASSERT_TRUE(outJack != nullptr && inJack != nullptr && inJack->x < outJack->x);
  int bypassLines = 0;
  for (const RoutingPrim& p : d) {
    if (p.kind != RoutingPrim::Kind::Line || p.dashed || p.pts.size() < 3) continue;
    const auto& a = p.pts.front();
    const auto& z = p.pts.back();
    if (a.x >= boxes[0]->x || a.y > boxes[0]->y + boxes[0]->h) continue;   // starts on the first line, before the Rider
    bool toSwitch = false;   // ends on a switch contact of the output, after Tone
    for (const RoutingPrim& c : d)
      if (c.kind == RoutingPrim::Kind::Circle && c.panelFill && std::hypot(c.x - z.x, c.y - z.y) <= c.r + 0.5 &&
          c.x > boxes[5]->x + boxes[5]->w)
        toSwitch = true;
    bypassLines += toSwitch ? 1 : 0;
  }
  ASSERT_TRUE(bypassLines == 1);
  int dashed = 0;
  for (const RoutingPrim& p : d) dashed += p.kind == RoutingPrim::Kind::Line && p.dashed ? 1 : 0;
  ASSERT_TRUE(dashed >= 3);
  // the detection and meter blocks of the preview are there
  for (const char* s : {"BREATH DETECTOR", "LEVEL", "LISTEN", "DELAY", "IN METER", "OUT METER", "GR"}) ASSERT_TRUE(findText(d, s) != nullptr);
  // legend: the symbols the diagram uses, one note line
  ASSERT_TRUE(r.legend.symbols.size() == 7 && r.legend.notes.size() == 1);
}

// Engine order at the input (engine.cpp): the input peaks and the input clip are read on the raw input, then the Input gain,
// then the lookahead delay; the detection taps the gained, not delayed signal. Listen: Breath Listen wins over De-ess Listen.
void test_routing_diagram_input_side_follows_the_engine() {
  const auto r = deepThroatRouting();
  const auto& d = r.flow;
  ASSERT_TRUE(countText(d, "INPUT") == 1);   // one Input gain stage, shared by the audio path and the detection
  const RoutingPrim* meter = findText(d, "IN METER");
  const RoutingPrim* input = findText(d, "INPUT");
  const RoutingPrim* delay = findText(d, "DELAY");
  ASSERT_TRUE(meter != nullptr && input != nullptr && delay != nullptr);
  ASSERT_TRUE(meter->x < input->x && input->x < delay->x);
  std::string notes;
  for (const auto& n : r.legend.notes) notes += n;
  ASSERT_TRUE(notes.find("BREATH wins") != std::string::npos && notes.find("DE-ESS band first") == std::string::npos);
}

void test_routing_diagram_inside_panel() {
  const auto& L = FairyTapes::Ui::backLayoutFor(kEditorHeight);
  const auto r = deepThroatRouting();
  ASSERT_TRUE(!r.flow.empty());
  for (const RoutingPrim& p : r.flow) {
    double x0, y0, x1, y1;
    FairyTapes::Ui::routingExtent(p, x0, y0, x1, y1);
    ASSERT_TRUE(x0 >= L.flow.x + 4 && x1 <= L.flow.x + L.flow.w - 4 && y0 >= L.flow.y - 8 && y1 <= L.flow.y + L.flow.h - 4);
  }
  for (const RoutingPrim& p : FairyTapes::Ui::legendDiagram(r.legend, L.legendSymbolY, L.legendNoteBaseline)) {
    double x0, y0, x1, y1;
    FairyTapes::Ui::routingExtent(p, x0, y0, x1, y1);
    ASSERT_TRUE(x0 >= L.legend.x && x1 <= L.legend.x + L.legend.w && y0 >= L.legend.y && y1 <= L.legend.y + L.legend.h);
  }
  for (const std::string& problem : FairyTapes::Ui::backContentProblems(backContent(Prefs{}), L))
    ASSERT_TRUE(problem.rfind("routing:", 0) != 0);
}

// Exactly the five components shipped in the bundle (the DSP is ours), with author and license; version and notices.
void test_credits_entries() {
  const auto c = credits();
  ASSERT_TRUE(c.size() == 5);
  const char* want[5][3] = {
      {"VST 3 SDK", "Steinberg Media Technologies", "MIT"},
      {"VSTGUI", "Steinberg Media Technologies", "BSD-3-Clause"},
      {"Zeyada (tape font)", "Kimberly Geswein", "OFL-1.1"},
      {"Rock Salt (tape font)", "Font Diner", "Apache-2.0"},
      {"Homemade Apple (tape font)", "Font Diner", "Apache-2.0"},
  };
  for (std::size_t i = 0; i < 5; ++i) ASSERT_TRUE(c[i].name == want[i][0] && c[i].author == want[i][1] && c[i].license == want[i][2]);
  const auto b = backCredits();
  ASSERT_TRUE(b.title == "DEEP THROAT");
  ASSERT_TRUE(b.versionLine.find(std::string("Version ") + kPluginVersion) == 0);
  ASSERT_TRUE(std::string(kPluginVersion) == "1.0.0");
  ASSERT_TRUE(b.entries.size() == 5 && b.entries[4].name == c[4].name);
  ASSERT_TRUE(!b.licenseLines.empty() && b.licenseLines[0].find("GPL-3.0") != std::string::npos);
  ASSERT_TRUE(b.notices.size() == 2);
  ASSERT_TRUE(b.notices[0] == "VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.");
  for (const char* other : {"Airwindows", "NeuralAmpModeler", "NAM", "RTNeural", "dusk"})
    for (const auto& e : b.entries) ASSERT_TRUE(e.name.find(other) == std::string::npos);
  const auto content = backContent(Prefs{});
  ASSERT_TRUE(content.credits.entries.size() == 5 && content.credits.title == b.title);
}

// No user-facing string of the back or the front names the devices that inspired the plug-in.
void test_no_brand_in_user_facing_strings() {
  std::vector<std::string> all;
  auto add = [&all](const std::string& s) { all.push_back(s); };
  for (const auto& row : modesRows()) {
    add(row.label);
    for (const auto& c : row.cells) add(c);
  }
  for (const auto& s : modesNotes()) add(s);
  for (const auto& s : modesIntro()) add(s);
  for (const auto& s : modesHeader()) add(s);
  add(ModesTab::kTitle);
  const auto content = backContent(Prefs{});
  add(content.headerLine);
  for (const auto& t : content.tabs) add(t.label);
  for (const auto& it : content.items) {
    add(it.label);
    add(it.hint);
    add(it.note);
    for (const auto& o : it.options) add(o);
  }
  for (const auto& s : content.notes) add(s);
  add(content.credits.title);
  add(content.credits.versionLine);
  for (const auto& s : content.credits.licenseLines) add(s);
  for (const auto& s : content.credits.notices) add(s);
  for (const auto& e : content.credits.entries) {
    add(e.name);
    add(e.author);
    add(e.license);
  }
  for (const auto& p : content.routing.flow) add(p.text);
  for (const auto& s : content.routing.legend.notes) add(s);
  add(content.routing.legend.meterLabel);
  for (const PanelText& t : kPanelTexts) add(t.text);
  for (const BoxSpec& b : kBoxes) add(b.title ? b.title : "");
  for (const ControlSpec& c : kControls) {
    add(c.name ? c.name : "");
    add(c.lo ? c.lo : "");
    add(c.hi ? c.hi : "");
    for (const char* ch : c.choices) add(ch ? ch : "");
  }
  add(kTagEqText);
  add(kTagCompText);
  for (Mode m : {Mode::Speak, Mode::Sing, Mode::Puke}) {
    add(modeRow(m).name);
    add(modeRow(m).description);
    add(modeTagLine1(m));
    add(modeTagLine2(m));
    add(detectorTag(m));
    add(riderMaxText(m));
    add(deEssNote(m));
  }
  const char* brands[] = {"distressor", "thistressor", "empirical", "waves", "vocal rider", "debreath", "de-breath",
                          "1176", "la-2a", "teletronix", "urei", "fairchild", "neve", "izotope", "fabfilter", "valentine"};
  ASSERT_TRUE(all.size() > 200);
  for (const std::string& s : all) {
    std::string low = s;
    for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (const char* b : brands) {
      // as a word: "neve" must not match "never"
      bool found = false;
      const std::size_t n = std::strlen(b);
      for (std::size_t at = low.find(b); at != std::string::npos; at = low.find(b, at + 1)) {
        const bool before = at > 0 && std::isalnum(static_cast<unsigned char>(low[at - 1]));
        const bool after = at + n < low.size() && std::isalnum(static_cast<unsigned char>(low[at + n]));
        found = found || (!before && !after);
      }
      if (found) std::fprintf(stderr, "  brand \"%s\" in \"%s\"\n", b, s.c_str());
      ASSERT_TRUE(!found);
    }
  }
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
  RUN_TEST(test_value_text_formats);
  RUN_TEST(test_value_text_parse_roundtrip);
  RUN_TEST(test_listen_belongs_to_its_module);
  RUN_TEST(test_mode_view_cells_match_faces);
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
  RUN_TEST(test_scope_traces_stay_inside_the_border);
  RUN_TEST(test_settings_items_reflect_prefs);
  RUN_TEST(test_settings_item_to_prefs);
  RUN_TEST(test_modes_rows_from_table);
  RUN_TEST(test_modes_follow_table_edit);
  RUN_TEST(test_active_mode_column);
  RUN_TEST(test_routing_diagram_blocks_in_engine_order);
  RUN_TEST(test_routing_diagram_input_side_follows_the_engine);
  RUN_TEST(test_routing_diagram_inside_panel);
  RUN_TEST(test_credits_entries);
  RUN_TEST(test_no_brand_in_user_facing_strings);
  { std::error_code ec; fs::remove_all(scratchDir(), ec); }
  std::printf("deep_throat_ui_tests: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed ? 1 : 0;
}
