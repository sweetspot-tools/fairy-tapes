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

namespace {
bool isMonoOrStereo(Steinberg::Vst::SpeakerArrangement a) {
  return a == Steinberg::Vst::SpeakerArr::kStereo || a == Steinberg::Vst::SpeakerArr::kMono;
}
} // namespace

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
  if (numIns != 1 || numOuts != 1 || !isMonoOrStereo(inputs[0]) || !isMonoOrStereo(outputs[0]))
    return Steinberg::kResultFalse;
  return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
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
  if (!in.channelBuffers32 || !out.channelBuffers32 || in.numChannels < 1 || out.numChannels < 1)
    return Steinberg::kResultOk;

  const bool bypass = param_value_[kBypass] >= 0.5;
  const float gain = bypass ? 1.0f
                            : (float)dbToGain(inputDbFromNorm(param_value_[kInput]) +
                                              outputDbFromNorm(param_value_[kOutput]));
  const Steinberg::int32 n = data.numSamples;
  for (Steinberg::int32 ch = 0; ch < out.numChannels; ++ch) {
    const float* src = in.channelBuffers32[ch < in.numChannels ? ch : 0];
    float* dst = out.channelBuffers32[ch];
    if (!src || !dst)
      continue;
    for (Steinberg::int32 i = 0; i < n; ++i)
      dst[i] = src[i] * gain;
  }
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
