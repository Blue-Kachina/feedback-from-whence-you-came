// Phase 2 acceptance: synthetic feedback is detected early, at the right frequency.

#include "test_signals.h"

#include <cmath>
#include <cstdio>

using namespace fwyc::test;

namespace {

constexpr float kNoiseDb = -60.0f;

bool near (float actual, double expected, double tolerancePct, double minToleranceHz = 0.0)
{
    return std::abs (actual - expected) <= std::max (expected * tolerancePct / 100.0, minToleranceHz);
}

// Loop resonance with ~300 Hz bandwidth (Q floor of 4 at low frequencies). Loop gain above 1
// across a much wider band is broadband runaway, which notches can't fix: that is the
// phase 4 emergency gain-reduction case, not something this detector is meant to resolve.
double resonanceQ (double fc)
{
    return std::max (4.0, fc / 300.0);
}

int countDetectionsAwayFrom (const RunResult& r, std::initializer_list<double> freqs, double tolerancePct)
{
    int count = 0;
    for (const auto& d : r.detections)
    {
        bool matched = false;
        for (double f : freqs)
            matched = matched || near (d.frequencyHz, f, tolerancePct);
        count += matched ? 0 : 1;
    }
    return count;
}

// A tone rising out of the noise floor, as feedback does in its linear-growth phase.
void growingTones (Checker& check)
{
    std::printf ("Growing tones (from %.0f dBFS)\n", kNoiseDb);
    for (double fs : {44100.0, 48000.0, 96000.0})
    {
        for (double freq : {300.0, 1000.0, 2400.0, 5000.0, 9000.0})
        {
            for (float rate : {40.0f, 200.0f})
            {
                const int n = static_cast<int> (fs * 1.5);
                Signal s = growingTone (fs, n, freq, kNoiseDb, rate);
                s += whiteNoise (n, kNoiseDb);

                const RunResult r = runDetector (s, fs);
                if (r.detections.empty ())
                {
                    std::printf ("  %5.0f Hz @ %3.0f dB/s, fs %.0f: not detected (max confidence %.2f)\n", freq,
                                 rate, fs, r.maxConfidence);
                    check.expect (false, "growing tone detected");
                    continue;
                }

                const Detection& d = r.detections.front ();
                const float toneDb = kNoiseDb + rate * static_cast<float> (d.timeSec);
                std::printf ("  %5.0f Hz @ %3.0f dB/s, fs %.0f: %6.1f ms, tone at %5.1f dBFS, est %7.1f Hz (%+.2f%%)\n",
                             freq, rate, fs, d.timeSec * 1000.0, toneDb, d.frequencyHz,
                             100.0 * (d.frequencyHz - freq) / freq);

                // 5 Hz floor: at the first detection a low tone is barely above the noise, and
                // a notch there is tens of Hz wide anyway. The estimate keeps refining while tracked.
                check.expect (near (d.frequencyHz, freq, 1.0, 5.0), "frequency within max(1%, 5 Hz)");
                check.expect (toneDb <= -25.0f, "detected before the tone reaches -25 dBFS");
                check.expect (countDetectionsAwayFrom (r, {freq}, 3.0) == 0, "no detections at other frequencies");
            }
        }
    }
}

// Closed mic/speaker loop with a room resonance: the howl builds from background noise.
void closedLoop (Checker& check)
{
    constexpr double fs = 48000.0;
    std::printf ("Closed loop (noise source)\n");
    for (double fc : {500.0, 1250.0, 2500.0, 5000.0})
    {
        for (double gain : {1.08, 1.3})
        {
            const int n = static_cast<int> (fs * 4.0);
            const LoopSpec loop {fc, resonanceQ (fc), gain, 0.02};
            const Signal mic = simulateFeedbackLoop (fs, whiteNoise (n, -50.0f), loop);
            const double loudAt = timeToReach (mic, fs, -6.0f);
            const RunResult r = runDetector (mic, fs);

            check.expect (loudAt > 0.0, "simulation howls (sanity)");
            if (r.detections.empty ())
            {
                std::printf ("  fc %5.0f, gain %.2f: not detected (max confidence %.2f)\n", fc, gain, r.maxConfidence);
                check.expect (false, "howl detected");
                continue;
            }

            const Detection& d = r.detections.front ();
            std::printf ("  fc %5.0f, gain %.2f: detected %6.1f ms at %7.1f Hz, %5.1f dBFS; howl hit -6 dBFS at %6.1f ms\n",
                         fc, gain, d.timeSec * 1000.0, d.frequencyHz, d.levelDb, loudAt * 1000.0);

            check.expect (near (d.frequencyHz, fc, 10.0), "howl frequency within the resonance");
            check.expect (d.timeSec < loudAt, "detected before the howl gets loud");
        }
    }
}

// The same loop with someone singing into the mic.
void closedLoopWithVoice (Checker& check)
{
    constexpr double fs = 48000.0;
    std::printf ("Closed loop (singing source)\n");
    for (double fc : {1250.0, 2500.0})
    {
        const int n = static_cast<int> (fs * 4.0);
        Signal source = harmonicNote (fs, n, {196.0, 12, -18.0f, 1.0f});
        source += whiteNoise (n, -55.0f);

        const Signal mic = simulateFeedbackLoop (fs, source, {fc, resonanceQ (fc), 1.2, 0.02});
        const double loudAt = timeToReach (mic, fs, -3.0f);
        const RunResult r = runDetector (mic, fs);

        if (r.detections.empty ())
        {
            std::printf ("  fc %5.0f: not detected (max confidence %.2f)\n", fc, r.maxConfidence);
            check.expect (false, "howl under voice detected");
            continue;
        }

        const Detection& d = r.detections.front ();
        std::printf ("  fc %5.0f: detected %6.1f ms at %7.1f Hz; mix hit -3 dBFS at %6.1f ms\n", fc,
                     d.timeSec * 1000.0, d.frequencyHz, loudAt * 1000.0);
        check.expect (near (d.frequencyHz, fc, 10.0), "detection is the howl, not a voice harmonic");
        check.expect (countDetectionsAwayFrom (r, {fc}, 10.0) == 0, "no detections on voice harmonics");
        // Known limitation, reported but not enforced: when the loop's gain peak sits on a sung
        // harmonic, the howl inherits the singer's vibrato and looks voice-like until it dominates.
        // Accepting more frequency wobble to catch it earlier costs false-positive protection on
        // sustained singing, and made detection worse on a real recording.
        if (loudAt >= 0.0 && d.timeSec >= loudAt)
            std::printf ("    LIMITATION: detected %.0f ms after the mix reached -3 dBFS\n",
                         (d.timeSec - loudAt) * 1000.0);
    }
}

void simultaneousHowls (Checker& check)
{
    constexpr double fs = 48000.0;
    const int n = static_cast<int> (fs * 1.0);
    Signal s = growingTone (fs, n, 1200.0, kNoiseDb, 60.0f);
    s += growingTone (fs, n, 3300.0, kNoiseDb - 6.0f, 60.0f);
    s += whiteNoise (n, kNoiseDb);

    const RunResult r = runDetector (s, fs);
    bool low = false, high = false;
    for (const auto& d : r.detections)
    {
        low = low || near (d.frequencyHz, 1200.0, 1.0);
        high = high || near (d.frequencyHz, 3300.0, 1.0);
    }
    std::printf ("Simultaneous howls: 1200 Hz %s, 3300 Hz %s\n", low ? "detected" : "missed",
                 high ? "detected" : "missed");
    check.expect (low && high, "both howls detected");
}

// A second howl growing while a first one already dominates the spectrum (as in a real
// recording: 3.3 kHz howling, then 5.6 kHz joining). The loud one mustn't hide the new one.
void howlUnderLoudHowl (Checker& check)
{
    constexpr double fs = 48000.0;
    constexpr float startDb = -70.0f, rate = 40.0f;
    const int n = static_cast<int> (fs * 1.5);
    Signal s = growingTone (fs, n, 3340.0, -12.0f, 0.0f);
    s += growingTone (fs, n, 5600.0, startDb, rate);
    s += whiteNoise (n, startDb);

    const RunResult r = runDetector (s, fs);
    const Detection* second = nullptr;
    for (const auto& d : r.detections)
        if (!second && near (d.frequencyHz, 5600.0, 1.0))
            second = &d;

    if (!second)
    {
        std::printf ("Howl under a -12 dBFS howl: missed\n");
        check.expect (false, "second howl detected");
        return;
    }
    const float levelDb = startDb + rate * static_cast<float> (second->timeSec);
    std::printf ("Howl under a -12 dBFS howl: detected at %.0f ms, %.1f dBFS\n", second->timeSec * 1000.0, levelDb);
    check.expect (levelDb <= -40.0f, "second howl detected before it reaches -40 dBFS");
}

} // namespace

int main ()
{
    Checker check;
    growingTones (check);
    closedLoop (check);
    closedLoopWithVoice (check);
    simultaneousHowls (check);
    howlUnderLoudHowl (check);
    return check.finish ("synthetic_feedback");
}
