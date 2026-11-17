#pragma once

#include <cmath>

namespace fwyc::dsp {

// Narrow peaking (bell) cut, implemented as a trapezoidal-integrated state-variable filter
// (Simper/Cytomic). Unlike a direct-form biquad it stays stable and click-free while frequency
// and depth change every few samples. At 0 dB depth the output equals the input exactly.
class NotchFilter
{
public:
    void reset () noexcept
    {
        ic1_ = 0.0f;
        ic2_ = 0.0f;
    }

    // depthDb >= 0 is the amount of cut at the centre frequency.
    void setParameters (double sampleRate, float frequencyHz, float depthDb, float q) noexcept;

    float process (float x) noexcept
    {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;
        return x + m1_ * v1;
    }

    // Decaying state in silence would otherwise become denormal and slow the CPU down.
    void flushDenormals () noexcept
    {
        if (std::abs (ic1_) < 1e-15f)
            ic1_ = 0.0f;
        if (std::abs (ic2_) < 1e-15f)
            ic2_ = 0.0f;
    }

private:
    float a1_ {1.0f};
    float a2_ {0.0f};
    float a3_ {0.0f};
    float m1_ {0.0f};
    float ic1_ {0.0f};
    float ic2_ {0.0f};
};

} // namespace fwyc::dsp
