#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

namespace lr
{
// Real-time worker threads that join the audio device's workgroup and help the
// audio thread run independent racks in parallel.
//
// Dispatch uses one 64-bit atomic packing {epoch, count, nextIndex}, so a
// worker can only claim an index of the batch it woke up for. The audio thread
// works on the batch too and spins until every claimed index is finished.
class WorkerPool
{
public:
    WorkerPool();
    ~WorkerPool();

    // Message thread. Restarts the threads if already running.
    void start (int numWorkers, const juce::AudioWorkgroup&, int blockSize, double sampleRate);
    void stop();

    int getNumWorkers() const noexcept { return (int) workers.size(); }

    // Audio thread. Calls fn(i) for i in [0, n) using the workers + caller.
    template <typename Fn>
    void parallelFor (int n, Fn&& fn)
    {
        auto trampoline = [] (void* ctx, int i) { (*static_cast<std::remove_reference_t<Fn>*> (ctx)) (i); };
        run (n, trampoline, &fn);
    }

private:
    using TaskFn = void (*) (void*, int);

    static constexpr int indexBits = 20, countBits = 20;
    static constexpr uint64_t indexMask = (1ull << indexBits) - 1;
    static constexpr uint64_t countMask = (1ull << countBits) - 1;

    static uint32_t epochOf (uint64_t v) noexcept { return (uint32_t) (v >> (indexBits + countBits)); }
    static int      countOf (uint64_t v) noexcept { return (int) ((v >> indexBits) & countMask); }
    static int      indexOf (uint64_t v) noexcept { return (int) (v & indexMask); }

    void run (int n, TaskFn fn, void* ctx);
    void workOn (uint32_t epoch);

    class Worker;
    std::vector<std::unique_ptr<Worker>> workers;

    std::atomic<uint64_t> dispatch { 0 };
    std::atomic<int> remaining { 0 };
    std::atomic<bool> exiting { false };
    TaskFn taskFn = nullptr;
    void* taskCtx = nullptr;
    uint32_t epoch = 0;
};

} // namespace lr
