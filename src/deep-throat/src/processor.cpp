/**
 * Fairy Tapes · Deep Throat — VST3 Audio Processor implementation.
 * Supported layouts: stereo in -> stereo out and mono in -> mono out (the mono path runs the
 * engine with a null right channel: same chain, same latency). Mixed layouts are rejected.
 */

#include "processor.h"
#include "messages.h"
#include "state_stream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace FairyTapes::DeepThroat {

Processor::Processor() {
  setControllerClass(kControllerUID);
  loadDefaults(param_value_);
  ensureSeed(decor_);   // a fresh instance saves a seed with its first getState
}

Steinberg::tresult PLUGIN_API Processor::initialize(Steinberg::FUnknown* context) {
  auto result = AudioEffect::initialize(context);
  if (result != Steinberg::kResultOk)
    return result;
  addAudioInput(STR16("Main Input"), Steinberg::Vst::SpeakerArr::kStereo);
  addAudioOutput(STR16("Main Output"), Steinberg::Vst::SpeakerArr::kStereo);
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
  Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
  Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {
  // Stereo -> stereo or mono -> mono only (the engine links the channels; no up/down mix).
  using namespace Steinberg::Vst::SpeakerArr;
  if (numIns != 1 || numOuts != 1 || inputs[0] != outputs[0] ||
      (inputs[0] != kStereo && inputs[0] != kMono))
    return Steinberg::kResultFalse;
  return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

Steinberg::tresult PLUGIN_API Processor::setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) {
  const auto result = AudioEffect::setupProcessing(newSetup);
  if (result == Steinberg::kResultOk) {
    engine_.prepare(newSetup.sampleRate);  // latency is known before activation
    display_limiter_.prepare(newSetup.sampleRate);
  }
  return result;
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
  // Every (re)activation follows setupProcessing: rebuild all DSP for the current rate. The host
  // re-reads getLatencySamples() after activation.
  if (state) {
    engine_.prepare(processSetup.sampleRate);
    display_limiter_.prepare(processSetup.sampleRate);
    display_acc_ = DisplayAccumulator{};
    level_acc_ = LevelAccumulator{};
  }
  return AudioEffect::setActive(state);
}

Steinberg::uint32 PLUGIN_API Processor::getLatencySamples() {
  return (Steinberg::uint32)engine_.latencySamples();
}

void Processor::updateParamsFromChanges(Steinberg::Vst::IParameterChanges* changes) {
  if (!changes)
    return;
  const Steinberg::int32 num = changes->getParameterCount();
  for (Steinberg::int32 i = 0; i < num; ++i) {
    auto* queue = changes->getParameterData(i);
    if (!queue)
      continue;
    const Steinberg::Vst::ParamID id = queue->getParameterId();
    Steinberg::Vst::ParamValue value = 0.;
    Steinberg::int32 sample_offset = 0;
    if (id == kClearClips) {  // writable display parameter: not part of the persistent table
      if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) == Steinberg::kResultOk &&
          value >= 0.5)
        clear_clips_requested_ = true;
      continue;
    }
    if (id >= kNumParams)
      continue;
    if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) == Steinberg::kResultOk)
      param_value_[id] = value;
  }
}

void Processor::publishDisplay(Steinberg::Vst::ProcessData& data) {
  display_acc_.add(engine_.takeDisplay());
  level_acc_.add(engine_.takeLevels());
  auto* out = data.outputParameterChanges;
  const auto put = [out](Steinberg::Vst::ParamID id, double v) {
    Steinberg::int32 index = 0;
    if (auto* queue = out->addParameterData(id, index))
      queue->addPoint(0, v, index);
  };
  if (out && clear_clips_ack_) {
    clear_clips_ack_ = false;
    put(kClearClips, 0.0);  // acknowledge: the editor's trigger goes back to 0
  }
  if (!display_limiter_.advance(data.numSamples) || !out)
    return;
  const auto norms = displayNorms(display_acc_.take(), level_acc_.take());
  for (size_t i = 0; i < norms.size(); ++i) put(kMeterBase + (Steinberg::Vst::ParamID)i, norms[i]);
}

Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data) {
  updateParamsFromChanges(data.inputParameterChanges);

  if (data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0)
    return Steinberg::kResultOk;

  Steinberg::Vst::AudioBusBuffers& in = data.inputs[0];
  Steinberg::Vst::AudioBusBuffers& out = data.outputs[0];
  if (!in.channelBuffers32 || !out.channelBuffers32 || in.numChannels < 1 ||
      out.numChannels < 1 || !in.channelBuffers32[0] || !out.channelBuffers32[0])
    return Steinberg::kResultOk;

  // Gains, IN, Bypass and Listen are smoothed / crossfaded inside the engine.
  engine_.setSettings(mapParams(param_value_));
  const float* inL = in.channelBuffers32[0];
  const float* inR = in.numChannels >= 2 ? in.channelBuffers32[1] : nullptr;  // null: mono
  float* outL = out.channelBuffers32[0];
  float* outR = out.numChannels >= 2 ? out.channelBuffers32[1] : nullptr;
  if (clear_clips_requested_) {
    engine_.clearClips();
    level_acc_.clearClips();
    clear_clips_requested_ = false;
    clear_clips_ack_ = true;
  }
  engine_.process(inL, inR, outL, outR, data.numSamples);
  publishDisplay(data);
  for (Steinberg::int32 ch = 2; ch < out.numChannels; ++ch)  // never with the accepted layouts
    if (float* extra = out.channelBuffers32[ch])
      for (Steinberg::int32 i = 0; i < data.numSamples; ++i) extra[i] = 0.0f;
  out.silenceFlags = 0;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
  std::vector<uint8_t> bytes;
  if (!readStateBytes(state, bytes))
    return Steinberg::kResultFalse;
  double values[kNumParams];
  Decor decor;
  if (!deserializeState(bytes.data(), bytes.size(), values, decor))
    return Steinberg::kResultFalse;
  ensureSeed(decor);   // v1 or seed 0: a new seed, saved by the next getState
  for (int i = 0; i < (int)kNumParams; ++i) param_value_[i] = values[i];
  {
    std::lock_guard<std::mutex> lk(decor_mutex_);
    decor_ = std::move(decor);
  }
  sendDecor();
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  Decor decor;
  {
    std::lock_guard<std::mutex> lk(decor_mutex_);
    decor = decor_;
  }
  std::vector<uint8_t> bytes;
  serializeState(param_value_, decor, bytes);
  Steinberg::int32 written = 0;
  if (state->write(bytes.data(), (Steinberg::int32)bytes.size(), &written) != Steinberg::kResultOk ||
      written != (Steinberg::int32)bytes.size())
    return Steinberg::kResultFalse;
  return Steinberg::kResultOk;
}

void Processor::sendDecor() {
  if (!getPeer())
    return;
  Steinberg::Vst::IMessage* msg = allocateMessage();
  if (!msg)
    return;
  Decor d;
  {
    std::lock_guard<std::mutex> lk(decor_mutex_);
    d = decor_;
  }
  encodeDecorMessage(msg, d);
  sendMessage(msg);
  msg->release();
}

Steinberg::tresult PLUGIN_API Processor::connect(Steinberg::Vst::IConnectionPoint* other) {
  const auto r = AudioEffect::connect(other);
  sendDecor();   // the controller learns the seed of this instance
  return r;
}

Steinberg::tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message) {
  Decor d;
  if (decodeDecorMessage(message, d)) {   // an editor edit, or the seed the controller had to generate
    std::lock_guard<std::mutex> lk(decor_mutex_);
    if (d.seed == 0) d.seed = decor_.seed;   // never clears the seed
    decor_ = std::move(d);
    return Steinberg::kResultOk;
  }
  return AudioEffect::notify(message);
}

} // namespace FairyTapes::DeepThroat
