#include "dsp/feedback_suppressor.h"

#include <algorithm>

namespace fwyc::dsp {

void FeedbackSuppressor::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    detector_.prepare (sampleRate);
    reset ();
}

void FeedbackSuppressor::reset () noexcept
{
    detector_.reset ();
}

void FeedbackSuppressor::process (const float* in, float* out, int numSamples) noexcept
{
    // Analysis observes the input before it is (possibly in-place) written.
    detector_.process (in, numSamples);

    // Phase 2: audio still passes through bit-exact. The notch bank arrives in phase 3.
    if (in != out)
        std::copy_n (in, numSamples, out);
}

} // namespace fwyc::dsp
