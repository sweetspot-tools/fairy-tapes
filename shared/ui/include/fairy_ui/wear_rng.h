// Fairy Tapes shared UI: the seeded random generator of the wear (mulberry32). The same seed gives the same sequence on
// every platform, so a plug-in instance looks the same every time its editor opens.
#pragma once

#include <cstddef>
#include <cstdint>

namespace FairyTapes::Ui {

class WearRng {
public:
  explicit WearRng(std::uint32_t seed) : a_(seed) {}
  double next() {
    a_ += 0x6D2B79F5u;
    std::uint32_t t = a_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>((t ^ (t >> 14))) / 4294967296.0;
  }
  double range(double lo, double hi) { return lo + next() * (hi - lo); }
  std::size_t pick(std::size_t n) { return static_cast<std::size_t>(next() * static_cast<double>(n)) % n; }

private:
  std::uint32_t a_;
};

}  // namespace FairyTapes::Ui
