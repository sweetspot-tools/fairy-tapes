/**
 * Fairy Tapes shared UI — the back of the panel without VSTGUI (docs/DESIGN_SYSTEM.md, "Retro del pannello"). The back
 * has a fixed template for the whole suite (stripper_rack.html, pillow_rack.html): the current tab as the big title
 * with "FAIRY TAPES" under it, the "PLUGIN · DESCRIPTION" line at the top right, the tab links and "<- BACK" on the
 * "FAIRY TAPES" line; SETTINGS = APPEARANCE (left, saved for the user) and BEHAVIOUR (right, saved with the project)
 * with rows of segmented choices, steppers and push buttons, NOTES under them; CREDITS = the plug-in box (version and
 * license), COMPONENTS AND LICENSES, NOTICES. Only the contents change: a plug-in supplies them as BackContent (items
 * with labels, current values and callbacks, the credits, optional custom tabs) and BackView (fairy_ui/back_view.h)
 * draws the template and handles the clicks.
 *
 * Header-only and free of VSTGUI so it is unit tested (tests/test_back_panel.cpp). Generalised from The Stripper's back
 * (src/the-stripper/src/ui/back_panel.h, back_view.cpp): same geometry, word wrapping ported as is. All visible text is
 * English. Coordinates are editor px of the 1000 x 628 rack panel.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace FairyTapes::Ui {

struct BackBox {
  double x = 0, y = 0, w = 0, h = 0;
};

// ---- The template ---------------------------------------------------------------------------------------------------
namespace Back {
constexpr double kWidth = 1000, kHeight = 628;
constexpr double kTitleX = 34, kTitleBaseline = 58, kTitleSize = 38;      // the current tab, bold italic
constexpr double kBrandX = 36, kBrandBaseline = 78;                       // "FAIRY TAPES"
constexpr double kHeaderRight = 966, kHeaderBaseline = 46;                // "PLUGIN · DESCRIPTION", right-aligned
constexpr double kLinkTop = 64, kLinkHeight = 20, kLinkWidth = 86;        // tab links and "<- BACK", text right-aligned
constexpr double kLinkBaseline = 78;
constexpr double kLastTabRight = 880, kTabSpacing = 90;
constexpr int kMaxTabs = 4;

constexpr BackBox kAppearanceBox{24, 102, 462, 364};
constexpr BackBox kBehaviourBox{514, 102, 462, 364};
constexpr BackBox kNotesBox{24, 484, 952, 120};
constexpr double kSubtitleBaseline = 130;                                 // "saved for you, ..." under the box titles
constexpr double kFirstRowY = 146, kRowStep = 48, kRowHeight = 24;
constexpr double kLabelInset = 26, kControlInset = 176;                   // from the left edge of the column box
constexpr int kMaxRows = 7;
constexpr double kArrowWidth = 24, kArrowGap = 2;                         // stepper: < name >
constexpr double kNoteGap = 12;                                           // between a control and its note
constexpr double kNotesFirstBaseline = 522, kNotesStep = 22;
constexpr int kMaxNotes = 4;

constexpr BackBox kCreditsTitleBox{24, 102, 952, 118};
constexpr double kVersionBaseline = 140, kLicenseFirstBaseline = 164, kLicenseStep = 20;
constexpr int kMaxLicenseLines = 3;
constexpr BackBox kComponentsBox{24, 246, 952, 252};
constexpr double kCreditFirstBaseline = 284, kCreditMaxStep = 30, kCreditLastBaseline = 480;
constexpr double kCreditNameX = 50, kCreditAuthorX = 350, kCreditLicenseRight = 950;
constexpr BackBox kNoticesBox{24, 522, 952, 88};
constexpr double kNoticesFirstBaseline = 556, kNoticesStep = 21;
constexpr int kMaxNotices = 3;
constexpr BackBox kCustomBox{24, 102, 952, 508};                          // a custom tab (e.g. BUTTONS) gets this area
} // namespace Back

// The link of tab `index` of `count` (right-aligned text), and "<- BACK" at the right edge.
inline BackBox tabLinkBox(int index, int count) {
  const double right = Back::kLastTabRight - Back::kTabSpacing * (count - 1 - index);
  return {right - Back::kLinkWidth, Back::kLinkTop, Back::kLinkWidth, Back::kLinkHeight};
}
inline BackBox backLinkBox() {
  return {Back::kHeaderRight - Back::kLinkWidth, Back::kLinkTop, Back::kLinkWidth, Back::kLinkHeight};
}
// "BACK ->" on the front panel: the same place as "<- BACK" on the back (one link only, DESIGN_SYSTEM.md).
inline BackBox frontBackLinkBox() { return backLinkBox(); }

// ---- What a plug-in supplies -----------------------------------------------------------------------------------------
enum class BackColumn { Appearance, Behaviour };
enum class BackItemKind {
  Choice,    // a row of exclusive options (a toggle is a choice of two); onChange(index)
  Stepper,   // "<" name ">", wraps around; onChange(index)
  Button,    // a grey push button, options[0] is its caption; onPress()
};

struct BackItem {
  int key = 0;   // unique; the plug-in uses it to refresh the item (BackView::setSelected)
  BackColumn column = BackColumn::Appearance;
  BackItemKind kind = BackItemKind::Choice;
  std::string label;   // white, 13 px (e.g. "WEAR")
  std::string hint;    // dim, 10 px, under the label (may be empty)
  std::vector<std::string> options;
  int selected = 0;
  double cellWidth = 84;     // Choice: width of one option; Stepper: width of the name; Button: width of the button
  std::string note;          // dim, 10 px, right of the control (e.g. "new wear, dirt and screws")
  bool enabled = true;       // a disabled item is drawn at 40 % and ignores clicks
  bool tapeFontOptions = false;   // Stepper: option i is written in tape font i (Zeyada, Rock Salt, Homemade Apple)
  std::function<void(int)> onChange;
  std::function<void()> onPress;
};

inline BackItem choiceItem(int key, BackColumn column, std::string label, std::string hint, std::vector<std::string> options,
                           int selected, double cellWidth = 84) {
  BackItem it;
  it.key = key;
  it.column = column;
  it.kind = BackItemKind::Choice;
  it.label = std::move(label);
  it.hint = std::move(hint);
  it.options = std::move(options);
  it.selected = selected;
  it.cellWidth = cellWidth;
  return it;
}
inline BackItem toggleItem(int key, BackColumn column, std::string label, std::string hint, std::string offText, std::string onText,
                           bool on, double cellWidth = 84) {
  return choiceItem(key, column, std::move(label), std::move(hint), {std::move(offText), std::move(onText)}, on ? 1 : 0, cellWidth);
}
inline BackItem stepperItem(int key, BackColumn column, std::string label, std::string hint, std::vector<std::string> options,
                            int selected, bool tapeFontOptions = false, double nameWidth = 176) {
  BackItem it = choiceItem(key, column, std::move(label), std::move(hint), std::move(options), selected, nameWidth);
  it.kind = BackItemKind::Stepper;
  it.tapeFontOptions = tapeFontOptions;
  return it;
}
inline BackItem buttonItem(int key, BackColumn column, std::string label, std::string caption, std::string note, double width = 120) {
  BackItem it = choiceItem(key, column, std::move(label), {}, {std::move(caption)}, 0, width);
  it.kind = BackItemKind::Button;
  it.note = std::move(note);
  return it;
}

enum class BackTabKind { Settings, Credits, Custom };
struct BackTab {
  std::string label;   // "SETTINGS", "CREDITS", "BUTTONS", ... (also the big title when active)
  BackTabKind kind = BackTabKind::Custom;
};

struct CreditEntry {
  std::string name, author, license;
};
struct BackCredits {
  std::string title;                       // the plug-in box ("PILLOW TALK")
  std::string versionLine;                 // "Version 1.0.0  ·  Fairy Tapes  ·  github.com/sweetspot-tools"
  std::vector<std::string> licenseLines;   // at most kMaxLicenseLines
  std::vector<CreditEntry> entries;        // COMPONENTS AND LICENSES
  std::vector<std::string> notices;        // at most kMaxNotices
};

struct BackContent {
  std::string headerLine;              // "PILLOW TALK  ·  TAPE ECHO VST3"
  std::vector<BackTab> tabs;           // SETTINGS and CREDITS at least
  std::vector<BackItem> items;         // in row order within each column
  std::vector<std::string> notes;      // NOTES box of SETTINGS, at most kMaxNotes
  BackCredits credits;
};

// ---- Geometry of the items ---------------------------------------------------------------------------------------
inline const BackBox& columnBox(BackColumn c) { return c == BackColumn::Behaviour ? Back::kBehaviourBox : Back::kAppearanceBox; }
inline double labelX(BackColumn c) { return columnBox(c).x + Back::kLabelInset; }
inline double itemRowY(int row) { return Back::kFirstRowY + Back::kRowStep * row; }

// The row of items[index] within its column (items are laid out in order, one row each).
inline int itemRow(const std::vector<BackItem>& items, std::size_t index) {
  int row = 0;
  for (std::size_t i = 0; i < index && i < items.size(); ++i)
    if (items[i].column == items[index].column) ++row;
  return row;
}

inline double itemControlWidth(const BackItem& it) {
  switch (it.kind) {
  case BackItemKind::Choice: return it.cellWidth * static_cast<double>(it.options.size());
  case BackItemKind::Stepper: return 2 * Back::kArrowWidth + 2 * Back::kArrowGap + it.cellWidth;
  case BackItemKind::Button: return it.cellWidth;
  }
  return 0;
}
inline BackBox itemControlBox(const BackItem& it, int row) {
  return {columnBox(it.column).x + Back::kControlInset, itemRowY(row), itemControlWidth(it), Back::kRowHeight};
}

// ---- Hit tests -------------------------------------------------------------------------------------------------------
inline bool contains(const BackBox& b, double x, double y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }
// The option under x in a row of `count` equal cells, or -1.
inline int segmentAt(const BackBox& b, int count, double x) {
  if (count <= 0 || !(x >= b.x && x < b.x + b.w)) return -1;
  const int i = static_cast<int>((x - b.x) / (b.w / count));
  return i >= 0 && i < count ? i : -1;
}
// -1 on the left arrow, +1 on the right arrow, 0 on the name.
inline int stepperDirectionAt(const BackBox& b, double x) {
  if (x < b.x + Back::kArrowWidth) return -1;
  if (x >= b.x + b.w - Back::kArrowWidth) return 1;
  return 0;
}
inline int wrapIndex(int current, int delta, int count) { return count > 0 ? ((current + delta) % count + count) % count : 0; }
inline int clampIndex(int i, int count) { return count > 0 ? std::clamp(i, 0, count - 1) : 0; }

// ---- Credits -----------------------------------------------------------------------------------------------------------
inline double creditRowStep(int count) {
  if (count <= 1) return Back::kCreditMaxStep;
  return std::min(Back::kCreditMaxStep, (Back::kCreditLastBaseline - Back::kCreditFirstBaseline) / (count - 1));
}
inline double creditBaseline(int index, int count) { return Back::kCreditFirstBaseline + creditRowStep(count) * index; }

// ---- Lookup --------------------------------------------------------------------------------------------------------
inline int findItem(const BackContent& c, int key) {
  for (std::size_t i = 0; i < c.items.size(); ++i)
    if (c.items[i].key == key) return static_cast<int>(i);
  return -1;
}
inline int tabIndexOf(const BackContent& c, BackTabKind kind) {
  for (std::size_t i = 0; i < c.tabs.size(); ++i)
    if (c.tabs[i].kind == kind) return static_cast<int>(i);
  return -1;
}

// What is wrong with the content for the template (empty = fine): mandatory tabs, rows and controls inside their box,
// unique keys, valid selections, complete credits, line counts. Plug-ins check their content in a unit test.
inline std::vector<std::string> backContentProblems(const BackContent& c) {
  std::vector<std::string> out;
  if (c.tabs.empty() || static_cast<int>(c.tabs.size()) > Back::kMaxTabs) out.push_back("tabs: 1 to 4 tabs");
  if (tabIndexOf(c, BackTabKind::Settings) < 0) out.push_back("tabs: no SETTINGS tab");
  if (tabIndexOf(c, BackTabKind::Credits) < 0) out.push_back("tabs: no CREDITS tab");
  for (const BackTab& t : c.tabs)
    if (t.label.empty()) out.push_back("tabs: a tab without a label");
  std::set<int> keys;
  for (std::size_t i = 0; i < c.items.size(); ++i) {
    const BackItem& it = c.items[i];
    const std::string name = it.label.empty() ? "(no label)" : it.label;
    if (!keys.insert(it.key).second) out.push_back(name + ": key used twice");
    if (it.label.empty()) out.push_back("item without a label");
    if (it.options.empty()) out.push_back(name + ": no options");
    else if (it.kind != BackItemKind::Button && (it.selected < 0 || it.selected >= static_cast<int>(it.options.size())))
      out.push_back(name + ": selected out of range");
    const int row = itemRow(c.items, i);
    if (row >= Back::kMaxRows) out.push_back(name + ": too many rows in the column");
    const BackBox b = itemControlBox(it, row), col = columnBox(it.column);
    if (b.x + b.w > col.x + col.w - 10 || b.y + b.h > col.y + col.h - 8) out.push_back(name + ": the control leaves its box");
  }
  if (static_cast<int>(c.notes.size()) > Back::kMaxNotes) out.push_back("notes: more than 4 lines");
  if (c.credits.title.empty() || c.credits.versionLine.empty()) out.push_back("credits: no title or version");
  if (static_cast<int>(c.credits.licenseLines.size()) > Back::kMaxLicenseLines) out.push_back("credits: more than 3 license lines");
  if (static_cast<int>(c.credits.notices.size()) > Back::kMaxNotices) out.push_back("credits: more than 3 notices");
  if (c.credits.entries.empty()) out.push_back("credits: no components");
  for (const CreditEntry& e : c.credits.entries)
    if (e.name.empty() || e.author.empty() || e.license.empty()) out.push_back("credit entry with an empty field");
  return out;
}

// ---- Word wrapping (custom tabs) ----------------------------------------------------------------------------------------
// True when the string is well-formed UTF-8 (no truncated or stray continuation bytes).
inline bool decodeValid(const std::string& s) {
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    if (len == 0 || i + len > s.size()) return false;
    for (std::size_t k = 1; k < len; ++k)
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    i += len;
  }
  return true;
}

// Breaks `text` into lines no wider than `maxWidth` (`measure` gives the pixel width of a string), at spaces; a word wider than
// a line is cut (never in the middle of a UTF-8 sequence). Nothing is dropped.
inline std::vector<std::string> wrapText(const std::string& text, double maxWidth, const std::function<double(const std::string&)>& measure) {
  std::vector<std::string> lines;
  std::vector<std::string> words;
  {
    std::string w;
    for (char c : text) {
      if (c == ' ' || c == '\n') {
        if (!w.empty()) words.push_back(w);
        w.clear();
      } else {
        w.push_back(c);
      }
    }
    if (!w.empty()) words.push_back(w);
  }
  std::string line;
  auto flush = [&] {
    if (!line.empty()) lines.push_back(line);
    line.clear();
  };
  for (const std::string& word : words) {
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (measure(candidate) <= maxWidth) {
      line = candidate;
      continue;
    }
    flush();
    std::string rest = word;
    while (!rest.empty() && measure(rest) > maxWidth) {   // a word wider than the line: cut it by measured width
      std::size_t cut = rest.size();
      while (cut > 0) {
        do { --cut; } while (cut > 0 && (static_cast<unsigned char>(rest[cut]) & 0xC0) == 0x80);
        if (cut == 0 || measure(rest.substr(0, cut)) <= maxWidth) break;
      }
      if (cut == 0) {   // not even one character fits: take one anyway so the text still ends
        cut = 1;
        while (cut < rest.size() && (static_cast<unsigned char>(rest[cut]) & 0xC0) == 0x80) ++cut;
      }
      lines.push_back(rest.substr(0, cut));
      rest = rest.substr(cut);
    }
    line = rest;
  }
  flush();
  return lines;
}

} // namespace FairyTapes::Ui
