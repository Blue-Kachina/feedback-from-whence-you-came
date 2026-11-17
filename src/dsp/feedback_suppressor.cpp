#include "dsp/feedback_suppressor.h"

#include <algorithm>

namespace fwyc::dsp {

void FeedbackSuppressor::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    reset ();
}

void FeedbackSuppressor::reset () noexcept
{
}

void FeedbackSuppressor::process (const float* in, float* out, int numSamples) noexcept
{
    // Phase 1: bit-exact pass-through. Detection and the notch bank arrive in phases 2-3.
    if (in != out)
        std::copy_n (in, numSamples, out);
}

} // namespace fwyc::dsp
