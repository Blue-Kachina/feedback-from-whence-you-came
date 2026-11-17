#include "dsp/filter_bank.h"

#include <algorithm>
#include <cmath>

namespace fwyc::dsp {

void FilterBank::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    reset ();
}

void FilterBank::reset () noexcept
{
    slots_ = {};
    state_ = {};
    for (auto& f : filters_)
        f.reset ();
    numActive_ = 0;
    status_ = {};
    budgetVictim_ = -1;
    recovering_ = false;
    secondsSinceAnyFeedback_ = 0.0f;
}

void FilterBank::update (const std::array<FeedbackCandidate, FeedbackDetector::kMaxCandidates>& candidates,
                         int numSamples) noexcept
{
    const float dt = static_cast<float> (numSamples / sampleRate_);

    for (auto& s : slots_)
        s.feedbackPresent = false;

    // Strongest detections claim slots first.
    std::array<int, FeedbackDetector::kMaxCandidates> order {};
    int count = 0;
    for (int i = 0; i < FeedbackDetector::kMaxCandidates; ++i)
        if (candidates[static_cast<size_t> (i)].detected)
            order[static_cast<size_t> (count++)] = i;
    std::sort (order.begin (), order.begin () + count, [&] (int a, int b) {
        return candidates[static_cast<size_t> (a)].levelDb > candidates[static_cast<size_t> (b)].levelDb;
    });

    status_ = {};
    for (int i = 0; i < count; ++i)
    {
        const auto& c = candidates[static_cast<size_t> (order[static_cast<size_t> (i)])];
        int index = findSlot (c.frequencyHz);
        if (index < 0)
            index = allocateSlot ();
        if (index < 0)
        {
            status_.overflow = true; // every slot is fighting active feedback
            continue;
        }

        auto& slot = slots_[static_cast<size_t> (index)];
        auto& st = state_[static_cast<size_t> (index)];
        if (!slot.inUse)
        {
            slot.inUse = true;
            slot.frequencyHz = c.frequencyHz;
        }
        if (!slot.feedbackPresent)
        {
            st.targetFrequencyHz = c.frequencyHz;
            st.growthDbPerSec = c.growthDbPerSec;
            st.levelDb = c.levelDb;
            slot.levelDb = c.levelDb;
            slot.feedbackPresent = true;
        }
    }

    if (count > 0)
        secondsSinceAnyFeedback_ = 0.0f;
    else if (!holdsPaused_)
        secondsSinceAnyFeedback_ += dt;
    recovering_ = secondsSinceAnyFeedback_ >= config_.recoverAfterSeconds;
    // Over budget, the idle notch that has gone longest without feedback gives way first.
    // Idle doesn't mean unneeded (a notch that is working keeps its feedback away), so only
    // one at a time.
    float totalDepth = 0.0f;
    budgetVictim_ = -1;
    for (int i = 0; i < kMaxFilters; ++i)
    {
        const auto& s = slots_[static_cast<size_t> (i)];
        totalDepth += s.depthDb;
        if (s.inUse && !s.feedbackPresent &&
            (budgetVictim_ < 0 || state_[static_cast<size_t> (i)].secondsSinceSeen >
                                      state_[static_cast<size_t> (budgetVictim_)].secondsSinceSeen))
            budgetVictim_ = i;
    }
    if (totalDepth <= config_.maxTotalDepthDb)
        budgetVictim_ = -1;

    numActive_ = 0;
    for (int i = 0; i < kMaxFilters; ++i)
    {
        if (!slots_[static_cast<size_t> (i)].inUse)
            continue;

        advanceSlot (i, dt);

        const auto& slot = slots_[static_cast<size_t> (i)];
        const auto& st = state_[static_cast<size_t> (i)];
        status_.totalDepthDb += slot.depthDb;
        if (slot.feedbackPresent)
        {
            status_.losing = status_.losing || st.secondsLosing >= config_.losingSeconds;
            status_.loudestLevelDb = std::max (status_.loudestLevelDb, st.levelDb);
            const float outputDb = st.levelDb - slot.depthDb;
            if (outputDb > status_.loudestOutputDb)
            {
                status_.loudestOutputDb = outputDb;
                status_.loudestGrowthDbPerSec = st.growthDbPerSec;
            }
        }

        auto& filter = filters_[static_cast<size_t> (i)];
        if (slot.inUse && slot.depthDb > 0.0f)
        {
            filter.setParameters (sampleRate_, slot.frequencyHz, slot.depthDb, config_.q);
            filter.flushDenormals ();
            active_[static_cast<size_t> (numActive_++)] = i;
        }
        else
        {
            filter.reset (); // idle at 0 dB: bypassed entirely, restarts clean
        }
    }
}

void FilterBank::escalate () noexcept
{
    for (int i = 0; i < kMaxFilters; ++i)
    {
        auto& s = slots_[static_cast<size_t> (i)];
        if (s.inUse && (s.feedbackPresent || state_[static_cast<size_t> (i)].secondsSinceSeen < config_.holdSeconds))
            s.targetDepthDb = config_.maxDepthDb;
    }
}

int FilterBank::findSlot (float frequencyHz) const noexcept
{
    const float tolerance =
        std::max (frequencyHz * config_.matchTolerancePct / 100.0f, config_.minMatchToleranceHz);

    int best = -1;
    float bestDistance = tolerance;
    for (int i = 0; i < kMaxFilters; ++i)
    {
        const auto& s = slots_[static_cast<size_t> (i)];
        if (!s.inUse)
            continue;
        const float distance = std::abs (s.frequencyHz - frequencyHz);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

int FilterBank::allocateSlot () noexcept
{
    for (int i = 0; i < kMaxFilters; ++i)
        if (!slots_[static_cast<size_t> (i)].inUse)
            return i;

    // Steal the shallowest slot whose feedback has stopped; never one still fighting feedback.
    int victim = -1;
    for (int i = 0; i < kMaxFilters; ++i)
    {
        const auto& s = slots_[static_cast<size_t> (i)];
        if (!s.feedbackPresent && (victim < 0 || s.depthDb < slots_[static_cast<size_t> (victim)].depthDb))
            victim = i;
    }
    if (victim >= 0)
        freeSlot (victim);
    return victim;
}

void FilterBank::advanceSlot (int index, float dt) noexcept
{
    auto& s = slots_[static_cast<size_t> (index)];
    auto& st = state_[static_cast<size_t> (index)];

    if (s.feedbackPresent)
    {
        if (!st.wasPresent)
        {
            // New episode. Repeat offenders earn a lasting floor.
            ++s.episodes;
            float otherFloors = 0.0f;
            for (int i = 0; i < kMaxFilters; ++i)
                if (i != index)
                    otherFloors += slots_[static_cast<size_t> (i)].floorDb;
            const float floor = std::min ({config_.maxFloorDb, static_cast<float> (s.episodes - 1) * config_.floorStepDb,
                                           config_.maxTotalFloorDb - otherFloors});
            s.floorDb = std::max (s.floorDb, floor);

            const float severity = std::clamp ((st.growthDbPerSec - config_.slowGrowthDbPerSec) /
                                                   (config_.fastGrowthDbPerSec - config_.slowGrowthDbPerSec),
                                               0.0f, 1.0f);
            const float initial =
                config_.initialDepthDb + severity * (config_.fastInitialDepthDb - config_.initialDepthDb);
            s.targetDepthDb = std::max (s.targetDepthDb, initial);
            st.secondsSinceDeepen = 0.0f;
        }

        st.secondsSinceSeen = 0.0f;
        st.secondsSinceDeepen += dt;
        if (st.secondsSinceDeepen >= config_.deepenIntervalSeconds)
        {
            st.secondsSinceDeepen = 0.0f;
            if (st.growthDbPerSec > config_.notDecayingDbPerSec)
                s.targetDepthDb = std::min (config_.maxDepthDb, s.targetDepthDb + config_.deepenStepDb);
        }

        const float glide = 1.0f - std::exp (-dt / config_.glideSeconds);
        s.frequencyHz += glide * (st.targetFrequencyHz - s.frequencyHz);

        const bool atFullDepth = s.depthDb >= config_.maxDepthDb - 0.01f;
        const bool growing = st.growthDbPerSec > config_.losingGrowthDbPerSec;
        st.secondsLosing = atFullDepth && growing ? st.secondsLosing + dt : 0.0f;
    }
    else
    {
        if (!holdsPaused_)
            st.secondsSinceSeen += dt;
        st.secondsLosing = 0.0f;
        s.floorDb = std::max (0.0f, s.floorDb - config_.floorReleaseDbPerSecond * dt);
        if (index == budgetVictim_)
        {
            s.targetDepthDb = std::max (0.0f, s.targetDepthDb - config_.budgetReleaseDbPerSecond * dt);
            s.floorDb = std::min (s.floorDb, s.targetDepthDb);
        }
        else if (st.secondsSinceSeen >= config_.holdSeconds)
        {
            const float rate = recovering_ ? config_.recoveryReleaseDbPerSecond : config_.releaseDbPerSecond;
            s.targetDepthDb = std::max (s.floorDb, s.targetDepthDb - rate * dt);
        }
    }
    st.wasPresent = s.feedbackPresent;
    s.secondsEngaged += dt;

    // The depth limit is a user control and may have been lowered.
    s.targetDepthDb = std::min (s.targetDepthDb, config_.maxDepthDb);
    s.floorDb = std::min (s.floorDb, config_.maxDepthDb);

    // The target already ramps down at the release rate in force; the depth follows it.
    if (s.depthDb < s.targetDepthDb)
        s.depthDb = std::min (s.targetDepthDb, s.depthDb + config_.attackDbPerSecond * dt);
    else
        s.depthDb = s.targetDepthDb;

    const bool released = st.secondsSinceSeen >= config_.holdSeconds || index == budgetVictim_;
    if (!s.feedbackPresent && released && s.depthDb <= 0.0f && s.floorDb <= 0.0f)
        freeSlot (index);
}

void FilterBank::freeSlot (int index) noexcept
{
    slots_[static_cast<size_t> (index)] = {};
    state_[static_cast<size_t> (index)] = {};
    filters_[static_cast<size_t> (index)].reset ();
}

} // namespace fwyc::dsp
