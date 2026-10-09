/**
 * Fairy Tapes · Pillow Talk — VST3 module entry and factory.
 * The SDK factory macros give a heap-allocated, ref-counted CPluginFactory (the host
 * releases it) and export GetPluginFactory with SMTG_EXPORT_SYMBOL.
 */

#include "controller.h"
#include "processor.h"
#include "cids.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#define stringPluginName "Pillow Talk"
#define stringVersion "1.0.0"

namespace {
Steinberg::FUnknown* createProcessor(void*) {
  return static_cast<Steinberg::Vst::IAudioProcessor*>(new FairyTapes::PillowTalk::Processor);
}
Steinberg::FUnknown* createController(void*) {
  return static_cast<Steinberg::Vst::IEditController*>(new FairyTapes::PillowTalk::Controller);
}
} // namespace

BEGIN_FACTORY_DEF("Fairy Tapes", "https://github.com/fairy-tapes", "")

  DEF_CLASS2(INLINE_UID_FROM_FUID(FairyTapes::PillowTalk::kProcessorUID),
             PClassInfo::kManyInstances, kVstAudioEffectClass, stringPluginName,
             Vst::kDistributable, "Fx|Delay", stringVersion, kVstVersionString, createProcessor)

  DEF_CLASS2(INLINE_UID_FROM_FUID(FairyTapes::PillowTalk::kControllerUID),
             PClassInfo::kManyInstances, kVstComponentControllerClass, stringPluginName, 0, "",
             stringVersion, kVstVersionString, createController)

END_FACTORY

// Called by the SDK platform entry point (ModuleEntry / bundleEntry / InitDll).
bool InitModule() { return true; }
bool DeinitModule() { return true; }
