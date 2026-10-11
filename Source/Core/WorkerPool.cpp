#include "WorkerPool.h"
#include "Platform/Platform.h"

namespace lr
{
class WorkerPool::Worker final : public juce::Thread
{
public:
    Worker (WorkerPool& p, juce::AudioWorkgroup wg, int index)
        : juce::Thread ("LiveRack worker " + juce::String (index)), pool (p), workgroup (std::move (wg)) {}

    void run() override
    {
        juce::WorkgroupToken token;
        workgroup.join (token);
        platform::prepareRealtimeWorker();
        juce::FloatVectorOperations::disableDenormalisedNumberSupport (true);

        uint32_t lastEpoch = epochOf (pool.dispatch.load (std::memory_order_acquire));

        while (! pool.exiting.load (std::memory_order_acquire))
        {
            const auto v = pool.dispatch.load (std::memory_order_acquire);

            if (epochOf (v) == lastEpoch)
            {
                pool.dispatch.wait (v, std::memory_order_acquire);
                continue;
            }

            lastEpoch = epochOf (v);
            pool.workOn (lastEpoch);
        }
    }

private:
    WorkerPool& pool;
    juce::AudioWorkgroup workgroup;
};

WorkerPool::WorkerPool() = default;
WorkerPool::~WorkerPool() { stop(); }

void WorkerPool::start (int numWorkers, const juce::AudioWorkgroup& workgroup, int blockSize, double sampleRate)
{
    stop();
    exiting.store (false);

    const auto options = juce::Thread::RealtimeOptions{}
                             .withApproximateAudioProcessingTime (juce::jmax (16, blockSize), sampleRate);

    for (int i = 0; i < numWorkers; ++i)
    {
        auto w = std::make_unique<Worker> (*this, workgroup, i);
        if (! w->startRealtimeThread (options))
            w->startThread (juce::Thread::Priority::highest);
        workers.push_back (std::move (w));
    }
}

void WorkerPool::stop()
{
    if (workers.empty())
        return;

    exiting.store (true, std::memory_order_release);
    dispatch.fetch_add (1ull << (indexBits + countBits), std::memory_order_release); // new epoch, count 0
    dispatch.notify_all();

    for (auto& w : workers)
        w->stopThread (2000);

    workers.clear();
}

void WorkerPool::run (int n, TaskFn fn, void* ctx)
{
    if (n <= 0)
        return;

    // Wake only the helpers this batch can use: the caller takes one item.
    const int wake = juce::jmin (n - 1, getActiveWorkers());

    if (wake <= 0)
    {
        for (int i = 0; i < n; ++i)
            fn (ctx, i);
        return;
    }

    // Safe to overwrite: the previous batch fully finished before we returned.
    taskFn = fn;
    taskCtx = ctx;
    remaining.store (n, std::memory_order_relaxed);

    epoch = (epochOf (dispatch.load (std::memory_order_relaxed)) + 1) & 0xffffffu; // 24-bit epoch
    const uint64_t packed = ((uint64_t) epoch << (indexBits + countBits)) | ((uint64_t) n << indexBits);
    dispatch.store (packed, std::memory_order_release);
    for (int i = 0; i < wake; ++i)
        dispatch.notify_one();

    workOn (epoch);

    while (remaining.load (std::memory_order_acquire) > 0)
        platform::cpuRelax();
}

void WorkerPool::workOn (uint32_t batchEpoch)
{
    for (;;)
    {
        auto v = dispatch.load (std::memory_order_acquire);

        if (epochOf (v) != batchEpoch || indexOf (v) >= countOf (v))
            return;

        if (! dispatch.compare_exchange_weak (v, v + 1, std::memory_order_acq_rel))
            continue;

        // The batch can't move on while this claimed index is unfinished,
        // so taskFn/taskCtx are still the ones for batchEpoch.
        taskFn (taskCtx, indexOf (v));
        remaining.fetch_sub (1, std::memory_order_acq_rel);
    }
}

} // namespace lr
