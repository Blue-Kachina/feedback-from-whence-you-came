// Phase 3 acceptance: with the suppressor inside a simulated mic/speaker loop, feedback that
// would otherwise howl is stopped, the loop tolerates more gain, and other content survives.

#include "dsp/feedback_suppressor.h"
#include "test_signals.h"

#include <cmath>
#include <cstdio>

using namespace fwyc::test;
using fwyc::dsp::FeedbackSuppressor;

namespace {

constexpr double kFs = 48000.0;

double resonanceQ (double fc)
{
    return std::max (4.0, fc / 300.0);
}

struct LoopRun
{
    Signal mic;
    double firstNotchSec = -1.0;
};

LoopRun runLoop (const Signal& source, const LoopSpec& loop, bool withSuppressor)
{
    if (!withSuppressor)
        return {simulateFeedbackLoop (kFs, source, loop)};

    FeedbackSuppressor engine;
    engine.prepare (kFs, 64);
    LoopRun run;
    size_t processed = 0;
    run.mic = simulateFeedbackLoop (kFs, source, loop, [&] (const float* in, float* out, int n) {
        engine.process (in, out, n);
        processed += static_cast<size_t> (n);
        if (run.firstNotchSec < 0.0)
            for (const auto& slot : engine.filterBank ().slots ())
                if (slot.inUse)
                    run.firstNotchSec = static_cast<double> (processed) / kFs;
    });
    return run;
}

// Seconds from the first notch until the mic is at least 10 dB below its overall peak, or back
// to the source's own level if it never rose that far above it (20 ms windows); -1 if never.
double recoverySeconds (const LoopRun& run, const Signal& source)
{
    const size_t window = static_cast<size_t> (kFs * 0.02);
    const float target = std::max (peakDb (run.mic, 0, run.mic.size ()) - 10.0f,
                                   peakDb (source, 0, source.size ()) + 3.0f);
    for (auto i = static_cast<size_t> (run.firstNotchSec * kFs); i + window <= run.mic.size (); i += window)
        if (peakDb (run.mic, i, i + window) < target)
            return static_cast<double> (i + window) / kFs - run.firstNotchSec;
    return -1.0;
}

// The loop howls if the mic still hits -20 dBFS in the last second.
bool howls (const Signal& mic)
{
    return peakDb (mic, mic.size () - static_cast<size_t> (kFs), mic.size ()) > -20.0f;
}

void stopsHowling (Checker& check)
{
    std::printf ("Howl prevention (noise source at -50 dBFS, 4 s)\n");
    const int n = static_cast<int> (kFs * 4.0);
    const Signal source = whiteNoise (n, -50.0f);
    const size_t lastSecond = static_cast<size_t> (n) - static_cast<size_t> (kFs);

    for (double fc : {500.0, 1250.0, 2500.0, 5000.0})
    {
        for (double gain : {1.08, 1.3})
        {
            const LoopSpec loop {fc, resonanceQ (fc), gain, 0.02};
            const LoopRun open = runLoop (source, loop, false);
            const LoopRun guarded = runLoop (source, loop, true);

            const float openPeak = peakDb (open.mic, 0, open.mic.size ());
            const float guardedPeak = peakDb (guarded.mic, 0, guarded.mic.size ());
            const float guardedTail = rmsDb (guarded.mic, lastSecond, guarded.mic.size ());
            const double recovery = recoverySeconds (guarded, source);
            std::printf ("  fc %5.0f, gain %.2f: unguarded peak %6.1f dBFS | guarded: first notch %4.0f ms, peak %6.1f "
                         "dBFS, -10 dB %3.0f ms later, last second %6.1f dBFS RMS\n",
                         fc, gain, openPeak, guarded.firstNotchSec * 1000.0, guardedPeak, recovery * 1000.0,
                         guardedTail);

            check.expect (howls (open.mic), "unguarded loop howls (sanity)");
            check.expect (!howls (guarded.mic), "guarded loop has stopped howling by the last second");
            // How loud it got before the first notch is the detector's latency (see synthetic_feedback);
            // this measures the notches: once engaged, they must pull the loop down quickly.
            check.expect (recovery >= 0.0 && recovery < 0.15,
                          "10 dB below peak (or back to source level) within 150 ms of the first notch");
        }
    }
}

// Highest loop gain (in steps) that doesn't howl, with and without the suppressor.
double maxStableGain (const Signal& source, double fc, bool withSuppressor)
{
    double stable = 0.0;
    for (double gain = 0.9; gain <= 3.0; gain += 0.05)
    {
        if (howls (runLoop (source, {fc, resonanceQ (fc), gain, 0.02}, withSuppressor).mic))
            break;
        stable = gain;
    }
    return stable;
}

void stableGainImprovement (Checker& check)
{
    std::printf ("Maximum stable gain\n");
    const Signal source = whiteNoise (static_cast<int> (kFs * 4.0), -50.0f);
    for (double fc : {1250.0, 2500.0, 5000.0})
    {
        const double without = maxStableGain (source, fc, false);
        const double with = maxStableGain (source, fc, true);
        const double improvementDb = 20.0 * std::log10 (with / without);
        std::printf ("  fc %5.0f: %.2f -> %.2f (+%.1f dB)\n", fc, without, with, improvementDb);
        check.expect (improvementDb >= 3.0, "at least 3 dB more gain before feedback");
    }
}

// A singer plus a howl: the howl is notched, the voice comes through.
void voiceSurvives (Checker& check)
{
    std::printf ("Voice through a notched howl (offline, no loop)\n");
    const int n = static_cast<int> (kFs * 3.0);
    Signal s = harmonicNote (kFs, n, {196.0, 12, -18.0f, 1.0f});
    s += growingTone (kFs, n, 2500.0, -60.0f, 60.0f, -12.0f);
    s += whiteNoise (n, -60.0f);

    FeedbackSuppressor engine;
    engine.prepare (kFs, 64);
    Signal out (s.size ());
    for (int pos = 0; pos < n; pos += 64)
        engine.process (s.data () + pos, out.data () + pos, std::min (64, n - pos));

    const size_t from = static_cast<size_t> (kFs * 2.0);
    const float howlCut = toneLevelDb (s, from, s.size (), kFs, 2500.0) - toneLevelDb (out, from, out.size (), kFs, 2500.0);
    std::printf ("  howl at 2500 Hz cut by %.1f dB\n", howlCut);
    check.expect (howlCut >= 15.0f, "howl cut by at least 15 dB once the notch has deepened");

    float worstHarmonic = 0.0f;
    for (int k = 1; k <= 8; ++k)
    {
        const double f = 196.0 * k;
        const float change = toneLevelDb (out, from, out.size (), kFs, f) - toneLevelDb (s, from, s.size (), kFs, f);
        worstHarmonic = std::max (worstHarmonic, std::abs (change));
    }
    std::printf ("  voice harmonics 1-8 changed by at most %.2f dB\n", worstHarmonic);
    check.expect (worstHarmonic < 0.5f, "voice harmonics within 0.5 dB");
}

} // namespace

int main ()
{
    Checker check;
    stopsHowling (check);
    stableGainImprovement (check);
    voiceSurvives (check);
    return check.finish ("suppression");
}
