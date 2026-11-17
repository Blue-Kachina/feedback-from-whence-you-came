// Phase 1 acceptance: the engine passes audio through bit-exact with zero added latency,
// both out-of-place and in-place, across the supported sample rates.

#include "dsp/feedback_suppressor.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

int failures = 0;

void check (bool ok, const char* what, double sampleRate)
{
    if (!ok)
    {
        std::printf ("FAIL: %s @ %.0f Hz\n", what, sampleRate);
        ++failures;
    }
}

std::vector<float> makeSine (double sampleRate, int numSamples)
{
    std::vector<float> s (static_cast<size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        s[static_cast<size_t> (i)] =
            static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * 1000.0 * i / sampleRate));
    return s;
}

} // namespace

int main ()
{
    constexpr int blockSize = 64;

    for (double sampleRate : {44100.0, 48000.0, 96000.0})
    {
        const auto input = makeSine (sampleRate, blockSize * 50);

        fwyc::dsp::FeedbackSuppressor engine;
        engine.prepare (sampleRate, blockSize);

        std::vector<float> output (input.size ());
        for (size_t pos = 0; pos < input.size (); pos += blockSize)
            engine.process (input.data () + pos, output.data () + pos, blockSize);
        check (output == input, "out-of-place output differs from input", sampleRate);

        auto inPlace = input;
        engine.reset ();
        for (size_t pos = 0; pos < inPlace.size (); pos += blockSize)
            engine.process (inPlace.data () + pos, inPlace.data () + pos, blockSize);
        check (inPlace == input, "in-place output differs from input", sampleRate);
    }

    if (failures == 0)
        std::printf ("passthrough: OK\n");
    return failures == 0 ? 0 : 1;
}
