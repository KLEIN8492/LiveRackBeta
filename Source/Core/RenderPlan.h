#pragma once

#include "Runtime.h"
#include "PortRef.h"
#include "ClockBridge.h"
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace lr
{
// ---- Input to the builder (plain data, easy to construct in tests) ----------

struct RackSpec
{
    juce::String id;
    int numChannels = 1;                // 1 = mono, 2 = stereo
    std::vector<PortRef> inputs;        // channels of any interface
    std::vector<juce::String> sources;  // rack ids summed into this rack (bus)
    std::vector<PortRef> outputs;       // channels of any interface
    bool critical = false;
    bool bypassed = false;
    std::shared_ptr<RackRuntime> runtime;
    std::vector<std::shared_ptr<SlotRuntime>> slots;
};

// An interface that is open and running.
struct DeviceSpec
{
    juce::String name;
    int numInputs = 0, numOutputs = 0;
    int blockSize = 64;
};

// Returns the bridge for a stream key, creating/configuring it if new, so
// bridges (and their learned drift) survive plan rebuilds.
using BridgeProvider = std::function<std::shared_ptr<ClockBridge> (const juce::String& key, int channels,
                                                                   int producerBlock, int consumerBlock)>;

struct PlanSpec
{
    std::vector<RackSpec> racks;
    std::vector<DeviceSpec> devices;
    double sampleRate = 48000.0;
    BridgeProvider bridges;
};

// ---- The immutable, preallocated per-interface plan the audio thread runs ----

struct SlotJob
{
    std::shared_ptr<SlotRuntime> slot;
    int procChannels = 1;               // channels handed to processBlock
};

// Where one rack input comes from.
struct InputTap
{
    bool fromImport = false;            // false: this interface's input
    int import = -1;                    // index into RenderPlan::imports
    int channel = 0;                    // device input channel, or bridge channel
    int rackChannel = -1;               // -1: feed every rack channel
};

struct RackJob
{
    RackSpec spec;                      // slots are moved into `slots` below
    std::vector<SlotJob> slots;
    std::vector<InputTap> taps;
    std::vector<int> sourceJobs;        // bus sources on this interface (indices into jobs)
    std::vector<int> sourceImports;     // bus sources on other interfaces (indices into imports)
    std::vector<std::pair<int, int>> localOutputs; // (device output channel, rack channel)
    juce::AudioBuffer<float> buffer, dry, backup;
    juce::MidiBuffer midi;
};

// Audio arriving from another interface, read once per block into `buffer`.
struct Import
{
    std::shared_ptr<ClockBridge> bridge;
    juce::AudioBuffer<float> buffer;
    std::vector<std::pair<int, int>> toOutputs; // (device output channel, bridge channel)
};

// This interface's input channels needed elsewhere.
struct InputExport
{
    std::shared_ptr<ClockBridge> bridge;
    std::vector<int> deviceChannels;
    std::vector<const float*> ptrs;     // filled per block (preallocated)
};

// A rack's output needed by another interface (its outputs or a bus there).
struct RackExport
{
    std::shared_ptr<ClockBridge> bridge;
    int job = -1;
    std::vector<const float*> ptrs;
};

struct RenderPlan
{
    juce::String device;
    std::vector<RackJob> jobs;
    std::vector<std::vector<int>> levels; // job indices; level n only reads levels < n
    std::vector<Import> imports;
    std::vector<InputExport> inputExports;
    std::vector<RackExport> rackExports;
    int maxBlockSize   = 512;
    double sampleRate  = 48000.0;
    uint64_t generation = 0;            // assigned by AudioEngine::setPlan
};

struct BuildResult
{
    std::map<juce::String, std::unique_ptr<RenderPlan>> plans; // by interface name
    std::map<juce::String, juce::String> homes;                // rack id -> interface
    juce::String error;
};

} // namespace lr
