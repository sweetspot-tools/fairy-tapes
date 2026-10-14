/**
 * Fairy Tapes · The Stripper — VST3 module entry and factory.
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

#define stringPluginName "The Stripper"
#include "version.h"
#define stringVersion FairyTapes::TheStripper::kPluginVersion

namespace {
Steinberg::FUnknown* createProcessor(void*) {
  return static_cast<Steinberg::Vst::IAudioProcessor*>(new FairyTapes::TheStripper::Processor);
}
Steinberg::FUnknown* createController(void*) {
  return static_cast<Steinberg::Vst::IEditController*>(new FairyTapes::TheStripper::Controller);
}
} // namespace

BEGIN_FACTORY_DEF("Fairy Tapes", "https://github.com/fairy-tapes", "")

  DEF_CLASS2(INLINE_UID_FROM_FUID(FairyTapes::TheStripper::kProcessorUID),
             PClassInfo::kManyInstances, kVstAudioEffectClass, stringPluginName,
             Vst::kDistributable, "Fx", stringVersion, kVstVersionString, createProcessor)

  DEF_CLASS2(INLINE_UID_FROM_FUID(FairyTapes::TheStripper::kControllerUID),
             PClassInfo::kManyInstances, kVstComponentControllerClass, stringPluginName, 0, "",
             stringVersion, kVstVersionString, createController)

END_FACTORY

// InitModule / DeinitModule come from the SDK's public.sdk/source/main/moduleinit.cpp (sdk library):
// they run the registered ModuleInitializers, among them VSTGUI::init from vstguieditor.cpp.
