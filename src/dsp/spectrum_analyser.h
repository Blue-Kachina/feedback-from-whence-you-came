#pragma once

#include "dsp/fft.h"

#include <complex>
#include <vector>

// Spectrum for display: a longer window than the detector's, so the low end is legible, read
// out on demand onto log-spaced bands.
//
// Real-time contract: prepare() allocates; reset(), push() and analyse() do not.
// analyse() costs one FFT, so call it at display rate, not per block.

namespace fwyc::dsp {

class SpectrumAnalyser
{
public:
    static constexpr int kFftSize = 4096;

    void prepare (double sampleRate);
    void reset () noexcept;

    void push (const float* in, int numSamples) noexcept;

    // numBands log-spaced bands from minHz to maxHz, in dBFS (full-scale sine = 0 dB). A band
    // wider than an FFT bin reads its loudest bin; a narrower one interpolates between bins.
    void analyse (float minHz, float maxHz, float* outDb, int numBands) noexcept;

private:
    double sampleRate_ {48000.0};
    float binHz_ {0.0f};
    float normDb_ {0.0f};

    Fft fft_;
    std::vector<float> window_;
    std::vector<float> ring_;
    int ringPos_ {0};
    std::vector<std::complex<float>> spectrum_;
    std::vector<float> binDb_;
};

} // namespace fwyc::dsp
