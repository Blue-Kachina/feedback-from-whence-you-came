#include "controller.h"
#include "plugin_ids.h"
#include "processor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (fwyc::kProcessorUID),
            PClassInfo::kManyInstances,
            kVstAudioEffectClass,
            stringPluginName,
            Vst::kDistributable,
            stringSubCategory,
            FWYC_VERSION_STR,
            kVstVersionString,
            fwyc::Processor::createInstance)

DEF_CLASS2 (INLINE_UID_FROM_FUID (fwyc::kControllerUID),
            PClassInfo::kManyInstances,
            kVstComponentControllerClass,
            stringPluginName " Controller",
            0,
            "",
            FWYC_VERSION_STR,
            kVstVersionString,
            fwyc::Controller::createInstance)

END_FACTORY
