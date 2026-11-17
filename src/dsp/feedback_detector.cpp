#include "dsp/feedback_detector.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fwyc::dsp {

namespace {

// Feature ranges mapped to 0..1 scores (smoothstep between the two values).
constexpr float kNarrowLowDb = 8.0f, kNarrowHighDb = 20.0f;      // PNPR
constexpr float kIsolatedLowDb = 6.0f, kIsolatedHighDb = 18.0f;  // PHPR
constexpr float kDominantLowDb = 6.0f, kDominantHighDb = 16.0f;  // PAPR
constexpr float kStableLowPct = 0.2f, kStableHighPct = 0.7f;     // frequency spread (inverted)

// Feature smoothing is asymmetric: evidence against feedback (a harmonic appears, the peak
// widens) lands within a few frames; evidence for it accumulates slowly. A single clean-looking
// frame, e.g. the onset of a sung note, can't push confidence up.
constexpr float kFeatureRiseSeconds = 0.1f;
constexpr float kFeatureFallSeconds = 0.01f;

// Frequency is smoothed before measuring its spread: beating between loop modes inside one
// bin (tens of Hz) averages out, while vibrato (~5 Hz) still shows.
constexpr float kFrequencySmoothingSeconds = 0.025f;
constexpr float kSpreadWindowSeconds = 0.1f;
// Several loop modes inside one or two bins beat, so the strongest peak hops between them.
// They can't be resolved at this window length anyway and one notch covers them, so they
// are followed as one track rather than restarting persistence on every hop.
constexpr float kMatchToleranceBins = 0.75f;
constexpr float kMaxMissSeconds = 0.006f; // peak may vanish this long (beating) before the track ends
constexpr float kMainLobeBins = 2.5f;          // Hann main lobe half-width, plus margin
constexpr int kNeighbourFirstBin = 4;          // PNPR neighbour ring, bins either side of the peak
constexpr int kNeighbourLastBin = 8;

// Feedback doesn't fade by itself: a peak decaying faster than this can't start a detection
// (whistle and note tails). An existing detection is not released by it, because a notch
// pulling real feedback down also makes it decay.
// Beating dips can briefly read as -70 dB/s; whistle tails measured -87 to -89 dB/s.
constexpr float kMaxOnsetDecayDbPerSec = 40.0f;
constexpr float kHarmonicToleranceBins = 0.5f; // harmonic peak must sit this close to k * f
constexpr float kMissDecay = 0.8f;
constexpr float kNoHarmonicPhprDb = 60.0f;

// Harmonic relationships checked for PHPR: the peak could be a fundamental (2x, 3x)
// or an overtone (1/2, 1/3, 2/3, 3/2) of a voice.
constexpr float kHarmonicRatios[] = {0.5f, 1.0f / 3.0f, 2.0f / 3.0f, 1.5f, 2.0f, 3.0f};

float smoothstep (float edge0, float edge1, float x) noexcept
{
    const float t = std::clamp ((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float powerToDb (float power) noexcept
{
    return 10.0f * std::log10 (power + 1e-30f);
}

float emaCoefficient (double frameRate, float seconds) noexcept
{
    return static_cast<float> (1.0 - std::exp (-1.0 / (frameRate * seconds)));
}

// Moves towards x quickly when x is lower (less feedback-like), slowly when higher.
void smoothFeature (float& value, float x, float rise, float fall) noexcept
{
    value += (x < value ? fall : rise) * (x - value);
}

} // namespace

void FeedbackDetector::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    const int target = static_cast<int> (std::lround (sampleRate * config_.windowSeconds));
    fftSize_ = 64;
    while (fftSize_ < target && fftSize_ < 8192)
        fftSize_ <<= 1;

    hopSize_ = std::max (1, fftSize_ / config_.hopDivisor);
    frameRate_ = sampleRate / hopSize_;
    binHz_ = static_cast<float> (sampleRate / fftSize_);

    fft_.prepare (fftSize_);

    // Periodic Hann window; normDb_ makes a full-scale sine read 0 dB.
    window_.resize (static_cast<size_t> (fftSize_));
    double windowSum = 0.0;
    for (int n = 0; n < fftSize_; ++n)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * n / fftSize_);
        window_[static_cast<size_t> (n)] = static_cast<float> (w);
        windowSum += w;
    }
    normDb_ = static_cast<float> (20.0 * std::log10 (windowSum / 2.0));

    ring_.assign (static_cast<size_t> (fftSize_), 0.0f);
    spectrum_.resize (static_cast<size_t> (fftSize_));
    specDb_.resize (static_cast<size_t> (fftSize_ / 2 + 1));
    scratch_.resize (specDb_.size ());

    riseCoeff_ = emaCoefficient (frameRate_, kFeatureRiseSeconds);
    fallCoeff_ = emaCoefficient (frameRate_, kFeatureFallSeconds);
    freqSmoothCoeff_ = emaCoefficient (frameRate_, kFrequencySmoothingSeconds);
    spreadCoeff_ = emaCoefficient (frameRate_, kSpreadWindowSeconds);
    persistenceFrames_ = std::max (1.0f, static_cast<float> (config_.persistenceSeconds * frameRate_));
    settleFrames_ = (fftSize_ + hopSize_ - 1) / hopSize_;
    maxMisses_ = std::max (2, static_cast<int> (std::lround (kMaxMissSeconds * frameRate_)));

    const int half = fftSize_ / 2;
    const float maxHz = std::min (config_.maxFrequencyHz, static_cast<float> (0.45 * sampleRate));
    minBin_ = std::max (2, static_cast<int> (std::ceil (config_.minFrequencyHz / binHz_)));
    maxBin_ = std::min (half - 2, static_cast<int> (std::floor (maxHz / binHz_)));

    reset ();
}

void FeedbackDetector::reset () noexcept
{
    std::fill (ring_.begin (), ring_.end (), 0.0f);
    ringPos_ = 0;
    samplesUntilFrame_ = hopSize_;
    tracks_ = {};
    candidates_ = {};
}

const std::array<FeedbackCandidate, FeedbackDetector::kMaxCandidates>&
FeedbackDetector::candidates () const noexcept
{
    return candidates_;
}

void FeedbackDetector::process (const float* in, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        ring_[static_cast<size_t> (ringPos_)] = in[i];
        if (++ringPos_ == fftSize_)
            ringPos_ = 0;

        if (--samplesUntilFrame_ == 0)
        {
            analyseFrame ();
            samplesUntilFrame_ = hopSize_;
        }
    }
}

void FeedbackDetector::analyseFrame () noexcept
{
    // ringPos_ points at the oldest sample.
    for (int n = 0; n < fftSize_; ++n)
    {
        int idx = ringPos_ + n;
        if (idx >= fftSize_)
            idx -= fftSize_;
        spectrum_[static_cast<size_t> (n)] = {ring_[static_cast<size_t> (idx)] * window_[static_cast<size_t> (n)], 0.0f};
    }

    fft_.forward (spectrum_.data ());

    for (size_t b = 0; b < specDb_.size (); ++b)
        specDb_[b] = powerToDb (std::norm (spectrum_[b])) - normDb_;

    updateTracks (findPeaks ());
}

int FeedbackDetector::findPeaks () noexcept
{
    const auto first = specDb_.begin () + minBin_;
    const auto last = specDb_.begin () + maxBin_ + 1;
    const auto count = last - first;

    // Spectral floor: median level across the analysis range.
    std::copy (first, last, scratch_.begin ());
    std::nth_element (scratch_.begin (), scratch_.begin () + count / 2, scratch_.begin () + count);
    const float floorDb = scratch_[static_cast<size_t> (count / 2)];

    double meanPower = 0.0;
    for (int b = minBin_; b <= maxBin_; ++b)
        meanPower += std::norm (spectrum_[static_cast<size_t> (b)]);
    const float meanPowerDb = powerToDb (static_cast<float> (meanPower / static_cast<double> (count))) - normDb_;

    int numPeaks = 0;
    for (int b = minBin_; b <= maxBin_; ++b)
    {
        const float v = specDb_[static_cast<size_t> (b)];
        if (v <= specDb_[static_cast<size_t> (b - 1)] || v < specDb_[static_cast<size_t> (b + 1)])
            continue;
        if (v < config_.minLevelDb || v - floorDb < config_.minProminenceDb)
            continue;

        const Peak peak = measurePeak (b, meanPowerDb);
        if (numPeaks < kMaxPeaksPerFrame)
        {
            peaks_[static_cast<size_t> (numPeaks++)] = peak;
        }
        else
        {
            auto weakest = std::min_element (peaks_.begin (), peaks_.end (),
                                             [] (const Peak& a, const Peak& b) { return a.levelDb < b.levelDb; });
            if (peak.levelDb > weakest->levelDb)
                *weakest = peak;
        }
    }

    std::sort (peaks_.begin (), peaks_.begin () + numPeaks,
               [] (const Peak& a, const Peak& b) { return a.levelDb > b.levelDb; });
    return numPeaks;
}

FeedbackDetector::Peak FeedbackDetector::measurePeak (int bin, float meanPowerDb) const noexcept
{
    const float a = specDb_[static_cast<size_t> (bin - 1)];
    const float m = specDb_[static_cast<size_t> (bin)];
    const float c = specDb_[static_cast<size_t> (bin + 1)];

    // Parabolic interpolation on the log spectrum refines frequency between bins.
    const float denom = a - 2.0f * m + c;
    const float delta = denom < 0.0f ? std::clamp (0.5f * (a - c) / denom, -0.5f, 0.5f) : 0.0f;
    const float exactBin = static_cast<float> (bin) + delta;
    const float levelDb = m - 0.25f * (a - c) * delta;

    // Neighbouring power, excluding the Hann main lobe (+-2 bins) plus one bin of margin, so a
    // tight cluster of loop modes beating around one frequency isn't counted against itself.
    const int half = fftSize_ / 2;
    double neighbourPower = 0.0;
    int neighbours = 0;
    for (int offset = kNeighbourFirstBin; offset <= kNeighbourLastBin; ++offset)
    {
        for (int b : {bin - offset, bin + offset})
        {
            if (b >= 1 && b < half)
            {
                neighbourPower += std::norm (spectrum_[static_cast<size_t> (b)]);
                ++neighbours;
            }
        }
    }
    const float neighbourDb =
        powerToDb (static_cast<float> (neighbourPower / std::max (1, neighbours))) - normDb_;

    Peak peak;
    peak.frequencyHz = exactBin * binHz_;
    peak.levelDb = levelDb;
    peak.pnprDb = levelDb - neighbourDb;
    peak.phprDb = std::min (kNoHarmonicPhprDb, levelDb - harmonicLevelDb (exactBin));
    peak.paprDb = levelDb - meanPowerDb;
    return peak;
}

float FeedbackDetector::harmonicLevelDb (float exactBin) const noexcept
{
    // A harmonic counts only if it is a genuine spectral peak at the harmonic frequency:
    // a nearby unrelated tone, or the peak's own main lobe, must not count.
    const int half = fftSize_ / 2;
    float strongest = -200.0f;
    for (float ratio : kHarmonicRatios)
    {
        const float target = exactBin * ratio;
        const int centre = static_cast<int> (std::lround (target));
        for (int k = centre - 1; k <= centre + 1; ++k)
        {
            if (k < 2 || k + 1 > half || std::abs (static_cast<float> (k) - exactBin) <= kMainLobeBins)
                continue;

            const float a = specDb_[static_cast<size_t> (k - 1)];
            const float m = specDb_[static_cast<size_t> (k)];
            const float c = specDb_[static_cast<size_t> (k + 1)];
            if (m <= a || m < c)
                continue;

            const float denom = a - 2.0f * m + c;
            const float delta = denom < 0.0f ? std::clamp (0.5f * (a - c) / denom, -0.5f, 0.5f) : 0.0f;
            if (std::abs (static_cast<float> (k) + delta - target) <= kHarmonicToleranceBins)
                strongest = std::max (strongest, m - 0.25f * (a - c) * delta);
        }
    }
    return strongest;
}

void FeedbackDetector::updateTracks (int numPeaks) noexcept
{
    for (auto& t : tracks_)
        t.matched = false;

    const float tolerance = kMatchToleranceBins * binHz_;

    // Strongest peaks claim tracks first.
    for (int p = 0; p < numPeaks; ++p)
    {
        const Peak& peak = peaks_[static_cast<size_t> (p)];

        int slot = -1;
        float bestDistance = tolerance;
        for (int s = 0; s < kMaxCandidates; ++s)
        {
            if (!candidates_[static_cast<size_t> (s)].active || tracks_[static_cast<size_t> (s)].matched)
                continue;
            const float distance = std::abs (candidates_[static_cast<size_t> (s)].frequencyHz - peak.frequencyHz);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                slot = s;
            }
        }

        if (slot < 0)
        {
            for (int s = 0; s < kMaxCandidates && slot < 0; ++s)
                if (!candidates_[static_cast<size_t> (s)].active)
                    slot = s;
            if (slot < 0)
                continue; // all slots busy; weaker peaks wait for one to free up
        }

        updateTrack (slot, peak);
    }

    for (int s = 0; s < kMaxCandidates; ++s)
    {
        auto& c = candidates_[static_cast<size_t> (s)];
        auto& t = tracks_[static_cast<size_t> (s)];
        if (!c.active || t.matched)
            continue;

        if (++t.misses > maxMisses_)
        {
            releaseTrack (s);
        }
        else
        {
            c.confidence *= kMissDecay;
            if (c.confidence < config_.releaseThreshold)
                c.detected = false;
        }
    }
}

void FeedbackDetector::updateTrack (int slot, const Peak& peak) noexcept
{
    auto& c = candidates_[static_cast<size_t> (slot)];
    auto& t = tracks_[static_cast<size_t> (slot)];

    // A new track (or one still settling) restarts its statistics: until one full window has
    // passed, the frame may straddle the onset, smearing frequency, level and features.
    if (!c.active || t.age < settleFrames_)
    {
        const int age = c.active ? t.age : 0;
        c = {};
        t = {};
        c.active = true;
        t.age = age;
        t.freqSmooth = peak.frequencyHz;
        t.freqMean = peak.frequencyHz;
        c.pnprDb = peak.pnprDb;
        c.phprDb = peak.phprDb;
        c.paprDb = peak.paprDb;
    }
    else
    {
        // Exponentially weighted mean/variance of the smoothed frequency.
        t.freqSmooth += freqSmoothCoeff_ * (peak.frequencyHz - t.freqSmooth);
        const float a = spreadCoeff_;
        const float diff = t.freqSmooth - t.freqMean;
        const float incr = a * diff;
        t.freqMean += incr;
        t.freqVar = (1.0f - a) * (t.freqVar + diff * incr);

        smoothFeature (c.pnprDb, peak.pnprDb, riseCoeff_, fallCoeff_);
        smoothFeature (c.phprDb, peak.phprDb, riseCoeff_, fallCoeff_);
        smoothFeature (c.paprDb, peak.paprDb, riseCoeff_, fallCoeff_);
    }

    t.matched = true;
    t.misses = 0;
    ++t.age;
    c.frequencyHz = peak.frequencyHz;
    c.levelDb = peak.levelDb;

    t.levels[static_cast<size_t> (t.historyPos)] = peak.levelDb;
    t.historyPos = (t.historyPos + 1) % kHistory;
    t.historyCount = std::min (t.historyCount + 1, kHistory);
    if (t.historyCount >= 4)
        c.growthDbPerSec += riseCoeff_ * (growthDbPerSec (t) - c.growthDbPerSec);

    const float fastGrowth = smoothstep (0.0f, config_.fastGrowthDbPerSec, c.growthDbPerSec);
    t.progress += 1.0f + 2.0f * fastGrowth;
    const float persistence = std::min (1.0f, t.progress / persistenceFrames_);

    c.frequencySpreadPct = 100.0f * std::sqrt (t.freqVar) / std::max (1.0f, t.freqMean);

    const float narrow = smoothstep (kNarrowLowDb, kNarrowHighDb, c.pnprDb);
    const float isolated = smoothstep (kIsolatedLowDb, kIsolatedHighDb, c.phprDb);
    const float dominant = smoothstep (kDominantLowDb, kDominantHighDb, c.paprDb);

    // Several loop modes growing inside one bin beat against each other and make the frequency
    // estimate jitter. Sustained steep growth is itself strong evidence, so it excuses instability.
    const float runaway =
        smoothstep (0.5f * config_.fastGrowthDbPerSec, 1.5f * config_.fastGrowthDbPerSec, c.growthDbPerSec);
    const float stable = std::max (runaway, 1.0f - smoothstep (kStableLowPct, kStableHighPct, c.frequencySpreadPct));

    // Square root of the product: features near 1 barely lower it, but any one clearly
    // voice-like feature (score near 0) pulls it down hard. A fourth root (true geometric
    // mean) proved too lenient: one veto at 0.08 still left 0.39.
    c.confidence = persistence * std::sqrt (narrow * isolated * dominant * stable);

    if (c.confidence >= config_.detectThreshold && (c.detected || c.growthDbPerSec > -kMaxOnsetDecayDbPerSec))
        c.detected = true;
    else if (c.confidence < config_.releaseThreshold)
        c.detected = false;
}

void FeedbackDetector::releaseTrack (int slot) noexcept
{
    candidates_[static_cast<size_t> (slot)] = {};
    tracks_[static_cast<size_t> (slot)] = {};
}

float FeedbackDetector::growthDbPerSec (const Track& t) const noexcept
{
    const int n = t.historyCount;
    if (n < 4)
        return 0.0f;

    // Least-squares slope of level (dB) over the last n frames, oldest first.
    const int oldest = (t.historyPos - n + kHistory) % kHistory;
    const float xMean = 0.5f * static_cast<float> (n - 1);
    float yMean = 0.0f;
    for (int i = 0; i < n; ++i)
        yMean += t.levels[static_cast<size_t> ((oldest + i) % kHistory)];
    yMean /= static_cast<float> (n);

    float num = 0.0f, den = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float dx = static_cast<float> (i) - xMean;
        num += dx * (t.levels[static_cast<size_t> ((oldest + i) % kHistory)] - yMean);
        den += dx * dx;
    }
    return static_cast<float> (num / den * frameRate_);
}

} // namespace fwyc::dsp
