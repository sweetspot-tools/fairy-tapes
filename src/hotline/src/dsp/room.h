#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace FairyTapes::Hotline::DSP {

/**
 * Small room: Schroeder reverb (4 damped feedback combs in parallel, 2 allpasses in series).
 * y = x + amount * wet, amount 0..1; exactly x at amount 0. Buffers are sized in prepare() for
 * 192 kHz (delays scale with fs), so set()/process()/clear() never allocate. Feedback is set for
 * a ~0.5 s decay. Cheap set(), keeps the tail; non-finite amount is ignored, non-finite input
 * samples are read as 0.
 * spread > 0 (Freeverb-style stereo spread) adds spread * round(23 * fs / 44100) samples to every
 * delay, so a second instance gives a decorrelated tail; spread 0 is the original room.
 */
class Room {
public:
  void prepare(double fs, int spread = 0) {
    fs_ = std::clamp(fs > 0.0 ? fs : 48000.0, 1000.0, 192000.0);
    const double scale = fs_ / 44100.0;
    const int sp = std::max(0, spread);
    const int extra = sp * (int)std::lround(kSpreadBase * scale);           // this rate
    const int extraMax = sp * (int)std::lround(kSpreadBase * 192000.0 / 44100.0);  // buffer room
    for (int i = 0; i < kCombs; ++i) {
      comb_[i].len = std::max(1, (int)std::lround(kCombBase[i] * scale)) + extra;
      comb_[i].g = (float)std::pow(10.0, -3.0 * comb_[i].len / (fs_ * kRt60));
      comb_[i].buf.assign((size_t)std::ceil(kCombBase[i] * 192000.0 / 44100.0) + 2 + (size_t)extraMax, 0.f);
    }
    for (int i = 0; i < kAps; ++i) {
      ap_[i].len = std::max(1, (int)std::lround(kApBase[i] * scale)) + extra;
      ap_[i].buf.assign((size_t)std::ceil(kApBase[i] * 192000.0 / 44100.0) + 2 + (size_t)extraMax, 0.f);
    }
    amount_ = 0.f;
    clear();
    // measure the tail on the impulse response: last sample within 60 dB of the wet peak
    const int maxN = (int)(5.0 * fs_);
    double peak = 0;
    int last = 0;
    std::vector<float> h((size_t)maxN);
    for (int i = 0; i < maxN; ++i) { h[(size_t)i] = wet(i == 0 ? 1.f : 0.f); peak = std::max(peak, (double)std::fabs(h[(size_t)i])); }
    for (int i = 0; i < maxN; ++i) if (std::fabs(h[(size_t)i]) >= peak * 1e-3) last = i;
    tail_ = std::min(last + 1, maxN);
    clear();
  }
  void set(double amount) {
    if (!std::isfinite(amount)) return;
    const float a = (float)std::clamp(amount, 0.0, 1.0);
    const bool wasNeutral = amount_ <= 0.f;
    amount_ = a;
    if (wasNeutral && a > 0.f) clear();  // buffers are frozen while bypassed: drop them on re-activation
  }
  float process(float x) {
    if (amount_ <= 0.f) return x;
    if (!std::isfinite(x)) x = 0.f;
    return x + amount_ * wet(x);
  }
  void clear() {
    for (auto& c : comb_) { std::fill(c.buf.begin(), c.buf.end(), 0.f); c.pos = 0; c.lp = 0.f; }
    for (auto& a : ap_) { std::fill(a.buf.begin(), a.buf.end(), 0.f); a.pos = 0; }
  }
  bool neutral() const { return amount_ <= 0.f; }
  /** Samples after which the wet tail has fallen below -60 dB of its peak. */
  int tailSamples() const { return tail_; }

private:
  static constexpr int kCombs = 4, kAps = 2;
  static constexpr double kRt60 = 0.5, kDamp = 0.25;
  static constexpr int kCombBase[kCombs] = {1116, 1188, 1277, 1356};  // samples at 44.1 kHz
  static constexpr int kApBase[kAps] = {556, 441};
  static constexpr double kSpreadBase = 23.0;  // samples at 44.1 kHz per unit of spread
  struct Comb { std::vector<float> buf; int len = 1, pos = 0; float g = 0.f, lp = 0.f; };
  struct Ap { std::vector<float> buf; int len = 1, pos = 0; };

  static float flush(float v) { return std::fabs(v) < 1e-20f ? 0.f : v; }
  float wet(float x) {
    float s = 0.f;
    for (auto& c : comb_) {
      const float y = c.buf[(size_t)c.pos];
      c.lp = flush(y * (1.f - (float)kDamp) + c.lp * (float)kDamp);
      c.buf[(size_t)c.pos] = flush(x + c.lp * c.g);
      if (++c.pos >= c.len) c.pos = 0;
      s += y;
    }
    s *= 0.125f;  // comb average (1/4) at half level: amount 1 is a wet roughly level with the dry
    for (auto& a : ap_) {
      const float b = a.buf[(size_t)a.pos];
      a.buf[(size_t)a.pos] = flush(s + b * 0.5f);
      if (++a.pos >= a.len) a.pos = 0;
      s = b - s;
    }
    return s;
  }
  double fs_ = 48000.0;
  float amount_ = 0.f;
  int tail_ = 0;
  Comb comb_[kCombs];
  Ap ap_[kAps];
};

}  // namespace FairyTapes::Hotline::DSP
