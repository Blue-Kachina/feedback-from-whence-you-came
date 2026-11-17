#pragma once

#include "dsp/filter_bank.h"

// Last-resort broadband gain reduction, for feedback the notches alone aren't stopping.
//
// Policy:
//   trigger  feedback at the mic is at least minFeedbackDb and the bank overflows (more
//            feedback than notches) or a notch at full depth is losing; or detected feedback is
//            still loud after its notch (severeOutputDb) and growing
//   engage   cut initialReductionDb at once (ramped at attackDbPerSecond, so no click), then
//            stepDb more every stepIntervalSeconds while still triggered, up to maxReductionDb
//   release  holdSeconds after the trigger clears, ease back at releaseDbPerSecond
//
// At 0 dB reduction the stage is an exact identity.
//
// Real-time contract: no allocation; update() runs per chunk, processSample() per sample.

namespace fwyc::dsp {

struct EmergencyGainConfig
{
    bool enabled = true;
    // Overflow and losing notches only count once feedback at the mic is this loud: while it
    // is quieter the notches still have time to adapt, and cutting everything would only hide
    // the howl from them until the gain came back.
    float minFeedbackDb = -30.0f;
    float severeOutputDb = -12.0f;
    float initialReductionDb = 6.0f;
    float stepDb = 3.0f;
    float stepIntervalSeconds = 0.1f;
    float maxReductionDb = 12.0f;
    float attackDbPerSecond = 200.0f;
    float holdSeconds = 1.0f;
    float releaseDbPerSecond = 3.0f;
};

class EmergencyGain
{
public:
    explicit EmergencyGain (EmergencyGainConfig config = {}) : config_ (config) {}

    void prepare (double sampleRate);
    void reset () noexcept;

    // Advance by numSamples given the bank's status; ramps the gain across the next numSamples.
    void update (const FilterBankStatus& status, int numSamples) noexcept;

    float processSample (float x) noexcept
    {
        if (remaining_ > 0)
        {
            gain_ += step_;
            if (--remaining_ == 0)
                gain_ = endGain_;
        }
        return gain_ == 1.0f ? x : x * gain_;
    }

    void setEnabled (bool enabled) noexcept { config_.enabled = enabled; }
    bool triggered () const noexcept { return triggered_; }
    float reductionDb () const noexcept { return reductionDb_; }
    const EmergencyGainConfig& config () const noexcept { return config_; }

private:
    EmergencyGainConfig config_;
    double sampleRate_ {48000.0};

    bool triggered_ {false};
    float targetDb_ {0.0f};
    float reductionDb_ {0.0f};
    float secondsSinceStep_ {0.0f};
    float secondsSinceTrigger_ {0.0f};

    float gain_ {1.0f};
    float endGain_ {1.0f};
    float step_ {0.0f};
    int remaining_ {0};
};

} // namespace fwyc::dsp
