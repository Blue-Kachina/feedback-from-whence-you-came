// Phase 2 acceptance: sustained singing and ordinary program material are not flagged as feedback.

#include "test_signals.h"

#include <cstdio>
#include <string>

using namespace fwyc::test;

namespace {

constexpr double kFs = 48000.0;
constexpr int kSamples = static_cast<int> (kFs * 3.0);

void expectNoDetections (Checker& check, const std::string& name, Signal signal)
{
    signal += whiteNoise (kSamples, -60.0f, 7);
    const RunResult r = runDetector (signal, kFs);
    const auto& c = r.mostConfident;
    std::printf ("  %-30s max confidence %.2f at %7.1f Hz (pnpr %5.1f, phpr %5.1f, papr %5.1f, spread %.2f%%, "
                 "growth %+.0f dB/s) %s\n",
                 name.c_str (), r.maxConfidence, r.maxConfidenceHz, c.pnprDb, c.phprDb, c.paprDb,
                 c.frequencySpreadPct, c.growthDbPerSec, r.detections.empty () ? "" : "<-- DETECTED");
    check.expect (r.detections.empty (), name.c_str ());
}

} // namespace

int main ()
{
    Checker check;

    std::printf ("Held notes (12 harmonics, -12 dBFS)\n");
    for (double f0 : {110.0, 220.0, 440.0, 880.0, 1046.5})
    {
        const auto label = std::to_string (static_cast<int> (f0)) + " Hz";
        expectNoDetections (check, label + " steady", harmonicNote (kFs, kSamples, {f0, 12, -12.0f}));
        expectNoDetections (check, label + " vibrato 1.5%", harmonicNote (kFs, kSamples, {f0, 12, -12.0f, 1.5f}));
    }

    std::printf ("Crescendo (growth must not be enough on its own)\n");
    for (double f0 : {220.0, 660.0})
    {
        NoteSpec note {f0, 12, -45.0f};
        note.crescendoDbPerSec = 40.0f;
        note.crescendoMaxDb = 35.0f;
        expectNoDetections (check, std::to_string (static_cast<int> (f0)) + " Hz, +40 dB/s",
                            harmonicNote (kFs, kSamples, note));
    }

    std::printf ("Non-tonal input\n");
    expectNoDetections (check, "white noise -20 dBFS", whiteNoise (kSamples, -20.0f, 3));
    expectNoDetections (check, "silence", Signal (kSamples, 0.0f));

    return check.finish ("vocal_false_positives");
}
