/**
 * Fairy Tapes · Happy Ending — tests for the pure editor layout (ui/editor_size.h, ui/layout.h): no VSTGUI.
 * Build target: happy_ending_ui_tests.
 */
#include "ui/editor_size.h"
#include "ui/layout.h"
#include "ui/readouts.h"
#include "ui/value_text.h"

#include "tuning.h"

#include "fairy_ui/screws.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace FairyTapes::HappyEnding;
using namespace FairyTapes::HappyEnding::Ui;

namespace {

int g_failed = 0;
int g_passed = 0;

#define ASSERT_TRUE(expr) do { \
  if (!(expr)) { \
    std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); \
    ++g_failed; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  const int before = g_failed; \
  fn(); \
  if (g_failed == before) { ++g_passed; std::printf("PASS %s\n", #fn); } \
  else std::printf("FAIL %s\n", #fn); \
} while(0)

bool inside(const Rect& in, const Rect& out) {
  return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}
bool overlap(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
const Rect kPanel{0, 0, static_cast<float>(kEditorWidth), static_cast<float>(kEditorHeight)};

bool isFinalModeControl(Steinberg::Vst::ParamID id) {
  return id == kFinalEffect || id == kFinalCurve || id == kFinalDrive;
}

// Everything on the panel that a control must not cover (and that a tape must keep clear of).
std::vector<Rect> fixedReadouts() {
  std::vector<Rect> out{kScreenRect, kLufsScreenRect};
  for (const MeterRow& r : kMeterRows) out.push_back(meterBarRect(r));
  for (const GrRow& r : kGrRows) out.push_back(grBarRect(r));
  for (const ClipLed& c : kClipLeds) out.push_back(clipLedRect(c));
  for (const Led& l : kActLeds) out.push_back({l.cx - l.r, l.cy - l.r, 2 * l.r, 2 * l.r});
  out.push_back({kTargetLed.cx - kTargetLed.r, kTargetLed.cy - kTargetLed.r, 2 * kTargetLed.r, 2 * kTargetLed.r});
  return out;
}

UiState allLive() {
  UiState s;
  s.bypass = false;
  for (bool& b : s.stageOn) b = true;
  for (bool& b : s.bandOn) b = true;
  s.listen = -1;
  s.set = false;
  s.finalMode = 0;
  return s;
}

bool dimmedAndLocked(Steinberg::Vst::ParamID id, const UiState& s, int choice = -1) {
  const ControlSpec* c = controlOf(id, choice);
  return c != nullptr && controlDimmed(*c, s) && controlLocked(*c, s);
}
bool live(Steinberg::Vst::ParamID id, const UiState& s, int choice = -1) {
  const ControlSpec* c = controlOf(id, choice);
  return c != nullptr && !controlDimmed(*c, s) && !controlLocked(*c, s);
}

// ---------------------------------------------------------------------------------------------------------------------

void test_editor_size() {
  ASSERT_TRUE(kEditorWidth == 1280 && kEditorHeight == 890);
  const PixelSize a = scaledEditorSize(1.0, FairyTapes::Ui::EditorSize::S100);
  ASSERT_TRUE(a.width == 1280 && a.height == 890);
  const PixelSize b = scaledEditorSize(2.0, FairyTapes::Ui::EditorSize::S75);
  ASSERT_TRUE(b.width == 1920 && b.height == 1335);
  const PixelSize c = scaledEditorSize(std::numeric_limits<double>::quiet_NaN(), FairyTapes::Ui::EditorSize::S125);
  ASSERT_TRUE(c.width == 1600 && c.height == 1113);
  // the screws sit in the corners of this panel (shared rule, Task 1)
  const auto sp = FairyTapes::Ui::screwPositions(1280, 890);
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(kScrewPos[i][0] == static_cast<float>(sp[static_cast<std::size_t>(i)].x));
    ASSERT_TRUE(kScrewPos[i][1] == static_cast<float>(sp[static_cast<std::size_t>(i)].y));
  }
  ASSERT_TRUE(kScrewPos[3][0] == 1262.f && kScrewPos[3][1] == 872.f);
}

void test_layout_binds_every_param_once() {
  for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id) {
    int n = 0;
    for (const ControlSpec& c : kControls)
      if (c.id == id) ++n;
    if (id == kOversampling) {
      ASSERT_TRUE(n == 0);   // on the back (BEHAVIOUR), not on the front
    } else {
      if (n != 1) std::fprintf(stderr, "  param %u bound %d times\n", static_cast<unsigned>(id), n);
      ASSERT_TRUE(n == 1);
    }
  }
  int listen = 0, set = 0;
  bool choiceSeen[3] = {false, false, false};
  for (const ControlSpec& c : kControls) {
    ASSERT_TRUE(c.id < kNumParams || c.id == kListenBand || c.id == kSetActive);   // no display ID, nothing else
    ASSERT_TRUE(c.id < kDisplayBase);
    if (c.id == kListenBand) {
      ++listen;
      ASSERT_TRUE(c.choiceIndex >= 0 && c.choiceIndex <= 2);
      choiceSeen[c.choiceIndex] = true;
    } else {
      ASSERT_TRUE(c.choiceIndex == -1);
    }
    if (c.id == kSetActive) ++set;
  }
  ASSERT_TRUE(listen == 3 && set == 1);
  ASSERT_TRUE(choiceSeen[0] && choiceSeen[1] && choiceSeen[2]);
  // lookup
  ASSERT_TRUE(controlOf(kOversampling) == nullptr);
  ASSERT_TRUE(controlOf(kGlueMix) != nullptr && controlOf(kGlueMix)->id == kGlueMix);
  ASSERT_TRUE(controlOf(kListenBand, 1) != nullptr && controlOf(kListenBand, 1)->choiceIndex == 1);
  ASSERT_TRUE(controlOf(kListenBand, 3) == nullptr);
  ASSERT_TRUE(controlOf(kDispInL) == nullptr);
  // each LISTEN sits in the button row of its bell, right of the bell's M/S
  for (int k = 0; k < 3; ++k) {
    const ControlSpec* l = controlOf(kListenBand, k);
    const ControlSpec* ms = controlOf(static_cast<Steinberg::Vst::ParamID>(kLmMs + 5 * k));
    ASSERT_TRUE(l != nullptr && ms != nullptr && l->box == Box::FineEq);
    ASSERT_TRUE(std::fabs(faceCentre(*l).y - faceCentre(*ms).y) < 0.5f);
    ASSERT_TRUE(std::fabs(faceCentre(*l).x - faceCentre(*ms).x - 34.f) < 0.5f);
  }
  // the parameters that need a list of names for their faces have one, of the right size
  ASSERT_TRUE(litChoiceOf(kLimitStyle) != nullptr && litChoiceOf(kLimitStyle)->count == kParams[kLimitStyle].steps);
  ASSERT_TRUE(litChoiceOf(kFinalMode) != nullptr && litChoiceOf(kFinalMode)->count == kParams[kFinalMode].steps);
  ASSERT_TRUE(litChoiceOf(kGlueMix) == nullptr);
  for (const ControlSpec& c : kControls)
    if (c.kind == Kind::LitChoice) ASSERT_TRUE(litChoiceOf(c.id) != nullptr);
}

void test_layout_rects_inside() {
  constexpr int n = static_cast<int>(sizeof(kBoxes) / sizeof(kBoxes[0]));
  ASSERT_TRUE(n == 12);
  for (int i = 0; i < n; ++i) {
    ASSERT_TRUE(inside(kBoxes[i].rect, kPanel));
    for (int j = i + 1; j < n; ++j) ASSERT_TRUE(!overlap(kBoxes[i].rect, kBoxes[j].rect));
  }
  // anchors from the preview (final coordinates: group shifts applied)
  ASSERT_TRUE(boxOf(Box::FineEq).rect.y == 302.f && boxOf(Box::VibeEq).rect.y == 586.f && boxOf(Box::Clip).rect.y == 758.f);
  ASSERT_TRUE(boxOf(Box::Input).rect.y == 92.f && boxOf(Box::Screen).rect.x == 782.f);
  for (const ControlSpec& c : kControls) {
    if (!inside(viewRect(c), boxOf(c.box).rect))
      std::fprintf(stderr, "  control %u (%s) outside its box\n", static_cast<unsigned>(c.id), c.name ? c.name : "-");
    ASSERT_TRUE(inside(viewRect(c), boxOf(c.box).rect));
  }
  for (const ActionSpec& a : kActions) ASSERT_TRUE(inside(viewRect(a), boxOf(a.box).rect));
  ASSERT_TRUE(inside(kScreenRect, boxOf(Box::Screen).rect));
  ASSERT_TRUE(inside(kLufsScreenRect, boxOf(Box::Level).rect));
  for (const MeterRow& r : kMeterRows) ASSERT_TRUE(inside(meterBarRect(r), boxOf(Box::Level).rect));
  for (const GrRow& r : kGrRows) ASSERT_TRUE(inside(grBarRect(r), boxOf(Box::Level).rect));
  for (const ClipLed& c : kClipLeds) ASSERT_TRUE(inside(clipLedRect(c), boxOf(Box::Level).rect));
  ASSERT_TRUE(inside(kLufsResetRect, kLufsScreenRect));
  for (const Led& l : kActLeds) ASSERT_TRUE(inside({l.cx - l.r, l.cy - l.r, 2 * l.r, 2 * l.r}, boxOf(Box::VibeEq).rect));
}

void test_layout_no_control_overlap() {
  std::vector<std::pair<Rect, const ControlSpec*>> rects;
  for (const ControlSpec& c : kControls) rects.push_back({viewRect(c), &c});
  for (const ActionSpec& a : kActions) rects.push_back({viewRect(a), nullptr});
  for (std::size_t i = 0; i < rects.size(); ++i)
    for (std::size_t j = i + 1; j < rects.size(); ++j) {
      const ControlSpec* a = rects[i].second;
      const ControlSpec* b = rects[j].second;
      // EFFECT / CURVE (INFLATE) and DRIVE (CLIP) of FINAL are never visible together
      if (a && b && isFinalModeControl(a->id) && isFinalModeControl(b->id) && (a->id == kFinalDrive) != (b->id == kFinalDrive))
        continue;
      if (overlap(rects[i].first, rects[j].first))
        std::fprintf(stderr, "  overlap: %s / %s\n", a && a->name ? a->name : "?", b && b->name ? b->name : "?");
      ASSERT_TRUE(!overlap(rects[i].first, rects[j].first));
    }
  for (const auto& r : rects)
    for (const Rect& f : fixedReadouts()) ASSERT_TRUE(!overlap(r.first, f));
  // the exception is real: the FINAL knobs share the place
  ASSERT_TRUE(overlap(viewRect(*controlOf(kFinalDrive)), viewRect(*controlOf(kFinalEffect))));
}

void test_in_buttons_top_right() {
  const Box boxes[7] = {Box::FineEq, Box::VibeEq, Box::Glue, Box::Clip, Box::Limit, Box::Final, Box::Metal};
  const Steinberg::Vst::ParamID ins[7] = {kFineOn, kVibeOn, kGlueOn, kClipOn, kLimitOn, kFinalOn, kMetalOn};
  for (int i = 0; i < 7; ++i) {
    const ControlSpec* c = controlOf(ins[i]);
    ASSERT_TRUE(c != nullptr && c->kind == Kind::Toggle && c->box == boxes[i]);
    ASSERT_TRUE(std::strcmp(c->name, "IN") == 0);
    const Rect& r = boxOf(boxes[i]).rect;
    const Point p = faceCentre(*c);
    ASSERT_TRUE(std::fabs(p.x - (r.x + r.w - 29.f)) <= 2.f);
    ASSERT_TRUE(std::fabs(p.y - (r.y + 38.f)) <= 2.f);
    ASSERT_TRUE(stageInParam(stageOf(boxes[i])) == ins[i]);
  }
  // BYPASS and MATCH follow the same corner in OUTPUT and LOUDNESS
  const Rect& out = boxOf(Box::Output).rect;
  ASSERT_TRUE(std::fabs(faceCentre(*controlOf(kBypass)).x - (out.x + out.w - 29.f)) <= 2.f);
  const ActionSpec& m = actionOf(Action::Match);
  const Rect& ld = boxOf(Box::Loudness).rect;
  ASSERT_TRUE(m.box == Box::Loudness && std::fabs(m.rect.x + m.rect.w / 2 - (ld.x + ld.w - 29.f)) <= 2.f);
}

void test_titles_bottom_and_tags() {
  for (const BoxSpec& b : kBoxes) {
    if (b.box == Box::Screen) {
      ASSERT_TRUE(b.title == nullptr);   // the screen box has no title
      continue;
    }
    ASSERT_TRUE(b.title != nullptr && b.title[0] != '\0');
    for (const char* p = b.title; *p; ++p) ASSERT_TRUE(!std::islower(static_cast<unsigned char>(*p)));
    const Point t = titleAnchor(b);
    ASSERT_TRUE(t.y == b.rect.y + b.rect.h + 5.f);     // on the bottom border
    ASSERT_TRUE(t.x == b.rect.x + b.rect.w / 2);
  }
  ASSERT_TRUE(std::strcmp(engineTag(Box::FineEq, 0), "MULTI-Q") == 0);
  ASSERT_TRUE(std::strcmp(engineTag(Box::VibeEq, 0), "TUBE-1A") == 0);
  ASSERT_TRUE(std::strcmp(engineTag(Box::Glue, 1), "4K-G") == 0);
  ASSERT_TRUE(std::strcmp(engineTag(Box::Clip, 0), "C-SOFTLY") == 0);
  ASSERT_TRUE(std::strcmp(engineTag(Box::Final, 0), "INFL-Z") == 0);
  ASSERT_TRUE(std::strcmp(engineTag(Box::Final, 1), "C-ONLY2") == 0);
  for (Box b : {Box::Limit, Box::Metal, Box::Input, Box::Output, Box::Level, Box::Loudness, Box::Screen})
    for (int mode : {0, 1}) ASSERT_TRUE(engineTag(b, mode) == nullptr);
  // the tags sit at the top left of their box (x + 16, y + 22), as in the preview
  const Point ft = tagPosition(Box::FineEq);
  ASSERT_TRUE(ft.x == 40.f && ft.y == 324.f);
  const Point fn = tagPosition(Box::Final);
  ASSERT_TRUE(fn.x == 657.f && fn.y == 780.f);
}

void test_dimming_rules() {
  UiState s = allLive();
  for (const ControlSpec& c : kControls)
    ASSERT_TRUE(!controlDimmed(c, s) && (!controlLocked(c, s) || c.id == kFinalDrive));   // DRIVE: hidden in INFLATE
  for (const ActionSpec& a : kActions) ASSERT_TRUE(!actionDimmed(a, s));

  // BYPASS: everything but itself (actions included); the meters and the LUFS screen stay live
  s.bypass = true;
  for (const ControlSpec& c : kControls) {
    if (c.id == kBypass) ASSERT_TRUE(!controlDimmed(c, s) && !controlLocked(c, s));
    else ASSERT_TRUE(controlDimmed(c, s) && controlLocked(c, s));
  }
  for (const ActionSpec& a : kActions) ASSERT_TRUE(actionDimmed(a, s));
  ASSERT_TRUE(!clipLedDimmed(0, s) && !clipLedDimmed(8, s));   // IN and OUT stay lit
  for (int i = 1; i < 8; ++i) ASSERT_TRUE(clipLedDimmed(i, s));
  for (int k = 0; k < 5; ++k) ASSERT_TRUE(grRowDimmed(k, s));

  // a stage IN off: the stage dims and locks, but not its IN
  s = allLive();
  s.stageOn[static_cast<int>(Stage::Glue)] = false;
  ASSERT_TRUE(dimmedAndLocked(kGlueThresh, s) && dimmedAndLocked(kGlueRatio, s) && dimmedAndLocked(kGlueWarm, s));
  ASSERT_TRUE(live(kGlueOn, s) && live(kTilt, s) && live(kClipDrive, s));
  ASSERT_TRUE(clipLedDimmed(3, s) && !clipLedDimmed(2, s) && grRowDimmed(0, s) && !grRowDimmed(1, s));

  // FINE EQ off: the band INs dim too, the stage IN does not
  s = allLive();
  s.stageOn[static_cast<int>(Stage::Fine)] = false;
  ASSERT_TRUE(dimmedAndLocked(kLsOn, s) && dimmedAndLocked(kMidFreq, s) && dimmedAndLocked(kListenBand, s, 1));
  ASSERT_TRUE(live(kFineOn, s));

  // a band off: its controls dim and lock, its IN stays live; the other bands are untouched
  s = allLive();
  s.bandOn[2] = false;   // LO MID
  ASSERT_TRUE(dimmedAndLocked(kLmFreq, s) && dimmedAndLocked(kLmMs, s) && dimmedAndLocked(kListenBand, s, 0));
  ASSERT_TRUE(live(kLmOn, s) && live(kMidFreq, s) && live(kListenBand, s, 1));

  // LISTEN on MID: the other bands dim and lock, their IN included; MID and the rest of the panel stay live
  s = allLive();
  s.listen = 1;
  ASSERT_TRUE(dimmedAndLocked(kLmFreq, s) && dimmedAndLocked(kLmOn, s) && dimmedAndLocked(kHpOn, s));
  ASSERT_TRUE(dimmedAndLocked(kHsGain, s) && dimmedAndLocked(kListenBand, s, 0) && dimmedAndLocked(kListenBand, s, 2));
  ASSERT_TRUE(live(kMidFreq, s) && live(kMidOn, s) && live(kMidQ, s) && live(kListenBand, s, 1));
  ASSERT_TRUE(live(kFineOn, s) && live(kGlueThresh, s));
  // LISTEN counts as off while its bell is off (as in the engine): nothing else dims
  s.bandOn[3] = false;
  ASSERT_TRUE(live(kLmFreq, s) && live(kHpOn, s));

  // SET: LIMIT dims and locks except RELEASE and SET
  s = allLive();
  s.set = true;
  ASSERT_TRUE(dimmedAndLocked(kLimitThresh, s) && dimmedAndLocked(kLimitStyle, s) && dimmedAndLocked(kLimitOn, s));
  ASSERT_TRUE(live(kLimitRelease, s) && live(kSetActive, s) && live(kGlueThresh, s) && live(kFinalEffect, s));
  // SET with LIMIT off: LIMIT's IN stays live (no dead end), SET dims with its stage
  s.stageOn[static_cast<int>(Stage::Limit)] = false;
  ASSERT_TRUE(live(kLimitOn, s) && dimmedAndLocked(kSetActive, s) && dimmedAndLocked(kLimitRelease, s));

  // METALHEAD off: the box dims except its IN
  s = allLive();
  s.stageOn[static_cast<int>(Stage::Metal)] = false;
  ASSERT_TRUE(dimmedAndLocked(kPush, s) && live(kMetalOn, s));
  ASSERT_TRUE(grRowDimmed(4, s) && clipLedDimmed(7, s));
  // the ACT LEDs follow VIBE EQ
  s = allLive();
  ASSERT_TRUE(!actLedDimmed(s));
  s.stageOn[static_cast<int>(Stage::Vibe)] = false;
  ASSERT_TRUE(actLedDimmed(s));

  // uiStateFrom reads the parameters with the plug-in's index conversion
  double norm[kNumParams];
  loadDefaults(norm);
  UiState d = uiStateFrom(norm, -1, false);
  ASSERT_TRUE(!d.bypass && d.listen == -1 && !d.set && d.finalMode == 0);
  for (int i = 0; i < 6; ++i) ASSERT_TRUE(d.stageOn[i]);
  ASSERT_TRUE(!d.stageOn[6]);   // METALHEAD off by default
  for (bool b : d.bandOn) ASSERT_TRUE(b);
  norm[kBypass] = 0.6;          // index 1 of a switch
  norm[kFinalMode] = 0.51;      // index 1
  norm[kMidOn] = 0.4;           // index 0: off
  d = uiStateFrom(norm, 2, true);
  ASSERT_TRUE(d.bypass && d.finalMode == 1 && !d.bandOn[3] && d.listen == 2 && d.set);
  ASSERT_TRUE(uiStateFrom(norm, 7, false).listen == -1 && uiStateFrom(norm, -3, false).listen == -1);
}

void test_final_mode_visibility() {
  UiState s = allLive();
  s.finalMode = 0;   // INFLATE: EFFECT and CURVE
  ASSERT_TRUE(controlVisible(*controlOf(kFinalEffect), s) && controlVisible(*controlOf(kFinalCurve), s));
  ASSERT_TRUE(!controlVisible(*controlOf(kFinalDrive), s) && controlLocked(*controlOf(kFinalDrive), s));
  ASSERT_TRUE(!controlLocked(*controlOf(kFinalEffect), s));
  s.finalMode = 1;   // CLIP: DRIVE
  ASSERT_TRUE(!controlVisible(*controlOf(kFinalEffect), s) && !controlVisible(*controlOf(kFinalCurve), s));
  ASSERT_TRUE(controlVisible(*controlOf(kFinalDrive), s) && !controlLocked(*controlOf(kFinalDrive), s));
  ASSERT_TRUE(controlLocked(*controlOf(kFinalCurve), s));
  for (int mode : {0, 1}) {
    s.finalMode = mode;
    for (const ControlSpec& c : kControls)
      if (!isFinalModeControl(c.id)) ASSERT_TRUE(controlVisible(c, s));
  }
  // the three FINAL knobs belong to FINAL and dim with it
  s.stageOn[static_cast<int>(Stage::Final)] = false;
  ASSERT_TRUE(controlDimmed(*controlOf(kFinalDrive), s) && !controlDimmed(*controlOf(kFinalOn), s));
}

bool hasSourceName(const std::string& text) {
  std::string t = text;
  std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  for (const char* w : {"pultec", "ssl", "airwindows", "dusk"})
    if (t.find(w) != std::string::npos) return true;
  return false;
}

void test_silkscreen_items_cover_controls() {
  using FairyTapes::Ui::PrintedItem;
  using FairyTapes::Ui::PrintKind;
  const std::vector<PrintedItem> items = silkscreenItems();
  std::size_t expected = 0;
  for (const ControlSpec& c : kControls) {
    if (c.kind == Kind::LitChoice || c.kind == Kind::HeadToggle) continue;   // the names are on the faces
    ++expected;
    int found = 0;
    for (const PrintedItem& it : items) {
      if (it.id != static_cast<int>(c.id)) continue;
      const Point p = c.kind == Kind::Knob ? knobCentre(c) : faceCentre(c);
      if (std::fabs(it.cx - p.x) > 0.01 || std::fabs(it.cy - p.y) > 0.01) continue;
      ++found;
      if (c.kind == Kind::Knob) {
        ASSERT_TRUE(it.kind == PrintKind::Knob && it.capRadius == c.capRadius);
        ASSERT_TRUE(it.lo == c.lo && it.hi == c.hi && !it.lo.empty() && !it.hi.empty());
      } else if (c.kind == Kind::Cycle) {
        ASSERT_TRUE(it.kind == PrintKind::Cycle);
      } else {
        ASSERT_TRUE(it.kind == PrintKind::Button);
      }
      ASSERT_TRUE(!it.name.empty() && it.name == c.name);
    }
    if (found != 1) std::fprintf(stderr, "  control %u printed %d times\n", static_cast<unsigned>(c.id), found);
    ASSERT_TRUE(found == 1);
  }
  // MATCH is an action with its label printed above the face; the view buttons carry their names on the face
  int match = 0;
  for (const PrintedItem& it : items)
    if (it.id == actionPrintId(Action::Match) && it.kind == PrintKind::Button && it.name == "MATCH") ++match;
  ASSERT_TRUE(match == 1);
  ASSERT_TRUE(items.size() == expected + 1);

  // no source names anywhere in the front panel text (only the block tags)
  for (const PrintedItem& it : items) ASSERT_TRUE(!hasSourceName(it.name) && !hasSourceName(it.lo) && !hasSourceName(it.hi));
  for (const ControlSpec& c : kControls)
    for (const char* t : {c.name, c.lo, c.hi})
      if (t) ASSERT_TRUE(!hasSourceName(t));
  for (const PanelText& t : kPanelTexts) ASSERT_TRUE(!hasSourceName(t.text));
  for (const BoxSpec& b : kBoxes)
    if (b.title) ASSERT_TRUE(!hasSourceName(b.title));
  for (const ActionSpec& a : kActions) ASSERT_TRUE(!hasSourceName(a.name));
  for (const LitChoiceSpec& l : kLitChoices)
    for (int i = 0; i < l.count; ++i) ASSERT_TRUE(!hasSourceName(l.names[i]));
  for (const ClipLed& c : kClipLeds) ASSERT_TRUE(!hasSourceName(c.label));
  for (Box b : {Box::FineEq, Box::VibeEq, Box::Glue, Box::Clip, Box::Final})
    for (int mode : {0, 1}) ASSERT_TRUE(!hasSourceName(engineTag(b, mode)));
  // and the texts the spec asks for are there
  bool shame = false, level = false, matched = false;
  for (const PanelText& t : kPanelTexts) {
    if (std::strcmp(t.text, "SHAME ON YOU") == 0) shame = t.stage == Stage::Metal;
    if (std::strcmp(t.text, "level") == 0) level = t.stage == Stage::Clip;
    if (std::strcmp(t.text, "matched") == 0) matched = t.stage == Stage::Clip;
  }
  ASSERT_TRUE(shame && level && matched);
}

void test_tape_slots_clear() {
  // A tape is up to 96 x 26 and turned up to 9 degrees: with its torn ends and shadow it reaches 54 px either side of
  // its centre in x and 25 px in y (as in Deep Throat). Every position a slot allows keeps clear of the controls, the
  // screens, the meters, the LEDs and the screws.
  std::vector<Rect> keepClear = fixedReadouts();
  for (const ControlSpec& c : kControls) keepClear.push_back(viewRect(c));
  for (const ActionSpec& a : kActions) keepClear.push_back(viewRect(a));
  for (const auto& s : kScrewPos) keepClear.push_back({s[0] - 12, s[1] - 12, 24, 24});
  ASSERT_TRUE(kNumTapeSlots == 6);
  for (const TapeSlot& t : kTapeSlots) {
    const Rect reach{t.cx - 54 - t.jx, t.cy - 25 - t.jy, 2 * (54 + t.jx), 2 * (25 + t.jy)};
    ASSERT_TRUE(inside(reach, kPanel));
    for (const Rect& r : keepClear) ASSERT_TRUE(!overlap(reach, r));
  }
}

// ---- Value texts (ui/value_text.h) ------------------------------------------------------------------------------------
// A full table of normalized values with HI PASS M/S at `hpMsIndex` (every other parameter at its default).
void norms(double* all, int hpMsIndex) {
  loadDefaults(all);
  all[kHpMs] = normFromIndex(kHpMs, hpMsIndex);
}

void test_value_text_golden() {
  // The texts under the controls: units, signs and the named positions (the controller prints exactly these).
  double all[kNumParams];
  norms(all, 0);
  const auto t = [&](ParamID id, int index) { return formatValue(id, normFromIndex(id, index), all); };
  ASSERT_TRUE(t(kInputGain, 0) == "-12.0 dB" && t(kInputGain, 24) == "+0.0 dB");
  ASSERT_TRUE(t(kCeiling, 1) == "-0.3 dB" && t(kTarget, 3) == "-7 LUFS");
  ASSERT_TRUE(t(kBypass, 1) == "On" && t(kHmOn, 0) == "Off");
  ASSERT_TRUE(t(kLmMs, 1) == "Mid" && t(kLmMs, 2) == "Side" && t(kLmQ, 3) == "Surgical");
  ASSERT_TRUE(t(kMidFreq, 4) == "1 kHz" && t(kLmFreq, 4) == "250 Hz" && t(kHsFreq, 6) == "20 kHz");
  ASSERT_TRUE(t(kGlueRelease, 4) == "Auto" && t(kGlueRelease, 1) == "0.3 s" && t(kGlueRatio, 2) == "4:1");
  ASSERT_TRUE(t(kGlueMix, 10) == "50 %" && t(kGlueThresh, 0) == "-20 dB" && t(kGlueAttack, 0) == "0.1 ms");
  ASSERT_TRUE(t(kFinalMode, 0) == "Inflate" && t(kFinalMode, 1) == "Clip");
  ASSERT_TRUE(t(kLimitStyle, 2) == "Dyn" && t(kLimitRelease, 4) == "Style" && t(kLimitRelease, 6) == "+2" &&
              t(kLimitRelease, 0) == "-4");
  ASSERT_TRUE(t(kLoudness, 7) == "3.5" && t(kTilt, 0) == "-3.0 dB" && t(kTube, 2) == "2");
  ASSERT_TRUE(t(kOversampling, 0) == "Off" && t(kOversampling, 2) == "4x");
  ASSERT_TRUE(t(kGlueScHpf, 0) == "Off" && t(kGlueScHpf, 3) == "150 Hz");
  // an out-of-range norm is clamped, an unknown id gives nothing
  ASSERT_TRUE(formatValue(kInputGain, 7.0, all) == "+12.0 dB" && formatValue(kInputGain, -1.0, all) == "-12.0 dB");
  ASSERT_TRUE(formatValue(kNumParams, 0.0, all).empty());
}

void test_value_text_hp_side_scale() {
  // HI PASS FREQ reads the scale of its own M/S: SIDE has the mono-bass scale (the ledger item of engine Task 13).
  double st[kNumParams], side[kNumParams];
  norms(st, 0);
  norms(side, 2);
  ASSERT_TRUE(formatValue(kHpFreq, 1.0, st) == "40 Hz");
  ASSERT_TRUE(formatValue(kHpFreq, 1.0, side) == "200 Hz");
  ASSERT_TRUE(formatValue(kHpFreq, 0.0, side) == "Off" && formatValue(kHpFreq, normFromIndex(kHpFreq, 1), side) == "60 Hz");
  // no table of norms: the normal scale
  ASSERT_TRUE(formatValue(kHpFreq, 1.0, nullptr) == "40 Hz");
  // typing "60" on SIDE picks the 60 Hz step (index 1); on ST the nearest of 20..40 is the last step
  double v = -1.0;
  ASSERT_TRUE(parseValue(kHpFreq, "60", side, v) && indexFromNorm(kHpFreq, v) == 1);
  ASSERT_TRUE(parseValue(kHpFreq, "60", st, v) && indexFromNorm(kHpFreq, v) == 5);
  ASSERT_TRUE(parseValue(kHpFreq, "160", side, v) && indexFromNorm(kHpFreq, v) == 4);
  ASSERT_TRUE(parseValue(kHpFreq, "80 Hz", side, v) && indexFromNorm(kHpFreq, v) == 2);
  ASSERT_TRUE(parseValue(kHpFreq, "35", st, v) && indexFromNorm(kHpFreq, v) == 4);
  // the named text is read on the scale in use too: "200 hz" exists only on SIDE
  ASSERT_TRUE(parseValue(kHpFreq, "200 hz", side, v) && indexFromNorm(kHpFreq, v) == 5);
  ASSERT_TRUE(parseValue(kHpFreq, "Off", side, v) && indexFromNorm(kHpFreq, v) == 0);
}

void test_value_text_parse_roundtrip() {
  // parse(format(v)) == v for every step of every parameter (HI PASS on both its scales)
  for (int ms = 0; ms < 3; ++ms) {
    double all[kNumParams];
    norms(all, ms);
    for (int i = 0; i < static_cast<int>(kNumParams); ++i) {
      const auto id = static_cast<ParamID>(i);
      for (int s = 0; s < kParams[id].steps; ++s) {
        const double v = normFromIndex(id, s);
        const std::string text = formatValue(id, v, all);
        ASSERT_TRUE(!text.empty());
        double back = -1.0;
        ASSERT_TRUE(parseValue(id, text, all, back));
        ASSERT_TRUE(back == v);
      }
    }
  }
  double all[kNumParams];
  norms(all, 0);
  double v = -1.0;
  // case and spaces are ignored; a bare number picks the nearest step
  ASSERT_TRUE(parseValue(kLmQ, "  SURGICAL ", all, v) && indexFromNorm(kLmQ, v) == 3);
  ASSERT_TRUE(parseValue(kInputGain, "-3", all, v) && indexFromNorm(kInputGain, v) == 18);
  ASSERT_TRUE(parseValue(kInputGain, "+3.0 dB", all, v) && indexFromNorm(kInputGain, v) == 30);
  ASSERT_TRUE(parseValue(kInputGain, "99", all, v) && indexFromNorm(kInputGain, v) == 48);
  // text that is no value gives false and leaves the result alone
  v = 0.25;
  ASSERT_TRUE(!parseValue(kLmQ, "bogus", all, v) && v == 0.25);
  ASSERT_TRUE(!parseValue(kInputGain, "", all, v) && !parseValue(kInputGain, "dB", all, v));
  ASSERT_TRUE(!parseValue(kInputGain, "nan", all, v) && !parseValue(kInputGain, "inf", all, v));
  ASSERT_TRUE(!parseValue(kBypass, "3", all, v) && !parseValue(kLmMs, "7", all, v));   // no value table: names only
  ASSERT_TRUE(!parseValue(kNumParams, "1", all, v) && v == 0.25);
}

// ---- Readouts (ui/readouts.h) -----------------------------------------------------------------------------------------
void test_lufs_readouts() {
  const std::string MINUS = "\xE2\x88\x92", DASH = "\xE2\x80\x94";
  ASSERT_TRUE(MINUS == kMinusSign && DASH == kEmDash && MINUS.size() == 3 && DASH.size() == 3);
  const double none = -std::numeric_limits<double>::infinity();
  ASSERT_TRUE(lufsText(none) == DASH && lufsText(std::nan("")) == DASH);
  ASSERT_TRUE(lufsText(-14.04) == MINUS+"14.0" && lufsText(-14.2) == MINUS+"14.2");
  ASSERT_TRUE(lufsText(-9.96) == MINUS+"10.0" && lufsText(-0.04) == "0.0" && lufsText(0.0) == "0.0");
  // TO TARGET: signed gap, one digit; the target is the TARGET choice (-14, -11, -9, -7)
  ASSERT_TRUE(toTargetText(none, 0) == DASH && toTargetText(std::nan(""), 0) == DASH);
  ASSERT_TRUE(toTargetText(-13.6, 0) == "+0.4" && toTargetText(-14.3, 0) == MINUS+"0.3");
  ASSERT_TRUE(toTargetText(-14.0, 0) == "0.0" && toTargetText(-14.02, 0) == "0.0");
  ASSERT_TRUE(toTargetText(-11.0, 1) == "0.0" && toTargetText(-12.0, 2) == MINUS+"3.0" && toTargetText(-6.0, 3) == "+1.0");
  // the LED is green within 0.5 LU of the target (the limit included), dark without a measurement
  ASSERT_TRUE(targetLedOn(-13.6, 0) && !targetLedOn(-13.4, 0));
  ASSERT_TRUE(targetLedOn(-14.4, 0) && !targetLedOn(-14.6, 0));
  ASSERT_TRUE(targetLedOn(-13.5, 0) && targetLedOn(-14.5, 0));
  ASSERT_TRUE(targetLedOn(-9.1, 2) && !targetLedOn(-9.6, 2) && targetLedOn(-7.0, 3));
  ASSERT_TRUE(!targetLedOn(none, 0) && !targetLedOn(std::nan(""), 0));
  // an index outside the choices is clamped, never read out of the table
  ASSERT_TRUE(toTargetText(-14.0, -3) == "0.0" && toTargetText(-7.0, 99) == "0.0");
}

void test_release_text_follows_tuning() {
  // real milliseconds: the base of the style times 2^((index - 4) / 3), as the limiter computes it
  ASSERT_TRUE(releaseText(0, 4) == "50 ms" && releaseText(1, 4) == "100 ms" && releaseText(2, 4) == "250 ms");
  ASSERT_TRUE(releaseText(0, 7) == "100 ms" && releaseText(0, 1) == "25 ms" && releaseText(1, 7) == "200 ms" && releaseText(2, 1) == "125 ms");
  // the bases come from tuning.h: a copy with another base gives another text, the other styles stay
  double bases[5];
  for (int i = 0; i < 5; ++i) bases[i] = Tuning::kLimitReleaseBaseMs[i];
  ASSERT_TRUE(Ui::detail::releaseTextFrom(bases, 5, 0, 4) == releaseText(0, 4));
  bases[0] = 80.0;
  ASSERT_TRUE(Ui::detail::releaseTextFrom(bases, 5, 0, 4) == "80 ms");
  ASSERT_TRUE(Ui::detail::releaseTextFrom(bases, 5, 0, 4) != releaseText(0, 4));
  ASSERT_TRUE(Ui::detail::releaseTextFrom(bases, 5, 1, 4) == releaseText(1, 4));
  // style and index out of range are clamped
  ASSERT_TRUE(releaseText(-1, 4) == releaseText(0, 4) && releaseText(99, 4) == releaseText(4, 4));
  ASSERT_TRUE(releaseText(0, -5) == releaseText(0, 0) && releaseText(0, 50) == releaseText(0, 8));
}

} // namespace

int main() {
  RUN_TEST(test_editor_size);
  RUN_TEST(test_layout_binds_every_param_once);
  RUN_TEST(test_layout_rects_inside);
  RUN_TEST(test_layout_no_control_overlap);
  RUN_TEST(test_in_buttons_top_right);
  RUN_TEST(test_titles_bottom_and_tags);
  RUN_TEST(test_dimming_rules);
  RUN_TEST(test_final_mode_visibility);
  RUN_TEST(test_silkscreen_items_cover_controls);
  RUN_TEST(test_tape_slots_clear);
  RUN_TEST(test_value_text_golden);
  RUN_TEST(test_value_text_hp_side_scale);
  RUN_TEST(test_value_text_parse_roundtrip);
  RUN_TEST(test_lufs_readouts);
  RUN_TEST(test_release_text_follows_tuning);
  std::printf("%d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
