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
#include "dsp/nam_drive.h"
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
  std::vector<float> scratch_[2];
  DSP::CompressionStub compressionStub_;
  DSP::FourKEQ fourkEq_;
  DSP::NamDrive namDrive_;
  DSP::BlockParty limiter_;
  DSP::DeNoise gate_;

  double lastEqSampleRate_ = 0.0;
  int lastEqBlockSize_ = 0;
};

} // namespace FairyTapes::TheStripper
