/**
 * Fairy Tapes · The Stripper — VST3 Audio Processor.
 * Chain: Gain staging (not implemented) → EQ → Compression → Drive → Limiter → Gate.
 * EQ: 4K-EQ in both modes (Channel=Black/G, Mix bus=Brown/E). Compression: Multi-Comp DSP (Dusk port);
 * Channel = Voice/Drums/Classic, Mix bus = Bus only. No custom UI; host shows standard parameters.
 */

#pragma once

#include "cids.h"
#include "params.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "dsp/compression_stub.h"
#include "dsp/fourk_eq.h"
#include "dsp/blockparty.h"
#include "dsp/denoise.h"
#include "dsp/meter.h"
#include "nam_drive.h"
#include <string>
#include <vector>

namespace FairyTapes::TheStripper {

class Processor : public Steinberg::Vst::AudioEffect {
public:
  Processor();
  ~Processor() override = default;

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
  Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
  Steinberg::tresult PLUGIN_API setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

private:
  void updateParamsFromChanges(Steinberg::Vst::IParameterChanges* changes);
  void processChain(float* const* out, const float* const* in, Steinberg::int32 numChannels,
                    Steinberg::int32 numSamples, double sampleRate,
                    const float* sidechainL = nullptr, const float* sidechainR = nullptr);

  Steinberg::Vst::ParamValue param_value_[kNumParams];
  std::string modelPath_;  // NAM model path restored from / saved to the state (loaded in a later step)
  std::vector<float> scratch_[2];
  DSP::CompressionStub compressionStub_;
  DSP::FourKEQ fourkEq_;
  Shared::DSP::NamDrive namDrive_;
  DSP::BlockParty limiter_;
  DSP::DeNoise gate_;

  // ---- metering (audio thread only; values leave through outputParameterChanges, see process()) ----
  void prepareMeters(double sampleRate);
  void publishMeters(Steinberg::Vst::ProcessData& data);
  DSP::PeakMeter peakIn_[2];
  DSP::PeakMeter peakOut_[2];
  DSP::ClipDetector clip_[6];            // Input, EQ, Compression, Drive, Limiter, Output
  DSP::GainReductionMeter grComp_;
  DSP::GainReductionMeter grLimiter_;
  DSP::UpdateLimiter meterRate_;
  double meterSampleRate_ = 0.0;
  bool publishClearReset_ = false;       // tell the controller that the clear request was handled

  double lastEqSampleRate_ = 0.0;
  int lastEqBlockSize_ = 0;
};

} // namespace FairyTapes::TheStripper
