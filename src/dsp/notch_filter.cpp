#include "dsp/notch_filter.h"

#include <algorithm>
#include <numbers>

namespace fwyc::dsp {

void NotchFilter::setParameters (double sampleRate, float frequencyHz, float depthDb, float q) noexcept
{
    const double f = std::clamp (static_cast<double> (frequencyHz), 10.0, 0.49 * sampleRate);
    const double g = std::tan (std::numbers::pi * f / sampleRate);
    const double a = std::pow (10.0, -std::max (0.0f, depthDb) / 40.0); // gain at centre = a^2
    const double k = 1.0 / (q * a);

    const double a1 = 1.0 / (1.0 + g * (g + k));
    a1_ = static_cast<float> (a1);
    a2_ = static_cast<float> (g * a1);
    a3_ = static_cast<float> (g * g * a1);
    m1_ = static_cast<float> (k * (a * a - 1.0));
}

} // namespace fwyc::dsp
