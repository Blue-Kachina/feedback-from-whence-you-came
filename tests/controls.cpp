// Editor support: the user controls change the engine as intended, and the display spectrum
// puts a tone in the right band at the right level.

#include "dsp/feedback_suppressor.h"
#include "dsp/spectrum_analyser.h"
#include "test_signals.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

using namespace fwyc::test;
using fwyc::dsp::FeedbackSuppressor;
using fwyc::dsp::SpectrumAnalyser;

namespace {

constexpr double kFs = 48000.0;

float maxDepthDb (const FeedbackSuppressor& engine)
{
    float depth = 0.0f;
    for (const auto& s : engine.filterBank ().slots ())
        depth = std::max (depth, s.depthDb);
    return depth;
}

// Sustained feedback with no loop: notches deepen to the depth limit.
Signal howl (int n)
{
    return growingTone (kFs, n, 2500.0, -40.0f, 40.0f);
}

void spectrumPlacesTone (Checker& check)
{
    constexpr int kBands = 192;
    SpectrumAnalyser analyser;
    analyser.prepare (kFs);

    const int n = SpectrumAnalyser::kFftSize * 2;
    std::vector<float> tone (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        tone[static_cast<size_t> (i)] = 0.5f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 1000.0 * i / kFs));
    analyser.push (tone.data (), n);

    float bands[kBands];
    analyser.analyse (20.0f, 20000.0f, bands, kBands);
    const int loudest = static_cast<int> (std::max_element (bands, bands + kBands) - bands);
    const float centreHz = 20.0f * std::pow (1000.0f, (loudest + 0.5f) / kBands);
    std::printf ("Display spectrum, 1 kHz at -6 dBFS: loudest band %.0f Hz at %.1f dB\n", centreHz, bands[loudest]);
    check.expect (std::abs (centreHz - 1000.0f) < 40.0f, "tone lands in the 1 kHz band");
    check.expect (std::abs (bands[loudest] + 6.0f) < 1.5f, "tone reads about -6 dBFS");
    check.expect (bands[10] < -80.0f, "bands far from the tone stay quiet");
}

void depthLimitBinds (Checker& check)
{
    const int n = static_cast<int> (kFs * 3.0);
    Signal s = howl (n);
    std::vector<float> out (static_cast<size_t> (n));

    for (float limit : {6.0f, 24.0f})
    {
        FeedbackSuppressor engine;
        engine.prepare (kFs, 64);
        engine.setEmergencyEnabled (false);
        engine.setNotchMaxDepthDb (limit);
        float deepest = 0.0f;
        for (int pos = 0; pos < n; pos += 64)
        {
            engine.process (s.data () + pos, out.data () + pos, std::min (64, n - pos));
            deepest = std::max (deepest, maxDepthDb (engine));
        }
        std::printf ("Depth limit %.0f dB: deepest notch %.1f dB\n", limit, deepest);
        check.expect (deepest <= limit + 0.01f, "no notch deeper than the limit");
        check.expect (deepest >= limit - 0.01f, "sustained feedback reaches the limit");
    }

    // Lowering the limit while a notch is engaged pulls it up at once.
    FeedbackSuppressor engine;
    engine.prepare (kFs, 64);
    engine.setEmergencyEnabled (false);
    for (int pos = 0; pos < n; pos += 64)
        engine.process (s.data () + pos, out.data () + pos, std::min (64, n - pos));
    engine.setNotchMaxDepthDb (9.0f);
    engine.process (s.data (), out.data (), 64);
    check.expect (maxDepthDb (engine) <= 9.01f, "lowered limit applies immediately");
}

void detectOnlyPassesDry (Checker& check)
{
    const int n = static_cast<int> (kFs * 2.0);
    Signal s = howl (n);
    std::vector<float> out (static_cast<size_t> (n));

    FeedbackSuppressor engine;
    engine.prepare (kFs, 64);
    engine.setDetectOnly (true);
    engine.reset ();
    bool identical = true;
    for (int pos = 0; pos < n; pos += 64)
        engine.process (s.data () + pos, out.data () + pos, std::min (64, n - pos));
    for (int i = 0; i < n; ++i)
        identical = identical && out[static_cast<size_t> (i)] == s[static_cast<size_t> (i)];
    std::printf ("Detect only: output %s the input, deepest (unapplied) notch %.1f dB\n",
                 identical ? "equals" : "differs from", maxDepthDb (engine));
    check.expect (identical, "detect only passes audio through bit-exact");
    check.expect (maxDepthDb (engine) > 0.0f, "detect only still runs the notch policy");

    engine.clearNotches ();
    check.expect (maxDepthDb (engine) == 0.0f, "clear drops every notch");
}

void sensitivityShiftsThreshold (Checker& check)
{
    FeedbackSuppressor engine;
    engine.prepare (kFs, 64);
    engine.setDetectionThresholds (0.75f, 0.55f);
    const auto& cfg = engine.detector ().config ();
    check.expect (cfg.detectThreshold == 0.75f && cfg.releaseThreshold == 0.55f, "thresholds applied");
}

} // namespace

int main ()
{
    Checker check;
    spectrumPlacesTone (check);
    depthLimitBinds (check);
    detectOnlyPassesDry (check);
    sensitivityShiftsThreshold (check);
    return check.finish ("controls");
}
