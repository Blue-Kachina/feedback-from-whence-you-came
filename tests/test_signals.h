#pragma once

// Deterministic synthetic signals and a detector harness shared by the DSP tests.

#include "dsp/feedback_detector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

namespace fwyc::test {

using Signal = std::vector<float>;

inline float dbToGain (float db)
{
    return std::pow (10.0f, db / 20.0f);
}

inline Signal& operator+= (Signal& a, const Signal& b)
{
    for (size_t i = 0; i < std::min (a.size (), b.size ()); ++i)
        a[i] += b[i];
    return a;
}

// Linear congruential generator: same noise on every platform and run.
class Rng
{
public:
    explicit Rng (uint32_t seed) : state_ (seed) {}

    float uniform () // [-1, 1)
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float> (state_ >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }

private:
    uint32_t state_;
};

inline Signal whiteNoise (int numSamples, float rmsDb, uint32_t seed = 1)
{
    Rng rng (seed);
    const float gain = dbToGain (rmsDb) * std::sqrt (3.0f); // uniform noise RMS = 1/sqrt(3)
    Signal s (static_cast<size_t> (numSamples));
    for (auto& v : s)
        v = gain * rng.uniform ();
    return s;
}

// Sine whose peak level starts at startDb (dBFS) and changes linearly in dB, capped at maxDb.
inline Signal growingTone (double fs, int numSamples, double freqHz, float startDb, float dbPerSec,
                           float maxDb = -3.0f)
{
    Signal s (static_cast<size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = i / fs;
        const float level = std::min (maxDb, startDb + dbPerSec * static_cast<float> (t));
        s[static_cast<size_t> (i)] =
            dbToGain (level) * static_cast<float> (std::sin (2.0 * std::numbers::pi * freqHz * t));
    }
    return s;
}

struct NoteSpec
{
    double f0Hz = 220.0;
    int harmonics = 12;         // amplitudes fall as 1/k
    float levelDb = -12.0f;     // fundamental peak level, dBFS
    float vibratoPct = 0.0f;    // peak frequency deviation
    float vibratoHz = 5.5f;
    float crescendoDbPerSec = 0.0f;
    float crescendoMaxDb = 0.0f; // relative to levelDb
};

// Sung-note stand-in: a harmonic series with optional vibrato and crescendo.
inline Signal harmonicNote (double fs, int numSamples, const NoteSpec& note)
{
    Signal s (static_cast<size_t> (numSamples));
    double phase = 0.0; // fundamental phase, radians
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = i / fs;
        const double f = note.f0Hz *
                         (1.0 + note.vibratoPct / 100.0 * std::sin (2.0 * std::numbers::pi * note.vibratoHz * t));
        phase += 2.0 * std::numbers::pi * f / fs;

        const float level =
            note.levelDb + std::min (note.crescendoMaxDb, note.crescendoDbPerSec * static_cast<float> (t));
        const float gain = dbToGain (level);

        double v = 0.0;
        for (int k = 1; k <= note.harmonics; ++k)
            if (k * note.f0Hz < 0.45 * fs)
                v += std::sin (k * phase) / k;
        s[static_cast<size_t> (i)] = gain * static_cast<float> (v);
    }
    return s;
}

// RBJ band-pass biquad (0 dB peak gain), used as the loop's room/speaker resonance.
class BandPass
{
public:
    BandPass (double fs, double freqHz, double q)
    {
        const double w0 = 2.0 * std::numbers::pi * freqHz / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0_ = alpha / a0;
        b2_ = -alpha / a0;
        a1_ = -2.0 * std::cos (w0) / a0;
        a2_ = (1.0 - alpha) / a0;
    }

    double process (double x)
    {
        const double y = b0_ * x + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

private:
    double b0_, b2_, a1_, a2_;
    double x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

struct GainStep
{
    double atSeconds;
    double loopGain;
};

struct LoopSpec
{
    double resonanceHz = 2000.0;
    double q = 4.0;
    double loopGain = 1.2;      // > 1 at resonance: the loop is unstable and howls
    double delaySeconds = 0.02; // speaker -> mic acoustic path
    double secondResonanceHz = 0.0;  // optional second room mode, same q (0 = none)
    std::vector<GainStep> gainSteps; // optional loop gain changes, in time order (mic moved)
};

// Closed acoustic loop: mic = source + delayed, band-limited, amplified (processed) mic.
// `process (in, out, n)` sits between mic and speaker, as the plug-in would, run in blocks
// no longer than the loop delay. The speaker saturates (tanh) so a howl levels off.
// Returns the mic signal.
template <typename Process>
Signal simulateFeedbackLoop (double fs, const Signal& source, const LoopSpec& loop, Process&& process,
                             int blockSize = 64)
{
    const auto delay = static_cast<size_t> (std::lround (loop.delaySeconds * fs));
    const auto block = std::min (static_cast<size_t> (blockSize), delay);
    std::vector<double> line (delay, 0.0);
    size_t pos = 0;
    BandPass room (fs, loop.resonanceHz, loop.q);
    BandPass secondRoom (fs, loop.secondResonanceHz > 0.0 ? loop.secondResonanceHz : loop.resonanceHz, loop.q);

    Signal mic (source.size ());
    std::vector<float> speakerFeed (block);
    double gain = loop.loopGain;
    size_t nextStep = 0;
    for (size_t start = 0; start < source.size (); start += block)
    {
        while (nextStep < loop.gainSteps.size () && start / fs >= loop.gainSteps[nextStep].atSeconds)
            gain = loop.gainSteps[nextStep++].loopGain;

        const size_t n = std::min (block, source.size () - start);
        for (size_t i = 0; i < n; ++i)
            mic[start + i] = static_cast<float> (source[start + i] + line[(pos + i) % delay]);

        process (mic.data () + start, speakerFeed.data (), static_cast<int> (n));

        for (size_t i = 0; i < n; ++i)
        {
            double y = room.process (speakerFeed[i]);
            if (loop.secondResonanceHz > 0.0)
                y += secondRoom.process (speakerFeed[i]);
            line[(pos + i) % delay] = std::tanh (gain * y);
        }
        pos = (pos + n) % delay;
    }
    return mic;
}

// Loop with nothing in the path.
inline Signal simulateFeedbackLoop (double fs, const Signal& source, const LoopSpec& loop)
{
    return simulateFeedbackLoop (fs, source, loop,
                                 [] (const float* in, float* out, int n) { std::copy_n (in, n, out); });
}

inline float rmsDb (const Signal& s, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double> (s[i]) * s[i];
    return static_cast<float> (10.0 * std::log10 (sum / static_cast<double> (std::max<size_t> (1, to - from)) + 1e-20));
}

inline float peakDb (const Signal& s, size_t from, size_t to)
{
    float peak = 0.0f;
    for (size_t i = from; i < to; ++i)
        peak = std::max (peak, std::abs (s[i]));
    return 20.0f * std::log10 (peak + 1e-10f);
}

// Level of one frequency component (Goertzel), as the peak amplitude of a sine, in dBFS.
inline float toneLevelDb (const Signal& s, size_t from, size_t to, double fs, double freqHz)
{
    const double w = 2.0 * std::numbers::pi * freqHz / fs;
    const double coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double s0 = s[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    const double amplitude = 2.0 * std::sqrt (std::max (0.0, power)) / static_cast<double> (to - from);
    return static_cast<float> (20.0 * std::log10 (amplitude + 1e-10));
}

// Seconds until |signal| first exceeds the given peak level, or -1 if it never does.
inline double timeToReach (const Signal& s, double fs, float peakDb)
{
    const float threshold = dbToGain (peakDb);
    for (size_t i = 0; i < s.size (); ++i)
        if (std::abs (s[i]) > threshold)
            return static_cast<double> (i) / fs;
    return -1.0;
}

struct Detection
{
    double timeSec;
    float frequencyHz;
    float levelDb;
};

struct RunResult
{
    std::vector<Detection> detections; // one per rising edge of a candidate's detected flag
    float maxConfidence = 0.0f;
    float maxConfidenceHz = 0.0f;
    dsp::FeedbackCandidate mostConfident; // snapshot at maxConfidence, for diagnostics
};

inline RunResult runDetector (const Signal& signal, double fs, int blockSize = 64,
                              const dsp::FeedbackDetectorConfig& config = {})
{
    dsp::FeedbackDetector detector (config);
    detector.prepare (fs);

    RunResult result;
    std::array<bool, dsp::FeedbackDetector::kMaxCandidates> wasDetected {};
    const int total = static_cast<int> (signal.size ());
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int n = std::min (blockSize, total - pos);
        detector.process (signal.data () + pos, n);

        const auto& candidates = detector.candidates ();
        for (size_t s = 0; s < candidates.size (); ++s)
        {
            const auto& c = candidates[s];
            if (c.detected && !wasDetected[s])
                result.detections.push_back ({(pos + n) / fs, c.frequencyHz, c.levelDb});
            wasDetected[s] = c.detected;
            if (c.confidence > result.maxConfidence)
            {
                result.maxConfidence = c.confidence;
                result.maxConfidenceHz = c.frequencyHz;
                result.mostConfident = c;
            }
        }
    }
    return result;
}

// Minimal test bookkeeping: tests print what they measured, and fail with a reason.
class Checker
{
public:
    void expect (bool ok, const char* what)
    {
        if (!ok)
        {
            std::printf ("    FAIL: %s\n", what);
            ++failures_;
        }
    }

    int finish (const char* name) const
    {
        std::printf ("%s: %s (%d failure%s)\n", name, failures_ == 0 ? "OK" : "FAILED", failures_,
                     failures_ == 1 ? "" : "s");
        return failures_ == 0 ? 0 : 1;
    }

private:
    int failures_ = 0;
};

} // namespace fwyc::test
