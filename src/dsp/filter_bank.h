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
// Across the bank:
//   budget   notches add up to colour the voice: while the total cut exceeds maxTotalDepthDb,
//            the idle notch that has gone longest without feedback skips its hold and releases
//            fast, floor included; floors together never exceed maxTotalFloorDb
//   recovery once nothing has fed back anywhere for recoverAfterSeconds (the mic was moved
//            away from the speaker), idle notches release faster, down to their floors
//   status   reports when notches alone aren't coping, for the emergency gain stage
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

    float maxTotalDepthDb = 60.0f;
    float maxTotalFloorDb = 12.0f;
    float budgetReleaseDbPerSecond = 36.0f; // one full-depth notch every 0.5 s

    float recoverAfterSeconds = 5.0f;
    float recoveryReleaseDbPerSecond = 6.0f;

    // A notch at maxDepthDb whose feedback is still growing faster than losingGrowthDbPerSec
    // after losingSeconds is losing. Growing, not merely "not decaying": a held note wrongly
    // taken for feedback doesn't decay under a notch either, and mustn't duck the singer.
    float losingSeconds = 0.3f;
    float losingGrowthDbPerSec = 10.0f;
};

// How the bank is coping, after the latest update().
struct FilterBankStatus
{
    bool overflow {false}; // a detection found no free slot: every notch is fighting feedback
    bool losing {false};   // a notch is at full depth and its feedback still isn't decaying
    float loudestLevelDb {-200.0f};  // loudest detected feedback at the input (the mic)
    float loudestOutputDb {-200.0f}; // loudest detected feedback after its notch's cut
    float loudestGrowthDbPerSec {0.0f};
    float totalDepthDb {0.0f};
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
    float levelDb {-200.0f};       // feedback level at the mic when last detected
    float secondsEngaged {0.0f};   // since the slot was claimed
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

    // The emergency gain stage has had to step in: the notches weren't enough. Every notch
    // that has seen feedback within its hold goes to full depth, so that the howl doesn't
    // come straight back when the emergency gain releases.
    void escalate () noexcept;

    // While the emergency gain is cutting, nobody can tell whether a quiet notch is still
    // needed: holds don't count down (call before update()).
    void pauseHolds (bool paused) noexcept { holdsPaused_ = paused; }

    // User controls, safe to change between update() calls. Lowering the depth limit pulls
    // deeper notches up to it at once.
    void setQ (float q) noexcept { config_.q = q; }
    void setMaxDepthDb (float depthDb) noexcept { config_.maxDepthDb = depthDb; }
    void setHoldSeconds (float seconds) noexcept { config_.holdSeconds = seconds; }

    float processSample (float x) noexcept
    {
        for (int i = 0; i < numActive_; ++i)
            x = filters_[static_cast<size_t> (active_[static_cast<size_t> (i)])].process (x);
        return x;
    }

    const std::array<NotchSlot, kMaxFilters>& slots () const noexcept { return slots_; }
    const FilterBankStatus& status () const noexcept { return status_; }
    const FilterBankConfig& config () const noexcept { return config_; }

private:
    struct SlotState
    {
        float targetFrequencyHz;
        float growthDbPerSec;
        float levelDb;
        float secondsSinceSeen;
        float secondsSinceDeepen;
        float secondsLosing;
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

    FilterBankStatus status_ {};
    int budgetVictim_ {-1}; // idle slot releasing early because the bank is over budget
    bool recovering_ {false};
    bool holdsPaused_ {false};
    float secondsSinceAnyFeedback_ {0.0f};
};

} // namespace fwyc::dsp
