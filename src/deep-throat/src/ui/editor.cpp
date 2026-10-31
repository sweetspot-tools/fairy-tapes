// Deep Throat GUI: the plugin editor. Layout from the approved rack preview (docs/mockups/deep_throat_rack.html,
// ui/layout.h); structure from Pillow Talk's editor (layers, one control per parameter, Parameter dependents applied on
// the UI timer) and The Stripper's typed values.
#include "ui/editor.h"

#include "ui/dynamic_text.h"
#include "ui/panel_view.h"
#include "ui/readout_view.h"
#include "ui/scope_view.h"
#include "ui/value_text.h"

#include "fairy_ui/controls.h"
#include "fairy_ui/draw_util.h"
#include "fairy_ui/knob_math.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/sanitize.h"
#include "fairy_ui/silkscreen_view.h"
#include "fairy_ui/tape_draw.h"

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

#include <cstring>
#include <string>
#include <vector>

namespace FairyTapes::DeepThroat::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::ClipLed;
using FairyTapes::Ui::MeterBar;
using FairyTapes::Ui::MeterKind;
using FairyTapes::Ui::NormalizedControl;
using FairyTapes::Ui::RackCycle;
using FairyTapes::Ui::RackKnob;
using FairyTapes::Ui::RackLed;
using FairyTapes::Ui::RackLitChoice;
using FairyTapes::Ui::RackToggle;
using FairyTapes::Ui::SilkscreenView;
using FairyTapes::Ui::TapeAlign;
using Steinberg::Vst::ParamID;

namespace {

static_assert(kNumParams <= 32, "pending_ holds one bit per persistent parameter");

constexpr uint32_t kTimerMs = 30;
constexpr double kTimerSeconds = kTimerMs / 1000.0;   // the step of the meters' peak hold
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);
constexpr int32_t kValueEditTag = 9100;   // outside the parameter ids

// The text field of a typed value (The Stripper's pattern): Escape cancels, Enter or leaving the field confirms.
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

bool dynamicTextInput(ParamID id) {
  return id == kMode || id == kEqAmount || id == kTone || id == kCompRatio || id == kCompDist || id == kCompHpf;
}

} // namespace

Editor::Editor(Steinberg::Vst::EditController* controller) : VSTGUIEditor(controller) {
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

  frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
  frame->setBackgroundColor(kFrameBackground);
  createViews(frame);
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
  controls_.fill(nullptr);
  scope_ = nullptr;
  bars_.fill(nullptr);
  clips_.fill(nullptr);
  valueEdit_ = nullptr;
  valueEditId_ = kNumParams;
}

void Editor::createViews(CViewContainer* parent) {
  auto* ctl = getController();
  const CRect all(0, 0, kWidth, kHeight);
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
                                 FairyTapes::Ui::kAmber, valuesSeed_ + static_cast<uint32_t>(id) * 131u, 0.5, align);
    return true;
  });
  silk_->setTag(kTagEq, kTagEqText);
  silk_->setTag(kTagComp, kTagCompText);
  parent->addView(silk_);
  readouts_ = new ReadoutView(all);
  readouts_->setHandwriting(valuesHandwritten_, valuesFont_, valuesSeed_);
  parent->addView(readouts_);

  // SCOPE: under the controls (it never overlaps one).
  scope_ = new ScopeView(toCRect(kScopeRect));
  parent->addView(scope_);

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
void Editor::updateDisplays() {
  auto* ctl = getController();
  ScopePoint p;
  p.voice = static_cast<float>(displayNorm(ctl, kDispVoice, 0.0));
  p.rider = static_cast<float>(displayNorm(ctl, kDispRider, 0.5));
  p.breath = static_cast<float>(displayNorm(ctl, kDispBreath, 0.0));
  if (scope_) scope_->push(p);
  for (int i = 0; i < kMeterRowCount; ++i)
    if (MeterBar* bar = bars_[static_cast<std::size_t>(i)]) bar->setLevel(displayNorm(ctl, kMeterRows[i].id, 0.0), kTimerSeconds);
  for (int i = 0; i < kClipCount; ++i)
    if (ClipLed* clip = clips_[static_cast<std::size_t>(i)]) clip->setLit(displayNorm(ctl, kClips[i].id, 0.0) >= 0.5);
  if (readouts_) readouts_->setLampLit(p.breath > 0.5f);
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
  const uint32_t changed = pending_.exchange(0);
  for (ParamID id = 0; id < kNumParams; ++id)
    if (changed & bit(id)) syncFromController(id);
  // The field reported what was typed when it lost the focus (Enter, a click elsewhere): it is done.
  if (valueEdit_ && frame && frame->getFocusView() != valueEdit_) closeValueEdit(false);
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
  if (silk_) {
    silk_->setTag(kTagModeName, modeTagLine1(m));
    silk_->setTag(kTagModeInfo, modeTagLine2(m));
    silk_->setTag(kTagBreath, detectorTag(m));
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
  if (readouts_) {
    for (const Readout& r : kReadouts) readouts_->setDimmed(r.id, partDimmed(r.module));
    readouts_->setLampDimmed(partDimmed(Module::Breath));
  }
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
  if (!root_ || valueEdit_ || id >= kNumParams) return;
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

// ---- Control listener -----------------------------------------------------------------------------------------------
void Editor::valueChanged(CControl* control) {
  if (!control) return;
  if (control == valueEdit_) {   // a typed value: read unless Escape was pressed
    if (!static_cast<ValueEdit*>(valueEdit_)->cancelled) commitTypedValue(valueEditId_, valueEdit_->getText().getString());
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

} // namespace FairyTapes::DeepThroat::Ui
