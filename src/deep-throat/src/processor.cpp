/**
 * Fairy Tapes · Deep Throat — VST3 Audio Processor implementation.
 */

#include "processor.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace FairyTapes::DeepThroat {


Processor::Processor() {
  setControllerClass(kControllerUID);
  loadDefaults(param_value_);
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
  if (result == Steinberg::kResultOk)
    engine_.prepare(newSetup.sampleRate);  // latency is known before activation
  return result;
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
  // Every (re)activation follows setupProcessing: rebuild all DSP for the current rate. The host
  // re-reads getLatencySamples() after activation.
  if (state)
    engine_.prepare(processSetup.sampleRate);
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
    if (id >= kNumParams)
      continue;
    Steinberg::Vst::ParamValue value = 0.;
    Steinberg::int32 sample_offset = 0;
    if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) == Steinberg::kResultOk)
      param_value_[id] = value;
  }
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
  engine_.process(inL, inR, outL, outR, data.numSamples);
  for (Steinberg::int32 ch = 2; ch < out.numChannels; ++ch)  // never with the accepted layouts
    if (float* extra = out.channelBuffers32[ch])
      for (Steinberg::int32 i = 0; i < data.numSamples; ++i) extra[i] = 0.0f;
  out.silenceFlags = 0;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  uint8_t buf[kStateSize];
  Steinberg::int32 read = 0;
  if (state->read(buf, (Steinberg::int32)kStateSize, &read) != Steinberg::kResultOk)
    return Steinberg::kResultFalse;
  return deserializeState(buf, (size_t)read, param_value_) ? Steinberg::kResultOk
                                                            : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  std::vector<uint8_t> bytes;
  serializeState(param_value_, bytes);
  Steinberg::int32 written = 0;
  if (state->write(bytes.data(), (Steinberg::int32)bytes.size(), &written) != Steinberg::kResultOk ||
      written != (Steinberg::int32)bytes.size())
    return Steinberg::kResultFalse;
  return Steinberg::kResultOk;
}

} // namespace FairyTapes::DeepThroat
