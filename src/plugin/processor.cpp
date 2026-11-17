#include "processor.h"

#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace fwyc {

Processor::Processor ()
{
    setControllerClass (kControllerUID);
}

tresult PLUGIN_API Processor::initialize (FUnknown* context)
{
    tresult result = AudioEffect::initialize (context);
    if (result != kResultOk)
        return result;

    // Sits on a single microphone channel.
    addAudioInput (STR16 ("Mic In"), SpeakerArr::kMono);
    addAudioOutput (STR16 ("Mic Out"), SpeakerArr::kMono);
    return kResultOk;
}

tresult PLUGIN_API Processor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                  SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 && inputs[0] == SpeakerArr::kMono &&
        outputs[0] == SpeakerArr::kMono)
        return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing (ProcessSetup& setup)
{
    engine_.prepare (setup.sampleRate, setup.maxSamplesPerBlock);
    return AudioEffect::setupProcessing (setup);
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
    if (state)
        engine_.reset ();
    return AudioEffect::setActive (state);
}

void Processor::applyParameterChanges (IParameterChanges* changes)
{
    if (!changes)
        return;

    for (int32 i = 0, n = changes->getParameterCount (); i < n; ++i)
    {
        IParamValueQueue* queue = changes->getParameterData (i);
        if (!queue || queue->getPointCount () <= 0)
            continue;

        ParamValue value;
        int32 sampleOffset;
        if (queue->getPoint (queue->getPointCount () - 1, sampleOffset, value) != kResultOk)
            continue;

        switch (queue->getParameterId ())
        {
            case kBypassId: bypass_ = value >= 0.5; break;
        }
    }
}

tresult PLUGIN_API Processor::process (ProcessData& data)
{
    applyParameterChanges (data.inputParameterChanges);

    // Parameter-only flush, or host didn't connect buses.
    if (data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1)
        return kResultOk;

    AudioBusBuffers& in = data.inputs[0];
    AudioBusBuffers& out = data.outputs[0];
    if (in.numChannels < 1 || out.numChannels < 1)
        return kResultOk;

    const float* src = in.channelBuffers32[0];
    float* dst = out.channelBuffers32[0];

    if (bypass_)
    {
        if (src != dst)
            std::copy_n (src, data.numSamples, dst);
    }
    else
    {
        engine_.process (src, dst, data.numSamples);
    }

    out.silenceFlags = in.silenceFlags;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer (state, kLittleEndian);
    int32 version = 0;
    int32 bypass = 0;
    if (!streamer.readInt32 (version) || !streamer.readInt32 (bypass))
        return kResultFalse;

    bypass_ = bypass != 0;
    return kResultOk;
}

tresult PLUGIN_API Processor::getState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer (state, kLittleEndian);
    streamer.writeInt32 (kStateVersion);
    streamer.writeInt32 (bypass_ ? 1 : 0);
    return kResultOk;
}

} // namespace fwyc
