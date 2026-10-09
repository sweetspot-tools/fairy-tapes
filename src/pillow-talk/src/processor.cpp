/**
 * Fairy Tapes · Pillow Talk — VST3 Audio Processor implementation.
 */

#include "processor.h"
#include "pluginterfaces/base/ibstream.h"
#include "fairy_ui/knob_math.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace FairyTapes::PillowTalk {

namespace {
bool isMonoOrStereo(Steinberg::Vst::SpeakerArrangement a) {
  return a == Steinberg::Vst::SpeakerArr::kStereo || a == Steinberg::Vst::SpeakerArr::kMono;
}
} // namespace

Processor::Processor() {
  setControllerClass(kControllerUID);
  processContextRequirements.needTempo();
  loadDefaults(param_value_);
}

Steinberg::tresult PLUGIN_API Processor::initialize(Steinberg::FUnknown* context) {
  auto result = AudioEffect::initialize(context);
  if (result != Steinberg::kResultOk)
    return result;

  addAudioInput(STR16("Main Input"), Steinberg::Vst::SpeakerArr::kStereo);
  addAudioInput(STR16("Sidechain"), Steinberg::Vst::SpeakerArr::kStereo, Steinberg::Vst::kAux, 0);
  addAudioOutput(STR16("Main Output"), Steinberg::Vst::SpeakerArr::kStereo);

  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
  // Every (re)activation follows setupProcessing: rebuild all DSP for the current rate/block.
  if (state) {
    engine_.prepare(processSetup.sampleRate, processSetup.maxSamplesPerBlock);
    display_limiter_.prepare(processSetup.sampleRate);
  }
  return AudioEffect::setActive(state);
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
  Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
  Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {
  // Main only (1 in) or main + sidechain (2 in); single output; mono or stereo each.
  if (numOuts != 1 || (numIns != 1 && numIns != 2))
    return Steinberg::kResultFalse;
  for (Steinberg::int32 i = 0; i < numIns; ++i)
    if (!isMonoOrStereo(inputs[i]))
      return Steinberg::kResultFalse;
  if (!isMonoOrStereo(outputs[0]))
    return Steinberg::kResultFalse;
  return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

Steinberg::uint32 PLUGIN_API Processor::getTailSamples() {
  // Feedback up to 0.95: let the host keep processing so the repeats are not cut.
  return Steinberg::Vst::kInfiniteTail;
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

void Processor::publishDisplay(Steinberg::Vst::ProcessData& data) {
  if (!display_limiter_.advance(data.numSamples) || !data.outputParameterChanges)
    return;
  const DisplayValues d = engine_.lastDisplay();
  const double values[3] = {Ui::meterNormFromDb(d.voiceDb, -60.0, 0.0),
                            std::clamp((double)d.sendGain, 0.0, 1.0),
                            Ui::meterNormFromDb(d.echoDb, -60.0, 0.0)};
  for (int i = 0; i < 3; ++i) {
    Steinberg::int32 index = 0;
    auto* queue = data.outputParameterChanges->addParameterData(kDispVoice + i, index);
    if (queue)
      queue->addPoint(0, values[i], index);
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

  const auto* ctx = data.processContext;
  const double bpm = ctx && (ctx->state & Steinberg::Vst::ProcessContext::kTempoValid)
                         ? ctx->tempo
                         : kFallbackBpm;
  engine_.setSettings(mapParams(param_value_, bpm));

  const float* inL = in.channelBuffers32[0];
  const float* inR = in.numChannels >= 2 && in.channelBuffers32[1] ? in.channelBuffers32[1] : inL;
  float* outL = out.channelBuffers32[0];
  float* outR = out.numChannels >= 2 ? out.channelBuffers32[1] : nullptr;

  SidechainInput sc{nullptr, nullptr};
  if (data.numInputs >= 2) {
    const auto* bus = getAudioInput(1);
    sc = pickSidechain(param_value_[kSidechain] >= 0.5, bus && bus->isActive(),
                       data.inputs[1].numChannels, data.inputs[1].channelBuffers32);
  }

  engine_.process(inL, inR, sc.L, sc.R, outL, outR, data.numSamples);
  publishDisplay(data);
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

} // namespace FairyTapes::PillowTalk
