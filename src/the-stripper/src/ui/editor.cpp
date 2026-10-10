// The Stripper GUI: the plugin editor. Layout from the approved preview (spec section 4).
#include "ui/editor.h"

#include "controller.h"
#include "ui/panel_view.h"

#include "fairy_ui/controls.h"
#include "fairy_ui/editor_size.h"
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
  seen_.fill(std::nan(""));
}

void Editor::createViews(CViewContainer* parent) {
  parent->addView(new PanelView(CRect(0, 0, kWidth, kHeight), &skin_));

  for (const auto& spec : kControls) {
    NormalizedControl* control = nullptr;
    const CRect rect = toCRect(spec.rect);
    const auto tag = static_cast<int32_t>(spec.id);
    switch (spec.kind) {
    case Kind::Knob: {
      auto* knob = new AnalogKnob(rect, this, tag, spec.label, &skin_);
      Controller* ctl = ctl_;
      const ParamID id = spec.id;
      knob->setValueFormatter([ctl, id](float v) { return paramString(ctl, id, v); });
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
}

void Editor::onTimer() { syncFromController(false); }

void Editor::syncFromController(bool force) {
  bool changed = force;
  for (ParamID id = 0; id < kNumParams; ++id) {
    const ControlSpec* spec = specs_[id];
    NormalizedControl* control = controls_[id];
    if (!spec || !control) continue;
    const double v = FairyTapes::Ui::sanitizeNorm(ctl_->getParamNormalized(id), defaultNorm(ctl_, id));
    if (!force && v == seen_[id]) continue;
    seen_[id] = v;
    changed = true;
    control->setValueNormalized(static_cast<float>(uiValue(spec->inverted, v)));
    control->invalid();
  }
  if (changed) refreshModules();
}

// Dimming (module IN off or global bypass) and the Channel-only controls.
void Editor::refreshModules() {
  ModuleState state;
  state.globalBypass = seen_[kBypass] >= 0.5;
  for (const Module m : kModules) {
    const ControlSpec* in = inButtonOf(m);
    if (in && !std::isnan(seen_[in->id])) state.in[static_cast<int>(m)] = uiValue(in->inverted, seen_[in->id]) >= 0.5;
  }
  channelMode_ = isChannelMode(seen_[kStripMode]);

  for (const auto& spec : kControls) {
    NormalizedControl* control = controls_[spec.id];
    if (!control) continue;
    bool dim = false;
    if (spec.module != Module::None) dim = spec.isIn ? state.globalBypass : moduleDimmed(spec.module, state);
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
  refreshModules();
}

void Editor::controlBeginEdit(CControl* control) {
  if (const ControlSpec* spec = specOf(control)) ctl_->beginEdit(spec->id);
}

void Editor::controlEndEdit(CControl* control) {
  if (const ControlSpec* spec = specOf(control)) ctl_->endEdit(spec->id);
}

} // namespace FairyTapes::TheStripper::Ui
