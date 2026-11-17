// fwyc_analyze: runs the full suppressor (detector + notch bank) over a WAV file, offline,
// exactly as the plug-in would see it (block by block), and reports what it did.
//
//   fwyc_analyze <file.wav> [--channel N] [--block N] [--timeline MS] [--csv PATH] [--out PATH]
//
// Offline there is no acoustic loop: notching the output doesn't reduce the feedback in the
// input, so notches deepen to their maximum. The output shows what the notches remove.

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
        "  --out PATH      write the processed audio (32-bit float WAV) to PATH\n");
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
        // Feed in hop-aligned pieces so CSV rows line up with analysis frames; processing is
        // unaffected by how the block is split.
        const int blockEnd = std::min (pos + opt.blockSize, total);
        for (int p = pos; p < blockEnd;)
        {
            const int n = std::min (blockEnd - p, samplesToCsvFrame);
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
        bool anyDetected = false;
        for (size_t s = 0; s < cands.size (); ++s)
        {
            const auto& c = cands[s];
            anyDetected = anyDetected || c.detected;
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

    std::printf ("\nLevels\n");
    std::printf ("  whole file:          in %6.1f dBFS RMS, out %6.1f dBFS RMS\n", toDb (allIn / total),
                 toDb (allOut / total));
    if (feedbackSamples > 0)
        std::printf ("  while feedback seen: in %6.1f dBFS RMS, out %6.1f dBFS RMS (%.1f dB reduction, %.2f s)\n",
                     toDb (feedbackIn / feedbackSamples), toDb (feedbackOut / feedbackSamples),
                     toDb (feedbackIn / feedbackSamples) - toDb (feedbackOut / feedbackSamples),
                     static_cast<double> (feedbackSamples) / fs);

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
