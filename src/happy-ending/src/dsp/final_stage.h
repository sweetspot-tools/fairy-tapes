/**
 * Fairy Tapes · Happy Ending — FINAL: INFLATE or CLIP, both under the output ceiling (spec §5.6).
 *
 * INFLATE: the shared Inflator (shared/inflator/inflator.h, ZLInflator port, GPL-3.0) as in the
 * preview: the quartic shaper (curve1 = CURVE) and the quadratic one (curve2 ignored) blended 50 / 50,
 * no compensation, wet / dry = EFFECT. CURVE 0..1 goes straight to the shaper's own `curve`
 * parameter (the shaper maps it to c = -6 + 6 * curve); EFFECT 0..1 is the wet amount. The
 * inflator's full scale is the CEILING (the signal is divided by it and the result multiplied back),
 * so the body of the program is lifted and the peaks at the ceiling stay there; whatever the shaper
 * leaves above the ceiling (input above the ceiling with EFFECT < 1) is clamped to it.
 *
 * CLIP: ClipOnly2 (clip_only2.h, Airwindows, MIT) with its knee placed on the CEILING: DRIVE 0..+2 dB
 * is the gain in front (not compensated: it is the loudness), the soft clip rounds what goes over,
 * and a final clamp at the ceiling guarantees it.
 *
 * The two modes keep their own values: set() carries all of them, the stage stores them, and
 * only the active mode's values are heard. On / off and the change of mode are crossfades of
 * kFinalRampMs; off = the input delayed by latencySamples(), bit for bit. The latency is the same
 * in both modes and when off: `latencyMultiple` base-rate samples, i.e. a whole number of base-rate
 * samples whatever the oversampling factor the engine runs it at (ClipOnly2 itself needs one
 * sample, the INFLATE branch and the dry signal are delayed to line up with it).
 * Non-finite input samples are replaced by 0. No allocation in process().
 */

#pragma once

#include "clip_only2.h"
#include "inflator.h"
#include "tuning.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace FairyTapes::HappyEnding::DSP {

class FinalStage {
public:
  static constexpr int kInflate = 0;
  static constexpr int kClip = 1;

  // `latencyMultiple`: the oversampling factor the stage runs at (latencySamples() is that many samples,
  // one base-rate sample).
  void prepare(double sampleRate, int latencyMultiple = 1) {
    latency_ = std::max(1, latencyMultiple);
    ringL_.assign(static_cast<std::size_t>(latency_), 0.0f);
    ringR_.assign(static_cast<std::size_t>(latency_), 0.0f);
    pos_ = 0;
    rampSamples_ = std::max(1, static_cast<int>(std::lround(Tuning::kFinalRampMs * 0.001 * sampleRate)));
    clip_.reset();
    primed_ = false;
    s_ = Settings{};
    apply(true);
    shavedDb_ = 0.0f;
  }

  // mode: kInflate or kClip. effect, curve: 0..1 (INFLATE). driveDb: 0..+2 (CLIP). ceilingDb: <= 0.
  void set(bool on, int mode, float effect, float curve, float driveDb, float ceilingDb) {
    s_.on = on;
    s_.mode = mode == kClip ? kClip : kInflate;
    s_.effect = std::clamp(effect, 0.0f, 1.0f);
    s_.curve = std::clamp(curve, 0.0f, 1.0f);
    s_.driveDb = std::clamp(driveDb, 0.0f, 12.0f);
    s_.ceilingDb = std::min(ceilingDb, 0.0f);
    apply(!primed_);
    primed_ = true;
  }

  bool on() const { return s_.on; }
  int mode() const { return s_.mode; }
  float effect() const { return s_.effect; }
  float curve() const { return s_.curve; }
  float driveDb() const { return s_.driveDb; }
  int latencySamples() const { return latency_; }

  // dB the stage took off the peaks of the last block (what it asked for against what it delivered:
  // the clamp at the ceiling in INFLATE, the soft clip and the clamp in CLIP), for the GR row.
  float shavedDb() const { return shavedDb_; }

  void process(float* left, float* right, int n) {
    double askedPeak = 0.0, outPeak = 0.0;
    const auto cf = static_cast<float>(ceilLin_);
    for (int i = 0; i < n; ++i) {
      if (remaining_ > 0) {
        wet_ += wetStep_;
        gain_ += gainStep_;
        if (--remaining_ == 0) {
          wet_ = wetTarget_;
          gain_ = gainTarget_;
        }
        inflator_.setWet(static_cast<float>(wet_));
      }
      wD_ = glide(wD_, wDT_);
      wI_ = glide(wI_, wIT_);
      wC_ = glide(wC_, wCT_);
      const bool useInflate = wI_ > 0.0 || wIT_ > 0.0;
      const bool dryOnly = wD_ >= 1.0;

      float* io[2] = {left, right};
      float out[2];
      for (int c = 0; c < 2; ++c) {
        const float x = std::isfinite(io[c][i]) ? io[c][i] : 0.0f;
        std::vector<float>& ring = c == 0 ? ringL_ : ringR_;
        const auto p = static_cast<std::size_t>(pos_);
        const float d = ring[p];  // delayed by `latency_` samples
        const float dEarly = latency_ == 1 ? x : ring[(p + 1) % static_cast<std::size_t>(latency_)];  // one sample less
        ring[p] = x;

        // CLIP branch (always runs, so its state is the right one the moment it is switched on)
        const double driven = static_cast<double>(dEarly) * gain_;
        const double yc = clip_.process(c, driven * clipIn_) * clipOut_;

        double asked = std::fabs(static_cast<double>(d)) * wD_ + std::fabs(driven) * wC_;
        double y = static_cast<double>(d) * wD_ + yc * wC_;
        if (useInflate) {
          double yi = d;
          if (wet_ > 0.0) {
            const float u = static_cast<float>(static_cast<double>(d) / ceilLin_);
            yi = static_cast<double>(inflator_(u)) * ceilLin_;
          }
          y += yi * wI_;
          asked += std::fabs(yi) * wI_;
        }
        if (dryOnly) {
          out[c] = d;
          asked = std::fabs(static_cast<double>(d));
        } else {
          out[c] = std::clamp(static_cast<float>(y), -cf, cf);
        }
        askedPeak = std::max(askedPeak, asked);
        outPeak = std::max(outPeak, static_cast<double>(std::fabs(out[c])));
      }
      pos_ = (pos_ + 1) % latency_;
      left[i] = out[0];
      right[i] = out[1];
    }
    shavedDb_ = (askedPeak > 1e-9 && outPeak > 1e-9)
                    ? static_cast<float>(std::max(0.0, 20.0 * std::log10(askedPeak / outPeak)))
                    : 0.0f;
  }

private:
  struct Settings {
    bool on = true;
    int mode = kInflate;
    float effect = 0.5f;
    float curve = 0.25f;
    float driveDb = 2.0f;
    float ceilingDb = -0.3f;
  };

  double glide(double v, double target) const {
    if (v == target) return v;
    const double step = 1.0 / rampSamples_;
    return std::fabs(target - v) <= step ? target : v + (target > v ? step : -step);
  }

  void apply(bool snap) {
    // the ceiling as the largest float not above it: a float output at or below it is at or below
    // the exact ceiling too
    const double c = std::pow(10.0, static_cast<double>(s_.ceilingDb) / 20.0);
    float cf = static_cast<float>(c);
    if (static_cast<double>(cf) > c) cf = std::nextafter(cf, 0.0f);
    ceilLin_ = static_cast<double>(cf);
    clipIn_ = ClipOnly2::kKnee / ceilLin_;
    clipOut_ = ceilLin_ / ClipOnly2::kKnee;

    inflator_.setShapes(s_.curve, s_.curve, 0.5f, false);

    wDT_ = s_.on ? 0.0 : 1.0;
    wIT_ = s_.on && s_.mode == kInflate ? 1.0 : 0.0;
    wCT_ = s_.on && s_.mode == kClip ? 1.0 : 0.0;
    wetTarget_ = s_.effect;
    gainTarget_ = std::pow(10.0, static_cast<double>(s_.driveDb) / 20.0);
    if (snap) {
      wD_ = wDT_;
      wI_ = wIT_;
      wC_ = wCT_;
      wet_ = wetTarget_;
      gain_ = gainTarget_;
      remaining_ = 0;
      inflator_.setWet(static_cast<float>(wet_));
    } else if (wet_ != wetTarget_ || gain_ != gainTarget_) {
      remaining_ = rampSamples_;
      wetStep_ = (wetTarget_ - wet_) / rampSamples_;
      gainStep_ = (gainTarget_ - gain_) / rampSamples_;
    }
  }

  Settings s_;
  bool primed_ = false;
  int latency_ = 1;
  int rampSamples_ = 480;
  std::vector<float> ringL_, ringR_;
  int pos_ = 0;

  inflator::Inflator<float> inflator_;
  ClipOnly2 clip_;
  double ceilLin_ = 1.0, clipIn_ = 1.0, clipOut_ = 1.0;
  double wD_ = 0.0, wI_ = 1.0, wC_ = 0.0;        // weights of the dry, INFLATE and CLIP signals
  double wDT_ = 0.0, wIT_ = 1.0, wCT_ = 0.0;     // ... and where they are heading
  double wet_ = 0.5, wetTarget_ = 0.5, wetStep_ = 0.0;
  double gain_ = 1.0, gainTarget_ = 1.0, gainStep_ = 0.0;
  int remaining_ = 0;
  float shavedDb_ = 0.0f;
};

} // namespace FairyTapes::HappyEnding::DSP
