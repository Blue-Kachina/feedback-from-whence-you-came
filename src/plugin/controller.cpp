#include "controller.h"

#include "params.h"
#include "plugin_ids.h"
#include "ui/editor.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace fwyc {

namespace {

// Continuous parameter shown in the units the user thinks in (dB, Q, seconds, %).
class DisplayParameter : public Parameter
{
public:
    enum Kind
    {
        kSensitivity,
        kDepth,
        kWidth,
        kRelease,
    };

    DisplayParameter (const TChar* title, ParamID id, const TChar* units, ParamValue defaultNormalized, Kind kind)
    : Parameter (title, id, units, defaultNormalized, 0, ParameterInfo::kCanAutomate), kind_ (kind)
    {
    }

    ParamValue toPlain (ParamValue norm) const SMTG_OVERRIDE
    {
        switch (kind_)
        {
            case kSensitivity: return norm * 100.0;
            case kDepth: return params::depthDb (norm);
            case kWidth: return params::notchQ (norm);
            case kRelease: return params::holdSeconds (norm);
        }
        return norm;
    }

    ParamValue toNormalized (ParamValue plain) const SMTG_OVERRIDE
    {
        double norm = plain;
        switch (kind_)
        {
            case kSensitivity: norm = plain / 100.0; break;
            case kDepth: norm = params::depthNorm (plain); break;
            case kWidth: norm = plain > 0.0 ? params::widthNorm (plain) : 0.0; break;
            case kRelease: norm = plain > 0.0 ? params::releaseNorm (plain) : 0.0; break;
        }
        return std::clamp (norm, 0.0, 1.0);
    }

    void toString (ParamValue norm, String128 string) const SMTG_OVERRIDE
    {
        char text[64];
        const double plain = toPlain (norm);
        switch (kind_)
        {
            case kSensitivity:
                std::snprintf (text, sizeof (text), "%s %.0f%%", plain < 34.0 ? "Low" : plain < 67.0 ? "Normal" : "High",
                               plain);
                break;
            case kDepth: std::snprintf (text, sizeof (text), "-%.1f dB", plain); break;
            case kWidth: std::snprintf (text, sizeof (text), "Q %.1f", plain); break;
            case kRelease: std::snprintf (text, sizeof (text), plain < 10.0 ? "%.1f s" : "%.0f s", plain); break;
        }
        StringConvert::convert (text, string);
    }

    bool fromString (const TChar* string, ParamValue& norm) const SMTG_OVERRIDE
    {
        // Accept a bare number in display units: "62", "-12 dB", "Q 8", "4.5 s".
        const std::string text = StringConvert::convert (string);
        const char* p = text.c_str ();
        while (*p && !(std::isdigit (static_cast<unsigned char> (*p)) || *p == '.'))
            ++p;
        if (!*p)
            return false;
        norm = toNormalized (std::atof (p));
        return true;
    }

private:
    Kind kind_;
};

} // namespace

tresult PLUGIN_API Controller::initialize (FUnknown* context)
{
    tresult result = EditController::initialize (context);
    if (result != kResultOk)
        return result;

    parameters.addParameter (STR16 ("Bypass"), nullptr, 1, 0,
                             ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypassId);
    parameters.addParameter (new DisplayParameter (STR16 ("Sensitivity"), kSensitivityId, STR16 ("%"),
                                                   params::kSensitivityDefault, DisplayParameter::kSensitivity));
    parameters.addParameter (new DisplayParameter (STR16 ("Depth"), kDepthId, STR16 ("dB"), params::kDepthDefault,
                                                   DisplayParameter::kDepth));
    parameters.addParameter (new DisplayParameter (STR16 ("Width"), kWidthId, STR16 ("Q"), params::kWidthDefault,
                                                   DisplayParameter::kWidth));
    parameters.addParameter (new DisplayParameter (STR16 ("Release"), kReleaseId, STR16 ("s"), params::kReleaseDefault,
                                                   DisplayParameter::kRelease));

    auto* mode = new StringListParameter (STR16 ("Mode"), kModeId, nullptr,
                                          ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    mode->appendString (STR16 ("Auto notch"));
    mode->appendString (STR16 ("Detect only"));
    parameters.addParameter (mode);
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

    if (version >= 2)
    {
        for (ParamID id : {kSensitivityId, kDepthId, kWidthId, kReleaseId, kModeId})
        {
            double value = 0.0;
            if (!streamer.readDouble (value))
                return kResultFalse;
            setParamNormalized (id, value);
        }
    }
    return kResultOk;
}

tresult PLUGIN_API Controller::notify (IMessage* message)
{
    if (dataExchange_.onMessage (message))
        return kResultOk;
    return EditController::notify (message);
}

IPlugView* PLUGIN_API Controller::createView (FIDString name)
{
    if (FIDStringsEqual (name, ViewType::kEditor))
        return new ui::Editor (this);
    return nullptr;
}

void PLUGIN_API Controller::queueOpened (DataExchangeUserContextID, uint32, TBool& dispatchOnBackgroundThread)
{
    dispatchOnBackgroundThread = false;
}

void PLUGIN_API Controller::queueClosed (DataExchangeUserContextID)
{
}

void PLUGIN_API Controller::onDataExchangeBlocksReceived (DataExchangeUserContextID, uint32 numBlocks,
                                                          DataExchangeBlock* blocks, TBool)
{
    // Only the newest frame matters.
    for (uint32 i = numBlocks; i-- > 0;)
    {
        if (blocks[i].size >= sizeof (MeterFrame))
        {
            std::memcpy (&meter_, blocks[i].data, sizeof (MeterFrame));
            ++meterCount_;
            return;
        }
    }
}

void Controller::clearNotches ()
{
    if (auto message = owned (allocateMessage ()))
    {
        message->setMessageID (kMsgClearNotches);
        sendMessage (message);
    }
}

void Controller::editParameter (ParamID id, ParamValue normalized)
{
    beginEdit (id);
    setParamNormalized (id, normalized);
    performEdit (id, normalized);
    endEdit (id);
}

} // namespace fwyc
