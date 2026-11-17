#pragma once

#include "dsp/feedback_suppressor.h"
#include "dsp/spectrum_analyser.h"
#include "params.h"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <atomic>

namespace fwyc {

struct MeterFrame;

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor ();

    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*> (new Processor ());
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API connect (Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API disconnect (Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;
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
    static constexpr double kMeterRateHz = 30.0;

    void applyParameterChanges (Steinberg::Vst::IParameterChanges* changes);
    void applyControls () noexcept;
    void updateMeters (const float* out, int numSamples) noexcept;
    void fillMeterFrame (MeterFrame& frame) noexcept;

    dsp::FeedbackSuppressor engine_;
    bool bypass_ {false};
    double sensitivity_ {params::kSensitivityDefault};
    double depth_ {params::kDepthDefault};
    double width_ {params::kWidthDefault};
    double release_ {params::kReleaseDefault};
    double mode_ {0.0};

    // Set from the UI thread, handled on the audio thread.
    std::atomic<bool> clearRequested_ {false};

    // Metering for the editor.
    Steinberg::Vst::DataExchangeHandler dataExchange_;
    dsp::SpectrumAnalyser spectrum_;
    int meterInterval_ {1600};
    int samplesUntilMeter_ {1600};
    float inPeak_ {0.0f};
    float outPeak_ {0.0f};
};

} // namespace fwyc
