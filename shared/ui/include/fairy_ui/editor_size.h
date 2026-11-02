// Fairy Tapes shared UI: editor window size math (no SDK or VSTGUI dependency, unit tested).
// An editor is laid out at a fixed logical size; the host's content scale factor
// (IPlugViewContentScaleSupport) zooms the frame and scales the window in pixels. On top of it the user picks SIZE
// (75 / 90 / 100 / 125 %) on the back of the panel: each plug-in has its own size and keeps it in its own preferences
// file (no common file here, only the math and the labels).
#pragma once

#include <cmath>
#include <string>

namespace FairyTapes::Ui {

constexpr double kMaxContentScale = 8.0;

// Non-finite or <= 0 -> 1; values above kMaxContentScale are clamped.
inline double sanitizeContentScale(double factor) {
  if (!std::isfinite(factor) || factor <= 0.0) return 1.0;
  return factor > kMaxContentScale ? kMaxContentScale : factor;
}

struct PixelSize {
  int width, height;
};

// The SIZE choice of the editor.
enum class EditorSize { S75, S90, S100, S125 };

constexpr double sizeFactor(EditorSize size) {
  switch (size) {
  case EditorSize::S75: return 0.75;
  case EditorSize::S90: return 0.9;
  case EditorSize::S100: return 1.0;
  case EditorSize::S125: return 1.25;
  }
  return 1.0;
}

// Window size in pixels for a logical size, a (not yet sanitized) content scale factor and the SIZE choice.
inline PixelSize scaledSize(int logicalWidth, int logicalHeight, double hostFactor, EditorSize size) {
  const double f = sanitizeContentScale(hostFactor) * sizeFactor(size);
  return {static_cast<int>(std::lround(logicalWidth * f)), static_cast<int>(std::lround(logicalHeight * f))};
}

// Window size in pixels without a SIZE choice (= EditorSize::S100).
inline PixelSize scaledSize(int logicalWidth, int logicalHeight, double factor) {
  return scaledSize(logicalWidth, logicalHeight, factor, EditorSize::S100);
}

// The zoom to give the CFrame: the sanitized host factor times the SIZE factor.
inline double frameZoom(double hostFactor, EditorSize size) { return sanitizeContentScale(hostFactor) * sizeFactor(size); }

// "75 %" ... "125 %" for the SIZE row, and "75" ... "125" for a plug-in's preferences file (size=...).
inline const char* sizeLabel(EditorSize size) {
  switch (size) {
  case EditorSize::S75: return "75 %";
  case EditorSize::S90: return "90 %";
  case EditorSize::S100: return "100 %";
  case EditorSize::S125: return "125 %";
  }
  return "100 %";
}
inline const char* sizeKey(EditorSize size) {
  switch (size) {
  case EditorSize::S75: return "75";
  case EditorSize::S90: return "90";
  case EditorSize::S100: return "100";
  case EditorSize::S125: return "125";
  }
  return "100";
}

// Reads a key ("75") or a label ("75 %"); anything else returns false and leaves `out` alone.
inline bool parseSizeLabel(const std::string& text, EditorSize& out) {
  for (EditorSize e : {EditorSize::S75, EditorSize::S90, EditorSize::S100, EditorSize::S125})
    if (text == sizeKey(e) || text == sizeLabel(e)) {
      out = e;
      return true;
    }
  return false;
}

} // namespace FairyTapes::Ui
