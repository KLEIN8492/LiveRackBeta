#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <vector>

#if defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
 #include <immintrin.h>
#elif defined (_M_ARM64)
 #include <intrin.h>
#endif

// Everything LiveRack needs from the operating system, one implementation file
// per OS (Platform_mac.mm, Platform_windows.cpp, Platform_linux.cpp). Code
// above this layer never includes OS headers.
namespace lr::platform
{
// Physical cores of the fastest class (P-cores on hybrid CPUs, all cores
// otherwise). Real-time threads are budgeted against this.
int performanceCoreCount();

// Spin-wait hint for real-time busy loops.
inline void cpuRelax() noexcept
{
   #if defined (_M_ARM64)
    __yield();
   #elif defined (__aarch64__)
    __builtin_arm_yield();
   #elif defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
    _mm_pause();
   #endif
}

// Called once on each real-time worker thread, after it starts. Windows:
// registers it with MMCSS "Pro Audio" so the scheduler treats it like the
// driver's own audio thread.
void prepareRealtimeWorker();

// Held for the whole session: no App Nap / power throttling / timer
// coalescing, no idle system sleep.
class LatencyCriticalActivity
{
public:
    explicit LatencyCriticalActivity (const char* reason);
    ~LatencyCriticalActivity();

    LatencyCriticalActivity (const LatencyCriticalActivity&) = delete;
    LatencyCriticalActivity& operator= (const LatencyCriticalActivity&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Audio driver types to offer, best first. Their names are the prefixes of
// interface names when there is more than one (see DeviceHub).
std::vector<std::unique_ptr<juce::AudioIODeviceType>> createAudioDeviceTypes();

// Short label for a driver type ("ASIO", "WASAPI", "ALSA", "JACK", ...).
juce::String driverLabel (const juce::AudioIODeviceType&);

// Whether a device the driver lists is worth offering, and the name to show.
// ALSA lists every PCM; only direct hardware devices are offered (no
// resampling/mixing layers, lowest latency) and shown without ALSA's
// "Direct hardware device..." description.
bool offerDevice (const juce::AudioIODeviceType&, const juce::String& deviceName);
juce::String shortDeviceName (const juce::AudioIODeviceType&, const juce::String& deviceName);

// True when creating a device object of this type loads its driver (ASIO):
// such devices must not be probed while one of the same type is running.
bool probingLoadsDriver (const juce::AudioIODeviceType&);

// Where the session files live (autosave, crash log, plugin list).
juce::File supportDirectory();

// A helper executable shipped next to the app (adds .exe on Windows).
juce::File helperExecutable (const juce::String& name);

// What the crash guard relaunches: the .app bundle on macOS, the executable
// elsewhere.
juce::File relaunchTarget();

int processId();

// argv[1..] as Unicode (Windows' narrow argv is in the ANSI code page).
juce::StringArray commandLineArguments (int argc, char* argv[]);

// Starts `exe args...` fully detached, so it outlives this process.
bool spawnDetached (const juce::File& exe, const juce::StringArray& args);

// Crashes must end the process immediately (Windows: no error-report dialog),
// so the guard can relaunch it. Call first thing at startup.
void configureCrashHandling();

// Ends the process now, from a state that can't be recovered (a plugin hung
// the audio thread). macOS/Linux abort for a crash report; Windows terminates
// without involving Windows Error Reporting.
[[noreturn]] void terminateNow();

// Non-empty when real-time scheduling is unavailable, with how to fix it.
juce::String realtimeWarning();

// Monospace font family for the UI.
juce::String monospaceFontName();

// Second plugin format to offer next to VST3 ("AudioUnit", "LV2" or empty).
juce::String secondaryPluginFormat();
} // namespace lr::platform
