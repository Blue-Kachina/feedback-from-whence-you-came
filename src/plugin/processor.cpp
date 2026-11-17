#include "processor.h"

#include "meter_frame.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace fwyc {

namespace {

float gainToDb (float gain)
{
    return 20.0f * std::log10 (gain + 1e-10f);
}

} // namespace

Processor::Processor ()
: dataExchange_ (this, [] (DataExchangeHandler::Config& config, const ProcessSetup&) {
    config.blockSize = sizeof (MeterFrame);
    config.numBlocks = 8;
    return true;
})
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

tresult PLUGIN_API Processor::connect (IConnectionPoint* other)
{
    tresult result = AudioEffect::connect (other);
    if (result == kResultTrue)
        dataExchange_.onConnect (other, getHostContext ());
    return result;
}

tresult PLUGIN_API Processor::disconnect (IConnectionPoint* other)
{
    dataExchange_.onDisconnect (other);
    return AudioEffect::disconnect (other);
}

tresult PLUGIN_API Processor::notify (IMessage* message)
{
    if (message && FIDStringsEqual (message->getMessageID (), kMsgClearNotches))
    {
        clearRequested_.store (true, std::memory_order_release);
        return kResultOk;
    }
    return AudioEffect::notify (message);
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
    spectrum_.prepare (setup.sampleRate);
    meterInterval_ = std::max (1, static_cast<int> (setup.sampleRate / kMeterRateHz));
    return AudioEffect::setupProcessing (setup);
}

tresult PLUGIN_API Processor::setActive (TBool state)
{
    if (state)
    {
        applyControls ();
        engine_.reset ();
        spectrum_.reset ();
        samplesUntilMeter_ = meterInterval_;
        inPeak_ = outPeak_ = 0.0f;
        dataExchange_.onActivate (processSetup);
    }
    else
    {
        dataExchange_.onDeactivate ();
    }
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
            case kSensitivityId: sensitivity_ = value; break;
            case kDepthId: depth_ = value; break;
            case kWidthId: width_ = value; break;
            case kReleaseId: release_ = value; break;
            case kModeId: mode_ = value; break;
        }
    }
}

void Processor::applyControls () noexcept
{
    engine_.setBypassed (bypass_);
    engine_.setDetectOnly (params::detectOnly (mode_));
    engine_.setDetectionThresholds (params::detectThreshold (sensitivity_), params::releaseThreshold (sensitivity_));
    engine_.setNotchMaxDepthDb (params::depthDb (depth_));
    engine_.setNotchQ (params::notchQ (width_));
    engine_.setNotchHoldSeconds (params::holdSeconds (release_));
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

    if (clearRequested_.exchange (false, std::memory_order_acquire))
        engine_.clearNotches ();

    // The engine crossfades bypass and mode changes and keeps analysing throughout.
    applyControls ();

    // Input metering before the engine runs: processing may be in place.
    spectrum_.push (src, data.numSamples);
    for (int32 i = 0; i < data.numSamples; ++i)
        inPeak_ = std::max (inPeak_, std::abs (src[i]));

    engine_.process (src, dst, data.numSamples);
    updateMeters (dst, data.numSamples);

    // Notch filters can ring briefly after the input falls silent, so never claim silence.
    out.silenceFlags = 0;
    return kResultOk;
}

void Processor::updateMeters (const float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        outPeak_ = std::max (outPeak_, std::abs (out[i]));

    samplesUntilMeter_ -= numSamples;
    if (samplesUntilMeter_ > 0)
        return;
    samplesUntilMeter_ += meterInterval_;
    samplesUntilMeter_ = std::max (samplesUntilMeter_, 1);

    auto block = dataExchange_.getCurrentOrNewBlock ();
    if (block.blockID == InvalidDataExchangeBlockID || block.size < sizeof (MeterFrame))
        return; // the editor is behind (or closed): skip this frame, keep the peaks for the next

    fillMeterFrame (*static_cast<MeterFrame*> (block.data));
    dataExchange_.sendCurrentBlock ();
    inPeak_ = outPeak_ = 0.0f;
}

void Processor::fillMeterFrame (MeterFrame& frame) noexcept
{
    const auto& detector = engine_.detector ();
    const auto& bank = engine_.filterBank ();
    const auto& slots = bank.slots ();

    frame.inPeakDb = gainToDb (inPeak_);
    frame.outPeakDb = gainToDb (outPeak_);
    frame.emergencyReductionDb = engine_.emergency ().reductionDb ();
    frame.analysedMinHz = detector.config ().minFrequencyHz;
    frame.analysedMaxHz = std::min (detector.config ().maxFrequencyHz, static_cast<float> (0.45 * engine_.sampleRate ()));
    frame.notchCapacity = dsp::FilterBank::kMaxFilters;
    frame.notchesEngaged = 0;
    frame.numNotches = 0;

    auto add = [&frame] (const MeterNotch& notch) {
        if (frame.numNotches < MeterFrame::kMaxNotches)
            frame.notches[frame.numNotches++] = notch;
    };

    // Active notches first, loudest first; then releasing; then candidates still building.
    std::array<int, dsp::FilterBank::kMaxFilters> order {};
    int count = 0;
    for (int i = 0; i < dsp::FilterBank::kMaxFilters; ++i)
        if (slots[static_cast<size_t> (i)].inUse)
            order[static_cast<size_t> (count++)] = i;
    std::sort (order.begin (), order.begin () + count, [&slots] (int a, int b) {
        const auto& sa = slots[static_cast<size_t> (a)];
        const auto& sb = slots[static_cast<size_t> (b)];
        if (sa.feedbackPresent != sb.feedbackPresent)
            return sa.feedbackPresent;
        return sa.levelDb > sb.levelDb;
    });
    for (int i = 0; i < count; ++i)
    {
        const auto& s = slots[static_cast<size_t> (order[static_cast<size_t> (i)])];
        if (s.depthDb > 0.0f)
            ++frame.notchesEngaged;
        add ({s.frequencyHz, s.levelDb, s.depthDb, s.secondsEngaged,
              s.feedbackPresent ? MeterNotch::kActive : MeterNotch::kReleasing});
    }

    constexpr float kBuildingConfidence = 0.25f;
    constexpr float kSameNotchPct = 3.0f;
    for (const auto& c : detector.candidates ())
    {
        if (!c.active || c.detected || c.confidence < kBuildingConfidence)
            continue;
        const bool notched = std::any_of (slots.begin (), slots.end (), [&c] (const dsp::NotchSlot& s) {
            return s.inUse && std::abs (s.frequencyHz - c.frequencyHz) <= s.frequencyHz * kSameNotchPct / 100.0f;
        });
        if (!notched)
            add ({c.frequencyHz, c.levelDb, 0.0f, 0.0f, MeterNotch::kBuilding});
    }

    spectrum_.analyse (MeterFrame::kSpectrumMinHz, MeterFrame::kSpectrumMaxHz, frame.spectrumDb,
                       MeterFrame::kSpectrumBands);
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

    // Version 1 states predate the controls: they keep their defaults.
    if (version >= 2)
    {
        double sensitivity, depth, width, release, mode;
        if (!streamer.readDouble (sensitivity) || !streamer.readDouble (depth) || !streamer.readDouble (width) ||
            !streamer.readDouble (release) || !streamer.readDouble (mode))
            return kResultFalse;
        sensitivity_ = sensitivity;
        depth_ = depth;
        width_ = width;
        release_ = release;
        mode_ = mode;
    }
    return kResultOk;
}

tresult PLUGIN_API Processor::getState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer (state, kLittleEndian);
    streamer.writeInt32 (kStateVersion);
    streamer.writeInt32 (bypass_ ? 1 : 0);
    streamer.writeDouble (sensitivity_);
    streamer.writeDouble (depth_);
    streamer.writeDouble (width_);
    streamer.writeDouble (release_);
    streamer.writeDouble (mode_);
    return kResultOk;
}

} // namespace fwyc
