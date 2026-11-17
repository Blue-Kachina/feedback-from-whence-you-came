// Transparency and bypass: without feedback the engine output is bit-exact with the input,
// and bypass changes crossfade instead of jumping.

#include "dsp/feedback_suppressor.h"
#include "test_signals.h"

#include <cmath>
#include <cstdio>

using namespace fwyc::test;
using fwyc::dsp::FeedbackSuppressor;

namespace {

void transparentWithoutFeedback (Checker& check)
{
    constexpr int blockSize = 64;
    for (double fs : {44100.0, 48000.0, 96000.0})
    {
        const int n = static_cast<int> (fs * 2.0);
        Signal input = harmonicNote (fs, n, {220.0, 12, -12.0f, 1.0f});
        input += whiteNoise (n, -60.0f);

        FeedbackSuppressor engine;
        engine.prepare (fs, blockSize);
        Signal output (input.size ());
        for (int pos = 0; pos < n; pos += blockSize)
            engine.process (input.data () + pos, output.data () + pos, std::min (blockSize, n - pos));

        auto inPlace = input;
        engine.reset ();
        for (int pos = 0; pos < n; pos += blockSize)
            engine.process (inPlace.data () + pos, inPlace.data () + pos, std::min (blockSize, n - pos));

        std::printf ("Sung note at %.0f Hz: out-of-place %s, in-place %s\n", fs,
                     output == input ? "bit-exact" : "DIFFERS", inPlace == input ? "bit-exact" : "DIFFERS");
        check.expect (output == input, "out-of-place output is bit-exact");
        check.expect (inPlace == input, "in-place output is bit-exact");
    }
}

// Engage notches with a howl, then bypass and un-bypass mid-stream.
void bypassCrossfades (Checker& check)
{
    constexpr double fs = 48000.0;
    constexpr int blockSize = 64;
    const int n = static_cast<int> (fs * 2.0);
    Signal input = growingTone (fs, n, 2500.0, -60.0f, 80.0f, -12.0f);
    input += whiteNoise (n, -60.0f);

    FeedbackSuppressor engine;
    engine.prepare (fs, blockSize);
    Signal output (input.size ());

    const int bypassAt = static_cast<int> (fs * 1.0);
    const int resumeAt = static_cast<int> (fs * 1.5);
    for (int pos = 0; pos < n; pos += blockSize)
    {
        engine.setBypassed (pos >= bypassAt && pos < resumeAt);
        engine.process (input.data () + pos, output.data () + pos, std::min (blockSize, n - pos));
    }

    // Fully bypassed after the 10 ms fade: exact input.
    bool exact = true;
    for (int i = bypassAt + static_cast<int> (fs * 0.011); i < resumeAt; ++i)
        exact = exact && output[static_cast<size_t> (i)] == input[static_cast<size_t> (i)];

    // No jumps: the output never moves between samples much faster than the input does.
    float maxInputStep = 0.0f, maxOutputStep = 0.0f;
    for (size_t i = 1; i < input.size (); ++i)
    {
        maxInputStep = std::max (maxInputStep, std::abs (input[i] - input[i - 1]));
        maxOutputStep = std::max (maxOutputStep, std::abs (output[i] - output[i - 1]));
    }

    const float notchedDb = rmsDb (output, static_cast<size_t> (bypassAt) - 4800, static_cast<size_t> (bypassAt));
    const float bypassedDb = rmsDb (output, static_cast<size_t> (resumeAt) - 4800, static_cast<size_t> (resumeAt));
    std::printf ("Bypass: notched %.1f dBFS -> bypassed %.1f dBFS, bypassed audio %s, largest step %.4f (input %.4f)\n",
                 notchedDb, bypassedDb, exact ? "bit-exact" : "DIFFERS", maxOutputStep, maxInputStep);
    check.expect (bypassedDb > notchedDb + 10.0f, "notches were active before bypass (sanity)");
    check.expect (exact, "bypassed output is bit-exact with input");
    check.expect (maxOutputStep <= maxInputStep * 1.1f, "no step discontinuities around bypass changes");
}

} // namespace

int main ()
{
    Checker check;
    transparentWithoutFeedback (check);
    bypassCrossfades (check);
    return check.finish ("passthrough");
}
