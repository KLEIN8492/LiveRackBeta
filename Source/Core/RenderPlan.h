#pragma once

#include "Runtime.h"
#include <memory>
#include <vector>

namespace lr
{
// ---- Input to the builder (plain data, easy to construct in tests) ----------

struct RackSpec
{
    juce::String id;
    int numChannels = 1;                // 1 = mono, 2 = stereo
    std::vector<int> inputs;            // device input channels
    std::vector<juce::String> sources;  // rack ids summed into this rack (bus)
    std::vector<int> outputs;           // device output channels
    bool critical = false;
    bool bypassed = false;
    std::shared_ptr<RackRuntime> runtime;
    std::vector<std::shared_ptr<SlotRuntime>> slots;
};

struct PlanSpec
{
    std::vector<RackSpec> racks;
    int numDeviceInputs  = 0;
    int numDeviceOutputs = 0;
    int maxBlockSize     = 512;
    double sampleRate    = 48000.0;
};

// ---- The immutable, preallocated plan the audio thread runs -----------------

struct SlotJob
{
    std::shared_ptr<SlotRuntime> slot;
    int procChannels = 1;               // channels handed to processBlock
};

struct RackJob
{
    RackSpec spec;                      // slots are moved into `slots` below
    std::vector<SlotJob> slots;
    std::vector<int> sourceJobs;        // indices into RenderPlan::jobs
    juce::AudioBuffer<float> buffer, dry, backup;
    juce::MidiBuffer midi;
};

struct RenderPlan
{
    std::vector<RackJob> jobs;
    std::vector<std::vector<int>> levels; // job indices; level n only reads levels < n
    int maxBlockSize   = 512;
    double sampleRate  = 48000.0;
    uint64_t generation = 0;            // assigned by AudioEngine::setPlan
};

struct BuildResult
{
    std::unique_ptr<RenderPlan> plan;
    juce::String error;
};

} // namespace lr
