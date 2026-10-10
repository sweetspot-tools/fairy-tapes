// Pillow Talk GUI: runtime panel wear (scratches, stains, dry-brush residue, specks, tape bodies).
// Pure C++ (no VSTGUI, no SDK). Everything derives deterministically from (seed, level).
// renderWear / renderTapes write into the caller's RGBA8 buffer (straight, non-premultiplied alpha)
// and never allocate; only layoutFromSeed returns std::vectors.
#pragma once

#include <cstdint>
#include <vector>

namespace FairyTapes::PillowTalk::Ui {

enum class Wear { Off, Light, Heavy };

// Design space is 1000 x 628 units; a buffer of w x h pixels maps it with x*w/1000, y*h/628.
constexpr float kWearDesignW = 1000.f;
constexpr float kWearDesignH = 628.f;

struct Rect {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

// A strip of paper tape: centre, size and rotation (degrees, clockwise), in panel units.
struct TapePlacement {
  float cx = 0.f, cy = 0.f, w = 0.f, h = 0.f, rotDeg = 0.f;
};

// A corner screw. `missing`: only the empty hole is drawn.
struct ScrewPlacement {
  float x = 0.f, y = 0.f, angleDeg = 0.f;
  bool cross = false;
  bool missing = false;
};

struct WearLayout {
  std::vector<TapePlacement> tapes;
  std::vector<ScrewPlacement> screws;
};

// Base placement (temporary home; Task 4 moves it to layout.h). Reference 1000 x 628 space.
struct TapeSlot { float cx, cy; };
constexpr TapeSlot kTapeSlots[] = {
    {150.f, 150.f}, {500.f, 135.f}, {850.f, 150.f}, {200.f, 540.f}, {550.f, 560.f}, {880.f, 535.f},
};
constexpr int kNumTapeSlots = static_cast<int>(sizeof(kTapeSlots) / sizeof(kTapeSlots[0]));
constexpr float kScrewPos[4][2] = {{18.f, 18.f}, {982.f, 18.f}, {18.f, 610.f}, {982.f, 610.f}};

// Pure and deterministic. Tapes: 0 (Off), 1-2 (Light), 2-3 (Heavy), always fully inside `panel`.
// Screws: always 4 entries at the corners (scaled to `panel`), at most one missing.
// The screw layout does not depend on `w` (changing Wear never moves screws).
WearLayout layoutFromSeed(uint32_t seed, Wear w, const Rect& panel);

// Clears the buffer to transparent, then draws stains, residue, scratches and specks for `level`.
// `scale` is the integer host scale factor (>= 1), used only as a floor for hairline widths.
// Off leaves the buffer fully transparent. Never touches memory outside w*h*4 bytes.
void renderWear(uint8_t* rgba, int w, int h, int scale, uint32_t seed, Wear level);

// Clears the buffer, then draws the paper bodies of the tapes of layoutFromSeed(seed, level,
// {0,0,w,h}) (shadow, paper, fibres, and the faint residue marks). Text is drawn elsewhere.
void renderTapes(uint8_t* rgba, int w, int h, int scale, uint32_t seed, Wear level);

} // namespace FairyTapes::PillowTalk::Ui
