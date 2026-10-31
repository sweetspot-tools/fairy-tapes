// Happy Ending GUI: the plugin editor. Layout from the preview (docs/mockups/happy_ending_rack.html, ui/layout.h);
// structure from Deep Throat's editor (layers in a frame or a plain container, the UI timer, the content scale), with
// the window also scaled by this plug-in's SIZE.
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

namespace FairyTapes::HappyEnding::Ui {

using namespace VSTGUI;

namespace {

constexpr uint32_t kTimerMs = 30;
constexpr CColor kFrameBackground(0x16, 0x16, 0x17);

// The user's preferences file; empty when the folder is unknown or the environment cannot be read.
std::filesystem::path userPrefsFile() {
  try {
    return prefsPath();
  } catch (...) {
    return {};
  }
}

} // namespace

Editor::Editor(Steinberg::Vst::EditController* controller) : VSTGUIEditor(controller) {
  loadAppearance(userPrefsFile());   // SIZE, so the host's first getSize is the window it will open
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
  const PixelSize px = windowSize();
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
  applyWindowSize();
  return Steinberg::kResultTrue;
#endif
}

void Editor::applyWindowSize() {
  const PixelSize px = windowSize();
  Steinberg::ViewRect r(rect.left, rect.top, rect.left + px.width, rect.top + px.height);
  setRect(r);   // getSize reports the scaled size even before the view is attached
  if (frame) frame->setZoom(FairyTapes::Ui::frameZoom(scale_, prefs_.size));   // views keep their logical coordinates
  if (root_ && plugFrame) plugFrame->resizeView(this, &r);   // shown: the host answers with onSize
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

  // The preferences again: SIZE may have changed in another instance since the view was made.
  loadAppearance(userPrefsFile());

  frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
  frame->setBackgroundColor(kFrameBackground);
  createViews(frame);
  const double zoom = FairyTapes::Ui::frameZoom(scale_, prefs_.size);
  if (zoom != 1.0) frame->setZoom(zoom);   // VSTGUIEditor::attached then reports the scaled size
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

void Editor::resetViewPointers() {
  root_ = nullptr;
  panel_ = nullptr;
  silk_ = nullptr;
  controls_.fill(nullptr);
}

void Editor::createViews(CViewContainer* parent) {
  const CRect all(0, 0, kWidth, kHeight);
  root_ = parent;

  // 1-2: panel body, grain, boxes, header lines, printed texts and engine tags.
  panel_ = new PanelView(all, &skin_);
  parent->addView(panel_);

  refreshUiState();
}

void Editor::onTimer() { refreshUiState(); }

void Editor::refreshUiState() {
  if (!panel_) return;
  auto* ctl = getController();
  double norm[kNumParams];
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) {
    const auto id = static_cast<Steinberg::Vst::ParamID>(i);
    const auto* p = ctl->getParameterObject(id);
    norm[i] = FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(id), p ? p->getInfo().defaultNormalizedValue : 0.0);
  }
  const int listen = listenFromNorm(FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(kListenBand), kDefaultListenNorm));
  const bool set = FairyTapes::Ui::sanitizeNorm(ctl->getParamNormalized(kSetActive), kDefaultSetNorm) >= 0.5;
  panel_->setUiState(uiStateFrom(norm, listen, set));
}

void Editor::valueChanged(CControl*) {}

void Editor::loadAppearance(const std::filesystem::path& file) {
  Prefs p;
  try {
    if (!file.empty()) p = loadPrefs(file);
  } catch (...) {
    p = Prefs{};
  }
  prefs_ = p;
  applyWindowSize();
}

} // namespace FairyTapes::HappyEnding::Ui
