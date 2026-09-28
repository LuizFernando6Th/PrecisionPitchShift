// PrecisionPitchShift — VST3 module entry / factory.
#include "public.sdk/source/main/pluginfactory.h"

#include "plugin/processor.h"
#include "plugin/controller.h"

#define PPS_VENDOR "PrecisionPitchShift"
#define PPS_URL "https://github.com/LuizFernando6Th/PrecisionPitchShift"
#define PPS_EMAIL ""

BEGIN_FACTORY_DEF(PPS_VENDOR, PPS_URL, PPS_EMAIL)

//--- Audio processor -----------------------------------------------
DEF_CLASS2(INLINE_UID_FROM_FUID(pps::kProcessorUID),
           PClassInfo::kManyInstances, kVstAudioEffectClass,
           "PrecisionPitchShift", Vst::kDistributable,
           "Fx|Pitch Shift", "1.0.0", kVstVersionString,
           pps::Processor::createInstance)

//--- Edit controller ------------------------------------------------
DEF_CLASS2(INLINE_UID_FROM_FUID(pps::kControllerUID),
           PClassInfo::kManyInstances, kVstComponentControllerClass,
           "PrecisionPitchShiftController", 0, "", "1.0.0",
           kVstVersionString, pps::Controller::createInstance)

END_FACTORY
