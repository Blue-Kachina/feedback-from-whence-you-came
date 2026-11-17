#pragma once

#include <complex>
#include <vector>

namespace fwyc::dsp {

// In-place iterative radix-2 complex FFT.
// prepare() allocates; forward() does not and is safe to call on the audio thread.
class Fft
{
public:
    void prepare (int size); // size must be a power of two
    int size () const noexcept { return size_; }

    void forward (std::complex<float>* data) const noexcept;

private:
    int size_ {0};
    std::vector<int> bitReverse_;
    std::vector<std::complex<float>> twiddles_;
};

} // namespace fwyc::dsp
