// The Stripper GUI: the plugin editor. Layout from the approved preview (spec section 4).
#include "ui/editor.h"

#include "controller.h"
#include "ui/model_browser.h"
#include "ui/model_source.h"
#include "ui/model_strip.h"
#include "ui/panel_view.h"

#include "fairy_ui/controls.h"
#include "fairy_ui/editor_size.h"
#include "fairy_ui/meters.h"
#include "fairy_ui/sanitize.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "vstgui/lib/cframe.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace FairyTapes::TheStripper::Ui {

using namespace VSTGUI;
using FairyTapes::Ui::AnalogKnob;
using FairyTapes::Ui::ChoiceButtons;
using FairyTapes::Ui::ClipLed;
using FairyTapes::Ui::MeterBar;
using FairyTapes::Ui::MeterKind;
using FairyTapes::Ui::NormalizedControl;
using FairyTapes::Ui::ToggleButton;
using Steinberg::Vst::ParamID;

namespace {

constexpr uint32_t kTimerMs = 30;
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);

CRect toCRect(const Rect& r) { return CRect(r.x, r.y, r.x + r.w, r.y + r.h); }

std::string paramString(Controller* ctl, ParamID id, double norm) {
  Steinberg::Vst::String128 s{};
  if (ctl->getParamStringByValue(id, norm, s) != Steinberg::kResultOk) return {};
  return Steinberg::Vst::StringConvert::convert(s);
}

double defaultNorm(Controller* ctl, ParamID id) {
  Steinberg::Vst::ParameterInfo info{};
  for (Steinberg::int32 i = 0; i < ctl->getParameterCount(); ++i)
    if (ctl->getParameterInfo(i, info) == Steinberg::kResultOk && info.id == id) return info.defaultNormalizedValue;
  return 0.0;
}

} // namespace

Editor::Editor(Controller* controller)
: VSTGUIEditor(nullptr), ctl_(controller) {
  ctl_->addRef();
  for (const auto& c : kControls) specs_[c.id] = &c;
  seen_.fill(std::nan(""));
  const auto px = FairyTapes::Ui::scaledSize(kWidth, kHeight, scale_);
  setRect(Steinberg::ViewRect(0, 0, px.width, px.height));
}

Editor::~Editor() {
  if (frame) close();
  ctl_->release();
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
  const auto px = FairyTapes::Ui::scaledSize(kWidth, kHeight, scale_);
  rect->right = rect->left + px.width;
  rect->bottom = rect->top + px.height;
  return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor factor) {
#if SMTG_OS_MACOS
  (void)factor;   // Cocoa backing scale handles Retina
  return Steinberg::kResultFalse;
#else
  const double s = FairyTapes::Ui::sanitizeContentScale(factor);
  if (s == scale_) return Steinberg::kResultTrue;
  scale_ = s;
  const auto px = FairyTapes::Ui::scaledSize(kWidth, kHeight, s);
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
  syncFromController(true);
  timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { onTimer(); }, kTimerMs, true);
  return true;
}

void PLUGIN_API Editor::close() {
  // Timer first: nothing may touch the views once they start going away.
  if (timer_) {
    timer_->stop();
    timer_ = nullptr;
  }
  if (frame) {
    // Removing the views closes any open knob gesture (controlEndEdit -> endEdit).
    frame->removeAll(true);
    if (frame->getNbReference() == 1)
      frame->close();
    else
      frame->forget();
    frame = nullptr;
  }
  controls_.fill(nullptr);
  strip_ = nullptr;
  browser_ = nullptr;
  browserClosing_ = false;
  modelState_ = -1;
  bars_.fill(nullptr);
  clips_.fill(nullptr);
  seen_.fill(std::nan(""));
}

void Editor::createViews(CViewContainer* parent) {
  parent->addView(new PanelView(CRect(0, 0, kWidth, kHeight), &skin_));

  for (const auto& spec : kControls) {
    if (spec.sharesView) continue;   // its parameter is shown by another entry's view
    NormalizedControl* control = nullptr;
    const CRect rect = toCRect(spec.rect);
    const auto tag = static_cast<int32_t>(spec.id);
    switch (spec.kind) {
    case Kind::Knob: {
      auto* knob = new AnalogKnob(rect, this, tag, spec.label, &skin_);
      Controller* ctl = ctl_;
      const ParamID id = spec.id;
      const bool drive = id == kDriveAmount;   // this knob also shows the saturation amount
      knob->setValueFormatter([this, ctl, id, drive](float v) { return paramString(ctl, drive ? driveParam_ : id, v); });
      control = knob;
      break;
    }
    case Kind::Toggle:
      control = new ToggleButton(rect, this, tag, spec.label, &skin_);
      break;
    case Kind::Choice: {
      std::vector<std::string> labels;
      for (const char* l : spec.choices)
        if (l) labels.emplace_back(l);
      control = new ChoiceButtons(rect, this, tag, labels, &skin_);
      break;
    }
    }
    control->setDefaultValue(static_cast<float>(uiValue(spec.inverted, defaultNorm(ctl_, spec.id))));
    parent->addView(control);
    controls_[spec.id] = control;
  }

  strip_ = new ModelStrip(toCRect(kModelStrip));
  strip_->onClick = [this] { openBrowser(); };
  parent->addView(strip_);

  for (std::size_t i = 0; i < bars_.size(); ++i) {
    const MeterRow& row = kMeterRows[i];
    bars_[i] = new MeterBar(toCRect(meterBarRect(row)), row.gainReduction ? MeterKind::GainReduction : MeterKind::Level,
                            kMeterLeds);
    parent->addView(bars_[i]);
  }
  for (int i = 0; i < kClipCount; ++i) {
    clips_[i] = new ClipLed(toCRect(clipRect(i)), kClips[i].label);
    clips_[i]->onClick = [this] {
      // One click clears every clip LED of the plugin (hidden writable parameter).
      ctl_->setParamNormalized(kClearClips, 1.0);
      ctl_->setParamNormalized(kClearClips, 0.0);   // re-arm: the next click is a new edit
    };
    parent->addView(clips_[i]);
  }
}

void Editor::onTimer() {
  if (browserClosing_) {   // the popup asked to close from inside its own mouse handler: remove it now
    browserClosing_ = false;
    if (browser_ && frame) frame->removeView(browser_, true);
    browser_ = nullptr;
  }
  syncFromController(false);
  updateMeters();
  updateModel();
  if (browser_) browser_->poll();
}

void Editor::updateModel() {
  const int state = ctl_->modelState();
  const std::string name = ctl_->modelName(), error = ctl_->modelError();
  if (state == modelState_ && name == modelName_ && error == modelError_) return;
  modelState_ = state;
  modelName_ = name;
  modelError_ = error;
  if (strip_) {
    using Kind = ModelStrip::Kind;
    if (state == 1) strip_->setModel(Kind::Loading, {});
    else if (state == 2 && !name.empty()) strip_->setModel(Kind::Loaded, displayName(name));
    else if (state == 3) strip_->setModel(Kind::Error, error.empty() ? "Could not load model" : error);
    else strip_->setModel(Kind::NoModel, {});
  }
  refreshModules();
}

void Editor::openBrowser() {
  if (!frame || browser_) return;
  browser_ = new ModelBrowser(CRect(0, 0, kWidth, kHeight), ctl_->modelPath());
  browser_->onLoad = [this](const std::string& path) { ctl_->requestModelLoad(path); };
  browser_->onClose = [this] { browserClosing_ = true; };
  frame->addView(browser_);
}

// Meter values come from the read-only parameters the processor writes (controller.meterValue()).
void Editor::updateMeters() {
  const auto now = std::chrono::steady_clock::now();
  const double dt = lastTick_.time_since_epoch().count() == 0 ? 0.0 : std::chrono::duration<double>(now - lastTick_).count();
  lastTick_ = now;
  for (std::size_t i = 0; i < bars_.size(); ++i) {
    if (!bars_[i]) continue;
    double v = ctl_->meterValue(kMeterRows[i].id);
    if (kMeterRows[i].gainReduction) v = grBarNorm(v, kMeterRows[i].id == kMeterGrComp ? kGrCompMaxDb : kGrLimiterMaxDb);
    bars_[i]->setLevel(v, dt);
    bars_[i]->setDimmed(meterDimmed(kMeterRows[i].alwaysLit, globalBypass_));
  }
  for (int i = 0; i < kClipCount; ++i) {
    if (!clips_[i]) continue;
    clips_[i]->setLit(ctl_->meterValue(kClips[i].id) >= 0.5);
    clips_[i]->setDimmed(meterDimmed(kClips[i].alwaysLit, globalBypass_));
  }
}

void Editor::syncFromController(bool force) {
  bool changed = force;
  for (ParamID id = 0; id < kNumParams; ++id) {
    const ControlSpec* spec = specs_[id];
    NormalizedControl* control = controls_[id];
    if (!spec) continue;   // (a shared parameter has a spec but no view of its own)
    const double v = FairyTapes::Ui::sanitizeNorm(ctl_->getParamNormalized(id), defaultNorm(ctl_, id));
    if (!force && v == seen_[id]) continue;
    seen_[id] = v;
    changed = true;
    if (control) {
      control->setValueNormalized(static_cast<float>(uiValue(spec->inverted, v)));
      control->invalid();
    }
  }
  if (changed) {
    syncDriveKnob();
    refreshModules();
  }
}

void Editor::syncDriveKnob() {
  NormalizedControl* knob = controls_[kDriveAmount];
  if (!knob) return;
  const bool namOn = seen_[kDriveNamOn] >= 0.5 || std::isnan(seen_[kDriveNamOn]);
  const Steinberg::Vst::ParamID active = driveEngineParam(namOn);
  driveParam_ = active;
  knob->setTag(static_cast<int32_t>(active));
  knob->setDefaultValue(static_cast<float>(defaultNorm(ctl_, active)));
  const double v = std::isnan(seen_[active]) ? defaultNorm(ctl_, active) : seen_[active];
  knob->setValueNormalized(static_cast<float>(v));
  knob->invalid();
}


// Dimming (module IN off or global bypass) and the Channel-only controls.
void Editor::refreshModules() {
  ModuleState state;
  state.globalBypass = seen_[kBypass] >= 0.5;
  globalBypass_ = state.globalBypass;
  for (const Module m : kModules) {
    const ControlSpec* in = inButtonOf(m);
    if (in && !std::isnan(seen_[in->id])) state.in[static_cast<int>(m)] = uiValue(in->inverted, seen_[in->id]) >= 0.5;
  }
  channelMode_ = isChannelMode(seen_[kStripMode]);
  namOn_ = seen_[kDriveNamOn] >= 0.5 || std::isnan(seen_[kDriveNamOn]);
  if (strip_) strip_->setSaturate(stripShowsSaturate(namOn_));

  for (const auto& spec : kControls) {
    NormalizedControl* control = controls_[spec.id];
    if (!control || spec.sharesView) continue;
    bool dim = false;
    if (spec.module != Module::None) dim = spec.isIn ? state.globalBypass : moduleDimmed(spec.module, state);
    if (spec.id == kDriveAmount && driveKnobDimmed(namOn_, modelState_ == 2)) dim = true;   // NAM without a model passes through
    control->setDimmed(dim);
    const bool visible = visibleInMode(spec.id, channelMode_);
    if (control->isVisible() != visible) {
      control->setVisible(visible);
      if (frame) frame->invalidRect(control->getViewSize());
    }
  }
}

const ControlSpec* Editor::specOf(CControl* control) const {
  const auto id = static_cast<ParamID>(control->getTag());
  return id < kNumParams ? specs_[id] : nullptr;
}

void Editor::valueChanged(CControl* control) {
  const ControlSpec* spec = specOf(control);
  if (!spec) return;
  const double ui = FairyTapes::Ui::sanitizeNorm(control->getValueNormalized(), control->getDefaultValue());
  const double v = paramValue(spec->inverted, ui);
  ctl_->performEdit(spec->id, v);
  seen_[spec->id] = ctl_->getParamNormalized(spec->id);
  if (spec->id == kDriveNamOn) syncDriveKnob();   // the Drive knob changes engine
  refreshModules();
}

void Editor::controlBeginEdit(CControl* control) {
  if (const ControlSpec* spec = specOf(control)) ctl_->beginEdit(spec->id);
}

void Editor::controlEndEdit(CControl* control) {
  if (const ControlSpec* spec = specOf(control)) ctl_->endEdit(spec->id);
}

} // namespace FairyTapes::TheStripper::Ui
