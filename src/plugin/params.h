#pragma once

#include <cmath>

// Normalized <-> engine value mappings, shared by the processor (applies them) and the
// controller (displays them). Defaults reproduce the engine's built-in tuning.

namespace fwyc::params {

// Sensitivity 0..100 %: higher detects feedback earlier and on less evidence.
// Detector threshold 0.8 (low) .. 0.4 (high); the release threshold keeps 0.2 of hysteresis.
inline float detectThreshold (double norm) { return static_cast<float> (0.8 - 0.4 * norm); }
inline float releaseThreshold (double norm) { return detectThreshold (norm) - 0.2f; }
constexpr double kSensitivityDefault = 0.5; // threshold 0.6

// Depth: deepest any notch may cut, 6..30 dB.
constexpr double kDepthMinDb = 6.0, kDepthMaxDb = 30.0;
inline float depthDb (double norm) { return static_cast<float> (kDepthMinDb + norm * (kDepthMaxDb - kDepthMinDb)); }
inline double depthNorm (double db) { return (db - kDepthMinDb) / (kDepthMaxDb - kDepthMinDb); }
constexpr double kDepthDefault = 0.5; // 18 dB

// Width: notch Q, log-mapped, turning up widens the notch (Q 40 .. 4).
constexpr double kQNarrow = 40.0, kQWide = 4.0;
inline float notchQ (double norm) { return static_cast<float> (kQNarrow * std::pow (kQWide / kQNarrow, norm)); }
inline double widthNorm (double q) { return std::log (q / kQNarrow) / std::log (kQWide / kQNarrow); }
constexpr double kWidthDefault = 0.30103; // Q 20

// Release: how long a notch holds after its feedback stops before easing off, log 0.5..30 s.
constexpr double kHoldMinSeconds = 0.5, kHoldMaxSeconds = 30.0;
inline float holdSeconds (double norm)
{
    return static_cast<float> (kHoldMinSeconds * std::pow (kHoldMaxSeconds / kHoldMinSeconds, norm));
}
inline double releaseNorm (double seconds)
{
    return std::log (seconds / kHoldMinSeconds) / std::log (kHoldMaxSeconds / kHoldMinSeconds);
}
constexpr double kReleaseDefault = 0.43762; // 3 s

// Mode: 0 = auto notch, 1 = detect only.
inline bool detectOnly (double norm) { return norm >= 0.5; }

} // namespace fwyc::params
