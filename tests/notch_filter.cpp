// Notch filter: accurate cut, narrow, exact identity at 0 dB, and click-free while moving.

#include "dsp/notch_filter.h"
#include "test_signals.h"

#include <cmath>
#include <cstdio>

using namespace fwyc::test;
using fwyc::dsp::NotchFilter;

namespace {

constexpr double kFs = 48000.0;
constexpr float kQ = 20.0f;

// Steady-state gain of a static notch at one frequency, in dB.
float gainAt (double freq, double notchFreq, float depthDb)
{
    NotchFilter f;
    f.setParameters (kFs, static_cast<float> (notchFreq), depthDb, kQ);

    const int n = static_cast<int> (kFs);
    const Signal in = growingTone (kFs, n, freq, -6.0f, 0.0f);
    Signal out (in.size ());
    for (size_t i = 0; i < in.size (); ++i)
        out[i] = f.process (in[i]);

    const size_t settle = in.size () / 2;
    return rmsDb (out, settle, out.size ()) - rmsDb (in, settle, in.size ());
}

void response (Checker& check)
{
    std::printf ("Response (Q %.0f)\n", kQ);
    for (double fc : {300.0, 1000.0, 5000.0})
    {
        for (float depth : {3.0f, 6.0f, 12.0f, 18.0f})
        {
            const float centre = gainAt (fc, fc, depth);
            const float octaveBelow = gainAt (fc / 2.0, fc, depth);
            const float octaveAbove = gainAt (fc * 2.0, fc, depth);
            std::printf ("  %5.0f Hz, -%4.1f dB: centre %+6.2f dB, octave below %+5.2f dB, above %+5.2f dB\n", fc,
                         depth, centre, octaveBelow, octaveAbove);
            check.expect (std::abs (centre + depth) < 0.3f, "centre cut within 0.3 dB of target");
            check.expect (std::abs (octaveBelow) < 0.3f && std::abs (octaveAbove) < 0.3f,
                          "an octave away is untouched (within 0.3 dB)");
        }
    }
}

void identityAtZeroDepth (Checker& check)
{
    NotchFilter f;
    f.setParameters (kFs, 1000.0f, 0.0f, kQ);
    const Signal in = whiteNoise (static_cast<int> (kFs), -12.0f);
    bool exact = true;
    for (float x : in)
        exact = exact && f.process (x) == x;
    std::printf ("Identity at 0 dB: %s\n", exact ? "exact" : "differs");
    check.expect (exact, "0 dB depth is an exact identity");
}

// Engage, deepen and sweep a notch while a tone an octave and a half away plays through it,
// updating parameters every 32 samples as the bank does. The tone must come through cleanly.
// The tone runs for a while first: its own abrupt start is broadband and would rightly be
// filtered, which is not what this measures.
void modulationIsClickFree (Checker& check)
{
    const int n = static_cast<int> (kFs * 0.6);
    const int engageAt = static_cast<int> (kFs * 0.1);
    const Signal in = growingTone (kFs, n, 1000.0, -6.0f, 0.0f);
    Signal out (in.size ());

    NotchFilter f;
    for (int start = 0; start < n; start += 32)
    {
        const double t = std::max (0, start - engageAt) / kFs;
        const float depth = static_cast<float> (std::min (18.0, t / 0.02 * 18.0));   // 0 -> 18 dB in 20 ms
        const float freq = static_cast<float> (3000.0 - std::min (1.0, t / 0.05) * 500.0); // 3 -> 2.5 kHz in 50 ms
        f.setParameters (kFs, freq, depth, kQ);
        for (int i = start; i < std::min (n, start + 32); ++i)
            out[static_cast<size_t> (i)] = start < engageAt ? in[static_cast<size_t> (i)]
                                                            : f.process (in[static_cast<size_t> (i)]);
    }

    // A sample-by-sample comparison would mostly measure the notch's small, inaudible phase shift
    // at 1 kHz. A click is a discontinuity: it shows up as a spike in the second difference.
    auto maxCurvature = [&] (const Signal& s) {
        float m = 0.0f;
        for (size_t i = static_cast<size_t> (engageAt) + 2; i < s.size (); ++i)
            m = std::max (m, std::abs (s[i] - 2.0f * s[i - 1] + s[i - 2]));
        return m;
    };
    const float curvatureRatio = maxCurvature (out) / maxCurvature (in);
    const size_t settled = static_cast<size_t> (engageAt) + static_cast<size_t> (kFs * 0.1);
    const float levelChange =
        toneLevelDb (out, settled, out.size (), kFs, 1000.0) - toneLevelDb (in, settled, in.size (), kFs, 1000.0);
    std::printf ("Moving notch near a 1 kHz tone: curvature %.3fx the input's, tone level %+.3f dB\n", curvatureRatio,
                 levelChange);
    check.expect (curvatureRatio < 1.05f, "no clicks while the notch engages and moves");
    check.expect (std::abs (levelChange) < 0.1f, "distant tone level unchanged (within 0.1 dB)");
}

} // namespace

int main ()
{
    Checker check;
    response (check);
    identityAtZeroDepth (check);
    modulationIsClickFree (check);
    return check.finish ("notch_filter");
}
