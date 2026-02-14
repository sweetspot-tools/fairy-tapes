/**
 * Fairy Tapes · The Stripper — VST3 Audio Processor implementation.
 * Chain: Gain staging (not implemented) → EQ → Compression → Drive → Limiter → Gate.
 * EQ: Channel = 4K-EQ active (bands + LP/HP), Pultec disabled. Mix bus = Pultec active (stub), 4K-EQ bands disabled.
 * LP/HP remain active in both modes when feasible (Channel: full 4K-EQ; Mix bus: 4K-EQ HPF/LPF only, bands flat).
 * Compression: stub until Multi-Comp (Dusk) integrated; Channel = usage modes, Mix bus = Bus only.
 */

#include "processor.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace FairyTapes::TheStripper {

namespace {
constexpr int kStateVersion = 4;
constexpr Steinberg::int32 kMaxBlockSize = 8192;
constexpr int kNumParamsV2 = 18;   // param count before 4K-EQ params
constexpr int kNumParamsV3 = 32;   // param count before Compressor Mode
}

Processor::Processor() {
  setControllerClass(kControllerUID);
  param_value_[kBypass] = kDefaultBypass;
  param_value_[kStripMode] = kDefaultStripMode;
  param_value_[kInputGain] = kDefaultInputGain;
  param_value_[kOutputGain] = kDefaultOutputGain;
  param_value_[kBypassGainStaging] = kDefaultBypassStage;
  param_value_[kBypassLPHP] = kDefaultBypassStage;
  param_value_[kBypassEQ] = kDefaultBypassStage;
  param_value_[kBypassCompression] = kDefaultBypassStage;
  param_value_[kBypassDrive] = kDefaultBypassStage;
  param_value_[kBypassLimiter] = kDefaultBypassStage;
  param_value_[kBypassGate] = kDefaultBypassStage;
  param_value_[kGainStagingTrim] = kDefaultGainStagingTrim;
  param_value_[kCompressAmount] = kDefaultCompressAmount;
  param_value_[kCompressMix] = kDefaultCompressMix;
  param_value_[kCompressorMode] = kDefaultCompressorMode;
  param_value_[kEqHPFFreq] = kDefaultEqHPFFreq;
  param_value_[kEqHPFOn] = kDefaultEqHPFOn;
  param_value_[kEqLPFFreq] = kDefaultEqLPFFreq;
  param_value_[kEqLPFOn] = kDefaultEqLPFOn;
  param_value_[kEqLFGain] = kDefaultEqLFGain;
  param_value_[kEqLFFreq] = kDefaultEqLFFreq;
  param_value_[kEqLFBell] = kDefaultEqLFBell;
  param_value_[kEqLMGain] = kDefaultEqLMGain;
  param_value_[kEqLMFreq] = kDefaultEqLMFreq;
  param_value_[kEqLMQ] = kDefaultEqLMQ;
  param_value_[kEqHMGain] = kDefaultEqHMGain;
  param_value_[kEqHMFreq] = kDefaultEqHMFreq;
  param_value_[kEqHMQ] = kDefaultEqHMQ;
  param_value_[kEqHFGain] = kDefaultEqHFGain;
  param_value_[kEqHFFreq] = kDefaultEqHFFreq;
  param_value_[kEqHFBell] = kDefaultEqHFBell;
  param_value_[kEqType] = kDefaultEqType;
  param_value_[kEqSaturation] = kDefaultEqSaturation;
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
  if (numIns != 1 || numOuts != 1)
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
                             double sampleRate) {
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

  // EQ: Channel = 4K-EQ active (bands + LP/HP). Mix bus = Pultec stub (no processing) but LP/HP active when feasible (4K-EQ HPF/LPF only).
  const bool bypass_eq = isBypassStage(param_value_[kBypassEQ]);
  const bool bypass_lphp = isBypassStage(param_value_[kBypassLPHP]);

  if (channel_mode && !bypass_eq) {
    // Channel: full 4K-EQ (bands + LP/HP); Pultec controls disabled in UI/design, not used here.
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
    eqp.eqTypeBlack = param_value_[kEqType] >= 0.5;
    eqp.saturation = static_cast<float>(param_value_[kEqSaturation]);
    fourkEq_.process(L, R, numSamples, eqp);
  } else if (!channel_mode && !bypass_eq) {
    // Mix bus: Pultec not implemented (stub). LP/HP active when feasible: run 4K-EQ with only HPF/LPF, bands flat.
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
    eqp.lfGain = 0.0f;
    eqp.lmGain = 0.0f;
    eqp.hmGain = 0.0f;
    eqp.hfGain = 0.0f;
    fourkEq_.process(L, R, numSamples, eqp);
  }

  // Compression: stub until Multi-Comp (Dusk) is integrated. Channel = Voice/Drums/Classic; Mix bus = Bus only.
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
    compressionStub_.process(L, R, numSamples);
  }

  // Drive, Limiter, Gate (stub: passthrough)

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

  processChain(out.channelBuffers32, in.channelBuffers32, num_channels, num_samples, sample_rate);

  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  Steinberg::IBStreamer s(state, kLittleEndian);
  Steinberg::int32 version = 0;
  if (s.readInt32(version) == false)
    return Steinberg::kResultFalse;
  const int n = (version == 1) ? 3 : (version == 2) ? kNumParamsV2 : (version == 3) ? kNumParamsV3 : kNumParams;
  for (int i = 0; i < n; ++i) {
    double v = 0.;
    if (s.readDouble(v) == false)
      return Steinberg::kResultFalse;
    if (i < kNumParams)
      param_value_[i] = static_cast<Steinberg::Vst::ParamValue>(v);
  }
  if (version == 1) {
    param_value_[kBypass] = 0.0;
    param_value_[kStripMode] = kDefaultStripMode;
    param_value_[kInputGain] = kDefaultInputGain;
    param_value_[kOutputGain] = kDefaultOutputGain;
    for (int i = kBypassGainStaging; i <= kBypassGate; ++i)
      param_value_[i] = kDefaultBypassStage;
    param_value_[kGainStagingTrim] = kDefaultGainStagingTrim;
    param_value_[kCompressAmount] = kDefaultCompressAmount;
    param_value_[kCompressMix] = kDefaultCompressMix;
    param_value_[kCompressorMode] = kDefaultCompressorMode;
  }
  if (version == 2) {
    param_value_[kEqHPFFreq] = param_value_[14];
    param_value_[kEqHPFOn] = param_value_[15];
    param_value_[kEqLPFFreq] = param_value_[16];
    param_value_[kEqLPFOn] = param_value_[17];
    param_value_[kCompressorMode] = kDefaultCompressorMode;
  }
  if (version == 3) {
    for (int i = kNumParams - 1; i >= static_cast<int>(kCompressorMode) + 1; --i)
      param_value_[i] = param_value_[i - 1];
    param_value_[kCompressorMode] = kDefaultCompressorMode;
  }
  if (version <= 2) {
    for (int i = kNumParamsV2; i < kNumParams; ++i)
      param_value_[i] = 0.5;
    param_value_[kEqHPFOn] = kDefaultEqHPFOn;
    param_value_[kEqLPFOn] = kDefaultEqLPFOn;
    param_value_[kEqLFBell] = kDefaultEqLFBell;
    param_value_[kEqHFBell] = kDefaultEqHFBell;
    param_value_[kEqType] = kDefaultEqType;
    param_value_[kEqSaturation] = kDefaultEqSaturation;
    param_value_[kEqHPFFreq] = kDefaultEqHPFFreq;
    param_value_[kEqLPFFreq] = kDefaultEqLPFFreq;
    param_value_[kEqLFGain] = kDefaultEqLFGain;
    param_value_[kEqLFFreq] = kDefaultEqLFFreq;
    param_value_[kEqLMGain] = kDefaultEqLMGain;
    param_value_[kEqLMFreq] = kDefaultEqLMFreq;
    param_value_[kEqLMQ] = kDefaultEqLMQ;
    param_value_[kEqHMGain] = kDefaultEqHMGain;
    param_value_[kEqHMFreq] = kDefaultEqHMFreq;
    param_value_[kEqHMQ] = kDefaultEqHMQ;
    param_value_[kEqHFGain] = kDefaultEqHFGain;
    param_value_[kEqHFFreq] = kDefaultEqHFFreq;
  }
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
  if (!state)
    return Steinberg::kResultFalse;
  Steinberg::IBStreamer s(state, kLittleEndian);
  if (s.writeInt32(kStateVersion) == false)
    return Steinberg::kResultFalse;
  for (int i = 0; i < kNumParams; ++i)
    if (s.writeDouble(static_cast<double>(param_value_[i])) == false)
      return Steinberg::kResultFalse;
  return Steinberg::kResultOk;
}

} // namespace FairyTapes::TheStripper
