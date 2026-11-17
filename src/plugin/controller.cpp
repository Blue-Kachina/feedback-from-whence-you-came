#include "controller.h"

#include "plugin_ids.h"

#include "base/source/fstreamer.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace fwyc {

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
    tresult result = EditController::initialize (context);
    if (result != kResultOk)
        return result;

    parameters.addParameter (STR16 ("Bypass"), nullptr, 1, 0,
                             ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypassId);
    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer (state, kLittleEndian);
    int32 version = 0;
    int32 bypass = 0;
    if (!streamer.readInt32 (version) || !streamer.readInt32 (bypass))
        return kResultFalse;

    setParamNormalized (kBypassId, bypass ? 1.0 : 0.0);
    return kResultOk;
}

} // namespace fwyc
