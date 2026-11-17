#pragma once

#include "dsp/feedback_detector.h"
#include "dsp/filter_bank.h"

// Top-level DSP engine. Standard C++ only: no Steinberg or JUCE dependency.
//
// Signal flow (mono, zero latency):  in -> detector (observes) ; in -> notch bank -> bypass mix -> out
//
// Real-time contract: prepare() may allocate; reset() and process() must not
// allocate, lock, or block, and process() adds zero samples of latency.

namespace fwyc::dsp {

class FeedbackSuppressor
{
public:
    // Detection and filter updates run in chunks of at most this many samples, so behaviour
    // doesn't depend on the host's block size.
    static constexpr int kChunkSize = 32;

    void prepare (double sampleRate, int maxBlockSize);
    void reset () noexcept;

    // Mono, in-place safe (in may equal out).
    void process (const float* in, float* out, int numSamples) noexcept;

    // Bypass crossfades over kBypassFadeSeconds; analysis keeps running while bypassed.
    void setBypassed (bool bypassed) noexcept { bypassed_ = bypassed; }
    bool bypassed () const noexcept { return bypassed_; }

    double sampleRate () const noexcept { return sampleRate_; }
    const FeedbackDetector& detector () const noexcept { return detector_; }
    const FilterBank& filterBank () const noexcept { return filterBank_; }

private:
    static constexpr double kBypassFadeSeconds = 0.01;

    double sampleRate_ {48000.0};
    int maxBlockSize_ {0};
    FeedbackDetector detector_;
    FilterBank filterBank_;

    bool bypassed_ {false};
    float wet_ {1.0f};     // 1 = processed, 0 = bypassed
    float wetStep_ {0.0f}; // per-sample fade increment
};

} // namespace fwyc::dsp
