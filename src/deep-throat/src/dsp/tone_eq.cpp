#include "dsp/tone_eq.h"

#include <algorithm>
#include <cmath>

namespace FairyTapes::DeepThroat::DSP {

namespace {
constexpr double kMudHz = 250.0, kPresenceHz = 3500.0, kAirHz = 12000.0, kQ = 0.8;
constexpr double kSmoothMs = 30.0;

struct Curve { double mud, presence, air; };  // dB at t = +1
constexpr Curve kCurves[3] = {{-2.0, 3.0, 2.0}, {-1.5, 2.0, 4.0}, {-3.0, 1.0, 0.0}};

double step(double cur, double target, double k) {
  return std::fabs(target - cur) < 1e-3 ? target : cur + (target - cur) * k;
}
}  // namespace

void ToneEq::prepare(double sampleRate, int channels) {
  sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  ch_.assign(std::max(1, channels), Channel{});
  airHz_ = std::min(kAirHz, 0.45 * sr_);
  smoothCoeff_ = 1.0 - std::exp(-(refreshInterval_ / sr_) / (kSmoothMs * 0.001));
  updateTargets();
  reset();
}

void ToneEq::reset() {
  for (auto& c : ch_) {
    c.mud.reset(); c.presence.reset(); c.air.reset();
    c.counter = 0;
    snap(c);
  }
}

void ToneEq::setMode(Mode mode) { mode_ = mode; updateTargets(); }

void ToneEq::setTone(float tone01) {
  tone_ = std::isfinite(tone01) ? std::clamp(tone01, 0.0f, 1.0f) : 0.5f;
  updateTargets();
}

void ToneEq::updateTargets() {
  const int m = std::clamp(static_cast<int>(mode_), 0, 2);
  const double t = (static_cast<double>(tone_) - 0.5) * 2.0;
  targetMudDb_ = kCurves[m].mud * t;
  targetPresenceDb_ = kCurves[m].presence * t;
  targetAirDb_ = kCurves[m].air * t;
}

void ToneEq::snap(Channel& c) {
  c.mudDb = targetMudDb_; c.presenceDb = targetPresenceDb_; c.airDb = targetAirDb_;
  c.mud.setPeaking(sr_, kMudHz, kQ, c.mudDb);
  c.presence.setPeaking(sr_, kPresenceHz, kQ, c.presenceDb);
  c.air.setHighShelf(sr_, airHz_, c.airDb);
}

void ToneEq::refresh(Channel& c) {
  c.mudDb = step(c.mudDb, targetMudDb_, smoothCoeff_);
  c.presenceDb = step(c.presenceDb, targetPresenceDb_, smoothCoeff_);
  c.airDb = step(c.airDb, targetAirDb_, smoothCoeff_);
  c.mud.setPeaking(sr_, kMudHz, kQ, c.mudDb);
  c.presence.setPeaking(sr_, kPresenceHz, kQ, c.presenceDb);
  c.air.setHighShelf(sr_, airHz_, c.airDb);
}

float ToneEq::process(int channel, float x) {
  if (channel < 0 || channel >= static_cast<int>(ch_.size())) return x;
  Channel& c = ch_[channel];
  if (!std::isfinite(x)) x = 0.0f;
  if (++c.counter >= refreshInterval_) {
    c.counter = 0;
    refresh(c);
  }
  float y = c.mud.process(x);
  y = c.presence.process(y);
  return c.air.process(y);
}

}  // namespace FairyTapes::DeepThroat::DSP
