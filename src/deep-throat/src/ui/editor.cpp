// Deep Throat GUI: the plugin editor. Layout from the approved rack preview (docs/mockups/deep_throat_rack.html,
// ui/layout.h).
#include "ui/editor.h"

#include "ui/panel_view.h"

#include "fairy_ui/sanitize.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <cstring>

namespace FairyTapes::DeepThroat::Ui {

using namespace VSTGUI;

namespace {

constexpr uint32_t kTimerMs = 30;
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);

bool switchOn(Steinberg::Vst::EditController* ctl, Steinberg::Vst::ParamID id, double fallback) {
  return FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(id), fallback) >= 0.5;
}

} // namespace

Editor::Editor(Steinberg::Vst::EditController* controller) : VSTGUIEditor(controller) {
  const PixelSize px = scaledEditorSize(scale_);
  setRect(Steinberg::ViewRect(0, 0, px.width, px.height));
}

Editor::~Editor() {
  if (frame) close();
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
  if (frame) {
    frame->removeAll(true);
    if (frame->getNbReference() == 1)
      frame->close();
    else
      frame->forget();
    frame = nullptr;
  }
  resetViewPointers();
}

void Editor::resetViewPointers() { panel_ = nullptr; }

void Editor::createViews(CViewContainer* parent) {
  // 1-2: panel body, grain, boxes and printed texts.
  panel_ = new PanelView(CRect(0, 0, kWidth, kHeight), &skin_);
  parent->addView(panel_);
  refreshDimming();
}

void Editor::onTimer() { refreshDimming(); }

void Editor::refreshDimming() {
  if (!panel_) return;
  auto* ctl = getController();
  DimState s;
  s.bypass = switchOn(ctl, kBypass, 0.0);
  for (int m = 1; m <= 6; ++m) s.in[m - 1] = switchOn(ctl, inParamOf(static_cast<Module>(m)), 1.0);
  panel_->setDimState(s);
}

} // namespace FairyTapes::DeepThroat::Ui
