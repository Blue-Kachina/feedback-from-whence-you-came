// fwyc_analyze: runs the full suppressor (detector, notch bank, emergency gain) over a WAV
// file, offline, exactly as the plug-in would see it (block by block), and reports what it did.
//
//   fwyc_analyze <file.wav> [--channel N] [--block N] [--timeline MS] [--csv PATH] [--out PATH]
//
// Offline there is no acoustic loop: notching the output doesn't reduce the feedback in the
// input, so notches deepen to their maximum and sustained feedback looks like a losing notch,
// which engages the emergency gain. The output shows what would be removed.

#include "dsp/feedback_suppressor.h"
#include "wav_reader.h"
#include "wav_writer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using fwyc::dsp::FeedbackCandidate;
using fwyc::dsp::FeedbackDetector;
using fwyc::dsp::FilterBank;

struct Options
{
    std::string path;
    std::string csvPath;
    std::string outPath;
    std::string jsonPath;
    int channel = -1;
    int blockSize = 64;
    double timelineMs = 0.0;
};

void printUsage ()
{
    std::printf (
        "usage: fwyc_analyze <file.wav> [options]\n"
        "  --channel N     analyse channel N (0-based) instead of the average of all channels\n"
        "  --block N       host block size in samples (default 64)\n"
        "  --timeline MS   every MS milliseconds, print levels, notches and the top candidates\n"
        "  --csv PATH      write every active candidate of every analysis frame to PATH\n"
        "  --out PATH      write the processed audio (32-bit float WAV) to PATH\n"
        "  --json PATH     write per-episode measurements and the summary to PATH (for scripts/measure.ps1)\n");
}

bool parseArgs (int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--channel" && hasValue)
            o.channel = std::atoi (argv[++i]);
        else if (arg == "--block" && hasValue)
            o.blockSize = std::max (1, std::atoi (argv[++i]));
        else if (arg == "--timeline" && hasValue)
            o.timelineMs = std::atof (argv[++i]);
        else if (arg == "--csv" && hasValue)
            o.csvPath = argv[++i];
        else if (arg == "--out" && hasValue)
            o.outPath = argv[++i];
        else if (arg == "--json" && hasValue)
            o.jsonPath = argv[++i];
        else if (arg == "-h" || arg == "--help")
            return false;
        else if (!arg.empty () && arg[0] != '-' && o.path.empty ())
            o.path = arg;
        else
        {
            std::printf ("unknown or incomplete option: %s\n", arg.c_str ());
            return false;
        }
    }
    return !o.path.empty ();
}

std::string formatTime (double seconds)
{
    char buf[32];
    std::snprintf (buf, sizeof buf, "%d:%06.3f", static_cast<int> (seconds / 60.0), std::fmod (seconds, 60.0));
    return buf;
}

double sumSquares (const float* x, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double> (x[i]) * x[i];
    return sum;
}

float toDb (double meanSquare)
{
    return static_cast<float> (10.0 * std::log10 (meanSquare + 1e-20));
}

void printFeatures (const FeedbackCandidate& c)
{
    std::printf ("%8.1f Hz %6.1f dB  conf %.2f  pnpr %5.1f  phpr %5.1f  papr %5.1f  spread %5.2f%%  growth %+7.1f dB/s",
                 c.frequencyHz, c.levelDb, c.confidence, c.pnprDb, c.phprDb, c.paprDb, c.frequencySpreadPct,
                 c.growthDbPerSec);
}

// Detected episodes grouped by frequency, for the summary.
struct Episode
{
    double start;
    double end;
    float frequencyHz;
    float peakLevelDb;
};

struct Cluster
{
    float frequencyHz;
    int episodes = 0;
    double firstStart = 0.0;
    double totalSeconds = 0.0;
    float peakLevelDb = -200.0f;
};

std::vector<Cluster> clusterEpisodes (const std::vector<Episode>& episodes)
{
    constexpr float kTolerancePct = 3.0f;
    std::vector<Cluster> clusters;
    for (const auto& e : episodes)
    {
        auto it = std::find_if (clusters.begin (), clusters.end (), [&] (const Cluster& c) {
            return std::abs (c.frequencyHz - e.frequencyHz) <= c.frequencyHz * kTolerancePct / 100.0f;
        });
        if (it == clusters.end ())
        {
            clusters.push_back ({e.frequencyHz, 0, e.start, 0.0, -200.0f});
            it = clusters.end () - 1;
        }
        ++it->episodes;
        it->firstStart = std::min (it->firstStart, e.start);
        it->totalSeconds += e.end - e.start;
        it->peakLevelDb = std::max (it->peakLevelDb, e.peakLevelDb);
    }
    std::sort (clusters.begin (), clusters.end (),
               [] (const Cluster& a, const Cluster& b) { return a.firstStart < b.firstStart; });
    return clusters;
}

// One detected howl followed from the moment its peak was first tracked until the track ends,
// for --json. In a live recording of the plug-in's input (the mic), the drop after detection
// is what the notches achieved in the room.
struct Measured
{
    double bornSeconds;        // peak first tracked (feedback onset as far as the detector can see)
    double detectSeconds;
    float frequencyHz;
    float detectLevelDb;
    float peakLevelDb;         // highest level from detection on
    double peakSeconds;
    double attenuatedSeconds;  // first time 10 dB below that peak; -1 if never
    bool faded;                // track ended (sank into the noise) before falling 10 dB
    double endSeconds;
    bool emergency;            // emergency gain engaged at some point during the episode
};

std::string jsonString (const std::string& s)
{
    std::string out = "\"";
    for (char ch : s)
    {
        if (ch == '"' || ch == '\\')
            out += '\\';
        out += ch;
    }
    return out + "\"";
}

std::string jsonTime (double seconds)
{
    if (seconds < 0.0)
        return "null";
    char buf[32];
    std::snprintf (buf, sizeof buf, "%.4f", seconds);
    return buf;
}

// One notch slot's lifetime, for the summary.
struct NotchLife
{
    double engaged;
    double freed;
    float frequencyHz;
    float maxDepthDb;
    int episodes;
};

} // namespace

int main (int argc, char** argv)
{
    Options opt;
    if (!parseArgs (argc, argv, opt))
    {
        printUsage ();
        return 2;
    }

    fwyc::tools::WavFile wav;
    std::string error;
    if (!fwyc::tools::readWav (opt.path, wav, error, opt.channel))
    {
        std::printf ("error: %s\n", error.c_str ());
        return 1;
    }

    fwyc::dsp::FeedbackSuppressor engine;
    engine.prepare (wav.sampleRate, opt.blockSize);
    const auto& detector = engine.detector ();
    const auto& bank = engine.filterBank ();

    const double fs = wav.sampleRate;
    const int total = static_cast<int> (wav.samples.size ());
    std::printf ("%s\n  %.0f Hz, %d-bit %s, %d channel%s (%s), %s\n", opt.path.c_str (), fs, wav.bitsPerSample,
                 wav.isFloat ? "float" : "PCM", wav.channels, wav.channels == 1 ? "" : "s",
                 opt.channel < 0 ? "averaged" : ("channel " + std::to_string (opt.channel)).c_str (),
                 formatTime (total / fs).c_str ());
    std::printf ("  FFT %d, hop %d (%.2f ms), block %d, %d notches (Q %.0f, %.0f..%.0f dB)\n\n", detector.fftSize (),
                 detector.hopSize (), 1000.0 / detector.frameRate (), opt.blockSize, FilterBank::kMaxFilters,
                 bank.config ().q, bank.config ().initialDepthDb, bank.config ().maxDepthDb);

    FILE* csv = nullptr;
    if (!opt.csvPath.empty ())
    {
        csv = std::fopen (opt.csvPath.c_str (), "w");
        if (!csv)
        {
            std::printf ("error: cannot write %s\n", opt.csvPath.c_str ());
            return 1;
        }
        std::fprintf (csv, "time_s,slot,frequency_hz,level_db,confidence,detected,pnpr_db,phpr_db,papr_db,"
                           "spread_pct,growth_db_per_s\n");
    }

    std::vector<float> output (wav.samples.size ());

    std::array<bool, FeedbackDetector::kMaxCandidates> wasDetected {};
    std::array<Episode, FeedbackDetector::kMaxCandidates> open {};
    std::vector<Episode> episodes;

    std::array<bool, FilterBank::kMaxFilters> notchInUse {};
    std::array<float, FilterBank::kMaxFilters> notchTarget {};
    std::array<NotchLife, FilterBank::kMaxFilters> notchOpen {};
    std::vector<NotchLife> notches;

    bool emergencyOn = false;
    double emergencyStart = 0.0, emergencySeconds = 0.0;
    float emergencyMaxDb = 0.0f, emergencyPeakDb = 0.0f;
    int emergencyCount = 0;

    std::array<bool, FeedbackDetector::kMaxCandidates> wasActive {};
    std::array<double, FeedbackDetector::kMaxCandidates> bornAt {};
    std::array<bool, FeedbackDetector::kMaxCandidates> measuring {};
    std::array<Measured, FeedbackDetector::kMaxCandidates> current {};
    std::vector<Measured> measured;

    // Level of input vs output while any feedback is detected.
    double feedbackIn = 0.0, feedbackOut = 0.0, allIn = 0.0, allOut = 0.0;
    long long feedbackSamples = 0;

    const int hop = detector.hopSize ();
    const int timelineEvery =
        opt.timelineMs > 0.0 ? std::max (1, static_cast<int> (std::lround (opt.timelineMs / 1000.0 * fs))) : 0;
    int nextTimeline = timelineEvery;
    int samplesToCsvFrame = hop;

    std::printf ("Events\n");
    for (int pos = 0; pos < total; pos += opt.blockSize)
    {
        // With --csv, feed in hop-aligned pieces so rows line up with analysis frames. That moves
        // the engine's chunk boundaries slightly, so without --csv each block goes in whole,
        // exactly as a host with this block size would deliver it.
        const int blockEnd = std::min (pos + opt.blockSize, total);
        for (int p = pos; p < blockEnd;)
        {
            const int n = csv ? std::min (blockEnd - p, samplesToCsvFrame) : blockEnd - p;
            engine.process (wav.samples.data () + p, output.data () + p, n);
            p += n;
            samplesToCsvFrame -= n;
            if (samplesToCsvFrame == 0)
            {
                samplesToCsvFrame = hop;
                if (csv)
                {
                    const auto& cands = detector.candidates ();
                    for (size_t s = 0; s < cands.size (); ++s)
                    {
                        const auto& c = cands[s];
                        if (c.active)
                            std::fprintf (csv, "%.5f,%zu,%.2f,%.2f,%.3f,%d,%.2f,%.2f,%.2f,%.3f,%.1f\n", p / fs, s,
                                          c.frequencyHz, c.levelDb, c.confidence, c.detected ? 1 : 0, c.pnprDb,
                                          c.phprDb, c.paprDb, c.frequencySpreadPct, c.growthDbPerSec);
                    }
                }
            }
        }

        const double now = blockEnd / fs;
        const int len = blockEnd - pos;
        const double blockIn = sumSquares (wav.samples.data () + pos, len);
        const double blockOut = sumSquares (output.data () + pos, len);
        allIn += blockIn;
        allOut += blockOut;

        const auto& cands = detector.candidates ();
        const bool emergencyNow = engine.emergency ().reductionDb () > 0.0f;
        bool anyDetected = false;
        for (size_t s = 0; s < cands.size (); ++s)
        {
            const auto& c = cands[s];
            anyDetected = anyDetected || c.detected;

            if (c.active && !wasActive[s])
                bornAt[s] = now;
            wasActive[s] = c.active;
            if (c.detected && !measuring[s])
            {
                measuring[s] = true;
                current[s] = {bornAt[s], now, c.frequencyHz, c.levelDb, c.levelDb, now, -1.0, false, now, false};
            }
            if (measuring[s])
            {
                auto& m = current[s];
                m.endSeconds = now;
                if (c.active)
                {
                    m.emergency = m.emergency || emergencyNow;
                    if (c.levelDb > m.peakLevelDb)
                    {
                        // A new peak: attenuation is measured from the highest point reached.
                        m.peakLevelDb = c.levelDb;
                        m.peakSeconds = now;
                        m.attenuatedSeconds = -1.0;
                    }
                    else if (m.attenuatedSeconds < 0.0 && c.levelDb <= m.peakLevelDb - 10.0f)
                    {
                        m.attenuatedSeconds = now;
                    }
                }
                else
                {
                    if (m.attenuatedSeconds < 0.0)
                    {
                        m.faded = true;
                        m.attenuatedSeconds = now;
                    }
                    measured.push_back (m);
                    measuring[s] = false;
                }
            }
            if (c.detected && !wasDetected[s])
            {
                open[s] = {now, now, c.frequencyHz, c.levelDb};
                std::printf ("  %s  DETECT   ", formatTime (now).c_str ());
                printFeatures (c);
                std::printf ("\n");
            }
            else if (c.detected)
            {
                open[s].end = now;
                open[s].peakLevelDb = std::max (open[s].peakLevelDb, c.levelDb);
            }
            else if (wasDetected[s])
            {
                open[s].end = now;
                episodes.push_back (open[s]);
                std::printf ("  %s  RELEASE  %8.1f Hz after %.2f s\n", formatTime (now).c_str (),
                             open[s].frequencyHz, now - open[s].start);
            }
            wasDetected[s] = c.detected;
        }
        if (anyDetected)
        {
            feedbackIn += blockIn;
            feedbackOut += blockOut;
            feedbackSamples += len;
        }

        const auto& slots = bank.slots ();
        for (size_t s = 0; s < slots.size (); ++s)
        {
            const auto& slot = slots[s];
            if (slot.inUse && !notchInUse[s])
            {
                notchOpen[s] = {now, now, slot.frequencyHz, slot.targetDepthDb, slot.episodes};
                notchTarget[s] = slot.targetDepthDb;
                std::printf ("  %s  NOTCH+   %8.1f Hz  slot %zu, cut to %.1f dB\n", formatTime (now).c_str (),
                             slot.frequencyHz, s, slot.targetDepthDb);
            }
            else if (slot.inUse)
            {
                if (slot.targetDepthDb > notchTarget[s] + 0.01f)
                    std::printf ("  %s  DEEPER   %8.1f Hz  slot %zu, cut to %.1f dB (floor %.1f, episode %d)\n",
                                 formatTime (now).c_str (), slot.frequencyHz, s, slot.targetDepthDb, slot.floorDb,
                                 slot.episodes);
                notchTarget[s] = slot.targetDepthDb;
                notchOpen[s].frequencyHz = slot.frequencyHz;
                notchOpen[s].maxDepthDb = std::max (notchOpen[s].maxDepthDb, slot.depthDb);
                notchOpen[s].episodes = slot.episodes;
            }
            else if (notchInUse[s])
            {
                notchOpen[s].freed = now;
                notches.push_back (notchOpen[s]);
                std::printf ("  %s  NOTCH-   %8.1f Hz  slot %zu released after %.2f s\n", formatTime (now).c_str (),
                             notchOpen[s].frequencyHz, s, now - notchOpen[s].engaged);
            }
            notchInUse[s] = slot.inUse;
        }

        const auto& emergency = engine.emergency ();
        if (emergency.reductionDb () > 0.0f && !emergencyOn)
        {
            const auto& st = bank.status ();
            emergencyOn = true;
            emergencyStart = now;
            emergencyMaxDb = 0.0f;
            ++emergencyCount;
            std::printf ("  %s  EMERG+   broadband cut (%s)\n", formatTime (now).c_str (),
                         st.overflow ? "more feedback than notches"
                         : st.losing ? "a full-depth notch is losing"
                                     : "feedback still loud after its notch");
        }
        else if (emergency.reductionDb () <= 0.0f && emergencyOn)
        {
            emergencyOn = false;
            emergencySeconds += now - emergencyStart;
            std::printf ("  %s  EMERG-   after %.2f s, max cut %.1f dB\n", formatTime (now).c_str (),
                         now - emergencyStart, emergencyMaxDb);
        }
        emergencyMaxDb = std::max (emergencyMaxDb, emergency.reductionDb ());
        emergencyPeakDb = std::max (emergencyPeakDb, emergency.reductionDb ());

        if (timelineEvery && blockEnd >= nextTimeline)
        {
            nextTimeline += timelineEvery;
            const int window = std::min (timelineEvery, blockEnd);
            const float inDb = toDb (sumSquares (wav.samples.data () + blockEnd - window, window) / window);
            const float outDb = toDb (sumSquares (output.data () + blockEnd - window, window) / window);
            std::printf ("  %s  ---      in %6.1f dBFS, out %6.1f dBFS", formatTime (now).c_str (), inDb, outDb);
            for (const auto& slot : slots)
                if (slot.inUse)
                    std::printf ("  [%.0f Hz -%.1f dB]", slot.frequencyHz, slot.depthDb);
            if (emergency.reductionDb () > 0.0f)
                std::printf ("  [all -%.1f dB]", emergency.reductionDb ());
            std::printf ("\n");

            std::array<const FeedbackCandidate*, FeedbackDetector::kMaxCandidates> active {};
            size_t numActive = 0;
            for (const auto& c : cands)
                if (c.active)
                    active[numActive++] = &c;
            std::sort (active.begin (), active.begin () + numActive,
                       [] (auto* a, auto* b) { return a->confidence > b->confidence; });
            for (size_t i = 0; i < std::min<size_t> (numActive, 3); ++i)
            {
                std::printf ("                      %s", active[i]->detected ? "* " : "  ");
                printFeatures (*active[i]);
                std::printf ("\n");
            }
        }
    }

    for (size_t s = 0; s < wasDetected.size (); ++s)
        if (wasDetected[s])
            episodes.push_back (open[s]);
    for (size_t s = 0; s < notchInUse.size (); ++s)
        if (notchInUse[s])
        {
            notchOpen[s].freed = total / fs;
            notches.push_back (notchOpen[s]);
        }

    if (emergencyOn)
        emergencySeconds += total / fs - emergencyStart;
    for (size_t s = 0; s < measuring.size (); ++s)
        if (measuring[s])
            measured.push_back (current[s]); // still going at the end of the file
    std::sort (measured.begin (), measured.end (),
               [] (const Measured& a, const Measured& b) { return a.detectSeconds < b.detectSeconds; });

    if (csv)
        std::fclose (csv);

    std::printf ("\nDetections\n");
    const auto clusters = clusterEpisodes (episodes);
    if (clusters.empty ())
        std::printf ("  no feedback detected\n");
    for (const auto& c : clusters)
        std::printf ("  %8.1f Hz  first at %s, %d episode%s, %.2f s total, peak %.1f dBFS\n", c.frequencyHz,
                     formatTime (c.firstStart).c_str (), c.episodes, c.episodes == 1 ? "" : "s", c.totalSeconds,
                     c.peakLevelDb);

    std::printf ("\nNotches\n");
    if (notches.empty ())
        std::printf ("  none engaged: output is identical to input\n");
    std::sort (notches.begin (), notches.end (),
               [] (const NotchLife& a, const NotchLife& b) { return a.engaged < b.engaged; });
    for (const auto& n : notches)
        std::printf ("  %8.1f Hz  %s to %s, max cut %.1f dB, %d episode%s\n", n.frequencyHz,
                     formatTime (n.engaged).c_str (), formatTime (n.freed).c_str (), n.maxDepthDb, n.episodes,
                     n.episodes == 1 ? "" : "s");

    std::printf ("\nEmergency gain\n");
    if (emergencyCount == 0)
        std::printf ("  never engaged\n");
    else
        std::printf ("  engaged %d time%s, %.2f s in total\n", emergencyCount, emergencyCount == 1 ? "" : "s",
                     emergencySeconds);

    std::printf ("\nLevels\n");
    std::printf ("  whole file:          in %6.1f dBFS RMS, out %6.1f dBFS RMS\n", toDb (allIn / total),
                 toDb (allOut / total));
    if (feedbackSamples > 0)
        std::printf ("  while feedback seen: in %6.1f dBFS RMS, out %6.1f dBFS RMS (%.1f dB reduction, %.2f s)\n",
                     toDb (feedbackIn / feedbackSamples), toDb (feedbackOut / feedbackSamples),
                     toDb (feedbackIn / feedbackSamples) - toDb (feedbackOut / feedbackSamples),
                     static_cast<double> (feedbackSamples) / fs);

    if (!opt.jsonPath.empty ())
    {
        FILE* json = std::fopen (opt.jsonPath.c_str (), "w");
        if (!json)
        {
            std::printf ("error: cannot write %s\n", opt.jsonPath.c_str ());
            return 1;
        }
        const double fbIn = feedbackSamples > 0 ? toDb (feedbackIn / feedbackSamples) : -200.0;
        const double fbOut = feedbackSamples > 0 ? toDb (feedbackOut / feedbackSamples) : -200.0;
        std::fprintf (json, "{\n  \"file\": %s,\n  \"sampleRate\": %.0f,\n  \"durationSeconds\": %.4f,\n"
                            "  \"blockSize\": %d,\n",
                      jsonString (opt.path).c_str (), fs, total / fs, opt.blockSize);
        std::fprintf (json, "  \"levels\": {\"inRmsDb\": %.2f, \"outRmsDb\": %.2f, \"feedbackSeconds\": %.4f, "
                            "\"feedbackInRmsDb\": %.2f, \"feedbackOutRmsDb\": %.2f},\n",
                      toDb (allIn / total), toDb (allOut / total), static_cast<double> (feedbackSamples) / fs, fbIn,
                      fbOut);
        std::fprintf (json, "  \"emergency\": {\"engagements\": %d, \"seconds\": %.4f, \"maxReductionDb\": %.2f},\n",
                      emergencyCount, emergencySeconds, emergencyPeakDb);
        std::fprintf (json, "  \"episodes\": [");
        for (size_t i = 0; i < measured.size (); ++i)
        {
            const auto& m = measured[i];
            std::fprintf (json,
                          "%s\n    {\"frequencyHz\": %.1f, \"bornSeconds\": %s, \"detectSeconds\": %s, "
                          "\"detectLevelDb\": %.2f, \"peakLevelDb\": %.2f, \"peakSeconds\": %s, "
                          "\"attenuatedSeconds\": %s, \"faded\": %s, \"endSeconds\": %s, \"emergency\": %s}",
                          i == 0 ? "" : ",", m.frequencyHz, jsonTime (m.bornSeconds).c_str (),
                          jsonTime (m.detectSeconds).c_str (), m.detectLevelDb, m.peakLevelDb,
                          jsonTime (m.peakSeconds).c_str (), jsonTime (m.attenuatedSeconds).c_str (),
                          m.faded ? "true" : "false", jsonTime (m.endSeconds).c_str (),
                          m.emergency ? "true" : "false");
        }
        std::fprintf (json, "\n  ]\n}\n");
        std::fclose (json);
    }

    if (!opt.outPath.empty ())
    {
        if (!fwyc::tools::writeWavFloat (opt.outPath, output, fs, error))
        {
            std::printf ("error: %s\n", error.c_str ());
            return 1;
        }
        std::printf ("\nProcessed audio written to %s\n", opt.outPath.c_str ());
    }
    return 0;
}
