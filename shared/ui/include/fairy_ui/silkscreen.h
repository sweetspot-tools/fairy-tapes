// Fairy Tapes shared UI: what the silkscreen layer prints (docs/DESIGN_SYSTEM.md, layer 3), independent of any plug-in.
// The plug-in describes its printed items (knobs, buttons, rows of choice buttons) and the block tags; this header
// decides where every dot and text goes (rack_geometry.h) and keeps the per-item state (value, dimmed, hidden).
// Pure, no VSTGUI: unit tested. SilkscreenView (fairy_ui/silkscreen_view.h) draws it.
#pragma once

#include "fairy_ui/rack_geometry.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace FairyTapes::Ui {

// Label: a name alone, centred on (cx, cy = baseline). Cycle: a cycling selector, name above the face and value under its LED.
enum class PrintKind { Knob, Button, Choice, Label, Cycle };

// One control as printed on the panel, in the coordinates of the silkscreen view.
struct PrintedItem {
  int id = 0;                                  // the plug-in's key (e.g. a parameter ID); several items may share one
  PrintKind kind = PrintKind::Knob;
  double cx = 0, cy = 0;                       // Knob: cap centre. Button: face centre. Choice: centre of the row of faces.
  double capRadius = Rack::kDefaultCapRadius;  // Knob
  double width = 0;                            // Choice: width of the row; one label over the centre of each equal cell
  std::string name;                            // Knob: name under the cap. Button: label above the face. Empty = none.
  std::string lo, hi;                          // Knob: end values at the ends of the scale. Empty = none.
  std::vector<std::string> labels;             // Choice: one label above each face
  bool showValue = true;                       // Knob: print the current value (yellow) under the name
};

// Where the tag of a block is printed (left end of the baseline), keyed by the plug-in's block number.
struct BlockTag {
  int key = 0;
  double x = 0, y = 0;
};

enum class TextRole { EndValue, Name, Value, Label };
enum class TextAlign { Left, Centre, Right };   // of the text relative to x

struct PrintDot {
  double x, y, r;
};
struct PrintText {
  std::string text;
  double x, y;   // y is the baseline
  TextAlign align;
  TextRole role;
};
struct PrintedParts {
  std::vector<PrintDot> dots;
  std::vector<PrintText> texts;
};

// Text sizes (bold) and the look of dimmed parts and tags, from the approved preview.
constexpr double kSilkEndSize = 8, kSilkNameSize = 12, kSilkValueSize = 10, kSilkLabelSize = 11, kSilkTagSize = 7;
constexpr float kSilkDimmedAlpha = 0.35f;   // a dimmed item (module out, global bypass)
constexpr double kSilkTagOpacity = 0.10;     // block tags are very faint on purpose

// Dots and texts of one item; `value` is the current value text (knobs only, printed when showValue and not empty).
inline PrintedParts printedParts(const PrintedItem& item, const std::string& value) {
  PrintedParts p;
  switch (item.kind) {
  case PrintKind::Knob: {
    const double cx = item.cx, cy = item.cy, r = item.capRadius;
    for (int d = 0; d < Rack::kScaleDots; ++d) {
      const Rack::Pt q = Rack::scaleDot(cx, cy, r, d);
      p.dots.push_back({q.x, q.y, Rack::scaleDotRadius(d)});
    }
    if (!item.lo.empty()) {
      const Rack::Pt q = Rack::endLabelLow(cx, cy, r);
      p.texts.push_back({item.lo, q.x, q.y, TextAlign::Right, TextRole::EndValue});
    }
    if (!item.hi.empty()) {
      const Rack::Pt q = Rack::endLabelHigh(cx, cy, r);
      p.texts.push_back({item.hi, q.x, q.y, TextAlign::Left, TextRole::EndValue});
    }
    if (!item.name.empty()) p.texts.push_back({item.name, cx, Rack::nameBaseline(cy, r), TextAlign::Centre, TextRole::Name});
    if (item.showValue && !value.empty())
      p.texts.push_back({value, cx, Rack::valueBaseline(cy, r), TextAlign::Centre, TextRole::Value});
    break;
  }
  case PrintKind::Button:
    if (!item.name.empty()) p.texts.push_back({item.name, item.cx, item.cy + Rack::kLabelOffsetY, TextAlign::Centre, TextRole::Label});
    break;
  case PrintKind::Cycle:
    if (!item.name.empty()) p.texts.push_back({item.name, item.cx, item.cy + Rack::kLabelOffsetY, TextAlign::Centre, TextRole::Label});
    if (!value.empty()) p.texts.push_back({value, item.cx, item.cy + Rack::kCycleValueOffsetY, TextAlign::Centre, TextRole::Value});
    break;
  case PrintKind::Label:
    if (!item.name.empty()) p.texts.push_back({item.name, item.cx, item.cy, TextAlign::Centre, TextRole::Name});
    break;
  case PrintKind::Choice: {
    const std::size_t n = item.labels.size();
    const double left = item.cx - item.width / 2;
    for (std::size_t k = 0; k < n; ++k) {
      if (item.labels[k].empty()) continue;
      const double x = left + item.width * (static_cast<double>(k) + 0.5) / static_cast<double>(n);
      p.texts.push_back({item.labels[k], x, item.cy + Rack::kLabelOffsetY, TextAlign::Centre, TextRole::Label});
    }
    break;
  }
  }
  return p;
}

// Left end of a text of measured width `w` aligned at `x`.
constexpr double alignedLeft(double x, double w, TextAlign a) {
  return a == TextAlign::Left ? x : a == TextAlign::Right ? x - w : x - w / 2;
}

// The printed items, the tags and their state. Setters return true when something visible changed (redraw).
class SilkscreenModel {
public:
  struct Tag {
    BlockTag slot;
    std::string text;
    bool dimmed = false;
  };

  SilkscreenModel(std::vector<PrintedItem> items, std::vector<BlockTag> tags) : items_(std::move(items)) {
    state_.resize(items_.size());
    for (const BlockTag& t : tags) tags_.push_back({t, {}, false});
  }

  // Every item with this id (unknown ids are ignored).
  bool setValueText(int id, const std::string& text) {
    return forEach(id, [&](State& s) { return assign(s.value, text); });
  }
  bool setDimmed(int id, bool dimmed) {
    return forEach(id, [&](State& s) { return assign(s.dimmed, dimmed); });
  }
  bool setHidden(int id, bool hidden) {
    return forEach(id, [&](State& s) { return assign(s.hidden, hidden); });
  }
  bool setTag(int key, const std::string& text) {
    Tag* t = tag(key);
    return t && assign(t->text, text);
  }
  bool setTagDimmed(int key, bool dimmed) {
    Tag* t = tag(key);
    return t && assign(t->dimmed, dimmed);
  }

  std::size_t size() const { return items_.size(); }
  const PrintedItem& item(std::size_t i) const { return items_[i]; }
  const std::string& value(std::size_t i) const { return state_[i].value; }
  bool dimmed(std::size_t i) const { return state_[i].dimmed; }
  bool hidden(std::size_t i) const { return state_[i].hidden; }
  PrintedParts parts(std::size_t i) const { return printedParts(items_[i], state_[i].value); }
  const std::vector<Tag>& tags() const { return tags_; }

private:
  struct State {
    std::string value;
    bool dimmed = false, hidden = false;
  };
  template <typename T>
  static bool assign(T& slot, const T& v) {
    if (slot == v) return false;
    slot = v;
    return true;
  }
  template <typename F>
  bool forEach(int id, F&& f) {
    bool changed = false;
    for (std::size_t i = 0; i < items_.size(); ++i)
      if (items_[i].id == id) changed = f(state_[i]) || changed;
    return changed;
  }
  Tag* tag(int key) {
    for (Tag& t : tags_)
      if (t.slot.key == key) return &t;
    return nullptr;
  }

  std::vector<PrintedItem> items_;
  std::vector<State> state_;
  std::vector<Tag> tags_;
};

} // namespace FairyTapes::Ui
