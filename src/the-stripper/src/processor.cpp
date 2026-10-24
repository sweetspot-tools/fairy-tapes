/**
 * Fairy Tapes · The Stripper — VST3 Audio Processor implementation.
 * Chain: Input → [Drive, Channel] → EQ (with the 4K saturation) → Compression → [Drive, Mix bus] → Limiter → Gate → Output.
 * The Drive block (NAM then TAPE) moves with Strip Mode: right after the input in Channel, before the limiter in Mix bus.
 * EQ: 4K-EQ in both modes. Channel = Black (G-series, tight). Mix bus = Brown (E-series, wide/glue).
 * LP/HP active in both modes. Compression: Multi-Comp DSP (Dusk Audio port); Channel = Voice/Drums/Classic, Mix bus = Bus only.
 */

#include "processor.h"
#include "state.h"
#include "messages.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"
#include <cstring>
#include <filesystem>
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

Processor::~Processor() {
  namDrive_.setLoadCallback(nullptr);   // the worker thread must not call back into a half-destroyed processor
}

Steinberg::tresult PLUGIN_API Processor::initialize(Steinberg::FUnknown* context) {
  auto result = AudioEffect::initialize(context);
  if (result != Steinberg::kResultOk)
    return result;

  namDrive_.setLoadCallback([this] { sendModelStatus(); });

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
    Steinberg::Vst::ParamValue value = 0.;
    Steinberg::int32 sample_offset = 0;
    if (id == kClearClips) {
      // The one writable meter parameter: a click on a clip LED asks for every clip LED to be cleared.
      if (queue->getPoint(queue->getPointCount() - 1, sample_offset, value) == Steinberg::kResultOk && value >= 0.5) {
        for (auto& c : clip_)
          c.clear();
        publishClearReset_ = true;
      }
      continue;
    }
    if (id < 0 || id >= kNumParams)
      continue;
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
    // Bypassed: output equals input, no stage works, so only the Input and Output clip LEDs can light.
    clip_[0].process(L, R, numSamples);
    clip_[5].process(L, R, numSamples);
    for (int i = 1; i <= 4; ++i)
      clip_[i].clear();
    grComp_.reset();
    grLimiter_.reset();
    grGate_.reset();
    if (out[0] != L) std::memcpy(out[0], L, static_cast<size_t>(numSamples) * sizeof(float));
    if (out[1] != R) std::memcpy(out[1], R, static_cast<size_t>(numSamples) * sizeof(float));
    return;
  }

  // Copy in -> scratch and apply input gain
  for (Steinberg::int32 i = 0; i < numSamples; ++i) {
    L[i] = inL[i] * input_lin;
    R[i] = inR[i] * input_lin;
  }

  clip_[0].process(L, R, numSamples);   // Input: output of the input gain stage

  // Drive block, Channel position: preamp / capture (NAM) and multitrack tape (TAPE, Tracking voicing) before the console.
  if (driveBlockAtStart(param_value_[kStripMode]))
    processDriveBlock(L, R, numSamples, sampleRate, channel_mode);

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
    fourkEq_.process(L, R, numSamples, eqp);
    // The 4K console saturation: last step of the EQ (G series in Channel, E series in Mix bus), out with the EQ's IN.
    if (sampleRate != saturationRate_) {
      eqSaturation_.setSampleRate(sampleRate);
      eqSaturation_.reset();
      saturationRate_ = sampleRate;
    }
    eqSaturation_.setConsoleType(channel_mode ? DSP::SSLSaturation::ConsoleType::GSeries : DSP::SSLSaturation::ConsoleType::ESeries);
    const float satAmount = static_cast<float>(std::clamp(param_value_[kEqSaturation], 0.0, 1.0));
    if (satAmount > 0.001f) {
      for (int i = 0; i < numSamples; ++i) {
        L[i] = eqSaturation_.processSample(L[i], satAmount, true);
        R[i] = eqSaturation_.processSample(R[i], satAmount, false);
      }
    }
    clip_[1].process(L, R, numSamples);
  } else {
    clip_[1].clear();
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
    grComp_.updateDb(compressionStub_.currentGainReductionDb(), numSamples);
    clip_[2].process(L, R, numSamples);
  } else {
    grComp_.reset();
    clip_[2].clear();
  }

  // Drive block, Mix bus position: outboard (NAM) and the 2-track mixdown machine (TAPE, Mixdown voicing) before the limiter.
  if (!driveBlockAtStart(param_value_[kStripMode]))
    processDriveBlock(L, R, numSamples, sampleRate, channel_mode);

  // Limiter: BlockParty (Airwindows)
  const bool bypass_limiter = isBypassStage(param_value_[kBypassLimiter]);
  if (!bypass_limiter) {
    limiter_.setSampleRate(sampleRate);
    limiter_.setParams(static_cast<float>(param_value_[kLimiterPound]), 1.0f);
    const float limiterIn = DSP::peakOf(L, R, numSamples);
    limiter_.process(L, R, numSamples);
    grLimiter_.update(limiterIn, DSP::peakOf(L, R, numSamples), numSamples);   // BlockParty has no gain-reduction read-out: peak ratio
    clip_[4].process(L, R, numSamples);
  } else {
    grLimiter_.reset();
    clip_[4].clear();
  }

  // Gate: DeNoise (Airwindows)
  const bool bypass_gate = isBypassStage(param_value_[kBypassGate]);
  if (!bypass_gate) {
    gate_.setSampleRate(sampleRate);
    gate_.setParams(static_cast<float>(param_value_[kGateThreshold]), 0.25f, 1.0f);
    const float gateIn = DSP::peakOf(L, R, numSamples);
    gate_.process(L, R, numSamples);
    grGate_.update(gateIn, DSP::peakOf(L, R, numSamples), numSamples);   // DeNoise has no read-out: peak ratio
  } else {
    grGate_.reset();
  }

  // Output gain and copy to out
  for (Steinberg::int32 i = 0; i < numSamples; ++i) {
    L[i] *= output_lin;
    R[i] *= output_lin;
  }
  clip_[5].process(L, R, numSamples);   // Output
  if (out[0] != L) std::memcpy(out[0], L, static_cast<size_t>(numSamples) * sizeof(float));
  if (out[1] != R) std::memcpy(out[1], R, static_cast<size_t>(numSamples) * sizeof(float));
}

void Processor::processDriveBlock(float* L, float* R, Steinberg::int32 numSamples, double sampleRate, bool channelMode) {
  if (isBypassStage(param_value_[kBypassDrive])) {
    clip_[3].clear();
    return;
  }
  // NAM (GPU or CPU inference): auto-compensated output, always 100% wet; no model loaded: passes the signal through.
  if (param_value_[kDriveNamOn] >= 0.5) {
    namDrive_.setSampleRate(sampleRate);
    namDrive_.setDrive(static_cast<float>(param_value_[kDriveAmount]));
    namDrive_.process(L, R, numSamples);
  }
  // TAPE: one knob over slam and head bump; the voicing follows Strip Mode (Channel = Tracking, Mix bus = Mixdown).
  if (param_value_[kDriveTapeOn] >= 0.5) {
    tape_.setSampleRate(sampleRate);
    tape_.setParams(static_cast<float>(param_value_[kDriveTapeAmount]),
                    channelMode ? DSP::TapeSaturation::Voicing::Tracking : DSP::TapeSaturation::Voicing::Mixdown);
    tape_.process(L, R, numSamples);
  }
  clip_[3].process(L, R, numSamples);
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

  if (sample_rate != meterSampleRate_)
    prepareMeters(sample_rate);
  if (num_channels >= 2 && num_samples > 0) {
    peakIn_[0].process(in.channelBuffers32[0], num_samples);    // raw input, before everything
    peakIn_[1].process(in.channelBuffers32[1], num_samples);
  }

  processChain(out.channelBuffers32, in.channelBuffers32, num_channels, num_samples, sample_rate,
               scL, scR);

  if (num_channels >= 2 && num_samples > 0) {
    peakOut_[0].process(out.channelBuffers32[0], num_samples);  // after the output gain
    peakOut_[1].process(out.channelBuffers32[1], num_samples);
  }
  if (meterRate_.advance(num_samples))
    publishMeters(data);

  return Steinberg::kResultOk;
}

namespace {
constexpr double kGateMeterReleaseDbPerSec = 600.0;   // the gate meter must show how fast the gate opens and closes
}

void Processor::prepareMeters(double sampleRate) {
  meterSampleRate_ = sampleRate;
  for (auto& m : peakIn_) m.prepare(sampleRate);
  for (auto& m : peakOut_) m.prepare(sampleRate);
  for (auto& c : clip_) c.prepare(sampleRate);
  grComp_.prepare(sampleRate);
  grLimiter_.prepare(sampleRate);
  grGate_.prepare(sampleRate);
  grGate_.setReleaseDbPerSec(kGateMeterReleaseDbPerSec);
  meterRate_.prepare(sampleRate, 16.0);
}

/// At most every 16 ms: write the read-only meter parameters (normalised) into the host's output queue.
void Processor::publishMeters(Steinberg::Vst::ProcessData& data) {
  if (!data.outputParameterChanges)
    return;
  auto put = [&](Steinberg::Vst::ParamID id, double v) {
    Steinberg::int32 index = 0;
    if (auto* q = data.outputParameterChanges->addParameterData(id, index)) {
      Steinberg::int32 point = 0;
      q->addPoint(0, v, point);
    }
  };
  using DSP::meterNormFromDb;
  put(kMeterInL, meterNormFromDb(peakIn_[0].levelDb(), -60.0, 6.0));
  put(kMeterInR, meterNormFromDb(peakIn_[1].levelDb(), -60.0, 6.0));
  put(kMeterOutL, meterNormFromDb(peakOut_[0].levelDb(), -60.0, 6.0));
  put(kMeterOutR, meterNormFromDb(peakOut_[1].levelDb(), -60.0, 6.0));
  put(kMeterGrComp, DSP::grValueFromDb(grComp_.value()));   // SSL 360 Link encoding (spec 4.1)
  put(kMeterGrLimiter, DSP::grValueFromDb(grLimiter_.value()));
  put(kMeterGrGate, DSP::grValueFromDb(grGate_.value()));
  put(kClipInput, clip_[0].lit() ? 1.0 : 0.0);
  put(kClipEq, clip_[1].lit() ? 1.0 : 0.0);
  put(kClipComp, clip_[2].lit() ? 1.0 : 0.0);
  put(kClipDrive, clip_[3].lit() ? 1.0 : 0.0);
  put(kClipLimiter, clip_[4].lit() ? 1.0 : 0.0);
  put(kClipOutput, clip_[5].lit() ? 1.0 : 0.0);
  if (publishClearReset_) {
    put(kClearClips, 0.0);
    publishClearReset_ = false;
  }
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
  requestModel(modelPath_);   // restore the model the project was saved with
  return Steinberg::kResultOk;
}

void Processor::requestModel(const std::string& path) {
  // A state restored with the model that is already loading or loaded does not reload it.
  const auto st = namDrive_.loadState();
  if (path == requestedPath_ && (st == Shared::DSP::NamDrive::LoadState::Loaded || st == Shared::DSP::NamDrive::LoadState::Loading))
    return;
  requestedPath_ = path;
  modelPath_ = path;
  const std::u8string u8(reinterpret_cast<const char8_t*>(path.data()), path.size());   // the path travels as UTF-8
  namDrive_.requestLoad(std::filesystem::path(u8));
}

void Processor::sendModelStatus() {
  std::lock_guard<std::mutex> lk(peerMutex_);
  if (!getPeer())
    return;
  Steinberg::Vst::IMessage* msg = allocateMessage();
  if (!msg)
    return;
  ModelStatus st;
  st.state = static_cast<int>(namDrive_.loadState());
  st.name = namDrive_.loadedModelName();
  st.error = namDrive_.lastError();
  st.gpu = st.state == 2 && namDrive_.isGpuActive();
  encodeStatusMessage(msg, st);
  sendMessage(msg);
  msg->release();
}

Steinberg::tresult PLUGIN_API Processor::connect(Steinberg::Vst::IConnectionPoint* other) {
  Steinberg::tresult r;
  {
    std::lock_guard<std::mutex> lk(peerMutex_);
    r = AudioEffect::connect(other);
  }
  sendModelStatus();   // a controller that connects late learns the current state
  return r;
}

Steinberg::tresult PLUGIN_API Processor::disconnect(Steinberg::Vst::IConnectionPoint* other) {
  std::lock_guard<std::mutex> lk(peerMutex_);
  return AudioEffect::disconnect(other);
}

Steinberg::tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message) {
  std::string path;
  if (decodeLoadMessage(message, path)) {
    requestModel(path);
    sendModelStatus();   // "Loading" right away; the worker reports the outcome through the callback
    return Steinberg::kResultOk;
  }
  return AudioEffect::notify(message);
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
