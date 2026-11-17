// Real-time contract: process() never allocates and runs well inside real time,
// across sample rates and host block sizes.

#include "dsp/feedback_suppressor.h"
#include "test_signals.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

// Count every heap allocation made while g_tracking is set.
namespace {
std::atomic<bool> g_tracking {false};
std::atomic<int> g_allocations {0};
} // namespace

void* operator new (std::size_t size)
{
    if (g_tracking.load (std::memory_order_relaxed))
        g_allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;
    throw std::bad_alloc ();
}

void* operator new[] (std::size_t size)
{
    return operator new (size);
}

void operator delete (void* p) noexcept
{
    std::free (p);
}

void operator delete[] (void* p) noexcept
{
    std::free (p);
}

void operator delete (void* p, std::size_t) noexcept
{
    std::free (p);
}

void operator delete[] (void* p, std::size_t) noexcept
{
    std::free (p);
}

using namespace fwyc::test;

int main ()
{
    Checker check;
    constexpr double kSeconds = 5.0;
    constexpr double kMaxRealTimeFraction = 0.25; // generous: also has to pass unoptimised Debug builds

    for (double fs : {44100.0, 48000.0, 96000.0})
    {
        // Busy input: voice, growing howl and noise keep many peaks tracked.
        const int n = static_cast<int> (fs * kSeconds);
        Signal input = harmonicNote (fs, n, {220.0, 12, -18.0f, 1.0f});
        input += growingTone (fs, n, 2500.0, -60.0f, 30.0f);
        input += whiteNoise (n, -50.0f);
        Signal output (input.size ());

        for (int block : {16, 64, 512})
        {
            fwyc::dsp::FeedbackSuppressor engine;
            engine.prepare (fs, block);

            g_allocations = 0;
            g_tracking = true;
            const auto start = std::chrono::steady_clock::now ();
            for (int pos = 0; pos < n; pos += block)
            {
                const int len = std::min (block, n - pos);
                engine.process (input.data () + pos, output.data () + pos, len);
            }
            const auto elapsed = std::chrono::steady_clock::now () - start;
            g_tracking = false;

            const double fraction = std::chrono::duration<double> (elapsed).count () / kSeconds;
            const int allocations = g_allocations.load ();
            std::printf ("  fs %6.0f, block %3d: %.3f%% of real time, %d allocations\n", fs, block, fraction * 100.0,
                         allocations);

            check.expect (allocations == 0, "no allocations in process()");
            check.expect (fraction < kMaxRealTimeFraction, "processing well inside real time");
        }
    }

    return check.finish ("realtime_performance");
}
