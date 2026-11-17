#include "dsp/emergency_gain.h"

#include <algorithm>
#include <cmath>

namespace fwyc::dsp {

void EmergencyGain::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    reset ();
}

void EmergencyGain::reset () noexcept
{
    triggered_ = false;
    targetDb_ = 0.0f;
    reductionDb_ = 0.0f;
    secondsSinceStep_ = 0.0f;
    secondsSinceTrigger_ = 0.0f;
    gain_ = endGain_ = 1.0f;
    step_ = 0.0f;
    remaining_ = 0;
}

void EmergencyGain::update (const FilterBankStatus& status, int numSamples) noexcept
{
    const float dt = static_cast<float> (numSamples / sampleRate_);

    const bool severe = status.loudestOutputDb >= config_.severeOutputDb && status.loudestGrowthDbPerSec > 0.0f;
    const bool overwhelmed = (status.overflow || status.losing) && status.loudestLevelDb >= config_.minFeedbackDb;
    const bool trigger = config_.enabled && (overwhelmed || severe);

    if (trigger)
    {
        if (!triggered_)
        {
            targetDb_ = std::max (targetDb_, config_.initialReductionDb);
            secondsSinceStep_ = 0.0f;
        }
        else if ((secondsSinceStep_ += dt) >= config_.stepIntervalSeconds)
        {
            secondsSinceStep_ = 0.0f;
            targetDb_ = std::min (config_.maxReductionDb, targetDb_ + config_.stepDb);
        }
        secondsSinceTrigger_ = 0.0f;
    }
    else
    {
        secondsSinceTrigger_ += dt;
        if (secondsSinceTrigger_ >= config_.holdSeconds || !config_.enabled)
            targetDb_ = std::max (0.0f, targetDb_ - config_.releaseDbPerSecond * dt);
    }
    triggered_ = trigger;

    // The target ramps down at the release rate; the attack is limited here.
    reductionDb_ = targetDb_ > reductionDb_ ? std::min (targetDb_, reductionDb_ + config_.attackDbPerSecond * dt)
                                            : targetDb_;

    // Ramp linearly to the new gain across this chunk. 0 dB gives exactly 1.
    gain_ = endGain_;
    endGain_ = reductionDb_ > 0.0f ? std::pow (10.0f, -reductionDb_ / 20.0f) : 1.0f;
    remaining_ = endGain_ == gain_ ? 0 : std::max (1, numSamples);
    step_ = remaining_ > 0 ? (endGain_ - gain_) / static_cast<float> (remaining_) : 0.0f;
}

} // namespace fwyc::dsp
