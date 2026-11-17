#pragma once

#include "dsp/feedback_detector.h"
#include "dsp/notch_filter.h"

#include <array>

// Bank of adaptive notch filters driven by the feedback detector.
//
// Policy, per slot:
//   engage   a detected peak claims a slot: cut quickly, deeper the faster the feedback is
//            growing (initialDepthDb..fastInitialDepthDb); glide to follow drift
//   deepen   every deepenIntervalSeconds while the feedback is still present and not decaying
//            (i.e. the notch isn't winning yet), cut deeper by deepenStepDb, up to maxDepthDb
//   hold     after the feedback stops, keep the depth for holdSeconds
//   release  then ease off at releaseDbPerSecond, down to the slot's floor
//   floor    every repeat episode at the same frequency raises a lasting shallow cut by
//            floorStepDb (up to maxFloorDb), which fades only very slowly: frequencies that keep
//            ringing stay tamed, while a one-off (a whistle, a tap) leaves nothing behind
//
// Real-time contract: no allocation; update() and process() are cheap enough to run per block.

namespace fwyc::dsp {

struct FilterBankConfig
{
    float q = 20.0f; // ~1/14 octave

    // First cut scales with how fast the feedback is growing: initialDepthDb at or below
    // slowGrowthDbPerSec, up to fastInitialDepthDb at fastGrowthDbPerSec and above.
    float initialDepthDb = 6.0f;
    float fastInitialDepthDb = 12.0f;
    float slowGrowthDbPerSec = 30.0f;
    float fastGrowthDbPerSec = 90.0f;

    float deepenStepDb = 3.0f;
    float deepenIntervalSeconds = 0.1f;
    float maxDepthDb = 18.0f;
    float notDecayingDbPerSec = -10.0f; // feedback fading slower than this means "not winning yet"

    float attackDbPerSecond = 300.0f;
    float holdSeconds = 3.0f;
    float releaseDbPerSecond = 2.0f;

    float floorStepDb = 1.5f;
    float maxFloorDb = 6.0f;
    float floorReleaseDbPerSecond = 0.02f;

    float glideSeconds = 0.05f;
    float matchTolerancePct = 2.0f;
    float minMatchToleranceHz = 40.0f;
};

struct NotchSlot
{
    bool inUse {false};
    bool feedbackPresent {false}; // a detection maps onto this slot right now
    float frequencyHz {0.0f};
    float depthDb {0.0f};       // current cut
    float targetDepthDb {0.0f};
    float floorDb {0.0f};
    int episodes {0};
};

class FilterBank
{
public:
    static constexpr int kMaxFilters = 8;

    explicit FilterBank (FilterBankConfig config = {}) : config_ (config) {}

    void prepare (double sampleRate);
    void reset () noexcept;

    // Advance the policy by numSamples using the detector's current candidates, then
    // refresh filter coefficients. Call once per (small) chunk, before process().
    void update (const std::array<FeedbackCandidate, FeedbackDetector::kMaxCandidates>& candidates,
                 int numSamples) noexcept;

    float processSample (float x) noexcept
    {
        for (int i = 0; i < numActive_; ++i)
            x = filters_[static_cast<size_t> (active_[static_cast<size_t> (i)])].process (x);
        return x;
    }

    const std::array<NotchSlot, kMaxFilters>& slots () const noexcept { return slots_; }
    const FilterBankConfig& config () const noexcept { return config_; }

private:
    struct SlotState
    {
        float targetFrequencyHz;
        float growthDbPerSec;
        float levelDb;
        float secondsSinceSeen;
        float secondsSinceDeepen;
        bool wasPresent;
    };

    int findSlot (float frequencyHz) const noexcept;
    int allocateSlot () noexcept;
    void advanceSlot (int index, float dt) noexcept;
    void freeSlot (int index) noexcept;

    FilterBankConfig config_;
    double sampleRate_ {48000.0};

    std::array<NotchSlot, kMaxFilters> slots_ {};
    std::array<SlotState, kMaxFilters> state_ {};
    std::array<NotchFilter, kMaxFilters> filters_ {};
    std::array<int, kMaxFilters> active_ {};
    int numActive_ {0};
};

} // namespace fwyc::dsp
