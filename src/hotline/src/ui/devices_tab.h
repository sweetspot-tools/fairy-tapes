/**
 * Fairy Tapes · Hotline — the DEVICES tab of the back (docs/DESIGN_SYSTEM.md: buttons that impose a configuration are explained
 * on the back, generated from the table the code uses): one card per device with its name (kDevices[i].name) and the lines of
 * describeDevice(i), laid out in a grid inside the custom area of the 1000 x 470 back (Back::kShort.custom, 952 x 346). The
 * lines are the engine's own text, never typed here. The grid is three columns by two rows, in the order of the Device buttons;
 * the card height follows the longest device (measured on the real table), and cardFits() says whether every line of a card is
 * inside it. Pure (no VSTGUI), unit tested (tests/test_back.cpp); ui/devices_tab.cpp draws the cards.
 */
#pragma once

#include "devices.h"

#include "fairy_ui/back_panel.h"

#include <string>
#include <vector>

namespace VSTGUI {
class CDrawContext;
struct CRect;
}

namespace FairyTapes::Hotline::Ui {

constexpr int kDeviceCardColumns = 3;
constexpr double kCardGap = 16;         // between cards, both ways
constexpr double kTitleFontSize = 13;   // the device name in the gap of the card's top border
constexpr double kLineFontSize = 12;    // the lines of describeDevice
constexpr double kLineStep = 16;
constexpr double kCardTitleHeight = 22; // from the top of the card to the top of the first line
constexpr double kCardPad = 10;         // from the last line (with its descender) to the bottom of the card
constexpr double kLineInset = 22;       // left of the lines (right too, for the width check)
constexpr double kTitleInset = 40;      // the title sits in the gap of the border: it must stay clear of the rounded corners
constexpr double kDescender = 3;

struct DeviceCard {
  int device = 0;
  std::string title;
  std::vector<std::string> lines;
  FairyTapes::Ui::BackBox box;
  double firstBaseline = 0;   // editor px of the back
  double step = kLineStep;
};

// Bold 11..13 px text: a generous width estimate (capitals are wide) so a line is never wider than the card.
inline double estimatedTextWidth(const std::string& s, double size) {
  std::size_t chars = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) ++chars;   // code points, not bytes
  return static_cast<double>(chars) * 0.70 * size;
}

inline std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : text) {
    if (c == '\n') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// The cards for `area`, in the order of kDevices.
inline std::vector<DeviceCard> devicesLayout(const FairyTapes::Ui::BackBox& area) {
  std::vector<DeviceCard> cards;
  const int rows = (kNumDevices + kDeviceCardColumns - 1) / kDeviceCardColumns;
  const double w = (area.w - kCardGap * (kDeviceCardColumns - 1)) / kDeviceCardColumns;
  const double h = (area.h - kCardGap * (rows - 1)) / rows;
  for (int i = 0; i < kNumDevices; ++i) {
    DeviceCard c;
    c.device = i;
    c.title = kDevices[i].name;
    c.lines = splitLines(describeDevice(i));
    c.box = {area.x + (w + kCardGap) * (i % kDeviceCardColumns), area.y + (h + kCardGap) * (i / kDeviceCardColumns), w, h};
    c.firstBaseline = c.box.y + kCardTitleHeight + kLineFontSize;
    cards.push_back(std::move(c));
  }
  return cards;
}

// Every line of the card is inside it, under the title, apart from the next one and not wider than the card.
inline bool cardFits(const DeviceCard& c) {
  if (c.lines.empty() || estimatedTextWidth(c.title, kTitleFontSize) > c.box.w - 2 * kTitleInset) return false;
  double prevBottom = c.box.y + kCardTitleHeight;
  for (std::size_t i = 0; i < c.lines.size(); ++i) {
    const double baseline = c.firstBaseline + c.step * static_cast<double>(i);
    if (baseline - kLineFontSize < prevBottom - 0.001) return false;
    if (baseline + kDescender > c.box.y + c.box.h - kCardPad) return false;
    if (estimatedTextWidth(c.lines[i], kLineFontSize) > c.box.w - 2 * kLineInset) return false;
    prevBottom = baseline + kDescender;
  }
  return true;
}

// The custom drawer of the DEVICES tab (ui/devices_tab.cpp): the cards inside `area`, the custom box of the back.
void drawDevicesTab(VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& area);

} // namespace FairyTapes::Hotline::Ui
