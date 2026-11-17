#pragma once

#include "dsp/fft.h"

#include <array>
#include <complex>
#include <vector>

// Detects developing acoustic feedback in a mono microphone signal.
//
// Short-window FFT analysis runs every hop. Spectral peaks are tracked across frames
// and scored on features that separate howl from voice:
//
//   narrowness   peak-to-neighbouring power ratio (PNPR)
//   isolation    peak-to-harmonic power ratio (PHPR): howl is a lone sinusoid, voices are harmonic
//   dominance    peak-to-average power ratio (PAPR): weak overtones don't qualify
//   stability    frequency spread over time: vibrato and pitch drift disqualify
//   persistence  how long the peak has been tracked; fast growth accelerates it
//
// Real-time contract: prepare() allocates; reset() and process() do not.
// process() only observes the signal; it never modifies or delays audio.

namespace fwyc::dsp {

struct FeedbackDetectorConfig
{
    // Analysis window length. FFT size is the next power of two at the current sample rate
    // (512 at 44.1/48 kHz, 1024 at 96 kHz), and hop = fftSize / hopDivisor.
    double windowSeconds = 512.0 / 48000.0;
    int hopDivisor = 4;

    float minFrequencyHz = 100.0f;
    float maxFrequencyHz = 12000.0f;

    float minLevelDb = -80.0f;      // dBFS (full-scale sine = 0 dB); quieter peaks are ignored
    float minProminenceDb = 15.0f;  // above the median spectral floor

    float persistenceSeconds = 0.064f; // tracking time needed for a steady peak to reach full confidence
    float fastGrowthDbPerSec = 60.0f;  // growth at which persistence accumulates 3x faster

    float detectThreshold = 0.6f;
    float releaseThreshold = 0.4f;
};

struct FeedbackCandidate
{
    bool active {false};   // slot is tracking a peak
    bool detected {false}; // confidence crossed detectThreshold (held until releaseThreshold)

    float frequencyHz {0.0f};
    float levelDb {-200.0f};
    float confidence {0.0f};
    float growthDbPerSec {0.0f};

    // Smoothed features, exposed for tuning and diagnostics.
    float pnprDb {0.0f};
    float phprDb {0.0f};
    float paprDb {0.0f};
    float frequencySpreadPct {0.0f};
};

class FeedbackDetector
{
public:
    static constexpr int kMaxCandidates = 16;

    explicit FeedbackDetector (FeedbackDetectorConfig config = {}) : config_ (config) {}

    void prepare (double sampleRate);
    void reset () noexcept;
    void process (const float* in, int numSamples) noexcept;

    const std::array<FeedbackCandidate, kMaxCandidates>& candidates () const noexcept;

    const FeedbackDetectorConfig& config () const noexcept { return config_; }
    int fftSize () const noexcept { return fftSize_; }
    int hopSize () const noexcept { return hopSize_; }
    double frameRate () const noexcept { return frameRate_; }

private:
    static constexpr int kMaxPeaksPerFrame = 24;
    static constexpr int kHistory = 16; // frames of level history for growth estimation

    struct Peak
    {
        float frequencyHz;
        float levelDb;
        float pnprDb;
        float phprDb;
        float paprDb;
    };

    struct Track
    {
        float freqSmooth;
        float freqMean;
        float freqVar;
        std::array<float, kHistory> levels;
        int historyCount;
        int historyPos;
        float progress;
        int age; // frames tracked
        int misses;
        bool matched;
    };

    void analyseFrame () noexcept;
    int findPeaks () noexcept;
    Peak measurePeak (int bin, float meanPowerDb) const noexcept;
    float harmonicLevelDb (float exactBin) const noexcept;
    void updateTracks (int numPeaks) noexcept;
    void updateTrack (int slot, const Peak& peak) noexcept;
    void releaseTrack (int slot) noexcept;
    float growthDbPerSec (const Track& track) const noexcept;

    FeedbackDetectorConfig config_;

    double sampleRate_ {48000.0};
    int fftSize_ {0};
    int hopSize_ {0};
    double frameRate_ {0.0};
    float binHz_ {0.0f};
    float normDb_ {0.0f};
    // Per-frame EMA coefficients (see the time constants in the .cpp).
    float riseCoeff_ {0.0f};
    float fallCoeff_ {0.0f};
    float freqSmoothCoeff_ {0.0f};
    float spreadCoeff_ {0.0f};
    float persistenceFrames_ {1.0f};
    int settleFrames_ {1}; // frames for one full analysis window
    int maxMisses_ {2};    // frames a track survives without a matching peak
    int minBin_ {1};
    int maxBin_ {1};

    Fft fft_;
    std::vector<float> window_;
    std::vector<float> ring_;
    int ringPos_ {0};
    int samplesUntilFrame_ {0};

    std::vector<std::complex<float>> spectrum_;
    std::vector<float> specDb_;
    std::vector<float> scratch_;
    std::array<Peak, kMaxPeaksPerFrame> peaks_ {};

    std::array<Track, kMaxCandidates> tracks_ {};
    std::array<FeedbackCandidate, kMaxCandidates> candidates_ {};
};

} // namespace fwyc::dsp
