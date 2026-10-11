#include "Platform.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <avrt.h>
#include <timeapi.h>
#include <werapi.h>
#include <shellapi.h>
#include <cstdlib>
#include <algorithm>
#include <vector>

#pragma comment (lib, "avrt.lib")
#pragma comment (lib, "winmm.lib")
#pragma comment (lib, "wer.lib")
#pragma comment (lib, "shell32.lib")

namespace lr::platform
{
// Physical cores of the highest efficiency class (P-cores on hybrid Intel /
// Snapdragon; on non-hybrid CPUs every core has class 0).
int performanceCoreCount()
{
    DWORD length = 0;
    ::GetLogicalProcessorInformationEx (RelationProcessorCore, nullptr, &length);
    if (length == 0)
        return juce::SystemStats::getNumPhysicalCpus();

    std::vector<char> buffer (length);
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*> (buffer.data());
    if (! ::GetLogicalProcessorInformationEx (RelationProcessorCore, info, &length))
        return juce::SystemStats::getNumPhysicalCpus();

    std::vector<BYTE> classes;
    for (DWORD offset = 0; offset < length;)
    {
        auto* core = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*> (buffer.data() + offset);
        if (core->Relationship == RelationProcessorCore)
            classes.push_back (core->Processor.EfficiencyClass);
        offset += core->Size;
    }

    if (classes.empty())
        return juce::SystemStats::getNumPhysicalCpus();

    const auto top = *std::max_element (classes.begin(), classes.end());
    return (int) std::count (classes.begin(), classes.end(), top);
}

void prepareRealtimeWorker()
{
    // Same MMCSS class ASIO drivers use for their own callback thread.
    DWORD taskIndex = 0;
    if (auto task = ::AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex))
        ::AvSetMmThreadPriority (task, AVRT_PRIORITY_CRITICAL);
}

// 1 ms timer resolution (also for the UI and stall timers), no power
// throttling of this process (EcoQoS would park it on E-cores and lower its
// clocks), and no idle sleep or display sleep while running.
struct LatencyCriticalActivity::Impl {};

LatencyCriticalActivity::LatencyCriticalActivity (const char*) : impl (std::make_unique<Impl>())
{
    ::timeBeginPeriod (1);

    PROCESS_POWER_THROTTLING_STATE state {};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    state.StateMask = 0; // = throttling off
    ::SetProcessInformation (::GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof (state));

    ::SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
}

LatencyCriticalActivity::~LatencyCriticalActivity()
{
    ::SetThreadExecutionState (ES_CONTINUOUS);
    ::timeEndPeriod (1);
}

std::vector<std::unique_ptr<juce::AudioIODeviceType>> createAudioDeviceTypes()
{
    std::vector<std::unique_ptr<juce::AudioIODeviceType>> types;
    // ASIO first (Dante Virtual Soundcard, consoles, pro interfaces). WASAPI
    // exclusive for anything without an ASIO driver: still bypasses the
    // Windows mixer, just with a larger minimum buffer.
    if (auto* asio = juce::AudioIODeviceType::createAudioIODeviceType_ASIO())
        types.emplace_back (asio);
    if (auto* wasapi = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::exclusive))
        types.emplace_back (wasapi);
    return types;
}

juce::String realtimeWarning() { return {}; }

int processId()
{
    return (int) ::GetCurrentProcessId();
}

bool spawnDetached (const juce::File& exe, const juce::StringArray& args)
{
    juce::StringArray all { exe.getFullPathName() };
    all.addArray (args);

    juce::String commandLine;
    for (auto& a : all)
        commandLine << (commandLine.isEmpty() ? "" : " ") << a.quoted();

    std::wstring cmd (commandLine.toWideCharPointer());
    STARTUPINFOW startup {};
    startup.cb = sizeof (startup);
    PROCESS_INFORMATION process {};

    // Breakaway: if we run inside a job (some launchers kill the whole job
    // when we exit), the guard must not die with us.
    const DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;

    if (! ::CreateProcessW (nullptr, cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &startup, &process)
        && ! ::CreateProcessW (nullptr, cmd.data(), nullptr, nullptr, FALSE, flags & ~(DWORD) CREATE_BREAKAWAY_FROM_JOB,
                               nullptr, nullptr, &startup, &process))
        return false;

    ::CloseHandle (process.hThread);
    ::CloseHandle (process.hProcess);
    return true;
}

juce::StringArray commandLineArguments (int, char*[])
{
    juce::StringArray args;
    int count = 0;
    if (auto** wide = ::CommandLineToArgvW (::GetCommandLineW(), &count))
    {
        for (int i = 1; i < count; ++i)
            args.add (juce::String (wide[i]));
        ::LocalFree (wide);
    }
    return args;
}

void configureCrashHandling()
{
    // A crash or abort must end the process at once, so the guard relaunches
    // it, instead of waiting on an error dialog nobody at FOH will click.
    ::SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    ::WerSetFlags (WER_FAULT_REPORTING_NO_UI);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

void terminateNow()
{
    ::TerminateProcess (::GetCurrentProcess(), 3);
    std::abort(); // unreachable
}
} // namespace lr::platform
