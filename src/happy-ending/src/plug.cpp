/**
 * Fairy Tapes · Happy Ending — VST3 module entry and factory.
 * The SDK factory macros give a heap-allocated, ref-counted CPluginFactory (the host
 * releases it) and export GetPluginFactory with SMTG_EXPORT_SYMBOL.
 */

#include "controller.h"
#include "processor.h"
#include "cids.h"
#include "version.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#define stringPluginName "Happy Ending"
#define stringVersion HAPPY_ENDING_VERSION

namespace {
Steinberg::FUnknown* createProcessor(void*) {
  return static_cast<Steinberg::Vst::IAudioProcessor*>(new FairyTapes::HappyEnding::Processor);
}
Steinberg::FUnknown* createController(void*) {
  return static_cast<Steinberg::Vst::IEditController*>(new FairyTapes::HappyEnding::Controller);
}
} // namespace

BEGIN_FACTORY_DEF("Sweetspot Tools", "https://github.com/sweetspot-tools/fairy-tapes", "")

  DEF_CLASS2(INLINE_UID_FROM_FUID(FairyTapes::HappyEnding::kProcessorUID),
             PClassInfo::kManyInstances, kVstAudioEffectClass, stringPluginName,
             Vst::kDistributable, "Fx|Mastering", stringVersion, kVstVersionString, createProcessor)

  DEF_CLASS2(INLINE_UID_FROM_FUID(FairyTapes::HappyEnding::kControllerUID),
             PClassInfo::kManyInstances, kVstComponentControllerClass, stringPluginName, 0, "",
             stringVersion, kVstVersionString, createController)

END_FACTORY

// InitModule / DeinitModule come from the SDK's public.sdk/source/main/moduleinit.cpp (sdk library).
