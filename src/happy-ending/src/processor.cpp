/**
 * Fairy Tapes · Happy Ending — VST3 Audio Processor implementation.
 */

#include "processor.h"
#include "messages.h"
#include "state.h"
#include "state_stream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace FairyTapes::HappyEnding {

namespace {
bool isMonoOrStereo(Steinberg::Vst::SpeakerArrangement a) {
  return a == Steinberg::Vst::SpeakerArr::kStereo || a == Steinberg::Vst::SpeakerArr::kMono;
}
} // namespace

Processor::Processor() {
  setControllerClass(kControllerUID);
  double defaults[kNumParams];
  loadDefaults(defaults);
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) param_[static_cast<std::size_t>(i)].store(defaults[i]);
}

Steinberg::tresult PLUGIN_API Processor::initialize(Steinberg::FUnknown* context) {
  auto result = AudioEffect::initialize(context);
  if (result != Steinberg::kResultOk)
    return result;
  addAudioInput(STR16("Main Input"), Steinberg::Vst::SpeakerArr::kStereo);
  addAudioOutput(STR16("Main Output"), Steinberg::Vst::SpeakerArr::kStereo);
  return Steinberg::kResultOk;
}

int Processor::oversamplingFactor() const {
  static constexpr int kFactors[3] = {1, 2, 4};
  const double n = param_[kOversampling].load(std::memory_order_relaxed);
  return kFactors[std::clamp(indexFromNorm(kOversampling, n), 0, 2)];
}

void Processor::prepareEngine() {
  const double rate = processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 48000.0;
  const int block = std::max(1, static_cast<int>(processSetup.maxSamplesPerBlock));
  engine_.prepare(rate, block, oversamplingFactor());
  scratch_.assign(static_cast<std::size_t>(block), 0.0f);
  display_limiter_.prepare(rate);
  hold_ = MeterHold{};
}

Steinberg::tresult PLUGIN_API Processor::setupProcessing(Steinberg::Vst::ProcessSetup& setup) {
  const auto result = AudioEffect::setupProcessing(setup);
  if (result != Steinberg::kResultOk)
    return result;
  prepareEngine();  // the latency is valid from here on
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
  // Every activation starts clean and picks up an OVERSAMPLING changed while the plug-in was running.
  if (state) {
    prepareEngine();
    clear_clips_requested_ = clear_clips_ack_ = false;
    reset_integrated_requested_ = reset_integrated_ack_ = false;
  }
  return AudioEffect::setActive(state);
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
  Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
  Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {
  // One in, one out, the same layout: mono/mono or stereo/stereo (a mono bus is processed dual-mono).
  if (numIns != 1 || numOuts != 1 || inputs[0] != outputs[0] || !isMonoOrStereo(inputs[0]))
    return Steinberg::kResultFalse;
  return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

Steinberg::uint32 PLUGIN_API Processor::getLatencySamples() {
  return static_cast<Steinberg::uint32>(engine_.latencySamples());
}

Steinberg::uint32 PLUGIN_API Processor::getTailSamples() {
  // The look-ahead holds the last samples back: let the host keep processing until they are out.
  return static_cast<Steinberg::uint32>(engine_.latencySamples());
}

void Processor::readParameterChanges(Steinberg::Vst::IParameterChanges* changes) {
  if (!changes)
    return;
  const Steinberg::int32 num = changes->getParameterCount();
  for (Steinberg::int32 i = 0; i < num; ++i) {
    auto* queue = changes->getParameterData(i);
    if (!queue || queue->getPointCount() <= 0)
      continue;
    Steinberg::Vst::ParamValue value = 0.;
    Steinberg::int32 sample_offset = 0;
    if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) != Steinberg::kResultOk || !std::isfinite(value))
      continue;
    const Steinberg::Vst::ParamID id = queue->getParameterId();
    if (id < kNumParams)
      param_[id].store(std::clamp(value, 0.0, 1.0), std::memory_order_relaxed);
    else if (id == kListenBand)
      listen_.store(listenFromNorm(value), std::memory_order_relaxed);
    else if (id == kSetActive)
      setActive_.store(value >= 0.5, std::memory_order_relaxed);
    else if (id == kClearClips) {  // writable display parameters: the editor sets 1, we write 0 back
      if (value >= 0.5) clear_clips_requested_ = true;
    } else if (id == kResetIntegrated) {
      if (value >= 0.5) reset_integrated_requested_ = true;
    }
  }
}

void Processor::publishDisplay(Steinberg::Vst::ProcessData& data, bool dualMono) {
  hold_.add(engine_.meters(), dualMono ? kDualMonoLufsOffsetDb : 0.0);
  auto* out = data.outputParameterChanges;
  if (!out) {
    clear_clips_ack_ = reset_integrated_ack_ = false;
    return;
  }
  const auto put = [out](Steinberg::Vst::ParamID id, double v) {
    Steinberg::int32 index = 0;
    if (auto* queue = out->addParameterData(id, index)) {
      Steinberg::int32 pointIndex = 0;
      queue->addPoint(0, v, pointIndex);
    }
  };
  if (clear_clips_ack_) {
    clear_clips_ack_ = false;
    put(kClearClips, 0.0);
  }
  if (reset_integrated_ack_) {
    reset_integrated_ack_ = false;
    put(kResetIntegrated, 0.0);
  }
  if (!display_limiter_.advance(data.numSamples))
    return;
  const DisplayFrame f = hold_.take();
  put(kDispInL, normFromPeakDb(f.inPeakDb[0]));
  put(kDispInR, normFromPeakDb(f.inPeakDb[1]));
  put(kDispOutL, normFromPeakDb(f.outPeakDb[0]));
  put(kDispOutR, normFromPeakDb(f.outPeakDb[1]));
  for (int r = 0; r < kNumGrRows; ++r) put(kDispGr0 + static_cast<Steinberg::Vst::ParamID>(r), normFromGrDb(f.grDb[r]));
  for (int l = 0; l < kNumLeds; ++l) put(kDispClip0 + static_cast<Steinberg::Vst::ParamID>(l), f.clip[l] ? 1.0 : 0.0);
  put(kDispLufsShort, normFromLufs(f.lufsShort));
  put(kDispLufsInt, normFromLufs(f.lufsIntegrated));
  put(kDispDynLow, std::clamp(static_cast<double>(f.dynLow), 0.0, 1.0));
  put(kDispDynAir, std::clamp(static_cast<double>(f.dynAir), 0.0, 1.0));
  put(kDispLatency, normFromLatency(engine_.latencySamples()));
  put(kDispGlueIn, normFromStageDb(f.stageInDb[kStageInGlue]));
  put(kDispGlueGr, normFromGrDb(f.glueGrDb));
  put(kDispLimitIn, normFromStageDb(f.stageInDb[kStageInLimit]));
  put(kDispLimitThresh, normFromThreshDb(f.limitThreshDb));
  put(kDispClipIn, normFromStageDb(f.stageInDb[kStageInClip]));
  put(kDispFinalIn, normFromStageDb(f.stageInDb[kStageInFinal]));
}

Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data) {
  readParameterChanges(data.inputParameterChanges);

  if (data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0)
    return Steinberg::kResultOk;
  Steinberg::Vst::AudioBusBuffers& in = data.inputs[0];
  Steinberg::Vst::AudioBusBuffers& out = data.outputs[0];
  if (!in.channelBuffers32 || !out.channelBuffers32 || in.numChannels < 1 || out.numChannels < 1 ||
      !in.channelBuffers32[0] || !out.channelBuffers32[0])
    return Steinberg::kResultOk;

  double norm[kNumParams];
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) norm[i] = param_[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
  engine_.set(mapParams(norm, listen_.load(std::memory_order_relaxed), setActive_.load(std::memory_order_relaxed)));

  if (clear_clips_requested_) {
    engine_.clearClips();
    hold_.clearClips();
    clear_clips_requested_ = false;
    clear_clips_ack_ = true;
  }
  if (reset_integrated_requested_) {
    engine_.resetIntegrated();
    reset_integrated_requested_ = false;
    reset_integrated_ack_ = true;
  }

  const int n = data.numSamples;
  const bool stereoOut = out.numChannels >= 2 && out.channelBuffers32[1];
  const float* inL = in.channelBuffers32[0];
  const float* inR = in.numChannels >= 2 && in.channelBuffers32[1] ? in.channelBuffers32[1] : inL;
  float* outL = out.channelBuffers32[0];
  float* outR = stereoOut ? out.channelBuffers32[1] : nullptr;
  if (inL != outL) std::memmove(outL, inL, sizeof(float) * static_cast<std::size_t>(n));
  if (outR) {
    if (inR != outR) std::memmove(outR, inR, sizeof(float) * static_cast<std::size_t>(n));
    engine_.process(outL, outR, n);
  } else if (!scratch_.empty()) {
    // mono output: the engine needs a distinct R buffer; feed it the channel (dual mono), keep L
    const int chunk = static_cast<int>(scratch_.size());
    for (int pos = 0; pos < n; pos += chunk) {
      const int len = std::min(chunk, n - pos);
      std::memcpy(scratch_.data(), inR + pos, sizeof(float) * static_cast<std::size_t>(len));
      engine_.process(outL + pos, scratch_.data(), len);
    }
  }

  publishDisplay(data, in.numChannels < 2 && out.numChannels < 2);
  out.silenceFlags = 0;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
  std::vector<std::uint8_t> bytes;
  if (!readStateBytes(state, bytes))
    return Steinberg::kResultFalse;
  double norm[kNumParams];
  if (!deserializeState(bytes.data(), bytes.size(), norm))
    return Steinberg::kResultFalse;
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) param_[static_cast<std::size_t>(i)].store(norm[i]);
  listen_.store(-1);          // session parameters: always off after a load
  setActive_.store(false);
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  double norm[kNumParams];
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) norm[i] = param_[static_cast<std::size_t>(i)].load();
  std::vector<std::uint8_t> bytes;
  serializeState(norm, bytes);
  Steinberg::int32 written = 0;
  if (state->write(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &written) != Steinberg::kResultOk ||
      written != static_cast<Steinberg::int32>(bytes.size()))
    return Steinberg::kResultFalse;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message) {
  int index = 0;
  if (decodeOversamplingMessage(message, index)) {
    param_[kOversampling].store(normFromIndex(kOversampling, index));
    return Steinberg::kResultOk;
  }
  return AudioEffect::notify(message);
}

} // namespace FairyTapes::HappyEnding
