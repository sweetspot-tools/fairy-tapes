// Hotline GUI: the plugin editor. Layout from the approved rack preview (docs/mockups/hotline_rack.html, ui/layout.h).
#include "ui/editor.h"

#include "ui/panel_view.h"

#include "fairy_ui/draw_util.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/rack_controls.h"
#include "fairy_ui/sanitize.h"
#include "fairy_ui/silkscreen_view.h"

#include "base/source/updatehandler.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/cviewcontainer.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <cstring>
#include <string>
#include <vector>

namespace FairyTapes::Hotline::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::ClipLed;
using FairyTapes::Ui::MeterBar;
using FairyTapes::Ui::MeterKind;
using FairyTapes::Ui::NormalizedControl;
using FairyTapes::Ui::RackKnob;
using FairyTapes::Ui::RackLed;
using FairyTapes::Ui::RackLitChoice;
using FairyTapes::Ui::RackToggle;
using FairyTapes::Ui::SilkscreenView;
using Steinberg::Vst::ParamID;

namespace {

static_assert(kNumParams <= 32, "pending_ holds one bit per persistent parameter");

constexpr uint32_t kTimerMs = 30;
constexpr double kTimerSeconds = kTimerMs / 1000.0;
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);

constexpr uint32_t bit(ParamID id) { return 1u << id; }

CRect toCRect(const Rect& r) { return CRect(r.x, r.y, r.x + r.w, r.y + r.h); }

double controllerNorm(Steinberg::Vst::EditController* ctl, ParamID id, double fallback) {
  return FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(id), fallback);
}

// The Morph cap (radius 34) is bigger than the shared knob cell allows: RackKnob draws its cap kCenterOffsetY below the
// top of its view, so this view is kBigKnobExtra taller above and draws the cap that much lower.
class BigRackKnob : public RackKnob {
public:
  BigRackKnob(const CRect& size, IControlListener* listener, int32_t tag, double capRadius, const FairyTapes::Ui::Skin* skin)
  : RackKnob(size, listener, tag, capRadius, skin) {}

  void draw(CDrawContext* ctx) override {
    CGraphicsTransform shift;
    shift.translate(0, kBigKnobExtra);
    CDrawContext::Transform transform(*ctx, shift);
    RackKnob::draw(ctx);
  }
};

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
  silk_ = nullptr;
}

void Editor::createViews(CViewContainer* parent) {
  auto* ctl = getController();
  const CRect all(0, 0, kWidth, kHeight);

  // 1-2: panel, grain, boxes and fixed texts. 3: silkscreen. (4: wear and tapes, 5: screws: Task 8.) Then the controls and
  // displays.
  parent->addView(new PanelView(all, &skin_));
  silk_ = new SilkscreenView(all, silkscreenItems());
  parent->addView(silk_);

  for (const ControlSpec& spec : kControls) {
    const CRect rect = toCRect(viewRect(spec));
    const auto tag = static_cast<int32_t>(spec.id);
    NormalizedControl* control = nullptr;
    switch (spec.kind) {
    case Kind::Knob:
      if (isBigKnob(spec.capRadius)) control = new BigRackKnob(rect, this, tag, spec.capRadius, &skin_);
      else control = new RackKnob(rect, this, tag, spec.capRadius, &skin_);
      break;
    case Kind::Toggle:
      control = new RackToggle(rect, this, tag, spec.led == Led::Yellow ? RackLed::Yellow : RackLed::Red, false, &skin_);
      break;
    case Kind::Choice: {
      std::vector<std::string> names(spec.choices, spec.choices + spec.choiceCount);
      control = new RackLitChoice(rect, this, tag, std::move(names), spec.columns, spec.faceWidth, &skin_);
      break;
    }
    }
    if (auto* p = ctl->getParameterObject(spec.id))
      control->setDefaultValue(static_cast<float>(p->getInfo().defaultNormalizedValue));
    parent->addView(control);
    controls_[spec.id] = control;
  }

  for (int i = 0; i < kMeterRowCount; ++i) {
    bars_[i] = new MeterBar(toCRect(meterBarRect(kMeterRows[i])), MeterKind::Level, kMeterLeds);
    parent->addView(bars_[i]);
  }
  for (int i = 0; i < kClipCount; ++i) {
    clips_[i] = new ClipLed(toCRect(clipRect(kClips[i])), kClips[i].label);
    clips_[i]->onClick = [this] { clearClips(); };
    parent->addView(clips_[i]);
  }
}

// One click clears the three clip LEDs: the hidden writable parameter goes to the processor, which clears the flags and
// answers with 0.
void Editor::clearClips() {
  auto* ctl = getController();
  ctl->setParamNormalized(kClipClear, 1.0);
  ctl->beginEdit(kClipClear);
  ctl->performEdit(kClipClear, 1.0);
  ctl->endEdit(kClipClear);
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
  updateDisplays();
}

// Display values (neutral until the host forwards the processor's output): LEVEL rows and clip LEDs.
void Editor::updateDisplays() {
  auto* ctl = getController();
  for (int i = 0; i < kMeterRowCount; ++i)
    if (bars_[i]) bars_[i]->setLevel(controllerNorm(ctl, kMeterRows[i].id, 0.0), kTimerSeconds);
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
  if (silk_ && spec && spec->kind == Kind::Knob) silk_->setValueText(static_cast<int>(id), panelValueText(id, norm));
  if (id == kBypass || id == kCharacterIn) {
    if (id == kBypass) state_.bypass = norm >= 0.5;
    else state_.characterIn = norm >= 0.5;
    refreshDimming();
  }
}

void Editor::refreshDimming() {
  for (const ControlSpec& c : kControls) {
    const bool dimmed = controlDimmed(c, state_);
    if (auto* control = controls_[c.id]) control->setDimmed(dimmed);
    if (silk_) silk_->setDimmed(static_cast<int>(c.id), dimmed);
  }
  for (int i = 0; i < kMeterRowCount; ++i)
    if (bars_[i]) bars_[i]->setDimmed(meterDimmed(kMeterRows[i], state_));
  for (int i = 0; i < kClipCount; ++i)
    if (clips_[i]) clips_[i]->setDimmed(clipDimmed(kClips[i], state_));
}

void Editor::valueChanged(CControl* control) {
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

} // namespace FairyTapes::Hotline::Ui
