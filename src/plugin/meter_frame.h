#pragma once

#include <cstdint>

// What the processor reports to the editor, about 30 times a second, through the VST3
// data exchange API. Plain data: it is copied byte for byte between the two sides.

namespace fwyc {

struct MeterNotch
{
    enum Kind : int32_t
    {
        kBuilding = 0,  // tracked and gaining confidence, no notch yet
        kActive = 1,    // feedback present, notch engaged
        kReleasing = 2, // feedback gone, notch holding or easing off
    };

    float frequencyHz;
    float levelDb;        // feedback level at the mic (last detected, for releasing notches)
    float depthDb;        // current cut, 0 while building
    float secondsEngaged; // since the notch was engaged
    int32_t kind;
};

struct MeterFrame
{
    static constexpr int kSpectrumBands = 192; // log-spaced
    static constexpr float kSpectrumMinHz = 20.0f;
    static constexpr float kSpectrumMaxHz = 20000.0f;
    static constexpr int kMaxNotches = 12;

    float inPeakDb;
    float outPeakDb;
    float emergencyReductionDb;
    float analysedMinHz; // the detector looks only between these
    float analysedMaxHz;
    int32_t notchesEngaged; // notches cutting right now
    int32_t notchCapacity;
    int32_t numNotches;     // valid entries in notches, active first
    MeterNotch notches[kMaxNotches];
    float spectrumDb[kSpectrumBands];
};

} // namespace fwyc
