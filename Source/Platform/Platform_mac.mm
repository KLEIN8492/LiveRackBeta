#include "Platform.h"
#import <Foundation/Foundation.h>
#include <sys/sysctl.h>

namespace lr::platform
{
int performanceCoreCount()
{
    int count = 0;
    size_t size = sizeof (count);
    if (sysctlbyname ("hw.perflevel0.physicalcpu", &count, &size, nullptr, 0) == 0 && count > 0)
        return count;
    return juce::SystemStats::getNumPhysicalCpus();
}

void prepareRealtimeWorker() {} // workers join the device's audio workgroup instead

// NSActivityLatencyCritical | NSActivityUserInitiated, system sleep disabled:
// exempt from App Nap and timer coalescing, no idle system sleep mid-show.
struct LatencyCriticalActivity::Impl
{
    void* token = nullptr; // retained id<NSObject>
};

LatencyCriticalActivity::LatencyCriticalActivity (const char* reason) : impl (std::make_unique<Impl>())
{
    const NSActivityOptions options = NSActivityLatencyCritical
                                    | NSActivityUserInitiated
                                    | NSActivityIdleSystemSleepDisabled;

    NSString* why = [NSString stringWithUTF8String: reason];
    id activity = [[NSProcessInfo processInfo] beginActivityWithOptions: options
                                                                 reason: why != nil ? why : @"LiveRack"];
    impl->token = (void*) CFBridgingRetain (activity);
}

LatencyCriticalActivity::~LatencyCriticalActivity()
{
    if (impl->token != nullptr)
        [[NSProcessInfo processInfo] endActivity: (id) CFBridgingRelease (impl->token)];
}

std::vector<std::unique_ptr<juce::AudioIODeviceType>> createAudioDeviceTypes()
{
    std::vector<std::unique_ptr<juce::AudioIODeviceType>> types;
    types.emplace_back (juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
    return types;
}

juce::String realtimeWarning() { return {}; }
} // namespace lr::platform
