// Phase 4 acceptance: karaoke protections. The mic pointed at the speaker and moved away again,
// more howls than notches, the emergency gain stage itself, and limits on notch build-up.

#include "dsp/emergency_gain.h"
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

float totalDepthDb (const FeedbackSuppressor& engine)
{
    float total = 0.0f;
    for (const auto& s : engine.filterBank ().slots ())
        total += s.depthDb;
    return total;
}

float totalFloorDb (const FeedbackSuppressor& engine)
{
    float total = 0.0f;
    for (const auto& s : engine.filterBank ().slots ())
        total += s.floorDb;
    return total;
}

// Someone singing; at 1 s the mic is pointed at the speaker (loop gain well over 1 at two room
// modes), at 4 s it is pointed away again. The howl must be stopped while the mic still faces
// the speaker, and once it's away everything must ease back to (at most) the lasting floors.
void micPointedAtSpeaker (Checker& check)
{
    std::printf ("Mic pointed at the speaker at 1 s, away again at 4 s (singer, room modes 2.5 + 4.1 kHz)\n");
    const int n = static_cast<int> (kFs * 16.0);
    Signal source = harmonicNote (kFs, n, {196.0, 12, -24.0f, 1.0f});
    source += whiteNoise (n, -55.0f);

    for (double gain : {1.6, 2.5})
    {
        LoopSpec loop {2500.0, resonanceQ (2500.0), 0.5, 0.02, 4100.0, {{1.0, gain}, {4.0, 0.5}}};
        const Signal open = simulateFeedbackLoop (kFs, source, loop);

        FeedbackSuppressor engine;
        engine.prepare (kFs, 64);
        size_t processed = 0;
        double emergencyOn = -1.0, emergencyOff = -1.0, settled = -1.0;
        float maxEmergencyDb = 0.0f;
        const Signal guarded = simulateFeedbackLoop (kFs, source, loop, [&] (const float* in, float* out, int len) {
            engine.process (in, out, len);
            processed += static_cast<size_t> (len);
            const double t = static_cast<double> (processed) / kFs;
            const float cut = engine.emergency ().reductionDb ();
            maxEmergencyDb = std::max (maxEmergencyDb, cut);
            if (cut > 0.0f && emergencyOn < 0.0)
                emergencyOn = t;
            if (cut > 0.0f)
                emergencyOff = -1.0;
            else if (emergencyOn >= 0.0 && emergencyOff < 0.0)
                emergencyOff = t;
            // Settled: no emergency cut, and the notches are down to their lasting floors.
            const bool atFloors = totalDepthDb (engine) <= totalFloorDb (engine) + 0.01f;
            if (t > 4.0 && cut == 0.0f && atFloors)
            {
                if (settled < 0.0)
                    settled = t;
            }
            else
            {
                settled = -1.0;
            }
        });

        const auto at = [] (double s) { return static_cast<size_t> (s * kFs); };
        const float voicePeak = peakDb (source, at (3.0), at (4.0));
        const float openPeak = peakDb (open, at (3.0), at (4.0));
        const float guardedPeak = peakDb (guarded, at (1.0), at (4.0));
        const float guardedLate = peakDb (guarded, at (3.0), at (4.0));
        std::printf ("  gain %.1f: voice alone %5.1f dBFS, unguarded %5.1f dBFS in 3-4 s | guarded: peak %5.1f dBFS, "
                     "%5.1f dBFS in 3-4 s; emergency %s (max %.0f dB, %.2f-%.2f s); settled at floors %.1f s after "
                     "the mic moved, floors %.1f dB in total\n",
                     gain, voicePeak, openPeak, guardedPeak, guardedLate, emergencyOn >= 0.0 ? "used" : "unused",
                     maxEmergencyDb, emergencyOn, emergencyOff, settled - 4.0, totalFloorDb (engine));

        check.expect (openPeak > voicePeak + 10.0f, "unguarded loop howls while the mic faces the speaker (sanity)");
        check.expect (guardedLate < voicePeak + 3.0f,
                      "guarded: within 3 dB of the voice alone while the mic still faces the speaker");
        check.expect (settled > 0.0 && settled - 4.0 < 10.0,
                      "within 10 s of the mic moving away: no emergency cut, notches back to their floors");
    }
}

// More howls than notches, each starting 100 ms after the last: the bank overflows and the
// emergency gain takes over.
void moreHowlsThanNotches (Checker& check)
{
    constexpr double stagger = 0.1;
    const int n = static_cast<int> (kFs * 3.0);
    Signal s = whiteNoise (n, -70.0f);
    // Room modes have no reason to be harmonically related: these are at least 9 FFT bins apart
    // and avoid the simple ratios the detector reads as a voice's harmonics.
    const double freqs[] = {1170.0, 2030.0, 2890.0, 3810.0, 4730.0, 5590.0, 6470.0, 7390.0, 8250.0, 9170.0};
    for (size_t k = 0; k < std::size (freqs); ++k)
    {
        const auto start = static_cast<size_t> (static_cast<double> (k) * stagger * kFs);
        const Signal tone = growingTone (kFs, n - static_cast<int> (start), freqs[k], -70.0f, 40.0f, -20.0f);
        for (size_t i = 0; i < tone.size (); ++i)
            s[start + i] += tone[i];
    }

    FeedbackSuppressor engine;
    engine.prepare (kFs, 64);
    Signal out (s.size ());
    bool overflow = false;
    double engagedAt = -1.0;
    for (int pos = 0; pos < n; pos += 64)
    {
        engine.process (s.data () + pos, out.data () + pos, std::min (64, n - pos));
        overflow = overflow || engine.filterBank ().status ().overflow;
        if (engagedAt < 0.0 && engine.emergency ().reductionDb () > 0.0f)
            engagedAt = (pos + 64) / kFs;
    }
    std::printf ("Ten howls, 100 ms apart: overflow %s, emergency engaged at %.0f ms\n",
                 overflow ? "reported" : "not reported", engagedAt * 1000.0);
    check.expect (overflow, "bank reports overflow");
    check.expect (engagedAt > 0.0, "emergency gain engages");
}

// The emergency stage alone: smooth, bounded, back to an exact identity afterwards.
void emergencyGainShape (Checker& check)
{
    fwyc::dsp::EmergencyGain stage;
    stage.prepare (kFs);
    fwyc::dsp::FilterBankStatus overwhelmed;
    overwhelmed.overflow = true;
    overwhelmed.loudestLevelDb = -10.0f;

    constexpr int chunk = 32;
    const int n = static_cast<int> (kFs * 8.0);
    float previous = 1.0f, maxStep = 0.0f, minGain = 1.0f;
    bool exactAfter = true;
    for (int pos = 0; pos < n; pos += chunk)
    {
        stage.update (pos < static_cast<int> (kFs * 0.5) ? overwhelmed : fwyc::dsp::FilterBankStatus {}, chunk);
        for (int i = 0; i < chunk; ++i)
        {
            const float g = stage.processSample (1.0f);
            maxStep = std::max (maxStep, std::abs (g - previous));
            minGain = std::min (minGain, g);
            previous = g;
            if (pos > static_cast<int> (kFs * 7.0))
                exactAfter = exactAfter && stage.processSample (0.123f) == 0.123f;
        }
    }
    const float maxCutDb = -20.0f * std::log10 (minGain);
    std::printf ("Emergency gain: max cut %.1f dB, largest per-sample gain step %.5f, exact identity after release: %s\n",
                 maxCutDb, maxStep, exactAfter ? "yes" : "no");
    check.expect (std::abs (maxCutDb - stage.config ().maxReductionDb) < 0.1f, "cuts up to its maximum");
    check.expect (maxStep < 0.001f, "gain moves smoothly (no step above 0.001 per sample)");
    check.expect (exactAfter, "exact identity once released");
}

// Brief howls at several frequencies, one after another and repeatedly, as when a mic is waved
// around: the bank's total cut and its lasting floors (repeat offenders) stay within budget.
void notchBuildUp (Checker& check, const char* name, std::initializer_list<double> freqs, int rounds)
{
    constexpr double burst = 0.5, gap = 0.3;
    const int perBurst = static_cast<int> (kFs * (burst + gap));
    const int bursts = static_cast<int> (freqs.size ()) * rounds;
    const int n = perBurst * bursts;

    Signal s = whiteNoise (n, -70.0f);
    for (int b = 0; b < bursts; ++b)
    {
        const double f = *(freqs.begin () + b % static_cast<int> (freqs.size ()));
        const Signal tone = growingTone (kFs, static_cast<int> (kFs * burst), f, -60.0f, 80.0f, -24.0f);
        const size_t start = static_cast<size_t> (b) * static_cast<size_t> (perBurst);
        for (size_t i = 0; i < tone.size (); ++i)
            s[start + i] += tone[i];
    }

    // Offline the howls never fade under a notch; the emergency stage would take that as losing.
    FeedbackSuppressor engine;
    engine.prepare (kFs, 64);
    engine.setEmergencyEnabled (false);
    Signal out (s.size ());
    float maxTotal = 0.0f, maxFloors = 0.0f;
    for (int pos = 0; pos < n; pos += 64)
    {
        engine.process (s.data () + pos, out.data () + pos, std::min (64, n - pos));
        maxTotal = std::max (maxTotal, totalDepthDb (engine));
        maxFloors = std::max (maxFloors, totalFloorDb (engine));
    }
    const auto& cfg = engine.filterBank ().config ();
    std::printf ("Notch build-up, %s (%d bursts): total cut peaked at %.1f dB (budget %.0f), floors at %.1f dB "
                 "(cap %.0f)\n",
                 name, bursts, maxTotal, cfg.maxTotalDepthDb, maxFloors, cfg.maxTotalFloorDb);
    // One new notch can engage before the oldest has released its share.
    check.expect (maxTotal <= cfg.maxTotalDepthDb + cfg.maxDepthDb, "total cut within budget (plus one notch)");
    check.expect (maxFloors <= cfg.maxTotalFloorDb + 0.01f, "lasting floors within their cap");
}

} // namespace

int main ()
{
    Checker check;
    micPointedAtSpeaker (check);
    moreHowlsThanNotches (check);
    emergencyGainShape (check);
    // Six frequencies would need 108 dB of notches at once: the total budget binds.
    notchBuildUp (check, "6 frequencies", {1170.0, 2030.0, 2890.0, 3810.0, 4730.0, 5590.0}, 5);
    // Three repeat offenders, five times each: within the total budget, but their floors
    // would add up to 18 dB without the floor cap.
    notchBuildUp (check, "3 repeat offenders", {1170.0, 2890.0, 4730.0}, 5);
    return check.finish ("protections");
}
