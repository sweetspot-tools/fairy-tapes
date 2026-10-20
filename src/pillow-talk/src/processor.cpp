/**
 * Fairy Tapes · Pillow Talk — VST3 Audio Processor implementation.
 */

#include "processor.h"
#include "messages.h"
#include "state_stream.h"
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
  ensureSeed(decor_);   // a fresh instance saves a seed with its first getState
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
  display_acc_.add(engine_.lastDisplay());
  level_acc_.add(engine_.lastLevels());
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
  const DisplayValues d = display_acc_.take();
  const LevelValues l = level_acc_.take();
  const double values[9] = {Ui::meterNormFromDb(d.voiceDb, -60.0, 0.0),
                            std::clamp((double)d.sendGain, 0.0, 1.0),
                            Ui::meterNormFromDb(d.echoDb, -60.0, 0.0),
                            Ui::meterNormFromDb(l.inL, -60.0, 0.0),
                            Ui::meterNormFromDb(l.inR, -60.0, 0.0),
                            Ui::meterNormFromDb(l.outL, -60.0, 0.0),
                            Ui::meterNormFromDb(l.outR, -60.0, 0.0),
                            l.clipIn ? 1.0 : 0.0,
                            l.clipOut ? 1.0 : 0.0};
  for (int i = 0; i < 9; ++i) put(kDispVoice + i, values[i]);
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
  const bool tempoValid = ctx && (ctx->state & Steinberg::Vst::ProcessContext::kTempoValid);
  const double bpm = tempoForBlock(tempoValid, tempoValid ? ctx->tempo : 0.0, fallback_bpm_.load(std::memory_order_relaxed));
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

  if (clear_clips_requested_) {
    engine_.clearClips();
    level_acc_.clearClips();
    clear_clips_requested_ = false;
    clear_clips_ack_ = true;
  }
  engine_.process(inL, inR, sc.L, sc.R, outL, outR, data.numSamples);
  publishDisplay(data);
  out.silenceFlags = 0;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
  std::vector<uint8_t> bytes;
  if (!readStateBytes(state, bytes))
    return Steinberg::kResultFalse;
  PluginState st;
  defaultState(st);
  if (!deserializeState(bytes.data(), bytes.size(), st))
    return Steinberg::kResultFalse;
  ensureSeed(st.decor);   // v1 or seed 0: a new seed, saved by the next getState
  for (int i = 0; i < (int)kNumParams; ++i) param_value_[i] = st.params[i];
  {
    std::lock_guard<std::mutex> lk(decor_mutex_);
    decor_ = std::move(st.decor);
    fallback_bpm_.store(decor_.behaviour.tempoFallbackBpm, std::memory_order_relaxed);
  }
  sendDecor();
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  PluginState st;
  for (int i = 0; i < (int)kNumParams; ++i) st.params[i] = param_value_[i];
  {
    std::lock_guard<std::mutex> lk(decor_mutex_);
    st.decor = decor_;
  }
  std::vector<uint8_t> bytes;
  serializeState(st, bytes);
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
    fallback_bpm_.store(decor_.behaviour.tempoFallbackBpm, std::memory_order_relaxed);
    return Steinberg::kResultOk;
  }
  return AudioEffect::notify(message);
}

} // namespace FairyTapes::PillowTalk
