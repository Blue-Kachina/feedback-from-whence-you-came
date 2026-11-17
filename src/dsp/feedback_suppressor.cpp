#include "dsp/feedback_suppressor.h"

#include <algorithm>

namespace fwyc::dsp {

void FeedbackSuppressor::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    detector_.prepare (sampleRate);
    filterBank_.prepare (sampleRate);
    wetStep_ = static_cast<float> (1.0 / (kBypassFadeSeconds * sampleRate));
    reset ();
}

void FeedbackSuppressor::reset () noexcept
{
    detector_.reset ();
    filterBank_.reset ();
    wet_ = bypassed_ ? 0.0f : 1.0f;
}

void FeedbackSuppressor::process (const float* in, float* out, int numSamples) noexcept
{
    const float wetTarget = bypassed_ ? 0.0f : 1.0f;

    for (int pos = 0; pos < numSamples; pos += kChunkSize)
    {
        const int n = std::min (kChunkSize, numSamples - pos);

        // Analyse the chunk before it is (possibly in-place) overwritten.
        detector_.process (in + pos, n);
        filterBank_.update (detector_.candidates (), n);

        for (int i = pos; i < pos + n; ++i)
        {
            const float x = in[i];
            const float y = filterBank_.processSample (x);

            if (wet_ != wetTarget)
                wet_ = wet_ < wetTarget ? std::min (wetTarget, wet_ + wetStep_) : std::max (wetTarget, wet_ - wetStep_);

            // wet_ exactly 1 or 0 gives exactly y or x.
            out[i] = wet_ == 1.0f ? y : (wet_ == 0.0f ? x : x + wet_ * (y - x));
        }
    }
}

} // namespace fwyc::dsp
