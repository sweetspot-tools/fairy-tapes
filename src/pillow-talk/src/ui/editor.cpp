// Pillow Talk GUI: the plugin editor. Layout from the approved rack preview (docs/mockups/pillow_rack.html, ui/layout.h).
#include "ui/editor.h"

#include "controller.h"
#include "state.h"
#include "ui/duck_scope.h"
#include "ui/panel_view.h"
#include "ui/prefs.h"
#include "ui/tape_view.h"
#include "ui/tapes.h"

#include "fairy_ui/controls.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/sanitize.h"
#include "fairy_ui/silkscreen_view.h"
#include "fairy_ui/wear_view.h"

#include "base/source/updatehandler.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ctextedit.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <algorithm>
#include <cstring>
#include <string>

namespace FairyTapes::PillowTalk::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::ClipLed;
using FairyTapes::Ui::MeterBar;
using FairyTapes::Ui::MeterKind;
using FairyTapes::Ui::NormalizedControl;
using FairyTapes::Ui::RackChoice;
using FairyTapes::Ui::RackKnob;
using FairyTapes::Ui::RackLed;
using FairyTapes::Ui::RackToggle;
using FairyTapes::Ui::SilkscreenView;
using FairyTapes::Ui::WearLevel;
using FairyTapes::Ui::WearMark;
using FairyTapes::Ui::WearView;
using Steinberg::Vst::ParamID;

namespace {

static_assert(kNumParams <= 32, "pending_ holds one bit per persistent parameter");
static_assert(kMaxTapeTexts == kNumTapeSlots, "one stored tape text per tape slot");
static_assert(kNumScrews == static_cast<int>(sizeof(kScrewPos) / sizeof(kScrewPos[0])), "one stored angle per screw");

constexpr uint32_t kTimerMs = 30;
constexpr double kTimerSeconds = kTimerMs / 1000.0;
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);
constexpr float kDimmedAlpha = 0.35f;   // DESIGN_SYSTEM.md: dimmed parts at 35 %
constexpr int32_t kTapeEditTag = 9000;   // + slot; outside the parameter ids
constexpr CCoord kTapeEditMinWidth = 120, kTapeEditHeight = 24;

// The text field of a tape (the pattern of The Stripper's typed values): Escape cancels, Enter or leaving the field
// confirms. CTextEdit itself would still report the typed text when Escape comes through the platform field.
class TapeEdit : public CTextEdit {
public:
  using CTextEdit::CTextEdit;
  bool cancelled = false;

  void onKeyboardEvent(KeyboardEvent& event) override {
    if (event.type == EventType::KeyDown && event.virt == VirtualKey::Escape) cancelled = true;
    CTextEdit::onKeyboardEvent(event);
  }

protected:
  ~TapeEdit() noexcept override = default;
  void platformOnKeyboardEvent(KeyboardEvent& event) override {
    if (event.type == EventType::KeyDown && event.virt == VirtualKey::Escape) cancelled = true;
    CTextEdit::platformOnKeyboardEvent(event);
  }
};

CRect toCRect(const Rect& r) { return CRect(r.x, r.y, r.x + r.w, r.y + r.h); }

constexpr uint32_t bit(ParamID id) { return 1u << id; }

std::string paramString(Steinberg::Vst::EditController* ctl, ParamID id, double norm) {
  Steinberg::Vst::String128 s{};
  if (ctl->getParamStringByValue(id, norm, s) != Steinberg::kResultOk) return {};
  return Steinberg::Vst::StringConvert::convert(s);
}

double controllerNorm(Steinberg::Vst::EditController* ctl, ParamID id, double fallback) {
  return FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(id), fallback);
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
  (void)factor;  // Cocoa backing scale handles Retina; VST3Editor keeps 1 there as well
  return Steinberg::kResultFalse;
#else
  const double s = sanitizeContentScale(factor);
  if (s == scale_) return Steinberg::kResultTrue;
  scale_ = s;
  const PixelSize px = scaledEditorSize(s);
  Steinberg::ViewRect r(rect.left, rect.top, rect.left + px.width, rect.top + px.height);
  setRect(r);  // getSize reports the scaled size even before the view is attached
  if (frame) {
    frame->setZoom(s);  // views keep their logical coordinates
    if (plugFrame) plugFrame->resizeView(this, &r);  // the host answers with onSize
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

  // Wear level and tape font from the user preferences (missing or bad file: defaults), seed from the instance decor.
  Prefs prefs;
  try {
    prefs = loadPrefs(prefsPath());
  } catch (...) {
    prefs = Prefs{};
  }
  wearLevel_ = prefs.wear;
  tapeFont_ = tapeFontIdOf(prefs.tapeFont);
  refreshDecor();

  frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
  frame->setBackgroundColor(kFrameBackground);
  createViews(frame);
  if (scale_ != 1.0) frame->setZoom(scale_);  // VSTGUIEditor::attached then reports the scaled size
  if (!frame->open(parent, platformType, config)) {
    close();
    return false;
  }
  watchParameters();
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
  closeTapeEdit(true);   // what was typed is kept, as when the field loses the focus
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
  controls_.fill(nullptr);
  bars_.fill(nullptr);
  clips_.fill(nullptr);
  scope_ = nullptr;
  silk_ = nullptr;
  wear_ = nullptr;
  root_ = nullptr;
  tapes_.clear();
  tapeEdit_ = nullptr;
  tapeEditSlot_ = -1;
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
  const uint32_t rev = owner_->decorRevision();
  if (decorKnown_ && rev == decorRevision_) return;
  const Decor d = owner_->decor();   // generates the seed if the controller has none yet
  decorRevision_ = owner_->decorRevision();
  decorKnown_ = true;
  const bool textsChanged = d.tapeTexts != tapeTexts_;
  tapeTexts_ = d.tapeTexts;
  if (d.seed != 0 && d.seed != seed_) {
    seed_ = d.seed;
    applyWear();   // new tapes, with the new texts
  } else if (textsChanged) {
    refreshTapeTexts();
  }
}

void Editor::setWearLevel(WearLevel level) {
  if (level == wearLevel_) return;
  wearLevel_ = level;
  applyWear();
}

void Editor::applyWear() {
  if (wear_) wear_->setMarks(wearMarks(seed_, wearLevel_));
  rebuildTapes();
}

void Editor::setTapeFont(FairyTapes::Ui::TapeFontId font) {
  tapeFont_ = font;
  for (TapeView* t : tapes_) t->setFont(font);
}

void Editor::rebuildTapes() {
  if (!root_) return;
  closeTapeEdit(true);
  for (TapeView* t : tapes_) root_->removeView(t, true);
  tapes_.clear();
  const WearLayout layout = layoutFromSeed(seed_, wearLevel_, Rect{0.f, 0.f, static_cast<float>(kWidth), static_cast<float>(kHeight)});
  for (const TapePlacement& p : layout.tapes) {
    auto* view = new TapeView(p, tapeFont_, seed_);
    view->setText(p.slot < static_cast<int>(tapeTexts_.size()) ? tapeTexts_[static_cast<std::size_t>(p.slot)] : std::string());
    view->onClick = [this](TapeView* t) { editTape(t); };
    if (scope_) root_->addView(view, scope_);   // above the wear, below the scope and the controls
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
  if (!frame || !tape || tapeEdit_) return;
  CRect box = tape->tapeRect();
  const CPoint c = box.getCenter();
  const CCoord w = std::max(box.getWidth() + 8, kTapeEditMinWidth);
  box = CRect(c.x - w / 2, c.y - kTapeEditHeight / 2, c.x + w / 2, c.y + kTapeEditHeight / 2);
  if (box.left < 0) box.offset(-box.left, 0);   // stays in the window
  if (box.right > kWidth) box.offset(kWidth - box.right, 0);
  tapeEdit_ = new TapeEdit(box, this, kTapeEditTag + tape->slot(), tape->text().c_str());
  tapeEditSlot_ = tape->slot();
  tapeEdit_->setFont(makeOwned<CFontDesc>("Arial", 13, kBoldFace));
  tapeEdit_->setFontColor(CColor(0x2b, 0x26, 0x20));
  tapeEdit_->setBackColor(CColor(0xd9, 0xcf, 0xa6));
  tapeEdit_->setFrameColor(CColor(0x8f, 0x85, 0x60));
  tapeEdit_->setHoriAlign(kCenterText);
  frame->addView(tapeEdit_);
  frame->setFocusView(tapeEdit_);   // starts the editing
}

void Editor::commitTapeText(int slot, const std::string& typed) {
  if (!owner_) return;
  const Decor d = withTapeText(owner_->decor(), slot, typed);
  tapeTexts_ = d.tapeTexts;
  owner_->editDecor(d);
  decorRevision_ = owner_->decorRevision();
  refreshTapeTexts();
}

void Editor::closeTapeEdit(bool confirm) {
  if (!tapeEdit_) return;
  auto* edit = static_cast<TapeEdit*>(tapeEdit_);
  if (frame && frame->getFocusView() == tapeEdit_) {
    if (!confirm) edit->cancelled = true;
    frame->setFocusView(nullptr);   // the field reports what was typed (valueChanged) unless cancelled
  }
  if (frame) frame->removeView(tapeEdit_, true);
  tapeEdit_ = nullptr;
  tapeEditSlot_ = -1;
}

void Editor::createViews(CViewContainer* parent) {
  auto* ctl = getController();
  const CRect all(0, 0, kWidth, kHeight);

  // 1-2: panel, grain, boxes and fixed texts. 3: silkscreen. 4: wear. 5: paper tapes. Then the controls and displays.
  root_ = parent;
  parent->addView(new PanelView(all, &skin_));
  silk_ = new SilkscreenView(all, silkscreenItems());
  parent->addView(silk_);
  wear_ = new WearView(all, wearMarks(seed_, wearLevel_));
  parent->addView(wear_);

  scope_ = new DuckScope(toCRect(kScopeRect));
  parent->addView(scope_);
  rebuildTapes();   // inserted before the scope

  for (const ControlSpec& spec : kControls) {
    const CRect rect = toCRect(viewRect(spec));
    const auto tag = static_cast<int32_t>(spec.id);
    const RackLed led = spec.led == Led::Yellow ? RackLed::Yellow : RackLed::Red;
    NormalizedControl* control = nullptr;
    switch (spec.kind) {
    case Kind::Knob: control = new RackKnob(rect, this, tag, spec.capRadius, &skin_); break;
    case Kind::Toggle: control = new RackToggle(rect, this, tag, led, false, &skin_); break;
    case Kind::Choice: control = new RackChoice(rect, this, tag, spec.choiceCount, led, &skin_); break;
    }
    if (auto* p = ctl->getParameterObject(spec.id))
      control->setDefaultValue(static_cast<float>(p->getInfo().defaultNormalizedValue));
    parent->addView(control);
    controls_[spec.id] = control;
  }

  for (int i = 0; i < kMeterRowCount; ++i) {
    const MeterRow& row = kMeterRows[i];
    bars_[i] = new MeterBar(toCRect(meterBarRect(row)), row.gainReduction ? MeterKind::GainReduction : MeterKind::Level,
                            kMeterLeds);
    parent->addView(bars_[i]);
  }
  for (int i = 0; i < kClipCount; ++i) {
    clips_[i] = new ClipLed(toCRect(clipRect(kClips[i])), kClips[i].label);
    clips_[i]->onClick = [this] { clearClips(); };
    parent->addView(clips_[i]);
  }
}

// One click clears both clip LEDs: the hidden writable parameter goes to the processor, which clears the flags and
// answers with 0 (Task 1).
void Editor::clearClips() {
  auto* ctl = getController();
  ctl->setParamNormalized(kClearClips, 1.0);
  ctl->beginEdit(kClearClips);
  ctl->performEdit(kClearClips, 1.0);
  ctl->endEdit(kClearClips);
}

void Editor::watchParameters() {
  auto* ctl = getController();
  for (ParamID id = 0; id < kNumParams; ++id) {
    if (auto* p = ctl->getParameterObject(id)) {
      watched_[id] = p;  // keeps the Parameter alive until removeDependent
      p->addDependent(this);
    }
  }
  pending_.store(0);
  for (ParamID id = 0; id < kNumParams; ++id) syncFromController(id);
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
  refreshDecor();
  if (tapeEdit_ && frame && frame->getFocusView() != tapeEdit_) closeTapeEdit(false);   // the field is done
  updateDisplays();
}

// Display values (neutral until the host forwards the processor's output): scope, LEVEL rows, clip LEDs.
void Editor::updateDisplays() {
  auto* ctl = getController();
  if (scope_)
    scope_->push(static_cast<float>(ctl->getParamNormalized(kDispVoice)),
                 static_cast<float>(ctl->getParamNormalized(kDispSend)),
                 static_cast<float>(ctl->getParamNormalized(kDispEcho)));
  for (int i = 0; i < kMeterRowCount; ++i) {
    if (!bars_[i]) continue;
    const MeterRow& row = kMeterRows[i];
    const double v = row.gainReduction ? duckMeterNorm(ctl->getParamNormalized(row.id))
                                       : controllerNorm(ctl, row.id, 0.0);
    bars_[i]->setLevel(v, kTimerSeconds);
  }
  for (int i = 0; i < kClipCount; ++i)
    if (clips_[i]) clips_[i]->setLit(controllerNorm(ctl, kClips[i].id, 0.0) >= 0.5);
}

void Editor::syncFromController(ParamID id) {
  auto* ctl = getController();
  const auto* p = ctl->getParameterObject(id);
  const double v = controllerNorm(ctl, id, p ? p->getInfo().defaultNormalizedValue : 0.0);
  if (auto* control = controls_[id]) {
    control->setValueNormalized(static_cast<float>(v));
    control->invalid();
  }
  applySideEffects(id, v);
}

void Editor::applySideEffects(ParamID id, double norm) {
  const ControlSpec* spec = controlOf(id);
  if (silk_ && spec && spec->kind == Kind::Knob) silk_->setValueText(static_cast<int>(id), paramString(getController(), id, norm));
  const bool on = norm >= 0.5;
  if (id == kBypass || id == kTimeSync || id == kHoldSync) {
    if (id == kBypass) dim_.bypass = on;
    else if (id == kTimeSync) dim_.timeSync = on;
    else dim_.holdSync = on;
    refreshDimming();
  }
  if (id == kThreshold && scope_) scope_->setThreshold(static_cast<float>(norm));
}

void Editor::refreshDimming() {
  for (const ControlSpec& c : kControls) {
    const bool dimmed = controlDimmed(c.id, dim_);
    if (auto* control = controls_[c.id]) control->setDimmed(dimmed);
    if (silk_) silk_->setDimmed(static_cast<int>(c.id), dimmed);
  }
  if (scope_) scope_->setAlphaValue(dim_.bypass ? kDimmedAlpha : 1.f);
  for (int i = 0; i < kMeterRowCount; ++i)
    if (bars_[i]) bars_[i]->setDimmed(dim_.bypass && !kMeterRows[i].alwaysLit);
}

void Editor::valueChanged(CControl* control) {
  if (control == tapeEdit_) {   // the writing on a tape: confirmed unless Escape was pressed
    if (!static_cast<TapeEdit*>(tapeEdit_)->cancelled) commitTapeText(tapeEditSlot_, tapeEdit_->getText().getString());
    return;
  }
  const auto id = static_cast<ParamID>(control->getTag());
  if (id >= kNumParams) return;
  auto* ctl = getController();
  const double v = FairyTapes::Ui::sanitizeNorm(control->getValueNormalized(), control->getDefaultValue());
  ctl->setParamNormalized(id, v);
  ctl->performEdit(id, v);
  applySideEffects(id, v);
}

void Editor::controlBeginEdit(CControl* control) {
  const auto id = static_cast<ParamID>(control->getTag());
  if (id < kNumParams) getController()->beginEdit(id);
}

void Editor::controlEndEdit(CControl* control) {
  const auto id = static_cast<ParamID>(control->getTag());
  if (id < kNumParams) getController()->endEdit(id);
}

} // namespace FairyTapes::PillowTalk::Ui
