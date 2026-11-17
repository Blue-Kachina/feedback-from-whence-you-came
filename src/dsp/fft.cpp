#include "dsp/fft.h"

#include <cassert>
#include <cmath>
#include <numbers>
#include <utility>

namespace fwyc::dsp {

void Fft::prepare (int size)
{
    assert (size >= 2 && (size & (size - 1)) == 0);
    size_ = size;

    int bits = 0;
    while ((1 << bits) < size)
        ++bits;

    bitReverse_.resize (static_cast<size_t> (size));
    for (int i = 0; i < size; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            r |= ((i >> b) & 1) << (bits - 1 - b);
        bitReverse_[static_cast<size_t> (i)] = r;
    }

    twiddles_.resize (static_cast<size_t> (size / 2));
    for (int k = 0; k < size / 2; ++k)
    {
        const double phase = -2.0 * std::numbers::pi * k / size;
        twiddles_[static_cast<size_t> (k)] = {static_cast<float> (std::cos (phase)),
                                              static_cast<float> (std::sin (phase))};
    }
}

void Fft::forward (std::complex<float>* data) const noexcept
{
    const int n = size_;

    for (int i = 0; i < n; ++i)
    {
        const int j = bitReverse_[static_cast<size_t> (i)];
        if (i < j)
            std::swap (data[i], data[j]);
    }

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2;
        const int step = n / len;
        for (int start = 0; start < n; start += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const std::complex<float> w = twiddles_[static_cast<size_t> (k * step)];
                const std::complex<float> u = data[start + k];
                const std::complex<float> x = data[start + k + half];
                // Explicit multiply: avoids the NaN/Inf recovery path of std::complex operator*.
                const std::complex<float> v {x.real () * w.real () - x.imag () * w.imag (),
                                             x.real () * w.imag () + x.imag () * w.real ()};
                data[start + k] = u + v;
                data[start + k + half] = u - v;
            }
        }
    }
}

} // namespace fwyc::dsp
