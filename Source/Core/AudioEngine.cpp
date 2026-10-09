#include "AudioEngine.h"
#include <sys/sysctl.h>

namespace lr
{
namespace
{
int performanceCoreCount()
{
    int count = 0;
    size_t size = sizeof (count);
    if (sysctlbyname ("hw.perflevel0.physicalcpu", &count, &size, nullptr, 0) == 0 && count > 0)
        return count;
    return juce::SystemStats::getNumPhysicalCpus();
}

double ticksToSeconds (juce::int64 ticks)
{
    return juce::Time::highResolutionTicksToSeconds (ticks);
}

// Adds `src` channels into `dst` channels, folding mono<->stereo.
void mixInto (juce::AudioBuffer<float>& dst, int dstChannels, const juce::AudioBuffer<float>& src, int srcChannels, int n)
{
    if (srcChannels == dstChannels)
    {
        for (int c = 0; c < dstChannels; ++c)
            dst.addFrom (c, 0, src, c, 0, n);
    }
    else if (srcChannels == 1)
    {
        for (int c = 0; c < dstChannels; ++c)
            dst.addFrom (c, 0, src, 0, 0, n);
    }
    else // stereo into mono
    {
        dst.addFrom (0, 0, src, 0, 0, n, 0.5f);
        dst.addFrom (0, 0, src, 1, 0, n, 0.5f);
    }
}
} // namespace

// A plugin that receives signal but outputs exact digital silence for two
// seconds is flagged (not bypassed: gates and mutes do this legitimately).
static void updateSilenceFlag (SlotRuntime& slot, const juce::AudioBuffer<float>& before,
                               const juce::AudioBuffer<float>& after, int channels, int n, double sampleRate)
{
    float in = 0.0f, out = 0.0f;
    for (int c = 0; c < channels; ++c)
    {
        in = juce::jmax (in, before.getMagnitude (c, 0, n));
        out = juce::jmax (out, after.getMagnitude (c, 0, n));
    }

    if (in > 0.001f && out == 0.0f)
        slot.silentSamples += n;
    else if (out > 0.0f)
        slot.silentSamples = 0;

    slot.silentOutput.store (slot.silentSamples > (int) (2.0 * sampleRate), std::memory_order_relaxed);
}

AudioEngine::AudioEngine()
{
    live = std::make_unique<RenderPlan>();
    live->generation = nextGeneration++;
    current.store (live.get());
}

AudioEngine::~AudioEngine()
{
    pool.stop();
}

// ---------------------------------------------------------------------------

void AudioEngine::setPlan (std::unique_ptr<RenderPlan> plan)
{
    jassert (juce::MessageManager::existsAndIsCurrentThread() || ! running.load());

    if (plan == nullptr)
        plan = std::make_unique<RenderPlan>();

    plan->generation = nextGeneration++;
    const auto newGeneration = plan->generation;

    auto old = std::move (live);
    live = std::move (plan);
    current.store (live.get(), std::memory_order_release);

    retired.push_back ({ std::move (old), newGeneration });
    collectGarbage();
}

void AudioEngine::clearPlan()
{
    setPlan (nullptr);
}

void AudioEngine::collectGarbage()
{
    const bool deviceIdle = ! running.load (std::memory_order_acquire);
    const auto seen = seenGeneration.load (std::memory_order_acquire);

    std::erase_if (retired, [&] (const Retired& r) { return deviceIdle || seen >= r.safeAfter; });
}

bool AudioEngine::waitUntilOldPlansReleased (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    for (;;)
    {
        collectGarbage();
        if (retired.empty())
            return true;
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        juce::Thread::sleep (1);
    }
}

void AudioEngine::startWorkers (int numWorkers, const juce::AudioWorkgroup& workgroup, int blockSize, double sampleRate)
{
    pool.start (numWorkers, workgroup, blockSize, sampleRate);
}

// ---------------------------------------------------------------------------

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto blockSize = device->getCurrentBufferSizeSamples();
    const auto sampleRate = device->getCurrentSampleRate();

    loadMeasurer.reset (sampleRate, blockSize);

    // Each device start brings a new workgroup: plugins with their own
    // processing threads must rejoin it to be scheduled as real-time.
    const auto workgroup = device->getWorkgroup();
    if (auto* plan = current.load (std::memory_order_acquire))
        for (auto& job : plan->jobs)
            for (auto& sj : job.slots)
                sj.slot->plugin->audioWorkgroupContextChanged (workgroup);
    // The host divides the performance cores between open interfaces.
    const int workers = juce::jmin (maxWorkers.load(), performanceCoreCount() - 1);
    startWorkers (juce::jmax (0, workers), device->getWorkgroup(), blockSize, sampleRate);
    running.store (true);
}

void AudioEngine::audioDeviceStopped()
{
    lastCallbackStart = 0;
    running.store (false);
    pool.stop();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outputs, int numOutputs,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, numSamples);

    // Flush denormals to zero for everything we and the plugins compute here:
    // decaying filters in near-silence otherwise get dramatically slower.
    const juce::ScopedNoDenormals noDenormals;

    const auto callbackStart = juce::Time::getHighResolutionTicks();
    auto raise = [] (std::atomic<uint32_t>& a, juce::int64 ticks)
    {
        const auto us = (uint32_t) (juce::Time::highResolutionTicksToSeconds (ticks) * 1.0e6);
        if (us > a.load (std::memory_order_relaxed))
            a.store (us, std::memory_order_relaxed);
    };
    if (lastCallbackStart != 0)
        raise (maxGapUs, callbackStart - lastCallbackStart);
    lastCallbackStart = callbackStart;
    lastNumSamples.store (numSamples, std::memory_order_relaxed);

    inCallback.store (true, std::memory_order_relaxed);
    heartbeat.fetch_add (1, std::memory_order_relaxed);

    auto* plan = current.load (std::memory_order_acquire);
    seenGeneration.store (plan->generation, std::memory_order_release);

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const bool hasWork = ! plan->jobs.empty() || ! plan->imports.empty() || ! plan->inputExports.empty();
    if (plan->maxBlockSize > 0 && hasWork)
    {
        for (int offset = 0; offset < numSamples; offset += plan->maxBlockSize)
        {
            const Io io { inputs, numInputs, offset, juce::jmin (plan->maxBlockSize, numSamples - offset) };
            renderChunk (*plan, io, outputs, numOutputs);
        }
    }

    raise (maxCallbackUs, juce::Time::getHighResolutionTicks() - callbackStart);
    inCallback.store (false, std::memory_order_relaxed);
}

void AudioEngine::renderChunk (RenderPlan& plan, const Io& io, float* const* outputs, int numOutputs)
{
    const int n = io.numSamples;

    // 1. Send this interface's inputs that racks elsewhere need.
    for (auto& ex : plan.inputExports)
    {
        for (size_t k = 0; k < ex.deviceChannels.size(); ++k)
        {
            const int ch = ex.deviceChannels[k];
            ex.ptrs[k] = ch < io.numInputs && io.inputs[ch] != nullptr ? io.inputs[ch] + io.offset : nullptr;
        }
        ex.bridge->write (ex.ptrs.data(), n);
    }

    // 2. Receive everything arriving from other interfaces.
    for (auto& im : plan.imports)
        im.bridge->read (im.buffer.getArrayOfWritePointers(), n);

    // 3. Racks, level by level, in parallel within a level.
    for (auto& level : plan.levels)
    {
        pool.parallelFor ((int) level.size(), [&] (int i)
        {
            processRack (plan, plan.jobs[(size_t) level[(size_t) i]], io);
        });
    }

    // 4. Outputs: local racks, then racks homed on other interfaces.
    auto addTo = [&] (int o, const float* src)
    {
        if (o < numOutputs && outputs[o] != nullptr)
            juce::FloatVectorOperations::add (outputs[o] + io.offset, src, n);
    };

    for (auto& job : plan.jobs)
        for (auto [o, rch] : job.localOutputs)
            addTo (o, job.buffer.getReadPointer (rch));

    for (auto& im : plan.imports)
        for (auto [o, bch] : im.toOutputs)
            addTo (o, im.buffer.getReadPointer (bch));

    // 5. Send rack outputs that other interfaces need.
    for (auto& ex : plan.rackExports)
    {
        const auto& job = plan.jobs[(size_t) ex.job];
        for (size_t c = 0; c < ex.ptrs.size(); ++c)
            ex.ptrs[c] = job.buffer.getReadPointer ((int) c);
        ex.bridge->write (ex.ptrs.data(), n);
    }
}

void AudioEngine::gatherInput (RenderPlan& plan, RackJob& job, const Io& io)
{
    auto& buffer = job.buffer;
    const int rc = job.spec.numChannels;
    const int n = io.numSamples;

    for (int c = 0; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    for (auto& tap : job.taps)
    {
        const float* src = nullptr;
        if (tap.fromImport)
            src = plan.imports[(size_t) tap.import].buffer.getReadPointer (tap.channel);
        else if (tap.channel < io.numInputs && io.inputs[tap.channel] != nullptr)
            src = io.inputs[tap.channel] + io.offset;

        if (src == nullptr)
            continue;

        if (tap.rackChannel < 0)
            for (int c = 0; c < rc; ++c)
                juce::FloatVectorOperations::add (buffer.getWritePointer (c), src, n);
        else
            juce::FloatVectorOperations::add (buffer.getWritePointer (tap.rackChannel), src, n);
    }

    for (int s : job.sourceJobs)
    {
        const auto& src = plan.jobs[(size_t) s];
        mixInto (buffer, rc, src.buffer, src.spec.numChannels, n);
    }

    for (int im : job.sourceImports)
    {
        const auto& src = plan.imports[(size_t) im].buffer;
        mixInto (buffer, rc, src, src.getNumChannels(), n);
    }
}

void AudioEngine::processRack (RenderPlan& plan, RackJob& job, const Io& io)
{
    const auto start = juce::Time::getHighResolutionTicks();
    auto& rt = *job.spec.runtime;
    const int rc = job.spec.numChannels;
    const int n = io.numSamples;

    gatherInput (plan, job, io);

    float inPeak = 0.0f;
    for (int c = 0; c < rc; ++c)
    {
        inPeak = juce::jmax (inPeak, job.buffer.getMagnitude (c, 0, n));
        job.dry.copyFrom (c, 0, job.buffer, c, 0, n);
    }
    RackRuntime::raisePeak (rt.inPeak, inPeak);

    const bool passThrough = job.spec.bypassed
                          || globalBypass.load (std::memory_order_relaxed)
                          || rt.dryFallback.load (std::memory_order_relaxed);

    if (! passThrough)
        runSlots (job, n, plan.sampleRate);

    const double elapsed = ticksToSeconds (juce::Time::getHighResolutionTicks() - start);
    rt.processMs.store ((float) (elapsed * 1000.0), std::memory_order_relaxed);

    if (! passThrough && checkRackTiming (rt.health, elapsed, n / plan.sampleRate, watchdog) == FaultReason::overtime)
    {
        const auto action = chooseFaultAction (job.spec.critical, FaultReason::overtime);

        if (action.rackToDry)
        {
            rt.dryFallback.store (true, std::memory_order_relaxed);
        }
        else if (action.bypassSlot)
        {
            SlotRuntime* slowest = nullptr;
            for (auto& sj : job.slots)
                if (sj.slot->isActive() && (slowest == nullptr || sj.slot->processMs.load() > slowest->processMs.load()))
                    slowest = sj.slot.get();

            if (slowest != nullptr)
                slowest->fault.store (FaultReason::overtime);
        }
    }

    if (rt.dryFallback.load (std::memory_order_relaxed))
        for (int c = 0; c < rc; ++c)
            job.buffer.copyFrom (c, 0, job.dry, c, 0, n);

    float outPeak = 0.0f;
    for (int c = 0; c < rc; ++c)
        outPeak = juce::jmax (outPeak, job.buffer.getMagnitude (c, 0, n));
    RackRuntime::raisePeak (rt.outPeak, outPeak);
}

void AudioEngine::runSlots (RackJob& job, int n, double sampleRate)
{
    auto& rt = *job.spec.runtime;
    const int rc = job.spec.numChannels;

    for (auto& sj : job.slots)
    {
        auto& slot = *sj.slot;
        if (! slot.isActive())
            continue;

        auto& plugin = *slot.plugin;
        if (slot.resetRequested.exchange (false, std::memory_order_relaxed))
            plugin.reset();

        // Extra plugin channels: a mono rack feeding a stereo plugin gets the
        // signal on both sides; anything beyond that (sidechains) is silent.
        for (int c = rc; c < sj.procChannels; ++c)
        {
            if (rc == 1 && c == 1)
                job.buffer.copyFrom (1, 0, job.buffer, 0, 0, n);
            else
                job.buffer.clear (c, 0, n);
        }

        for (int c = 0; c < rc; ++c)
            job.backup.copyFrom (c, 0, job.buffer, c, 0, n);

        juce::AudioBuffer<float> view (job.buffer.getArrayOfWritePointers(), sj.procChannels, n);
        job.midi.clear();

        if (inflight != nullptr) inflight->mark (slot.inflightRecord, true);
        slot.inProcess.store (true, std::memory_order_relaxed);
        const auto t0 = juce::Time::getHighResolutionTicks();

        plugin.processBlock (view, job.midi);

        slot.processMs.store ((float) (ticksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0),
                              std::memory_order_relaxed);
        slot.inProcess.store (false, std::memory_order_relaxed);
        if (inflight != nullptr) inflight->mark (slot.inflightRecord, false);

        // A stereo plugin on a mono rack: fold its output back to mono.
        if (rc == 1 && sj.procChannels >= 2 && plugin.getTotalNumOutputChannels() >= 2)
        {
            job.buffer.addFrom (0, 0, job.buffer, 1, 0, n);
            job.buffer.applyGain (0, 0, n, 0.5f);
        }

        updateSilenceFlag (slot, job.backup, job.buffer, rc, n, sampleRate);

        const auto verdict = checkSlotOutput (slot.health, job.buffer, rc, n, sampleRate, watchdog);
        if (verdict == FaultReason::none)
            continue;

        const auto action = chooseFaultAction (job.spec.critical, verdict);

        // Undo whatever the plugin did to this block.
        for (int c = 0; c < rc; ++c)
            job.buffer.copyFrom (c, 0, job.backup, c, 0, n);

        if (action.bypassSlot)
            slot.fault.store (verdict, std::memory_order_relaxed);

        if (action.rackToDry)
        {
            rt.dryFallback.store (true, std::memory_order_relaxed);
            return;
        }
    }
}

} // namespace lr
