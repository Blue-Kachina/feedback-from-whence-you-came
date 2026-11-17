#pragma once

#include "dsp/emergency_gain.h"
#include "dsp/feedback_detector.h"
#include "dsp/filter_bank.h"

// Top-level DSP engine. Standard C++ only: no Steinberg or JUCE dependency.
//
// Signal flow (mono, zero latency):
//   in -> detector (observes) ; in -> notch bank -> emergency gain -> bypass mix -> out
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

    // Detect only: analysis and the notch policy keep running, but the output is the dry input
    // (crossfaded like bypass).
    void setDetectOnly (bool detectOnly) noexcept { detectOnly_ = detectOnly; }
    bool detectOnly () const noexcept { return detectOnly_; }

    // Last-resort broadband gain reduction (on by default).
    void setEmergencyEnabled (bool enabled) noexcept { emergency_.setEnabled (enabled); }

    // User controls. See FeedbackDetector::setThresholds and the FilterBank setters.
    void setDetectionThresholds (float detect, float release) noexcept { detector_.setThresholds (detect, release); }
    void setNotchQ (float q) noexcept { filterBank_.setQ (q); }
    void setNotchMaxDepthDb (float depthDb) noexcept { filterBank_.setMaxDepthDb (depthDb); }
    void setNotchHoldSeconds (float seconds) noexcept { filterBank_.setHoldSeconds (seconds); }

    // Drops every notch and the emergency gain at once. Feedback still present is detected
    // and notched again from scratch.
    void clearNotches () noexcept
    {
        filterBank_.reset ();
        emergency_.reset ();
    }

    double sampleRate () const noexcept { return sampleRate_; }
    const FeedbackDetector& detector () const noexcept { return detector_; }
    const FilterBank& filterBank () const noexcept { return filterBank_; }
    const EmergencyGain& emergency () const noexcept { return emergency_; }

private:
    static constexpr double kBypassFadeSeconds = 0.01;

    double sampleRate_ {48000.0};
    int maxBlockSize_ {0};
    FeedbackDetector detector_;
    FilterBank filterBank_;
    EmergencyGain emergency_;

    bool bypassed_ {false};
    bool detectOnly_ {false};
    float wet_ {1.0f};     // 1 = processed, 0 = bypassed
    float wetStep_ {0.0f}; // per-sample fade increment
};

} // namespace fwyc::dsp
