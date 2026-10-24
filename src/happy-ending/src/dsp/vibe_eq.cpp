#include "dsp/vibe_eq.h"
#include "params.h"
#include "tuning.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace FairyTapes::HappyEnding::DSP {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Calibration of the port (Multi-Q Tube EQ, measured on passive tube program EQ hardware).
// What is voiced by ear lives in tuning.h.
constexpr double kPeakInteraction = 0.08;  // the boost grows a little with the cut
constexpr double kBaseQ = 0.55;            // base width of the LOW resonance
constexpr double kQInteraction = 0.015;    // the Q sharpens with the cut
constexpr double kDipInteraction = 0.06;   // the cut deepens a little with the boost
constexpr double kLowSatQ = 0.25;          // core saturation lowers the LOW Q by up to this share
constexpr double kAirSatQ = 0.20;          // the same for the AIR boost
constexpr double kLowInductorDrive = 0.3;  // inductor drive per step of the LOW amount
constexpr double kAirInductorDrive = 0.2;  // inductor drive per step of the AIR amount
constexpr double kStateLimit = 8.0;        // safety clamp of the filter memory

double tableValue(ParamID id, int index) {
  const ValueTable t = valueTable(id);
  const int last = static_cast<int>(t.size) - 1;
  return t.data[std::clamp(index, 0, last)];
}

// ADAA of the tube polynomial f(v) = b v^2 + c v^3 + d v^4 (Multi-Q ADAASaturation.h, first order).
constexpr double kPolyB = 0.015;   // H2, dominant (tube and transformer asymmetry)
constexpr double kPolyC = 0.002;   // H3, secondary
constexpr double kPolyD = 0.0005;  // H4, negligible
constexpr double kAdaaEps = 1e-9;

inline double poly(double v) { return v * v * (kPolyB + v * (kPolyC + v * kPolyD)); }
inline double polyAntiderivative(double v) {
  const double v3 = v * v * v;
  return v3 * (kPolyB / 3.0 + v * (kPolyC / 4.0 + v * kPolyD / 5.0));
}

} // namespace

// ---------------------------------------------------------------------------------------------
// InductorModel
// ---------------------------------------------------------------------------------------------

void InductorModel::prepare(double sampleRate, std::uint32_t characterSeed) {
  rmsDecay_ = std::exp(-1.0 / (0.045 * sampleRate));     // 45 ms RMS integration
  fluxDecay_ = std::exp(-1.0 / (0.00075 * sampleRate));  // 0.75 ms core flux
  hystDecay_ = std::exp(-1.0 / (0.00028 * sampleRate));  // 0.28 ms hysteresis
  reset();

  // Component tolerance: +-5 % on Q and +-2 % on the saturation threshold. Deterministic: the
  // same seed always gives the same unit (default: from the sample rate).
  const std::uint32_t seed = characterSeed != 0 ? characterSeed : static_cast<std::uint32_t>(sampleRate * 1000.0);
  std::mt19937 gen(seed);
  std::uniform_real_distribution<float> qDist(0.95f, 1.05f);
  std::uniform_real_distribution<float> satDist(0.98f, 1.02f);
  qVariation_ = qDist(gen);
  satVariation_ = satDist(gen);
}

void InductorModel::reset() {
  prevInput_ = hysteresisState_ = coreFlux_ = 0.0;
  rmsLevel_ = saturation_ = 0.0;
}

// Smooth fit of the inductor Q against frequency: peak around 250 Hz, falling at both ends
// (core losses at the bottom, skin effect and winding capacitance at the top).
double InductorModel::qShape(double hz) {
  const double centered = std::log10(std::max(hz, 10.0)) - 2.4;
  const double c2 = centered * centered;
  return std::max(1.0 - 0.22 * c2 - 0.04 * c2 * c2, 0.25);
}

double InductorModel::staticQ(double hz, double baseQ) const { return baseQ * qShape(hz) * qVariation_; }

double InductorModel::frequencyDependentQ(double hz, double baseQ) const {
  // Hot program raises the core losses: up to 15 % less Q.
  const double program = std::max(1.0 - rmsLevel_ * 0.15, 0.75);
  return staticQ(hz, baseQ) * program;
}

// B-H curve saturation plus hysteresis. Also tracks the saturation depth (inductance drops as the
// core saturates, which lowers the Q of the resonance).
double InductorModel::processNonlinearity(double input, double driveLevel) {
  if (!std::isfinite(input)) return 0.0;

  rmsLevel_ = rmsLevel_ * rmsDecay_ + input * input * (1.0 - rmsDecay_);
  const double rmsValue = std::sqrt(rmsLevel_);

  // Hot program lowers the threshold (core heating).
  const double threshold = std::max((0.65 - rmsValue * 0.15) * satVariation_, 0.35);

  double saturated = input;
  const double absInput = std::abs(input);
  if (absInput > threshold) {
    const double excess = (absInput - threshold) / (1.0 - threshold);
    const double langevin = std::tanh(excess * 2.5 * (1.0 + driveLevel));
    const double compressed = threshold + langevin * (1.0 - threshold) * 0.7;
    saturated = std::copysign(compressed, input);

    // H3 dominant (symmetric core), a little H2 from the residual asymmetry.
    saturated += 0.012 * driveLevel * excess * input * absInput;
    saturated += 0.018 * driveLevel * driveLevel * excess * input * input * input;

    saturation_ = saturation_ * 0.95 + excess * 0.05;
  } else {
    saturation_ *= 0.95;
  }

  // Hysteresis: a wider loop at higher drive.
  const double delta = saturated - prevInput_;
  coreFlux_ = std::clamp(coreFlux_ * fluxDecay_ + delta * 0.08 * driveLevel, -0.15, 0.15);
  hysteresisState_ = hysteresisState_ * hystDecay_ + coreFlux_ * (1.0 - hystDecay_);
  const double output = saturated + hysteresisState_ * 0.5;

  prevInput_ = input;
  return output;
}

double InductorModel::saturationLevel() const { return std::min(saturation_, 1.0); }
double InductorModel::rmsLevel() const { return std::sqrt(rmsLevel_); }

// ---------------------------------------------------------------------------------------------
// VibeTubeStage
// ---------------------------------------------------------------------------------------------
//
// Per sample: low split at about 300 Hz (feeds the transformer H3); polynomial waveshaper with
// ADAA on drive * 2 * input, divided by the drive again so that the small-signal gain stays 1
// (H2 grows with the drive, H3 with its square); extra H3 on the low band only; DC blocker for the
// even-order terms. Calibrated to a passive tube program EQ (0.15 % THD at 1 kHz, H2 dominant).

void VibeTubeStage::prepare(double sampleRate) {
  lfCoeff_ = 1.0 - std::exp(-2.0 * kPi * 300.0 / sampleRate);
  dcCoeff_ = std::clamp(1.0 - 2.0 * kPi * 8.0 / sampleRate, 0.9, 0.9999);
  reset();
}

void VibeTubeStage::reset() { ch_ = {}; }

double VibeTubeStage::processSample(double input, int ch, double drive) {
  if (!std::isfinite(input)) return 0.0;
  Channel& c = ch_[static_cast<std::size_t>(ch)];

  c.lf += lfCoeff_ * (input - c.lf);

  const double driveAmount = drive * 2.0;
  const double xdRaw = input * driveAmount;
  const double xd = xdRaw / std::sqrt(1.0 + xdRaw * xdRaw * 0.25);  // no polynomial explosion when hot

  const double dx = xd - c.prevXd;
  double distortion = std::abs(dx) < kAdaaEps ? poly(0.5 * (xd + c.prevXd))
                                              : (polyAntiderivative(xd) - polyAntiderivative(c.prevXd)) / dx;
  c.prevXd = xd;

  const double xdLfRaw = c.lf * driveAmount;
  const double xdLf = xdLfRaw / std::sqrt(1.0 + xdLfRaw * xdLfRaw * 0.25);
  distortion += 0.025 * xdLf * xdLf * xdLf;  // transformer H3, low band only

  const double y = input + distortion / driveAmount;

  const double out = y - c.dcX1 + dcCoeff_ * c.dcY1;
  c.dcX1 = y;
  c.dcY1 = out;
  return std::isfinite(out) ? out : input;
}

void VibeTubeStage::process(float* left, float* right, int n, double driveStart, double driveEnd) {
  if (n <= 0) return;
  const double step = (driveEnd - driveStart) / n;
  double drive = driveStart;
  for (int i = 0; i < n; ++i) {
    drive += step;
    if (drive < 1e-6) continue;  // no stage: the sample passes untouched
    left[i] = static_cast<float>(processSample(left[i], 0, drive));
    right[i] = static_cast<float>(processSample(right[i], 1, drive));
  }
}

// ---------------------------------------------------------------------------------------------
// VibeEq
// ---------------------------------------------------------------------------------------------

void VibeEq::Section::aim(const BiquadCoeffs& t, int len, bool snap) {
  target = t;
  if (snap) {
    current = t;
    step = BiquadCoeffs{};
    moving = false;
    return;
  }
  moving = false;
  for (int i = 0; i < BiquadCoeffs::kCount; ++i) {
    step.at(i) = (t.at(i) - current.at(i)) / len;
    if (step.at(i) != 0.0) moving = true;
  }
}

void VibeEq::Section::advance() {
  if (!moving) return;
  for (int i = 0; i < BiquadCoeffs::kCount; ++i) current.at(i) += step.at(i);
}

void VibeEq::Section::finish() {
  if (!moving) return;
  current = target;
  moving = false;
}

double VibeEq::Section::run(double x, int ch) {
  double* m = z[ch];
  const double y = current.b0 * x + m[0];
  m[0] = std::clamp(current.b1 * x - current.a1 * y + m[1], -kStateLimit, kStateLimit);
  m[1] = std::clamp(current.b2 * x - current.a2 * y, -kStateLimit, kStateLimit);
  return y;
}

void VibeEq::Section::clearState() { z[0][0] = z[0][1] = z[1][0] = z[1][1] = 0.0; }

void VibeEq::prepare(double sampleRate, int /*maxBlock*/) {
  sampleRate_ = sampleRate;
  settings_ = VibeSettings{};
  snap_ = true;
  flushLeft_ = 0;

  // The two channels are the same unit: same component tolerances, so that mono stays mono.
  const std::uint32_t seed = static_cast<std::uint32_t>(sampleRate * 1000.0);
  for (auto& ind : lowInductor_) ind.prepare(sampleRate, seed);
  for (auto& ind : airInductor_) ind.prepare(sampleRate, seed + 1);
  prepareTube(sampleRate);

  attackCoeff_ = 1.0 - std::exp(-1.0 / (Tuning::kDynAttackMs * 0.001 * sampleRate));
  releaseCoeff_ = 1.0 - std::exp(-1.0 / (Tuning::kDynReleaseMs * 0.001 * sampleRate));
  lowLpCoeff_ = 1.0 - std::exp(-2.0 * kPi * Tuning::kDynLowDetectHz / sampleRate);
  airLpCoeff_ = 1.0 - std::exp(-2.0 * kPi * Tuning::kDynAirDetectHz / sampleRate);

  for (auto& s : sections_) {
    s.current = s.target = s.step = BiquadCoeffs{};
    s.moving = false;
    s.clearState();
  }
  clearForBypass();
}

void VibeEq::prepareTube(double sampleRate) {
  tube_.prepare(sampleRate);
  tubeDrive_ = 0.0;
}

void VibeEq::clearForBypass() {
  for (auto& s : sections_) s.clearState();
  for (auto& ind : lowInductor_) ind.reset();
  for (auto& ind : airInductor_) ind.reset();
  lowLp_ = airLp_ = lowPower_ = airPower_ = 0.0;
  lowPull_ = airPull_ = 0.0;
  lowInductorDrive_ = airInductorDrive_ = 0.0;
  dynLow_.store(0.0f, std::memory_order_relaxed);
  dynAir_.store(0.0f, std::memory_order_relaxed);
}

void VibeEq::set(const VibeSettings& in) {
  VibeSettings s = in;
  s.lowFreqIndex = std::clamp(s.lowFreqIndex, 0, static_cast<int>(Tables::kLowFreqs.size()) - 1);
  s.airFreqIndex = std::clamp(s.airFreqIndex, 0, static_cast<int>(Tables::kAirFreqs.size()) - 1);
  s.tiltIndex = std::clamp(s.tiltIndex, 0, static_cast<int>(Tables::kTilts.size()) - 1);
  s.low = std::clamp(s.low, 0, 10);
  s.air = std::clamp(s.air, 0, 10);
  s.tube = std::clamp(s.tube, 0, 10);
  if (s == settings_) return;

  const bool wasOn = settings_.on;
  settings_ = s;
  if (!s.on && wasOn) flushLeft_ = 4 * Tuning::kVibeSubBlock;  // keep running until the filters are identity
  if (s.on) flushLeft_ = 0;
}

VibeEq::Network VibeEq::designNetwork(double lowPull, double airPull, bool program) const {
  Network n;
  if (!settings_.on) return n;  // identity

  const double sr = sampleRate_;
  const double lowAmount = settings_.low * (1.0 - lowPull);
  const double airAmount = settings_.air * (1.0 - airPull);

  // LOW: resonant bell at the chosen frequency, and one knob also drives the cut above it.
  if (lowAmount > 0.01) {
    const double hz = tableValue(kLowFreq, settings_.lowFreqIndex);
    const double atten = lowAmount * Tuning::kVibeLowAttenRatio;
    const double peakDb = lowAmount * Tuning::kVibeLowBoostDbPerStep + atten * lowAmount * kPeakInteraction;
    const InductorModel& ind = lowInductor_[0];
    double q = program ? ind.frequencyDependentQ(hz, kBaseQ) : ind.staticQ(hz, kBaseQ);
    q *= 1.0 + atten * kQInteraction;
    if (program) q *= 1.0 - ind.saturationLevel() * kLowSatQ;  // core saturation: broader peak
    matched::computePeaking(n.peak, hz, sr, peakDb, std::max(q, 0.2));

    const double dipDb = -(atten * Tuning::kVibeLowDipDbPerStep + lowAmount * atten * kDipInteraction);
    matched::computePeaking(n.dip, hz * Tuning::kVibeLowDipFreqRatio, sr, dipDb, Tuning::kVibeLowDipQ);
  }

  // AIR: broad boost.
  if (airAmount > 0.01) {
    const double hz = tableValue(kAirFreq, settings_.airFreqIndex);
    double q = Tuning::kVibeAirQ;
    if (program) q *= 1.0 - airInductor_[0].saturationLevel() * kAirSatQ;
    matched::computePeakingMatched(n.air, hz, sr, airAmount * Tuning::kVibeAirDbPerStep, q);
  }

  // TILT: opposite shelves around 1 kHz.
  const double tilt = tableValue(kTilt, settings_.tiltIndex);
  matched::computeLowShelf(n.tiltLow, Tuning::kVibeTiltHz, sr, -tilt, Tuning::kVibeTiltQ);
  matched::computeHighShelf(n.tiltHigh, Tuning::kVibeTiltHz, sr, tilt, Tuning::kVibeTiltQ);
  return n;
}

// DYN detectors: power followers (attack / release from tuning.h) on the band each boost works
// on. The pull-back grows with the level over the threshold.
void VibeEq::updateDetectors(const float* l, const float* r, int len) {
  const bool lowOn = settings_.on && settings_.dynLow && settings_.low > 0;
  const bool airOn = settings_.on && settings_.dynAir && settings_.air > 0;

  auto follow = [&](double& power, double p) {
    power += (p > power ? attackCoeff_ : releaseCoeff_) * (p - power);
  };
  auto pullOf = [](double power) {
    const double db = 10.0 * std::log10(std::max(power, 1e-12));
    return std::clamp((db - Tuning::kDynThresholdDb) / Tuning::kDynRangeDb, 0.0, 1.0);
  };

  if (lowOn) {
    for (int i = 0; i < len; ++i) {
      const double m = 0.5 * (static_cast<double>(l[i]) + r[i]);
      lowLp_ += lowLpCoeff_ * (m - lowLp_);
      follow(lowPower_, lowLp_ * lowLp_);
    }
  } else {
    lowLp_ = lowPower_ = 0.0;
  }
  if (airOn) {
    for (int i = 0; i < len; ++i) {
      const double m = 0.5 * (static_cast<double>(l[i]) + r[i]);
      airLp_ += airLpCoeff_ * (m - airLp_);
      const double hp = m - airLp_;
      follow(airPower_, hp * hp);
    }
  } else {
    airLp_ = airPower_ = 0.0;
  }

  const double lowFrac = lowOn ? pullOf(lowPower_) : 0.0;
  const double airFrac = airOn ? pullOf(airPower_) : 0.0;
  lowPull_ = lowFrac * Tuning::kDynMaxPullback;
  airPull_ = airFrac * Tuning::kDynMaxPullback;
  dynLow_.store(static_cast<float>(lowFrac), std::memory_order_relaxed);
  dynAir_.store(static_cast<float>(airFrac), std::memory_order_relaxed);
}

void VibeEq::processLinear(float* left, float* right, int n) {
  if (n <= 0) return;
  if (!settings_.on && flushLeft_ <= 0) return;  // bypassed and settled: identity

  const int sub = Tuning::kVibeSubBlock;
  for (int off = 0; off < n; off += sub) {
    const int len = std::min(sub, n - off);
    float* l = left + off;
    float* r = right + off;

    updateDetectors(l, r, len);

    const Network net = designNetwork(lowPull_, airPull_, true);
    sections_[kPeak].aim(net.peak, len, snap_);
    sections_[kDip].aim(net.dip, len, snap_);
    sections_[kAir].aim(net.air, len, snap_);
    sections_[kTiltLow].aim(net.tiltLow, len, snap_);
    sections_[kTiltHigh].aim(net.tiltHigh, len, snap_);
    snap_ = false;

    const double lowAmount = settings_.on ? settings_.low * (1.0 - lowPull_) : 0.0;
    const double airAmount = settings_.on ? settings_.air * (1.0 - airPull_) : 0.0;
    lowInductorDrive_ = lowAmount > 0.01 ? lowAmount * kLowInductorDrive : 0.0;
    airInductorDrive_ = airAmount > 0.01 ? airAmount * kAirInductorDrive : 0.0;

    for (int i = 0; i < len; ++i) {
      for (auto& s : sections_) s.advance();
      float* io[2] = {l + i, r + i};
      for (int ch = 0; ch < 2; ++ch) {
        double x = *io[ch];
        if (!std::isfinite(x)) x = 0.0;
        x = sections_[kPeak].run(x, ch);
        if (lowInductorDrive_ > 0.0) x = lowInductor_[static_cast<std::size_t>(ch)].processNonlinearity(x, lowInductorDrive_);
        x = sections_[kDip].run(x, ch);
        if (airInductorDrive_ > 0.0) x = airInductor_[static_cast<std::size_t>(ch)].processNonlinearity(x, airInductorDrive_);
        x = sections_[kAir].run(x, ch);
        x = sections_[kTiltLow].run(x, ch);
        x = sections_[kTiltHigh].run(x, ch);
        *io[ch] = static_cast<float>(x);
      }
    }
    for (auto& s : sections_) s.finish();
    if (!settings_.on) flushLeft_ -= len;
  }

  if (!settings_.on && flushLeft_ <= 0) clearForBypass();
}

void VibeEq::processTube(float* left, float* right, int n) {
  if (n <= 0) return;
  const double target = settings_.on ? settings_.tube / 10.0 * Tuning::kVibeTubeDriveMax : 0.0;
  if (target <= 0.0 && tubeDrive_ <= 0.0) return;  // no tube: identity

  const int ramp = std::min(n, Tuning::kVibeTubeRampSamples);
  tube_.process(left, right, ramp, tubeDrive_, target);
  if (n > ramp) tube_.process(left + ramp, right + ramp, n - ramp, target, target);
  tubeDrive_ = target;
  if (target <= 0.0) tube_.reset();
}

double VibeEq::magnitudeDb(double hz, bool staticSetting) const {
  if (!settings_.on) return 0.0;
  const double lowPull = staticSetting ? 0.0 : dynLow_.load(std::memory_order_relaxed) * Tuning::kDynMaxPullback;
  const double airPull = staticSetting ? 0.0 : dynAir_.load(std::memory_order_relaxed) * Tuning::kDynMaxPullback;
  const Network n = designNetwork(lowPull, airPull, false);
  return n.peak.magnitudeDb(hz, sampleRate_) + n.dip.magnitudeDb(hz, sampleRate_) +
         n.air.magnitudeDb(hz, sampleRate_) + n.tiltLow.magnitudeDb(hz, sampleRate_) +
         n.tiltHigh.magnitudeDb(hz, sampleRate_);
}

} // namespace FairyTapes::HappyEnding::DSP
