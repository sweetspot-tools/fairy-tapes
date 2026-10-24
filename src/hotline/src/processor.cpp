/**
 * Fairy Tapes · Hotline — VST3 Audio Processor implementation.
 */

#include "processor.h"
#include "state.h"
#include "state_stream.h"
#include "fairy_ui/knob_math.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <vector>

namespace FairyTapes::Hotline {

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

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
  // Every (re)activation follows setupProcessing: rebuild all DSP for the current rate/block.
  if (state) {
    engine_.prepare(processSetup.sampleRate, processSetup.maxSamplesPerBlock);
    engine_.reset();
    display_limiter_.prepare(processSetup.sampleRate);
    display_acc_.take();
    prepared_ = true;
  } else {
    prepared_ = false;
  }
  return AudioEffect::setActive(state);
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
  Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
  Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {
  if (numIns != 1 || numOuts != 1 || !inputs || !outputs)
    return Steinberg::kResultFalse;
  if (inputs[0] != Steinberg::Vst::SpeakerArr::kStereo || outputs[0] != Steinberg::Vst::SpeakerArr::kStereo)
    return Steinberg::kResultFalse;
  return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

Steinberg::uint32 PLUGIN_API Processor::getTailSamples() {
  return static_cast<Steinberg::uint32>(engine_.tailSamples());
}

void Processor::updateParamsFromChanges(Steinberg::Vst::IParameterChanges* changes) {
  if (!changes)
    return;
  const Steinberg::int32 num = changes->getParameterCount();
  for (Steinberg::int32 i = 0; i < num; ++i) {
    auto* queue = changes->getParameterData(i);
    if (!queue || queue->getPointCount() <= 0)
      continue;
    const Steinberg::Vst::ParamID id = queue->getParameterId();
    Steinberg::Vst::ParamValue value = 0.;
    Steinberg::int32 sample_offset = 0;
    if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) != Steinberg::kResultOk)
      continue;
    if (id == kClipClear) {   // writable display parameter: not part of the persistent table
      if (value >= 0.5) clear_clips_requested_ = true;
      continue;
    }
    if (id < static_cast<Steinberg::Vst::ParamID>(kNumParams))
      param_value_[id] = value;   // sanitized by mapParams
  }
}

void Processor::publishDisplay(Steinberg::Vst::ProcessData& data) {
  display_acc_.add(engine_.lastDisplay());
  auto* out = data.outputParameterChanges;
  if (!out)
    return;
  const auto put = [out](Steinberg::Vst::ParamID id, double v) {
    Steinberg::int32 index = 0;
    if (auto* queue = out->addParameterData(id, index))
      queue->addPoint(0, v, index);
  };
  if (clear_clips_ack_) {
    clear_clips_ack_ = false;
    put(kClipClear, 0.0);   // acknowledge: the editor's trigger goes back to 0
  }
  if (!display_limiter_.advance(data.numSamples))
    return;
  const DisplayValues d = display_acc_.take();
  const auto norm = [](float db) { return Ui::meterNormFromDb(db, -60.0, 6.0); };
  put(kMeterInL, norm(d.inPeakDb[0]));
  put(kMeterInR, norm(d.inPeakDb[1]));
  put(kMeterOutL, norm(d.outPeakDb[0]));
  put(kMeterOutR, norm(d.outPeakDb[1]));
  put(kClipIn, d.clipIn ? 1.0 : 0.0);
  put(kClipDrive, d.clipDrive ? 1.0 : 0.0);
  put(kClipOut, d.clipOut ? 1.0 : 0.0);
}

Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data) {
  updateParamsFromChanges(data.inputParameterChanges);

  if (!prepared_ || data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0 ||
      !data.inputs || !data.outputs)
    return Steinberg::kResultOk;

  Steinberg::Vst::AudioBusBuffers& in = data.inputs[0];
  Steinberg::Vst::AudioBusBuffers& out = data.outputs[0];
  if (!in.channelBuffers32 || !out.channelBuffers32 || in.numChannels < 1 ||
      out.numChannels < 1 || !in.channelBuffers32[0] || !out.channelBuffers32[0])
    return Steinberg::kResultOk;

  engine_.setSettings(mapParams(param_value_));

  const float* inL = in.channelBuffers32[0];
  const float* inR = in.numChannels >= 2 ? in.channelBuffers32[1] : nullptr;   // null: mono host track
  float* outL = out.channelBuffers32[0];
  float* outR = out.numChannels >= 2 ? out.channelBuffers32[1] : nullptr;

  if (clear_clips_requested_) {
    engine_.clearClips();
    display_acc_.clearClips();
    clear_clips_requested_ = false;
    clear_clips_ack_ = true;
  }
  engine_.process(inL, inR, outL, outR, data.numSamples);
  publishDisplay(data);
  out.silenceFlags = 0;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
  std::vector<uint8_t> bytes;
  if (!readStateBytes(state, bytes))
    return Steinberg::kResultFalse;
  double values[kNumParams];
  if (!deserializeState(bytes.data(), bytes.size(), values))
    return Steinberg::kResultFalse;
  for (int i = 0; i < static_cast<int>(kNumParams); ++i) param_value_[i] = values[i];
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  std::vector<uint8_t> bytes;
  serializeState(param_value_, bytes);
  Steinberg::int32 written = 0;
  if (state->write(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &written) != Steinberg::kResultOk ||
      written != static_cast<Steinberg::int32>(bytes.size()))
    return Steinberg::kResultFalse;
  return Steinberg::kResultOk;
}

} // namespace FairyTapes::Hotline
