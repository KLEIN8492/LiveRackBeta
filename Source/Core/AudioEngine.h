#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "RenderPlan.h"
#include "WorkerPool.h"
#include "InflightLog.h"

namespace lr
{
// The audio device callback. Runs the current RenderPlan: routes device
// inputs into racks, runs each dependency level of racks in parallel, applies
// the watchdog, and sums racks into the device outputs.
//
// Plans are swapped with a single atomic pointer store. A replaced plan is kept
// alive until the audio thread has provably moved past it, then freed on the
// message thread by collectGarbage() — so plugins are never destroyed or
// deallocated on the audio thread.
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // ---- message thread ----
    void setPlan (std::unique_ptr<RenderPlan>);
    void clearPlan();                         // swap in an empty plan
    bool waitUntilOldPlansReleased (int timeoutMs);
    void collectGarbage();

    void setInflightLog (InflightLog* log) noexcept   { inflight = log; }
    void setGlobalBypass (bool b) noexcept            { globalBypass.store (b); }
    bool isGlobalBypassed() const noexcept            { return globalBypass.load(); }

    // Starts the real-time workers. Done automatically when a device starts;
    // public so tests can drive the engine without a device.
    void startWorkers (int numWorkers, const juce::AudioWorkgroup&, int blockSize, double sampleRate);

    double getCpuLoad() const                         { return loadMeasurer.getLoadAsProportion(); }
    bool isRunning() const noexcept                   { return running.load(); }

    WatchdogConfig watchdog;

    // ---- read by the stall detector (message thread) ----
    std::atomic<uint64_t> heartbeat  { 0 };
    std::atomic<bool>     inCallback { false };

    // ---- juce::AudioIODeviceCallback ----
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    struct Io
    {
        const float* const* inputs;
        int numInputs;
        int offset;
        int numSamples;
    };

    void renderChunk (RenderPlan&, const Io&, float* const* outputs, int numOutputs);
    void processRack (RenderPlan&, RackJob&, const Io&);
    void gatherInput (RenderPlan&, RackJob&, const Io&);
    void runSlots (RackJob&, int numSamples, double sampleRate);

    std::atomic<RenderPlan*> current { nullptr };
    std::atomic<uint64_t> seenGeneration { 0 };
    std::atomic<bool> running { false };
    std::atomic<bool> globalBypass { false };

    std::unique_ptr<RenderPlan> live;          // owned by the message thread
    struct Retired { std::unique_ptr<RenderPlan> plan; uint64_t safeAfter; };
    std::vector<Retired> retired;
    uint64_t nextGeneration = 1;

    WorkerPool pool;
    InflightLog* inflight = nullptr;
    juce::AudioProcessLoadMeasurer loadMeasurer;
};

} // namespace lr
