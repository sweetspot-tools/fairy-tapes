/**
 * Fairy Tapes · The Stripper — VST3 module entry and factory.
 */

#include "controller.h"
#include "processor.h"
#include "cids.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/vsttypes.h"
#include <cstring>

#define stringPluginName "The Stripper"
#define stringControllerCategory "Component Controller Class"

// Plugin factory: Processor (index 0) and Controller (index 1)
namespace FairyTapes::TheStripper {

class Factory : public Steinberg::IPluginFactory {
public:
  Factory() { FUNKNOWN_CTOR }
  ~Factory() { FUNKNOWN_DTOR }

  Steinberg::tresult PLUGIN_API getFactoryInfo(Steinberg::PFactoryInfo* info) override {
    strncpy(info->vendor, "Fairy Tapes", Steinberg::PFactoryInfo::kNameSize - 1);
    info->vendor[Steinberg::PFactoryInfo::kNameSize - 1] = '\0';
    strncpy(info->url, "https://github.com/fairy-tapes", Steinberg::PFactoryInfo::kURLSize - 1);
    info->url[Steinberg::PFactoryInfo::kURLSize - 1] = '\0';
    info->email[0] = '\0';
    info->flags = Steinberg::PFactoryInfo::kUnicode;
    return Steinberg::kResultOk;
  }

  Steinberg::int32 PLUGIN_API countClasses() override { return 2; }

  Steinberg::tresult PLUGIN_API getClassInfo(Steinberg::int32 index,
                                            Steinberg::PClassInfo* info) override {
    if (index < 0 || index > 1)
      return Steinberg::kResultFalse;
    info->cardinality = Steinberg::PClassInfo::kManyInstances;
    strncpy(info->name, stringPluginName, Steinberg::PClassInfo::kNameSize - 1);
    info->name[Steinberg::PClassInfo::kNameSize - 1] = '\0';
    if (index == 0) {
      memcpy(info->cid, kProcessorUID.toTUID(), sizeof(Steinberg::TUID));
      strncpy(info->category, "Audio Module Class", Steinberg::PClassInfo::kCategorySize - 1);
    } else {
      memcpy(info->cid, kControllerUID.toTUID(), sizeof(Steinberg::TUID));
      strncpy(info->category, stringControllerCategory, Steinberg::PClassInfo::kCategorySize - 1);
    }
    info->category[Steinberg::PClassInfo::kCategorySize - 1] = '\0';
    return Steinberg::kResultOk;
  }

  Steinberg::tresult PLUGIN_API createInstance(Steinberg::FIDString cid,
                                               Steinberg::FIDString iid,
                                               void** obj) override {
    if (memcmp(cid, kProcessorUID.toTUID(), sizeof(Steinberg::TUID)) == 0) {
      auto* processor = new Processor;
      if (!processor)
        return Steinberg::kOutOfMemory;
      Steinberg::tresult result = processor->queryInterface(iid, obj);
      processor->release();
      return result;
    }
    if (memcmp(cid, kControllerUID.toTUID(), sizeof(Steinberg::TUID)) == 0) {
      auto* controller = new Controller;
      if (!controller)
        return Steinberg::kOutOfMemory;
      Steinberg::tresult result = controller->queryInterface(iid, obj);
      controller->release();
      return result;
    }
    return Steinberg::kNoInterface;
  }

  DECLARE_FUNKNOWN_METHODS
};

IMPLEMENT_REFCOUNT(Factory)

Steinberg::tresult PLUGIN_API Factory::queryInterface(const Steinberg::TUID _iid, void** obj) {
  QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid, Steinberg::IPluginFactory)
  QUERY_INTERFACE(_iid, obj, Steinberg::IPluginFactory::iid, Steinberg::IPluginFactory)
  *obj = nullptr;
  return Steinberg::kNoInterface;
}

} // namespace FairyTapes::TheStripper

// Module entry (required by VST3 host)
// Called by the SDK platform entry point (ModuleEntry / bundleEntry / InitDll).
bool InitModule() { return true; }
bool DeinitModule() { return true; }

extern "C" Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() {
  static FairyTapes::TheStripper::Factory factory;
  return &factory;
}
