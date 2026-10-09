// Pillow Talk GUI: fixed-capacity history of the three display traces (no VSTGUI, no SDK).
#pragma once

#include <cmath>
#include <vector>

namespace FairyTapes::PillowTalk::Ui {

// Circular buffer; index 0 is the oldest sample. Allocates only in the constructor.
class History {
public:
  explicit History(int capacity) : data_(capacity > 0 ? static_cast<std::size_t>(capacity) : 0) {}

  void push(float voice, float send, float echo) {
    if (data_.empty()) return;
    data_[next_] = {clean(voice, 0.f), clean(send, 1.f), clean(echo, 0.f)};
    next_ = (next_ + 1) % data_.size();
    if (size_ < static_cast<int>(data_.size())) ++size_;
  }

  int size() const { return size_; }
  float voice(int i) const { return at(i, &Sample::voice, 0.f); }
  float send(int i) const { return at(i, &Sample::send, 1.f); }
  float echo(int i) const { return at(i, &Sample::echo, 0.f); }

private:
  struct Sample {
    float voice = 0.f, send = 1.f, echo = 0.f;
  };

  static float clean(float v, float fallback) {
    if (!std::isfinite(v)) return fallback;
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
  }

  float at(int i, float Sample::*member, float neutral) const {
    if (i < 0 || i >= size_) return neutral;
    const std::size_t oldest = (next_ + data_.size() - static_cast<std::size_t>(size_)) % data_.size();
    return data_[(oldest + static_cast<std::size_t>(i)) % data_.size()].*member;
  }

  std::vector<Sample> data_;
  std::size_t next_ = 0;
  int size_ = 0;
};

} // namespace FairyTapes::PillowTalk::Ui
