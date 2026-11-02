/**
 * Fairy Tapes · The Stripper — editor layout table and dimming rules (header-only, no VSTGUI).
 */

#include "ui/layout.h"

#include "fairy_ui/rack_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>
#include <string>

using namespace FairyTapes::TheStripper;
using namespace FairyTapes::TheStripper::Ui;

namespace {
int g_failed = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr); ++g_failed; } } while (0)

bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
bool inside(const Rect& inner, const Rect& outer) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
         inner.y + inner.h <= outer.y + outer.h;
}

void test_controls_are_unique_and_valid() {
  std::set<unsigned> seen;
  for (const auto& c : kControls) {
    CHECK(c.id < kNumParams);
    CHECK(seen.insert(c.id).second);   // one control per parameter
    CHECK(c.rect.w > 0 && c.rect.h > 0);
  }
}

void test_only_inert_parameters_are_unbound() {
  // Gain staging is out of scope (inert); the filters share one IN button (kBypassLPHP), the per-filter switches stay at 1.
  const std::set<unsigned> allowed = {kBypassGainStaging, kGainStagingTrim, kEqHPFOn, kEqLPFOn};
  std::set<unsigned> bound;
  for (const auto& c : kControls) bound.insert(c.id);
  for (unsigned id = 0; id < kNumParams; ++id) CHECK(bound.count(id) || allowed.count(id));
  for (unsigned id : allowed) CHECK(!bound.count(id));
}

void test_controls_fit_window_and_their_box() {
  const Rect window{0, 0, kEditorWidth, kEditorHeight};
  for (const auto& c : kControls) {
    CHECK(inside(c.rect, window));
    if (c.module == Module::None) continue;
    const BoxSpec* box = boxOf(c.module);
    CHECK(box != nullptr);
    if (box) CHECK(inside(c.rect, box->rect));
  }
  for (const auto& b : kBoxes) CHECK(inside(b.rect, window));
}

void test_controls_do_not_overlap() {
  const int n = static_cast<int>(sizeof(kControls) / sizeof(kControls[0]));
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j) {
      CHECK(!overlaps(kControls[i].rect, kControls[j].rect));
    }
}

void test_bottom_margin() {
  double bottom = 0;
  for (const auto& b : kBoxes) bottom = std::max(bottom, b.rect.y + b.rect.h);
  CHECK(kEditorHeight - bottom >= 28);   // about 30 px under the last row of boxes (spec, Spazio)
}

void test_in_buttons_polarity() {
  // bypass parameters are inverted (IN lit = bypass 0); the filter switches are already positive
  CHECK(inButtonOf(Module::Eq) && inButtonOf(Module::Eq)->inverted);
  CHECK(inButtonOf(Module::Filters) && inButtonOf(Module::Filters)->inverted);   // one IN for both filters
  CHECK(inButtonOf(Module::Filters)->id == kBypassLPHP);
  CHECK(inButtonOf(Module::Input) == nullptr);
  for (const auto& c : kControls) {
    if (c.kind != Kind::Toggle || !c.inverted) continue;
    CHECK(c.id >= kBypassLPHP && c.id <= kBypassGate);   // only the kBypass* parameters are inverted
  }
}

void test_ui_value_mapping() {
  CHECK(uiValue(true, 0.0) == 1.0);
  CHECK(uiValue(true, 1.0) == 0.0);
  CHECK(uiValue(false, 0.25) == 0.25);
  CHECK(paramValue(true, 1.0) == 0.0);
  CHECK(paramValue(true, 0.0) == 1.0);
  CHECK(paramValue(false, 0.7) == 0.7);
}

void test_module_dimming() {
  ModuleState on;   // every IN lit, global bypass off
  for (const auto& m : kModules) CHECK(!moduleDimmed(m, on));

  ModuleState eqOff = on;
  eqOff.in[static_cast<int>(Module::Eq)] = false;
  CHECK(moduleDimmed(Module::Eq, eqOff));
  CHECK(!moduleDimmed(Module::Comp, eqOff));

  ModuleState byp = on;
  byp.globalBypass = true;
  for (const auto& m : kModules) CHECK(moduleDimmed(m, byp));   // global bypass dims every module, even Input/Output

  // A module without an IN button follows the global bypass only
  ModuleState noIn = on;
  noIn.in[static_cast<int>(Module::Input)] = false;
  CHECK(!moduleDimmed(Module::Input, noIn));
}

void test_mode_visibility() {
  // Voice/Drums/Classic exist only in Channel mode
  CHECK(visibleInMode(kCompressorMode, true));
  CHECK(!visibleInMode(kCompressorMode, false));
  CHECK(visibleInMode(kCompressAmount, false));
  CHECK(visibleInMode(kEqLFGain, false));
}
void test_meter_band_fits_level_box() {
  const BoxSpec* level = boxOf(Module::Level);
  CHECK(level != nullptr);
  for (const auto& row : kMeterRows) {
    CHECK(row.id >= kMeterBase && row.id < kNumMeterIDs);
    CHECK(inside(meterBarRect(row), level->rect));
  }
  for (int i = 0; i < kClipCount; ++i) {
    CHECK(kClips[i].id >= kClipInput && kClips[i].id <= kClipOutput);
    CHECK(inside(clipRect(i), level->rect));
    for (int j = i + 1; j < kClipCount; ++j) CHECK(!overlaps(clipRect(i), clipRect(j)));
  }
  // rows must not touch each other
  const int n = static_cast<int>(sizeof(kMeterRows) / sizeof(kMeterRows[0]));
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) CHECK(!overlaps(meterBarRect(kMeterRows[i]), meterBarRect(kMeterRows[j])));
    for (int c = 0; c < kClipCount; ++c) CHECK(!overlaps(meterBarRect(kMeterRows[i]), clipRect(c)));
  }
}

void test_drive_has_two_engines_in_series() {
  // NAM then TAPE, each with its own knob and its own on/off switch (type 2: label above, red LED), left of the module IN
  const ControlSpec* drive = nullptr; const ControlSpec* tape = nullptr; const ControlSpec* namSw = nullptr;
  const ControlSpec* tapeSw = nullptr; const ControlSpec* in = inButtonOf(Module::Drive);
  for (const auto& c : kControls) {
    if (c.id == kDriveAmount) drive = &c;
    if (c.id == kDriveTapeAmount) tape = &c;
    if (c.id == kDriveNamOn) namSw = &c;
    if (c.id == kDriveTapeOn) tapeSw = &c;
  }
  CHECK(drive && tape && namSw && tapeSw && in);
  if (!(drive && tape && namSw && tapeSw && in)) return;
  CHECK(drive->kind == Kind::Knob && tape->kind == Kind::Knob && drive->module == Module::Drive && tape->module == Module::Drive);
  CHECK(std::string(drive->label) == "DRIVE" && std::string(tape->label) == "TAPE");
  CHECK(drive->rect.x < tape->rect.x);   // NAM first, as in the signal path
  for (const ControlSpec* sw : {namSw, tapeSw}) {
    CHECK(sw->kind == Kind::Toggle && sw->module == Module::Drive && !sw->isIn && !sw->inverted);   // 1 = engine on = lit
    CHECK(sw->rect.y == in->rect.y && sw->rect.x < in->rect.x);                                   // same row, left of the IN
  }
  CHECK(std::string(namSw->label) == "NAM" && std::string(tapeSw->label) == "TAPE" && namSw->rect.x < tapeSw->rect.x);
  CHECK(choiceLookOf(kDriveNamOn) == nullptr);   // no longer an exclusive choice
  // no knob shares a view any more: the 4K saturation is back in the EQ
  for (const auto& c : kControls) CHECK(!c.sharesView);
  // an engine's knob dims when its switch is off; the NAM's also without a model (it passes the signal through)
  CHECK(!engineKnobDimmed(kDriveAmount, true, true, true));
  CHECK(engineKnobDimmed(kDriveAmount, false, true, true) && engineKnobDimmed(kDriveAmount, true, true, false));
  CHECK(!engineKnobDimmed(kDriveTapeAmount, false, true, false) && engineKnobDimmed(kDriveTapeAmount, true, false, true));
  CHECK(!engineKnobDimmed(kEqSaturation, false, false, false) && !engineKnobDimmed(kLimiterPound, false, false, false));
  CHECK(stripDimmed(false) && !stripDimmed(true));
  // the model strip sits right of the TAPE knob, inside the Drive box
  CHECK(!overlaps(kModelStrip, tape->rect) && !overlaps(kModelStrip, tapeSw->rect) && inside(kModelStrip, boxOf(Module::Drive)->rect));
}

void test_saturate_is_the_last_eq_knob() {
  const ControlSpec* sat = nullptr;
  double lastBand = 0;
  for (const auto& c : kControls) {
    if (c.id == kEqSaturation) sat = &c;
    else if (c.module == Module::Eq && c.kind == Kind::Knob) lastBand = std::max(lastBand, c.rect.x);
  }
  CHECK(sat && sat->module == Module::Eq && sat->kind == Kind::Knob && std::string(sat->label) == "SATURATE");
  if (sat) CHECK(sat->rect.x > lastBand && sat->rect.x < inButtonOf(Module::Eq)->rect.x);
  CHECK(knobLookOf(kEqSaturation) && knobLookOf(kEqSaturation)->radius == 22);
}

void test_block_tags() {
  using std::string;
  CHECK(string(blockTag(Module::Eq, true, 0, true, true)) == "4K-G");
  CHECK(string(blockTag(Module::Eq, false, 0, true, true)) == "4K-E");
  CHECK(string(blockTag(Module::Filters, true, 0, true, true)) == "4K-G" && string(blockTag(Module::Filters, false, 0, true, true)) == "4K-E");
  CHECK(string(blockTag(Module::Comp, true, 0.0, true, true)) == "OPTO-2A");
  CHECK(string(blockTag(Module::Comp, true, 0.5, true, true)) == "FET-76");
  CHECK(string(blockTag(Module::Comp, true, 1.0, true, true)) == "VCA-160");
  CHECK(string(blockTag(Module::Comp, false, 0.5, true, true)) == "4K-G");     // Mix bus: the Bus compressor
  // the Drive tag names the engines that are on
  CHECK(string(blockTag(Module::Drive, true, 0, true, true)) == "NAM \xC2\xB7 TAPE");
  CHECK(string(blockTag(Module::Drive, false, 0, true, false)) == "NAM");
  CHECK(string(blockTag(Module::Drive, true, 0, false, true)) == "TAPE");
  CHECK(string(blockTag(Module::Drive, true, 0, false, false)).empty());
  CHECK(string(blockTag(Module::Limiter, true, 0, true, true)) == "B-PARTY");
  CHECK(string(blockTag(Module::Gate, true, 0, true, true)) == "D-NOISE");
  CHECK(string(blockTag(Module::Input, true, 0, true, true)).empty() && string(blockTag(Module::Output, true, 0, true, true)).empty());
}

// The printed ends of the gate THRESH scale say where DeNoise really closes (2026-11-02): -inf ... -48 dB, the same numbers as
// the host text at 0 and 1 (before: -inf ... 0).
void test_gate_scale_ends_match_the_threshold_text() {
  const KnobLook* g = knobLookOf(kGateThreshold);
  CHECK(g != nullptr);
  if (!g) return;
  CHECK(std::string(g->lo) == "-\xE2\x88\x9E");
  CHECK(std::string(g->hi) == "-48");
  CHECK(gateThresholdText(1.0) == std::string(g->hi) + " dB");
  CHECK(gateThresholdText(0.0) == "-inf dB");
}

void test_look_tables() {
  // every knob has an end-value look
  for (const auto& c : kControls) {
    if (c.kind != Kind::Knob) continue;
    const KnobLook* look = knobLookOf(c.id);
    CHECK(look != nullptr);
    if (look) CHECK(look->radius >= 15 && look->radius <= 26 && look->lo && look->hi);
  }
  // exclusive choices have the name on the face (design system, 2026-10-21): a look for each, faces where the preview puts them
  for (const auto& c : kControls)
    if (c.kind == Kind::Choice) CHECK(choiceLookOf(c.id) != nullptr);
  auto centre = [](Steinberg::Vst::ParamID id, int i) {
    const ControlSpec* c = nullptr;
    for (const auto& k : kControls) if (k.id == id) c = &k;
    const ChoiceLook* l = choiceLookOf(id);
    int n = 0;
    while (n < 3 && c->choices[n]) ++n;
    const auto f = FairyTapes::Ui::Rack::litFace(FairyTapes::Ui::Rack::litChoiceCell(c->rect.w, c->rect.h, n, l->columns, i),
                                                 l->faceWidth, FairyTapes::Ui::Rack::kLitFaceHeight);
    return std::pair<double, double>{c->rect.x + (f.left + f.right) / 2, c->rect.y + (f.top + f.bottom) / 2};
  };
  CHECK(centre(kStripMode, 0) == std::make_pair(940.0, 198.0) && centre(kStripMode, 1) == std::make_pair(940.0, 224.0));
  CHECK(centre(kCompressorMode, 0) == std::make_pair(262.0, 498.0) && centre(kCompressorMode, 2) == std::make_pair(262.0, 550.0));
  CHECK(choiceLookOf(kCompressorMode)->faceWidth == 44 && choiceLookOf(kStripMode)->faceWidth == 34);
  CHECK(isBellButton(kEqLFBell) && isBellButton(kEqHFBell) && !isBellButton(kBypass));
  // the Bell filter exists only in the Black (Channel) EQ: in Bus mode the buttons are dimmed and inert, every other control stays in use
  CHECK(controlInUse(kEqLFBell, true) && controlInUse(kEqHFBell, true));
  CHECK(!controlInUse(kEqLFBell, false) && !controlInUse(kEqHFBell, false));
  CHECK(controlInUse(kEqLFGain, false) && controlInUse(kDriveAmount, false));
}

void test_meter_dimming() {
  // global bypass dims GR and the EQ/CMP/DRV/LIM clip LEDs; the level meters and the IN/OUT LEDs stay lit
  for (const auto& row : kMeterRows) {
    const bool gr = row.id == kMeterGrComp || row.id == kMeterGrLimiter || row.id == kMeterGrGate;
    CHECK(meterDimmed(row.alwaysLit, true) == !row.alwaysLit);
    CHECK(row.alwaysLit == !gr);
    CHECK(!meterDimmed(row.alwaysLit, false));
  }
  CHECK(kClips[0].alwaysLit && kClips[5].alwaysLit);
  for (int i = 1; i < 5; ++i) CHECK(!kClips[i].alwaysLit);
}
void test_gate_row_sits_between_gain_reduction_and_clip() {
  const MeterRow* limiter = nullptr; const MeterRow* gate = nullptr;
  for (const auto& r : kMeterRows) {
    if (r.id == kMeterGrLimiter) limiter = &r;
    if (r.id == kMeterGrGate) gate = &r;
  }
  CHECK(limiter && gate);
  if (!(limiter && gate)) return;
  CHECK(gate->gainReduction && !gate->alwaysLit && std::string(gate->tag) == "G");
  CHECK(gate->cy > limiter->cy);
  CHECK(gate->cy < kClipRowY - 8);   // above the clip LEDs
  CHECK(kGrGateMaxDb == 40.0);
}

void test_gr_bar_norm() {
  CHECK(grBarNorm(0.0, 20) == 0.0);
  CHECK(grBarNorm(std::nan(""), 20) == 0.0);
  CHECK(std::fabs(grBarNorm(0.9, 20) - 1.0) < 1e-9);          // 20 dB on a 20 dB scale
  CHECK(std::fabs(grBarNorm(1.0 - std::pow(10.0, -10.0 / 20.0), 20) - 0.5) < 1e-9);   // 10 dB -> half
  CHECK(grBarNorm(0.99, 20) == 1.0);                           // beyond the scale: full
  CHECK(grBarNorm(0.5, 0) == 0.0);
}
} // namespace

int main() {
  test_controls_are_unique_and_valid();
  test_only_inert_parameters_are_unbound();
  test_controls_fit_window_and_their_box();
  test_controls_do_not_overlap();
  test_bottom_margin();
  test_in_buttons_polarity();
  test_ui_value_mapping();
  test_module_dimming();
  test_mode_visibility();
  test_meter_band_fits_level_box();
  test_drive_has_two_engines_in_series();
  test_saturate_is_the_last_eq_knob();
  test_block_tags();
  test_look_tables();
  test_gate_scale_ends_match_the_threshold_text();
  test_meter_dimming();
  test_gr_bar_norm();
  test_gate_row_sits_between_gain_reduction_and_clip();
  if (g_failed) { std::fprintf(stderr, "%d check(s) failed\n", g_failed); return 1; }
  std::puts("test_layout: ok");
  return 0;
}
