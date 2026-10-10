/**
 * Fairy Tapes · The Stripper — VST3 Audio Processor implementation.
 * Chain: Gain staging (not implemented) → EQ → Compression → Drive → Limiter → Gate.
 * EQ: 4K-EQ in both modes. Channel = Black (G-series, tight). Mix bus = Brown (E-series, wide/glue).
 * LP/HP active in both modes. Compression: Multi-Comp DSP (Dusk Audio port); Channel = Voice/Drums/Classic, Mix bus = Bus only.
 */

#include "processor.h"
#include "state.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace FairyTapes::TheStripper {

namespace {
constexpr Steinberg::int32 kMaxBlockSize = 8192;
}

Processor::Processor() {
  setControllerClass(kControllerUID);
  double defaults[kNumParams];
  fillDefaultParams(defaults);
  for (int i = 0; i < kNumParams; ++i)
    param_value_[i] = defaults[i];
}

Steinberg::tresult PLUGIN_API Processor::initialize(Steinberg::FUnknown* context) {
  auto result = AudioEffect::initialize(context);
  if (result != Steinberg::kResultOk)
    return result;

  addAudioInput(STR16("Main Input"), Steinberg::Vst::SpeakerArr::kStereo);
  addAudioInput(STR16("Sidechain"), Steinberg::Vst::SpeakerArr::kStereo);
  addAudioOutput(STR16("Main Output"), Steinberg::Vst::SpeakerArr::kStereo);

  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
  if (state) {
    scratch_[0].resize(kMaxBlockSize);
    scratch_[1].resize(kMaxBlockSize);
  } else {
    scratch_[0].clear();
    scratch_[1].clear();
  }
  return AudioEffect::setActive(state);
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
  Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
  Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {
  // Main only (1 in) or main + sidechain (2 in); single stereo output
  if (numOuts != 1)
    return Steinberg::kResultFalse;
  if (numIns != 1 && numIns != 2)
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
    if (id < 0 || id >= kNumParams)
      continue;
    Steinberg::Vst::ParamValue value = 0.;
    Steinberg::int32 sample_offset = 0;
    if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) == Steinberg::kResultOk)
      param_value_[id] = value;
  }
}

void Processor::processChain(float* const* out, const float* const* in,
                             Steinberg::int32 numChannels, Steinberg::int32 numSamples,
                             double sampleRate,
                             const float* sidechainL, const float* sidechainR) {
  const bool global_bypass = param_value_[kBypass] >= 0.5;
  const float input_lin = static_cast<float>(normalizedToLinear(param_value_[kInputGain]));
  const float output_lin = static_cast<float>(normalizedToLinear(param_value_[kOutputGain]));
  const bool channel_mode = isChannelMode(param_value_[kStripMode]);
  const bool bypass_gain_staging = isBypassStage(param_value_[kBypassGainStaging]);
  const bool bypass_compression = isBypassStage(param_value_[kBypassCompression]);

  if (numChannels < 2 || numSamples <= 0 ||
      scratch_[0].size() < static_cast<size_t>(numSamples) ||
      scratch_[1].size() < static_cast<size_t>(numSamples))
    return;

  float* L = scratch_[0].data();
  float* R = scratch_[1].data();
  const float* inL = in[0];
  const float* inR = in[1];

  if (global_bypass) {
    for (Steinberg::int32 i = 0; i < numSamples; ++i) {
      L[i] = inL[i];
      R[i] = inR[i];
    }
    if (out[0] != L) std::memcpy(out[0], L, static_cast<size_t>(numSamples) * sizeof(float));
    if (out[1] != R) std::memcpy(out[1], R, static_cast<size_t>(numSamples) * sizeof(float));
    return;
  }

  // Copy in -> scratch and apply input gain
  for (Steinberg::int32 i = 0; i < numSamples; ++i) {
    L[i] = inL[i] * input_lin;
    R[i] = inR[i] * input_lin;
  }

  // Gain staging (Channel only): not implemented (design). Params kBypassGainStaging and kGainStagingTrim
  // are kept for future use; no processing applied here.

  // EQ: 4K-EQ in both modes. Channel = Black (G, tight); Mix bus = Brown (E, wide/glue).
  const bool bypass_eq = isBypassStage(param_value_[kBypassEQ]);
  const bool bypass_lphp = isBypassStage(param_value_[kBypassLPHP]);

  if (!bypass_eq) {
    if (sampleRate != lastEqSampleRate_ || numSamples != lastEqBlockSize_) {
      fourkEq_.prepare(sampleRate, numSamples);
      lastEqSampleRate_ = sampleRate;
      lastEqBlockSize_ = numSamples;
    }
    DSP::FourKEQParams eqp;
    eqp.hpfFreq = static_cast<float>(eqNormToHPFFreq(param_value_[kEqHPFFreq]));
    eqp.hpfEnabled = !bypass_lphp && (param_value_[kEqHPFOn] >= 0.5);
    eqp.lpfFreq = static_cast<float>(eqNormToLPFFreq(param_value_[kEqLPFFreq]));
    eqp.lpfEnabled = !bypass_lphp && (param_value_[kEqLPFOn] >= 0.5);
    eqp.lfGain = static_cast<float>(eqNormToGainDb(param_value_[kEqLFGain]));
    eqp.lfFreq = static_cast<float>(eqNormToLFFreq(param_value_[kEqLFFreq]));
    eqp.lfBell = param_value_[kEqLFBell] >= 0.5;
    eqp.lmGain = static_cast<float>(eqNormToGainDb(param_value_[kEqLMGain]));
    eqp.lmFreq = static_cast<float>(eqNormToLMFreq(param_value_[kEqLMFreq]));
    eqp.lmQ = static_cast<float>(eqNormToQ(param_value_[kEqLMQ]));
    eqp.hmGain = static_cast<float>(eqNormToGainDb(param_value_[kEqHMGain]));
    eqp.hmFreq = static_cast<float>(eqNormToHMFreq(param_value_[kEqHMFreq]));
    eqp.hmQ = static_cast<float>(eqNormToQ(param_value_[kEqHMQ]));
    eqp.hfGain = static_cast<float>(eqNormToGainDb(param_value_[kEqHFGain]));
    eqp.hfFreq = static_cast<float>(eqNormToHFFreq(param_value_[kEqHFFreq]));
    eqp.hfBell = param_value_[kEqHFBell] >= 0.5;
    eqp.eqTypeBlack = channel_mode; // Channel = Black (G), Mix bus = Brown (E)
    eqp.saturation = static_cast<float>(param_value_[kEqSaturation]);
    fourkEq_.process(L, R, numSamples, eqp);
  }

  // Compression: Multi-Comp (Opto/FET/VCA/Bus). Channel = Voice/Drums/Classic; Mix bus = Bus only.
  if (!bypass_compression) {
    compressionStub_.setSampleRate(sampleRate);
    compressionStub_.prepare(numSamples);
    const bool channel_mode = isChannelMode(param_value_[kStripMode]);
    const int effective_mode = channel_mode
        ? static_cast<int>(param_value_[kCompressorMode] * 2.0 + 0.5)  // 0, 0.5, 1 -> 0, 1, 2 (Voice, Drums, Classic)
        : 3;  // Mix bus: Bus
    compressionStub_.setMode(effective_mode);
    const float amount = static_cast<float>(param_value_[kCompressAmount]);
    const float mix = static_cast<float>(param_value_[kCompressMix]);
    compressionStub_.setParams(amount, mix);
    compressionStub_.process(L, R, numSamples, sidechainL, sidechainR);
  }

  // Drive: NAM (CPU inference; GPU path planned). Auto-compensated output; always 100% wet.
  const bool bypass_drive = isBypassStage(param_value_[kBypassDrive]);
  if (!bypass_drive) {
    namDrive_.setSampleRate(sampleRate);
    namDrive_.setDrive(static_cast<float>(param_value_[kDriveAmount]));
    namDrive_.process(L, R, numSamples);
  }

  // Limiter: BlockParty (Airwindows)
  const bool bypass_limiter = isBypassStage(param_value_[kBypassLimiter]);
  if (!bypass_limiter) {
    limiter_.setSampleRate(sampleRate);
    limiter_.setParams(static_cast<float>(param_value_[kLimiterPound]), 1.0f);
    limiter_.process(L, R, numSamples);
  }

  // Gate: DeNoise (Airwindows)
  const bool bypass_gate = isBypassStage(param_value_[kBypassGate]);
  if (!bypass_gate) {
    gate_.setSampleRate(sampleRate);
    gate_.setParams(static_cast<float>(param_value_[kGateThreshold]), 0.25f, 1.0f);
    gate_.process(L, R, numSamples);
  }

  // Output gain and copy to out
  for (Steinberg::int32 i = 0; i < numSamples; ++i) {
    L[i] *= output_lin;
    R[i] *= output_lin;
  }
  if (out[0] != L) std::memcpy(out[0], L, static_cast<size_t>(numSamples) * sizeof(float));
  if (out[1] != R) std::memcpy(out[1], R, static_cast<size_t>(numSamples) * sizeof(float));
}

Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data) {
  updateParamsFromChanges(data.inputParameterChanges);

  if (data.numInputs == 0 || data.numOutputs == 0)
    return Steinberg::kResultOk;

  Steinberg::Vst::AudioBusBuffers& in = data.inputs[0];
  Steinberg::Vst::AudioBusBuffers& out = data.outputs[0];

  if (!in.channelBuffers32 || !out.channelBuffers32)
    return Steinberg::kResultOk;

  const Steinberg::int32 num_channels = std::min(in.numChannels, out.numChannels);
  const Steinberg::int32 num_samples = data.numSamples;
  const double sample_rate = data.processContext && data.processContext->sampleRate > 0.0
                               ? data.processContext->sampleRate
                               : 44100.0;

  const float* scL = nullptr;
  const float* scR = nullptr;
  if (data.numInputs >= 2 && data.inputs[1].channelBuffers32 && data.inputs[1].numChannels >= 1) {
    scL = data.inputs[1].channelBuffers32[0];
    scR = (data.inputs[1].numChannels >= 2) ? data.inputs[1].channelBuffers32[1] : scL;
  }

  processChain(out.channelBuffers32, in.channelBuffers32, num_channels, num_samples, sample_rate,
               scL, scR);

  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  Steinberg::IBStreamer s(state, kLittleEndian);
  PluginState st;
  if (!readState(s, st))
    return Steinberg::kResultFalse;
  for (int i = 0; i < kNumParams; ++i)
    param_value_[i] = static_cast<Steinberg::Vst::ParamValue>(st.params[i]);
  modelPath_ = st.modelPath;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  Steinberg::IBStreamer s(state, kLittleEndian);
  PluginState st;
  for (int i = 0; i < kNumParams; ++i)
    st.params[i] = static_cast<double>(param_value_[i]);
  st.modelPath = modelPath_;
  return writeState(s, st) ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

} // namespace FairyTapes::TheStripper
