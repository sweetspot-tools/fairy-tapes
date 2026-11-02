// Deep Throat GUI: the plugin editor. Layout from the approved rack preview (docs/mockups/deep_throat_rack.html,
// ui/layout.h); structure from Pillow Talk's editor (layers, one control per parameter, Parameter dependents applied on
// the UI timer) and The Stripper's typed values.
#include "ui/editor.h"

#include "controller.h"
#include "state.h"
#include "ui/back_modes.h"
#include "ui/back_settings.h"
#include "ui/decor.h"
#include "ui/dynamic_text.h"
#include "ui/panel_view.h"
#include "ui/readout_view.h"
#include "ui/scope_view.h"
#include "ui/screw_angles.h"
#include "ui/tape_view.h"
#include "ui/tapes.h"
#include "ui/value_text.h"

#include "fairy_ui/back_view.h"
#include "fairy_ui/back_widgets.h"
#include "fairy_ui/controls.h"
#include "fairy_ui/draw_util.h"
#include "fairy_ui/knob_math.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/sanitize.h"
#include "fairy_ui/screw_view.h"
#include "fairy_ui/silkscreen_view.h"
#include "fairy_ui/tape_draw.h"
#include "fairy_ui/wear_view.h"

#include "base/source/updatehandler.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/events.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace FairyTapes::DeepThroat::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::BackContent;
using FairyTapes::Ui::BackItemKind;
using FairyTapes::Ui::BackView;
using FairyTapes::Ui::ClipLed;
using FairyTapes::Ui::MeterBar;
using FairyTapes::Ui::MeterKind;
using FairyTapes::Ui::NormalizedControl;
using FairyTapes::Ui::RackCycle;
using FairyTapes::Ui::RackKnob;
using FairyTapes::Ui::RackLed;
using FairyTapes::Ui::RackLitChoice;
using FairyTapes::Ui::RackToggle;
using FairyTapes::Ui::ScrewView;
using FairyTapes::Ui::SilkscreenView;
using FairyTapes::Ui::TapeAlign;
using FairyTapes::Ui::TextLink;
using FairyTapes::Ui::WearMark;
using FairyTapes::Ui::WearView;
using Steinberg::Vst::ParamID;

namespace {

static_assert(kNumParams <= 32, "pending_ holds one bit per persistent parameter");
static_assert(kMaxTapeTexts == kNumTapeSlots, "one stored tape text per tape slot");
static_assert(Editor::kNumScrewViews == kNumScrews, "one view per stored screw angle");
static_assert(kNumScrews == static_cast<int>(sizeof(kScrewPos) / sizeof(kScrewPos[0])), "one stored angle per screw");

constexpr uint32_t kTimerMs = 30;
constexpr double kTimerSeconds = kTimerMs / 1000.0;   // the step of the meters' peak hold
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);
constexpr int32_t kValueEditTag = 9100;   // outside the parameter ids
constexpr int32_t kTapeEditTag = 9000;    // + slot; outside the parameter ids
constexpr CCoord kTapeEditMinWidth = 120, kTapeEditHeight = 24;
constexpr uint32_t kBackWearSalt = 0x77u;   // the back's wear: another pattern from the same seed (the preview's 0x77)

// The text field of a typed value or of a tape (The Stripper's pattern): Escape cancels, Enter or leaving the field
// confirms.
// CTextEdit itself would still report the typed text when Escape comes through the platform field.
class ValueEdit : public CTextEdit {
public:
  using CTextEdit::CTextEdit;
  bool cancelled = false;

  void onKeyboardEvent(KeyboardEvent& event) override {
    if (event.type == EventType::KeyDown && event.virt == VirtualKey::Escape) cancelled = true;
    CTextEdit::onKeyboardEvent(event);
  }

protected:
  ~ValueEdit() noexcept override = default;
  void platformOnKeyboardEvent(KeyboardEvent& event) override {
    if (event.type == EventType::KeyDown && event.virt == VirtualKey::Escape) cancelled = true;
    CTextEdit::platformOnKeyboardEvent(event);
  }
};

CRect toCRect(const Rect& r) { return CRect(r.x, r.y, r.x + r.w, r.y + r.h); }

constexpr uint32_t bit(ParamID id) { return 1u << id; }

double defaultNorm(Steinberg::Vst::EditController* ctl, ParamID id) {
  const auto* p = ctl->getParameterObject(id);
  return p ? p->getInfo().defaultNormalizedValue : 0.0;
}

double controllerNorm(Steinberg::Vst::EditController* ctl, ParamID id) {
  return FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(id), defaultNorm(ctl, id));
}

// A display parameter (hidden, read-only, written by the processor): its normalized value, `fallback` when not finite.
double displayNorm(Steinberg::Vst::EditController* ctl, ParamID id, double fallback) {
  return FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(id), fallback);
}

// What a host string holds, ASCII (the parsers read numbers and units only; anything else becomes '?').
void toString128(const std::string& in, Steinberg::Vst::String128 out) {
  std::size_t i = 0;
  for (; i < in.size() && i < 127; ++i) {
    const auto c = static_cast<unsigned char>(in[i]);
    out[i] = static_cast<Steinberg::Vst::TChar>(c < 0x80 ? c : '?');
  }
  out[i] = 0;
}

// RE-ROLL: the decor with a new seed (new wear, tapes and screws), the screws' rotations back to 0 and the tapes blank.
// A seed of 0 or equal to the current one is replaced by another one (as Pillow Talk's rerolledDecor).
Decor rerolledDecor(Decor d, uint32_t newSeed) {
  if (newSeed == 0 || newSeed == d.seed) newSeed = (d.seed ^ 0x9E3779B9u) * 2654435761u + 0x7F4A7C15u;
  if (newSeed == 0 || newSeed == d.seed) newSeed = d.seed + 1u ? d.seed + 1u : 1u;
  d.seed = newSeed;
  for (float& a : d.screwAngles) a = 0.f;
  d.tapeTexts.clear();
  return d;
}

// ---- The MODES tab of the back (ui/back_modes.h), drawn in the template's custom area ----
constexpr CColor kModesYellow(0xf2, 0xc2, 0x1b);   // the active mode column (the preview's YEL)

void modesText(CDrawContext* ctx, const std::string& s, double x, double baseline, double size, const CColor& colour,
               bool centred = false) {
  if (s.empty()) return;
  ctx->setFont(makeOwned<CFontDesc>("Arial", size, kBoldFace));
  ctx->setFontColor(colour);
  const double w = centred ? ctx->getStringWidth(s.c_str()) : 0.0;
  ctx->drawString(s.c_str(), CPoint(x - w / 2, baseline));
}

void modesRule(CDrawContext* ctx, double x0, double x1, double y, double width, const CColor& colour) {
  ctx->setFrameColor(colour);
  ctx->setLineWidth(width);
  ctx->setLineStyle(kLineSolid);
  ctx->drawLine(CPoint(x0, y), CPoint(x1, y));
}

// `area` is the template's custom box (Back::kCustomBox) in view coordinates; the table is placed at the preview's
// editor px relative to it. The column of `activeColumn` (activeModeColumn) is yellow, the others white.
void drawModesTab(CDrawContext* ctx, const CRect& area, int activeColumn) {
  using FairyTapes::Ui::kDimWhite;
  using FairyTapes::Ui::kWhite;
  namespace M = ModesTab;
  const double ox = area.left - FairyTapes::Ui::Back::kCustomBox.x, oy = area.top - FairyTapes::Ui::Back::kCustomBox.y;
  FairyTapes::Ui::drawTitledBox(ctx, CRect(ox + M::kBox.x, oy + M::kBox.y, ox + M::kBox.x + M::kBox.w, oy + M::kBox.y + M::kBox.h),
                                M::kTitle, true);
  const auto intro = modesIntro();
  if (intro.size() >= 2) {
    modesText(ctx, intro[0], ox + M::kLabelX, oy + M::kIntroBaseline[0], 12, kWhite);
    modesText(ctx, intro[1], ox + M::kLabelX, oy + M::kIntroBaseline[1], 11, kDimWhite);
  }
  auto colourOf = [activeColumn](int j) { return j == activeColumn ? kModesYellow : kWhite; };
  const auto head = modesHeader();
  for (int j = 0; j < 3; ++j)
    modesText(ctx, head[static_cast<std::size_t>(j)], ox + M::kColumnX[j], oy + M::kHeadBaseline, M::kHeadSize, colourOf(j), true);
  CColor rule = kWhite;
  rule.alpha = 153;   // opacity .6
  modesRule(ctx, ox + M::kLabelX, ox + M::kRight, oy + M::kRuleY, 1.5, rule);
  CColor sep = kDimWhite;
  sep.alpha = 64;   // opacity .25
  const auto rows = modesRows();
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const double y = oy + M::kFirstRowBaseline + M::kRowStep * static_cast<double>(i);
    modesText(ctx, rows[i].label, ox + M::kLabelX, y, M::kRowSize, kWhite);
    for (int j = 0; j < 3; ++j)
      modesText(ctx, rows[i].cells[static_cast<std::size_t>(j)], ox + M::kColumnX[j], y, M::kRowSize, colourOf(j), true);
    if (i + 1 < rows.size()) modesRule(ctx, ox + M::kLabelX, ox + M::kRight, y + M::kSeparatorBelow, 1, sep);
  }
  const auto notes = modesNotes();
  for (std::size_t i = 0; i < notes.size(); ++i)
    modesText(ctx, notes[i], ox + M::kLabelX, oy + M::kNoteFirstBaseline + M::kNoteStep * static_cast<double>(i), M::kNoteSize,
              kDimWhite);
}

bool dynamicTextInput(ParamID id) {
  return id == kMode || id == kEqAmount || id == kTone || id == kCompRatio || id == kCompDist || id == kCompHpf;
}

} // namespace

Editor::Editor(Steinberg::Vst::EditController* controller)
: VSTGUIEditor(controller), owner_(dynamic_cast<Controller*>(controller)) {
  const PixelSize px = scaledEditorSize(scale_);
  setRect(Steinberg::ViewRect(0, 0, px.width, px.height));
  // Parameter dependents need the global update handler (VST3Editor creates it the same way).
  Steinberg::UpdateHandler::instance();
}

Editor::~Editor() {
  if (frame) close();
  unwatchParameters();
}

Steinberg::tresult PLUGIN_API Editor::isPlatformTypeSupported(Steinberg::FIDString type) {
#if SMTG_OS_LINUX
  // X11 embedding only: a Wayland surface would need host-provided Wayland interfaces.
  if (type && std::strcmp(type, Steinberg::kPlatformTypeWaylandSurfaceID) == 0) return Steinberg::kInvalidArgument;
#endif
  return VSTGUIEditor::isPlatformTypeSupported(type);
}

Steinberg::tresult PLUGIN_API Editor::checkSizeConstraint(Steinberg::ViewRect* rect) {
  if (!rect) return Steinberg::kInvalidArgument;
  const PixelSize px = scaledEditorSize(scale_);
  rect->right = rect->left + px.width;
  rect->bottom = rect->top + px.height;
  return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor factor) {
#if SMTG_OS_MACOS
  (void)factor;   // Cocoa backing scale handles Retina; VST3Editor keeps 1 there as well
  return Steinberg::kResultFalse;
#else
  const double s = sanitizeContentScale(factor);
  if (s == scale_) return Steinberg::kResultTrue;
  scale_ = s;
  const PixelSize px = scaledEditorSize(s);
  Steinberg::ViewRect r(rect.left, rect.top, rect.left + px.width, rect.top + px.height);
  setRect(r);   // getSize reports the scaled size even before the view is attached
  if (frame) {
    frame->setZoom(s);   // views keep their logical coordinates
    if (plugFrame) plugFrame->resizeView(this, &r);   // the host answers with onSize
  }
  return Steinberg::kResultTrue;
#endif
}

bool PLUGIN_API Editor::open(void* parent, const PlatformType& platformType) {
  if (frame) return false;

  IPlatformFrameConfig* config = nullptr;
#if SMTG_OS_LINUX
  // VSTGUI's X11 frame needs the host run loop (from the factory host context or the plug frame).
  X11::FrameConfig x11Config;
  auto* linuxFactory = getPlatformFactory().asLinuxFactory();
  if (!linuxFactory) return false;
  if (!linuxFactory->getRunLoop()) Steinberg::Linux::setupVSTGUIRunloop(plugFrame);
  x11Config.runLoop = linuxFactory->getRunLoop();
  if (!x11Config.runLoop) return false;
  config = &x11Config;
#endif

  // Appearance from the user preferences (missing or bad file: defaults); the seed comes from the instance decor.
  std::filesystem::path prefsFile;
  try {
    prefsFile = prefsPath();
  } catch (...) {
  }
  loadAppearance(prefsFile);

  frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
  frame->setBackgroundColor(kFrameBackground);
  createSides(frame);
  if (scale_ != 1.0) frame->setZoom(scale_);   // VSTGUIEditor::attached then reports the scaled size
  if (!frame->open(parent, platformType, config)) {
    close();
    return false;
  }
  timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { onTimer(); }, kTimerMs, true);
  return true;
}

void PLUGIN_API Editor::close() {
  // Timer first: nothing may touch the views once they start going away.
  if (timer_) {
    timer_->stop();
    timer_ = nullptr;
  }
  unwatchParameters();
  closeValueEdit(true);   // what was typed is kept, as when the field loses the focus
  closeTapeEdit(true);
  flip_.reset();          // a running turn: its view goes away with the frame's
  if (frame) {
    // Removing the views closes any open knob gesture (controlEndEdit -> endEdit).
    frame->removeAll(true);
    if (frame->getNbReference() == 1)
      frame->close();
    else
      frame->forget();
    frame = nullptr;
  }
  resetViewPointers();
}

void Editor::resetViewPointers() {
  root_ = nullptr;
  panel_ = nullptr;
  silk_ = nullptr;
  readouts_ = nullptr;
  lamp_ = nullptr;
  wear_ = nullptr;
  tapes_.clear();
  screws_.fill(nullptr);
  controls_.fill(nullptr);
  scope_ = nullptr;
  bars_.fill(nullptr);
  clips_.fill(nullptr);
  valueEdit_ = nullptr;
  valueEditId_ = kNumParams;
  tapeEdit_ = nullptr;
  tapeEditSlot_ = -1;
  front_ = nullptr;
  back_ = nullptr;
  backLink_ = nullptr;
}

void Editor::createSides(CViewContainer* host) {
  if (!host) return;
  front_ = new CViewContainer(CRect(0, 0, kWidth, kHeight));
  front_->setBackgroundColor(kFrameBackground);
  createViews(front_);
  host->addView(front_);
  buildBack(host);
  flip_ = FairyTapes::Ui::PanelFlip(host, front_, back_, scale_);
}

void Editor::createViews(CViewContainer* parent) {
  auto* ctl = getController();
  const CRect all(0, 0, kWidth, kHeight);
  refreshDecor();   // the instance seed, tape texts and screw rotations (no views yet)
  root_ = parent;

  // 1-2: panel body, grain, boxes and printed texts.
  panel_ = new PanelView(all, &skin_);
  parent->addView(panel_);

  // 3: silkscreen: the printed parts of the controls, the block tags, the read-outs and the BREATH lamp.
  std::vector<FairyTapes::Ui::BlockTag> tags;
  for (const TagSpec& t : kTags) tags.push_back(t.tag);
  silk_ = new SilkscreenView(all, silkscreenItems(), std::move(tags));
  silk_->setValueDrawer([this](CDrawContext* ctx, const FairyTapes::Ui::PrintText& t, const CPoint& origin, std::size_t item) {
    if (!valuesHandwritten_ || !silk_) return false;
    const int id = silk_->model().item(item).id;
    const TapeAlign align = t.align == FairyTapes::Ui::TextAlign::Left    ? TapeAlign::Left
                            : t.align == FairyTapes::Ui::TextAlign::Right ? TapeAlign::Right
                                                                          : TapeAlign::Center;
    FairyTapes::Ui::drawTapeText(ctx, valuesFont_, t.text, CPoint(origin.x + t.x, origin.y + t.y),
                                 FairyTapes::Ui::normalizedFontSize(valuesFont_, FairyTapes::Ui::kSilkValueSize),
                                 FairyTapes::Ui::kAmber, seed_ + static_cast<uint32_t>(id) * 131u, 0.5, align);
    return true;
  });
  silk_->setTag(kTagEq, kTagEqText);
  silk_->setTag(kTagComp, kTagCompText);
  parent->addView(silk_);
  readouts_ = new ReadoutView(all, ReadoutView::Part::Texts);
  readouts_->setHandwriting(valuesHandwritten_, valuesFont_, seed_);
  parent->addView(readouts_);

  // 4-6: wear, paper tapes, screws (inserted under the SCOPE); then the SCOPE, under the controls (it never overlaps one).
  wear_ = new WearView(all, wearMarks(seed_, wearLevel_));
  parent->addView(wear_);
  scope_ = new ScopeView(toCRect(kScopeRect));
  parent->addView(scope_);
  placeScrews();    // inserted before the SCOPE
  rebuildTapes();   // inserted before the screws

  // The controls, one per parameter.
  for (const ControlSpec& spec : kControls) {
    const CRect rect = toCRect(viewRect(spec));
    const auto tag = static_cast<int32_t>(spec.id);
    NormalizedControl* control = nullptr;
    switch (spec.kind) {
    case Kind::Knob: {
      auto* knob = new RackKnob(rect, this, tag, spec.capRadius, &skin_);
      const ParamID id = spec.id;
      knob->onValueClick = [this, id] { editValue(id); };
      control = knob;
      break;
    }
    case Kind::Toggle:
      control = new RackToggle(rect, this, tag, spec.led == Led::Yellow ? RackLed::Yellow : RackLed::Red, false, &skin_);
      break;
    case Kind::Choice: {
      std::vector<std::string> names(spec.choices, spec.choices + spec.choiceCount);
      control = new RackLitChoice(rect, this, tag, std::move(names), 1, kModeFaceW, &skin_);
      break;
    }
    case Kind::Cycle: control = new RackCycle(rect, this, tag, spec.choiceCount, &skin_); break;
    }
    control->setDefaultValue(static_cast<float>(defaultNorm(ctl, spec.id)));
    parent->addView(control);
    controls_[spec.id] = control;
  }

  // LEVEL: the LED rows and the clip LEDs; a click on a clip LED clears both.
  for (int i = 0; i < kMeterRowCount; ++i) {
    const MeterRow& row = kMeterRows[i];
    bars_[static_cast<std::size_t>(i)] =
        new MeterBar(toCRect(meterBarRect(row)), row.gainReduction ? MeterKind::GainReduction : MeterKind::Level, kMeterLeds);
    parent->addView(bars_[static_cast<std::size_t>(i)]);
  }
  for (int i = 0; i < kClipCount; ++i) {
    auto* clip = new ClipLed(toCRect(clipRect(kClips[i])), kClips[i].label);
    clip->onClick = [this] { clearClips(); };
    clips_[static_cast<std::size_t>(i)] = clip;
    parent->addView(clip);
  }

  // The BREATH lamp: an LED, over the wear, the tapes and the screws.
  lamp_ = new ReadoutView(all, ReadoutView::Part::Lamp);
  parent->addView(lamp_);

  // The way to the back of the panel: a small text at the right of the header (DESIGN_SYSTEM.md, one link only).
  const FairyTapes::Ui::BackBox link = FairyTapes::Ui::frontBackLinkBox();
  backLink_ = new TextLink(CRect(link.x, link.y, link.x + link.w, link.y + link.h), "BACK  \xE2\x86\x92");
  backLink_->onClick = [this] { startFlip(true); };
  parent->addView(backLink_);

  watchParameters();   // every control, value, text and dimming from the controller
}

// One click clears both clip LEDs: the hidden writable trigger goes to the processor, which clears the flags and answers
// with 0 (as Pillow Talk).
void Editor::clearClips() {
  auto* ctl = getController();
  ctl->setParamNormalized(kClearClips, 1.0);
  ctl->beginEdit(kClearClips);
  ctl->performEdit(kClearClips, 1.0);
  ctl->endEdit(kClearClips);
}

// The display values (neutral until the host forwards the processor's output): one SCOPE point, the LEVEL rows with their
// peak hold, the clip LEDs and the BREATH lamp. Read on every timer run: the processor writes them every 16 ms.
// As the preview: a breath (lamp, scope band) shows only with Breath IN on and no Bypass (the engine reports the raw
// detector), and the GR row is empty under Bypass (Comp IN off already reads 0 from the engine).
void Editor::updateDisplays() {
  auto* ctl = getController();
  const bool breathShown = !dim_.bypass && !moduleOut(Module::Breath, dim_);
  ScopePoint p;
  p.voice = static_cast<float>(displayNorm(ctl, kDispVoice, 0.0));
  p.rider = static_cast<float>(displayNorm(ctl, kDispRider, 0.5));
  p.breath = breathShown ? static_cast<float>(displayNorm(ctl, kDispBreath, 0.0)) : 0.f;
  if (scope_) scope_->push(p);
  for (int i = 0; i < kMeterRowCount; ++i) {
    const MeterRow& row = kMeterRows[i];
    const double v = row.gainReduction && dim_.bypass ? 0.0 : displayNorm(ctl, row.id, 0.0);
    if (MeterBar* bar = bars_[static_cast<std::size_t>(i)]) bar->setLevel(v, kTimerSeconds);
  }
  for (int i = 0; i < kClipCount; ++i)
    if (ClipLed* clip = clips_[static_cast<std::size_t>(i)]) clip->setLit(displayNorm(ctl, kClips[i].id, 0.0) >= 0.5);
  if (lamp_) lamp_->setLampLit(p.breath > 0.5f);
}

void Editor::watchParameters() {
  unwatchParameters();
  auto* ctl = getController();
  for (ParamID id = 0; id < kNumParams; ++id) {
    if (auto* p = ctl->getParameterObject(id)) {
      watched_[id] = p;   // keeps the Parameter alive until removeDependent
      p->addDependent(this);
    }
  }
  pending_.store(0);
  for (ParamID id = 0; id < kNumParams; ++id) syncFromController(id);
  refreshDynamicTexts();
  refreshDimming();
}

void Editor::unwatchParameters() {
  for (auto& p : watched_) {
    if (p) {
      p->removeDependent(this);
      p = nullptr;
    }
  }
  pending_.store(0);
}

void PLUGIN_API Editor::update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) {
  if (message != IDependent::kChanged) return;
  if (auto* p = Steinberg::FCast<Steinberg::Vst::Parameter>(changedUnknown)) {
    const ParamID id = p->getInfo().id;
    if (id < kNumParams) pending_.fetch_or(bit(id));
  }
}

void Editor::onTimer() {
  flip_.tick();   // the turn of the panel, if one is running
  const uint32_t changed = pending_.exchange(0);
  for (ParamID id = 0; id < kNumParams; ++id)
    if (changed & bit(id)) syncFromController(id);
  // The field reported what was typed when it lost the focus (Enter, a click elsewhere): it is done.
  if (valueEdit_ && frame && frame->getFocusView() != valueEdit_) closeValueEdit(false);
  if (tapeEdit_ && frame && frame->getFocusView() != tapeEdit_) closeTapeEdit(false);
  refreshDecor();   // only when the controller's decor revision changed
  updateDisplays();
}

// The step count of a stepped parameter (0 for the others).
static int stepCount(ParamID id) {
  switch (id) {
  case kMode: return kModeCount;
  case kCompRatio: return kCompRatioCount;
  case kCompDist: return kCompDistCount;
  case kCompHpf: return kCompHpfCount;
  default: return 0;
  }
}

void Editor::syncFromController(ParamID id) {
  if (id >= kNumParams) return;
  double v = controllerNorm(getController(), id);
  // A stepped parameter off the grid (an automation ramp): the step the DSP and the SDK display use (floor of n * count),
  // not the nearest one the choice control would pick, so the lit face always agrees with the sound and the texts.
  if (const int count = stepCount(id)) v = FairyTapes::Ui::normFromChoiceIndex(FairyTapes::DeepThroat::detail::stepIndex(v, count), count);
  if (NormalizedControl* control = controls_[id]) {
    control->setValueNormalized(static_cast<float>(v));
    control->invalid();
  }
  applySideEffects(id, v);
}

void Editor::applySideEffects(ParamID id, double norm) {
  if (silk_ && isKnobParam(id)) silk_->setValueText(static_cast<int>(id), formatKnobValue(id, norm));
  const bool on = norm >= 0.5;
  if (id == kBypass) {
    dim_.bypass = on;
    refreshDimming();
  } else if (isModuleIn(static_cast<FairyTapes::DeepThroat::ParamID>(id))) {
    for (int m = 1; m <= 6; ++m)
      if (inParamOf(static_cast<Module>(m)) == id) dim_.in[m - 1] = on;
    refreshDimming();
  }
  if (dynamicTextInput(id)) refreshDynamicTexts();
}

void Editor::refreshDynamicTexts() {
  auto* ctl = getController();
  const Mode m = modeFromNorm(controllerNorm(ctl, kMode));
  const double eq = controllerNorm(ctl, kEqAmount), tone = controllerNorm(ctl, kTone);
  const int column = activeModeColumn(m);   // the MODES tab of the back follows the Mode parameter
  if (column != modesColumn_) {
    modesColumn_ = column;
    if (back_) back_->invalid();
  }
  if (silk_) {
    silk_->setTag(kTagModeName, modeTagLine1(m));
    silk_->setTag(kTagModeInfo, modeTagLine2(m));
    silk_->setTag(kTagBreath, detectorTag(m, latency_));
    silk_->setTag(kTagDeEss, deEssNote(m));
    silk_->setValueText(kCompRatio, ratioText(compRatioChoiceFromNorm(controllerNorm(ctl, kCompRatio)), m));
    silk_->setValueText(kCompDist, distText(compDistChoiceFromNorm(controllerNorm(ctl, kCompDist)), m));
    silk_->setValueText(kCompHpf, hpfChoiceText(compHpfChoiceFromNorm(controllerNorm(ctl, kCompHpf))));
  }
  if (readouts_) {
    readouts_->setText(ReadoutId::RiderInfo, riderMaxText(m));
    readouts_->setText(ReadoutId::EqHpf, eqHpfText(m, eq));
    readouts_->setText(ReadoutId::EqMud, eqMudText(m, eq));
    readouts_->setText(ReadoutId::ToneMud, toneGainText(m, 0, tone));
    readouts_->setText(ReadoutId::TonePresence, toneGainText(m, 1, tone));
    readouts_->setText(ReadoutId::ToneAir, toneGainText(m, 2, tone));
    readouts_->setText(ReadoutId::LiveDetector, liveDetectorText(latency_));
  }
}

void Editor::refreshDimming() {
  for (const ControlSpec& c : kControls) {
    const bool dimmed = controlDimmed(c.id, dim_);
    if (NormalizedControl* control = controls_[c.id]) {
      if (control->isDimmed() != dimmed) {
        control->setDimmed(dimmed);
        control->invalid();
      }
    }
    if (silk_) silk_->setDimmed(static_cast<int>(c.id), dimmed);
  }
  auto partDimmed = [this](Module m) { return dim_.bypass || moduleOut(m, dim_); };
  if (readouts_)
    for (const Readout& r : kReadouts) readouts_->setDimmed(r.id, r.module != Module::None && partDimmed(r.module));   // MODE never
  if (lamp_) lamp_->setLampDimmed(partDimmed(Module::Breath));
  if (silk_)
    for (const TagSpec& t : kTags) silk_->setTagDimmed(t.tag.key, t.module != Module::None && partDimmed(t.module));   // MODE tags never
  if (panel_) panel_->setDimState(dim_);
  // SCOPE and LEVEL: only the global bypass, and not the input / output rows nor the clip LEDs.
  if (scope_) scope_->setDimmed(dim_.bypass);
  for (int i = 0; i < kMeterRowCount; ++i)
    if (MeterBar* bar = bars_[static_cast<std::size_t>(i)]) bar->setDimmed(dim_.bypass && !kMeterRows[i].alwaysLit);
}

// ---- Typed values ---------------------------------------------------------------------------------------------------
void Editor::editValue(ParamID id) {
  if (!root_ || valueEdit_ || tapeEdit_ || id >= kNumParams) return;
  auto* knob = dynamic_cast<RackKnob*>(controls_[id]);
  if (!knob) return;
  CRect area = knob->valueArea();
  area.inset(-4, -2);
  auto* edit = new ValueEdit(area, this, kValueEditTag, editFieldText(id, controllerNorm(getController(), id)).c_str());
  edit->setFont(makeOwned<CFontDesc>("Arial", 12, kBoldFace));
  edit->setFontColor(FairyTapes::Ui::kAmber);
  edit->setBackColor(CColor(0x05, 0x05, 0x05));
  edit->setFrameColor(FairyTapes::Ui::kAmber);
  edit->setHoriAlign(kCenterText);
  valueEdit_ = edit;
  valueEditId_ = id;
  root_->addView(edit);
  if (frame) frame->setFocusView(edit);   // starts the editing
}

void Editor::closeValueEdit(bool confirm) {
  if (!valueEdit_) return;
  CTextEdit* edit = valueEdit_;
  if (!confirm) static_cast<ValueEdit*>(edit)->cancelled = true;
  if (frame && frame->getFocusView() == edit) frame->setFocusView(nullptr);   // the field reports unless cancelled
  valueEdit_ = nullptr;
  valueEditId_ = kNumParams;
  if (root_ && root_->isChild(edit)) root_->removeView(edit, true);
}

// A value typed in goes the way of a turn of the knob: begin, perform, end (one undoable edit), then the views follow.
void Editor::commitTypedValue(ParamID id, const std::string& typed) {
  if (id >= kNumParams) return;
  auto* ctl = getController();
  Steinberg::Vst::String128 s{};
  toString128(typed, s);
  Steinberg::Vst::ParamValue n = 0;
  if (ctl->getParamValueByString(id, s, n) != Steinberg::kResultOk) return;   // not a value: ignored
  const double v = FairyTapes::Ui::sanitizeNorm(n, defaultNorm(ctl, id));
  ctl->beginEdit(id);
  ctl->setParamNormalized(id, v);
  ctl->performEdit(id, v);
  ctl->endEdit(id);
  syncFromController(id);
}

// ---- Appearance and instance decor: wear, paper tapes, screws ------------------------------------------------------
void Editor::loadAppearance(const std::filesystem::path& file) {
  Prefs p;
  try {
    if (!file.empty()) p = loadPrefs(file);
  } catch (...) {
    p = Prefs{};
  }
  prefsFile_ = file;
  prefs_ = p;   // the fields the editor does not show (notes) are kept for the next save
  setWearLevel(p.wear);
  setTapeFont(p.tapeFont);
  setValuesFont(p.handwrittenValues);
}

const std::vector<WearMark>& Editor::wearMarks(uint32_t seed, WearLevel level) {
  if (!marksValid_ || seed != marksSeed_ || level != marksLevel_) {
    marks_ = FairyTapes::Ui::generateWear(seed, level, kWidth, kHeight);
    marksSeed_ = seed;
    marksLevel_ = level;
    marksValid_ = true;
  }
  return marks_;
}

void Editor::refreshDecor() {
  if (!owner_) return;
  if (decorKnown_ && owner_->decorRevision() == decorRevision_) return;
  const Decor d = owner_->decor();   // generates the seed if the controller has none yet
  decorRevision_ = owner_->decorRevision();
  decorKnown_ = true;
  const bool textsChanged = d.tapeTexts != tapeTexts_;
  tapeTexts_ = d.tapeTexts;
  if (d.behaviour.latency != latency_) applyLatency(d.behaviour.latency);   // state recall, undo, another editor
  for (int i = 0; i < kNumScrewViews; ++i) screwAngles_[static_cast<std::size_t>(i)] = d.screwAngles[i];
  if (d.seed != 0 && d.seed != seed_) {
    seed_ = d.seed;
    applyWear();   // new marks and tapes (with the new texts), the handwriting of the new seed
  } else if (textsChanged) {
    refreshTapeTexts();
  }
  placeScrews();   // a new seed or new rotations (state recall, undo, re-roll)
}

void Editor::setWearLevel(WearLevel level) {
  prefs_.wear = level;
  if (back_) back_->setSelected(kSetWear, selectedFor(kSetWear, prefs_));
  if (level == wearLevel_) return;
  wearLevel_ = level;
  applyWear();
}

void Editor::setTapeFont(TapeFont font) {
  prefs_.tapeFont = font;
  if (back_) back_->setSelected(kSetTapeFont, selectedFor(kSetTapeFont, prefs_));
  if (font == tapeFont_) return;
  tapeFont_ = font;
  valuesFont_ = tapeFontIdOf(font);
  for (TapeView* t : tapes_) t->setFont(valuesFont_);
  applyHandwriting();
}

void Editor::setValuesFont(bool handwritten) {
  prefs_.handwrittenValues = handwritten;
  if (back_) back_->setSelected(kSetValuesFont, selectedFor(kSetValuesFont, prefs_));
  if (handwritten == valuesHandwritten_) return;
  valuesHandwritten_ = handwritten;
  applyHandwriting();
}

void Editor::applyWear() {
  if (wear_) wear_->setMarks(wearMarks(seed_, wearLevel_));
  applyBackWear();
  rebuildTapes();
  applyHandwriting();   // the handwriting varies with the seed
}

void Editor::applyHandwriting() {
  if (readouts_) readouts_->setHandwriting(valuesHandwritten_, valuesFont_, seed_);
  if (silk_) silk_->invalid();
}

void Editor::reroll() {
  if (!owner_) return;
  closeTapeEdit(true);
  owner_->editDecor(rerolledDecor(owner_->decor(), newRandomSeed()));
  refreshDecor();   // the new revision: new wear, tapes and screws
}

void Editor::rebuildTapes() {
  if (!root_) return;
  closeTapeEdit(true);
  for (TapeView* t : tapes_) root_->removeView(t, true);
  tapes_.clear();
  const WearLayout layout = layoutFromSeed(seed_, wearLevel_, Rect{0.f, 0.f, static_cast<float>(kWidth), static_cast<float>(kHeight)});
  for (const TapePlacement& p : layout.tapes) {
    auto* view = new TapeView(p, valuesFont_, seed_);
    view->setText(p.slot < static_cast<int>(tapeTexts_.size()) ? tapeTexts_[static_cast<std::size_t>(p.slot)] : std::string());
    view->onClick = [this](TapeView* t) { editTape(t); };
    // above the wear, below the screws, the SCOPE and the controls
    if (CView* above = screws_[0] ? static_cast<CView*>(screws_[0]) : static_cast<CView*>(scope_)) root_->addView(view, above);
    else root_->addView(view);
    tapes_.push_back(view);
  }
}

void Editor::refreshTapeTexts() {
  for (TapeView* t : tapes_) {
    const int slot = t->slot();
    t->setText(slot < static_cast<int>(tapeTexts_.size()) ? tapeTexts_[static_cast<std::size_t>(slot)] : std::string());
  }
}

// A click on a tape: a text field over it with what is written; Enter or leaving it confirms, Escape cancels.
void Editor::editTape(TapeView* tape) {
  if (!root_ || !tape || tapeEdit_ || valueEdit_) return;
  CRect box = tape->tapeRect();
  const CPoint c = box.getCenter();
  const CCoord w = std::max(box.getWidth() + 8, kTapeEditMinWidth);
  box = CRect(c.x - w / 2, c.y - kTapeEditHeight / 2, c.x + w / 2, c.y + kTapeEditHeight / 2);
  if (box.left < 0) box.offset(-box.left, 0);   // stays in the window
  if (box.right > kWidth) box.offset(kWidth - box.right, 0);
  auto* edit = new ValueEdit(box, this, kTapeEditTag + tape->slot(), tape->text().c_str());
  edit->setFont(makeOwned<CFontDesc>("Arial", 13, kBoldFace));
  edit->setFontColor(CColor(0x2b, 0x26, 0x20));
  edit->setBackColor(CColor(0xd9, 0xcf, 0xa6));
  edit->setFrameColor(CColor(0x8f, 0x85, 0x60));
  edit->setHoriAlign(kCenterText);
  tapeEdit_ = edit;
  tapeEditSlot_ = tape->slot();
  tapeEditReported_ = false;
  root_->addView(edit);
  if (frame) frame->setFocusView(edit);   // starts the editing
}

void Editor::commitTapeText(int slot, const std::string& typed) {
  if (!owner_) return;
  const Decor current = owner_->decor();
  const Decor d = withTapeText(current, slot, typed);   // printable ASCII, at most 14 characters
  if (d.tapeTexts == current.tapeTexts) return;          // nothing new: no edit, no dirty mark
  tapeTexts_ = d.tapeTexts;
  owner_->editDecor(d);
  decorRevision_ = owner_->decorRevision();
  refreshTapeTexts();
}

void Editor::closeTapeEdit(bool confirm) {
  if (!tapeEdit_) return;
  CTextEdit* edit = tapeEdit_;
  if (!confirm) static_cast<ValueEdit*>(edit)->cancelled = true;
  if (frame && frame->getFocusView() == edit) {
    frame->setFocusView(nullptr);   // the field reports unless cancelled
  } else if (confirm && !tapeEditReported_ && !static_cast<ValueEdit*>(edit)->cancelled) {
    commitTapeText(tapeEditSlot_, edit->getText().getString());   // no focus to lose (no window yet): confirmed here
  }
  tapeEdit_ = nullptr;
  tapeEditSlot_ = -1;
  if (root_ && root_->isChild(edit)) root_->removeView(edit, true);
}

void Editor::placeScrews() {
  if (!root_) return;
  const auto specs = screwSpecs(seed_);
  for (int i = 0; i < kNumScrewViews; ++i) {
    const auto k = static_cast<std::size_t>(i);
    const double shown = shownScrewAngle(specs[k].initialAngle, screwAngles_[k]);
    if (ScrewView* v = screws_[k]) {
      if (v->spec().cross != specs[k].cross || v->spec().missing != specs[k].missing ||
          v->spec().initialAngle != specs[k].initialAngle)
        v->setScrew(specs[k], shown);   // another seed: another screw
      else
        v->setAngle(shown);   // left alone while the user is turning it
      continue;
    }
    auto* view = new ScrewView(specs[k], shown);
    view->onTurned = [this, i](double angle) { screwTurned(i, angle); };
    if (scope_) root_->addView(view, scope_);   // over the wear and the tapes, below the SCOPE and the controls
    else root_->addView(view);
    screws_[k] = view;
  }
}

// The end of a drag: the rotation from the seeded angle goes to the instance decor (one edit, one dirty mark).
void Editor::screwTurned(int index, double shownDeg) {
  if (!owner_ || index < 0 || index >= kNumScrews || !screws_[static_cast<std::size_t>(index)]) return;
  const float user = userScrewAngle(shownDeg, screws_[static_cast<std::size_t>(index)]->spec().initialAngle);
  const Decor d = withScrewAngle(owner_->decor(), index, user);
  screwAngles_[static_cast<std::size_t>(index)] = d.screwAngles[index];
  owner_->editDecor(d);
  decorRevision_ = owner_->decorRevision();
}

// ---- Control listener -----------------------------------------------------------------------------------------------
void Editor::valueChanged(CControl* control) {
  if (!control) return;
  if (control == valueEdit_) {   // a typed value: read unless Escape was pressed
    if (!static_cast<ValueEdit*>(valueEdit_)->cancelled) commitTypedValue(valueEditId_, valueEdit_->getText().getString());
    return;
  }
  if (control == tapeEdit_) {   // the writing on a tape: stored unless Escape was pressed
    tapeEditReported_ = true;
    if (!static_cast<ValueEdit*>(tapeEdit_)->cancelled) commitTapeText(tapeEditSlot_, tapeEdit_->getText().getString());
    return;
  }
  const auto tag = control->getTag();
  if (tag < 0 || tag >= kNumParams) return;
  const auto id = static_cast<ParamID>(tag);
  auto* ctl = getController();
  const double v = FairyTapes::Ui::sanitizeNorm(control->getValueNormalized(), control->getDefaultValue());
  ctl->setParamNormalized(id, v);   // the dependent flag this raises only re-applies the same value: no echo
  ctl->performEdit(id, v);
  applySideEffects(id, v);
}

void Editor::controlBeginEdit(CControl* control) {
  const auto tag = control ? control->getTag() : -1;
  if (tag >= 0 && tag < kNumParams && control != valueEdit_) getController()->beginEdit(static_cast<ParamID>(tag));
}

void Editor::controlEndEdit(CControl* control) {
  const auto tag = control ? control->getTag() : -1;
  if (tag >= 0 && tag < kNumParams && control != valueEdit_) getController()->endEdit(static_cast<ParamID>(tag));
}

// ---- The back of the panel ------------------------------------------------------------------------------------------
void Editor::buildBack(CViewContainer* host) {
  if (!host) return;
  Behaviour behaviour;
  behaviour.latency = latency_;
  BackContent content = backContent(prefs_, behaviour);
  for (auto& item : content.items) {
    if (item.kind == BackItemKind::Button) {
      item.onPress = [this] { reroll(); };
    } else {
      const int key = item.key;
      item.onChange = [this, key](int index) { changeSetting(key, index); };
    }
  }
  BackView::Callbacks cb;
  cb.back = [this] { startFlip(false); };
  cb.tabChanged = [this](int tab) { backTab_ = tab; };
  back_ = new BackView(CRect(0, 0, kWidth, kHeight), &skin_, std::move(content), std::move(cb), backTab_);
  modesColumn_ = activeModeColumn(modeFromNorm(controllerNorm(getController(), kMode)));
  back_->setCustomDrawer(kTabModes, [this](CDrawContext* ctx, const CRect& area) { drawModesTab(ctx, area, modesColumn_); });
  back_->setVisible(false);
  host->addView(back_);
  applyBackWear();
}

void Editor::applyLatency(LatencyMode latency) {
  latency_ = latency;
  Behaviour b;
  b.latency = latency;
  if (back_) back_->setSelected(kSetLatency, selectedFor(kSetLatency, prefs_, b));
  refreshDynamicTexts();   // the BREATH tag and the LIVE text in MODE (no views yet: nothing to do)
}

void Editor::applyBackWear() {
  if (back_) back_->setWear(FairyTapes::Ui::generateWear(seed_ ^ kBackWearSalt, wearLevel_, kWidth, kHeight));
}

// Turns the panel. A text field open on a tape or a value is closed first, keeping what was typed.
void Editor::startFlip(bool toBack) {
  closeValueEdit(true);
  closeTapeEdit(true);
  flip_.start(toBack);
}

void Editor::changeSetting(int key, int index) {
  Behaviour b;
  b.latency = latency_;
  if (applyBehaviourSetting(key, index, b)) {
    if (b.latency == latency_) return;   // the same choice again: nothing to save or restart
    if (owner_) {   // saved with the project, sent to the processor; the controller asks the host for the restart
      Decor d = owner_->decor();
      d.behaviour = b;
      owner_->editDecor(d);
      decorRevision_ = owner_->decorRevision();
    }
    applyLatency(b.latency);
    return;
  }
  Prefs p = prefs_;
  if (!applySetting(key, index, p)) return;
  if (!prefsFile_.empty()) {
    try {
      savePrefs(prefsFile_, p);   // a failed write is ignored: the choice still applies to this editor
    } catch (...) {
    }
  }
  prefs_ = p;
  setWearLevel(p.wear);
  setTapeFont(p.tapeFont);
  setValuesFont(p.handwrittenValues);
}

} // namespace FairyTapes::DeepThroat::Ui
