#pragma once

#include "dsp/feedback_suppressor.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

namespace fwyc {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor ();

    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*> (new Processor ());
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
                                                      Steinberg::int32 numIns,
                                                      Steinberg::Vst::SpeakerArrangement* outputs,
                                                      Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
    void applyParameterChanges (Steinberg::Vst::IParameterChanges* changes);

    dsp::FeedbackSuppressor engine_;
    bool bypass_ {false};
};

} // namespace fwyc
