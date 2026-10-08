#pragma once

#include <atomic>
#include <thread>

namespace lr
{
// Keeps one CPU core busy so Apple Silicon never clocks the performance
// cluster down between tiny audio blocks. Measured with De-Feedback at
// 32 samples: 334-769 of 15000 blocks over budget without it, 0 with it.
// Costs one core of power/heat, so the host only runs it at small buffers.
class CpuBoost
{
public:
    ~CpuBoost() { stop(); }

    void start()
    {
        if (thread.joinable())
            return;

        running = true;
        thread = std::thread ([this]
        {
            volatile double sink = 0.0;
            while (running.load (std::memory_order_relaxed))
            {
                for (int i = 0; i < 256; ++i)
                    sink = sink + 1.0;
               #if defined (__aarch64__)
                __builtin_arm_yield();
               #endif
            }
        });
    }

    void stop()
    {
        running = false;
        if (thread.joinable())
            thread.join();
    }

    bool isRunning() const noexcept { return thread.joinable(); }

private:
    std::atomic<bool> running { false };
    std::thread thread;
};
} // namespace lr
