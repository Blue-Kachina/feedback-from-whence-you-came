#pragma once

#include "dsp/feedback_detector.h"

// Top-level DSP engine. Standard C++ only: no Steinberg or JUCE dependency.
//
// Real-time contract: prepare() may allocate; reset() and process() must not
// allocate, lock, or block, and process() adds zero samples of latency.

namespace fwyc::dsp {

class FeedbackSuppressor
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset () noexcept;

    // Mono, in-place safe (in may equal out).
    void process (const float* in, float* out, int numSamples) noexcept;

    double sampleRate () const noexcept { return sampleRate_; }
    const FeedbackDetector& detector () const noexcept { return detector_; }

private:
    double sampleRate_ {48000.0};
    int maxBlockSize_ {0};
    FeedbackDetector detector_;
};

} // namespace fwyc::dsp
