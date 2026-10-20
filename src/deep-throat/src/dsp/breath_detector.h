// Fairy Tapes · Deep Throat — BreathDetector: abstract breath probability source.
// The breath reducer consumes only process()'s output. No VST3 SDK dependency.
#pragma once

namespace FairyTapes::DeepThroat::DSP {

class BreathDetector {
public:
  virtual ~BreathDetector() = default;
  // Allocates all buffers; must be called off the audio thread before process().
  virtual void prepare(double sampleRate) = 0;
  // Clears the analysis state (keeps the sensitivity). Never allocates.
  virtual void reset() = 0;
  // Feeds one (stereo-linked mono) sample and returns the current breath probability in
  // [0, 1]. The value returned at sample t describes input sample t - latencySamples():
  // the caller delays the audio by latencySamples() to align it. Never allocates.
  virtual float process(float monoSample) = 0;
  virtual int latencySamples() const = 0;
  // 0..1 (default 0.5): higher detects weaker breaths.
  virtual void setSensitivity(float s01) = 0;
};

} // namespace FairyTapes::DeepThroat::DSP
