#include "dsp/spectrum_analyser.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fwyc::dsp {

void SpectrumAnalyser::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    binHz_ = static_cast<float> (sampleRate / kFftSize);
    fft_.prepare (kFftSize);

    // Hann window; normDb_ makes a full-scale sine read 0 dB.
    window_.resize (kFftSize);
    double windowSum = 0.0;
    for (int n = 0; n < kFftSize; ++n)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * n / kFftSize);
        window_[static_cast<size_t> (n)] = static_cast<float> (w);
        windowSum += w;
    }
    normDb_ = static_cast<float> (20.0 * std::log10 (windowSum / 2.0));

    ring_.assign (kFftSize, 0.0f);
    spectrum_.resize (kFftSize);
    binDb_.resize (kFftSize / 2 + 1);
    reset ();
}

void SpectrumAnalyser::reset () noexcept
{
    std::fill (ring_.begin (), ring_.end (), 0.0f);
    ringPos_ = 0;
}

void SpectrumAnalyser::push (const float* in, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        ring_[static_cast<size_t> (ringPos_)] = in[i];
        if (++ringPos_ == kFftSize)
            ringPos_ = 0;
    }
}

void SpectrumAnalyser::analyse (float minHz, float maxHz, float* outDb, int numBands) noexcept
{
    // ringPos_ points at the oldest sample.
    for (int n = 0; n < kFftSize; ++n)
    {
        int idx = ringPos_ + n;
        if (idx >= kFftSize)
            idx -= kFftSize;
        spectrum_[static_cast<size_t> (n)] = {ring_[static_cast<size_t> (idx)] * window_[static_cast<size_t> (n)], 0.0f};
    }
    fft_.forward (spectrum_.data ());

    for (size_t b = 0; b < binDb_.size (); ++b)
        binDb_[b] = 10.0f * std::log10 (std::norm (spectrum_[b]) + 1e-30f) - normDb_;

    const int lastBin = static_cast<int> (binDb_.size ()) - 1;
    const float ratio = std::pow (maxHz / minHz, 1.0f / static_cast<float> (numBands));
    float lo = minHz;
    for (int i = 0; i < numBands; ++i)
    {
        const float hi = lo * ratio;
        const int first = std::max (1, static_cast<int> (std::ceil (lo / binHz_)));
        const int last = std::min (lastBin, static_cast<int> (std::floor (hi / binHz_)));

        float db;
        if (first <= last)
        {
            db = binDb_[static_cast<size_t> (first)];
            for (int b = first + 1; b <= last; ++b)
                db = std::max (db, binDb_[static_cast<size_t> (b)]);
        }
        else
        {
            const float exact = std::clamp (std::sqrt (lo * hi) / binHz_, 0.0f, static_cast<float> (lastBin));
            const int b0 = std::min (static_cast<int> (exact), lastBin - 1);
            const float t = exact - static_cast<float> (b0);
            db = binDb_[static_cast<size_t> (b0)] + t * (binDb_[static_cast<size_t> (b0 + 1)] - binDb_[static_cast<size_t> (b0)]);
        }
        outDb[i] = db;
        lo = hi;
    }
}

} // namespace fwyc::dsp
