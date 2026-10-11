#include "Platform.h"
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <map>

extern char** environ;

namespace lr::platform
{
namespace
{
juce::String readSys (const juce::String& path)
{
    return juce::File (path).loadFileAsString().trim();
}
} // namespace

// Physical cores whose top frequency is within 10% of the fastest: P-cores on
// Intel hybrid / big cores on ARM big.LITTLE, every core otherwise. (Turbo
// Boost Max "favoured" cores differ by a few percent; E-cores by ~25%.)
int performanceCoreCount()
{
    std::map<std::pair<int, int>, long> cores; // (package, core) -> max kHz
    long fastest = 0;

    for (auto& cpu : juce::File ("/sys/devices/system/cpu").findChildFiles (juce::File::findDirectories, false, "cpu*"))
    {
        if (! cpu.getFileName().substring (3).containsOnly ("0123456789") || cpu.getFileName().length() == 3)
            continue; // cpufreq, cpuidle, ...

        const auto base = cpu.getFullPathName();
        const long khz = readSys (base + "/cpufreq/cpuinfo_max_freq").getLargeIntValue();
        const int package = readSys (base + "/topology/physical_package_id").getIntValue();
        const int core = readSys (base + "/topology/core_id").getIntValue();

        auto& f = cores[{ package, core }];
        f = juce::jmax (f, khz);
        fastest = juce::jmax (fastest, khz);
    }

    if (cores.empty() || fastest <= 0)
        return juce::SystemStats::getNumPhysicalCpus();

    int count = 0;
    for (auto& [id, khz] : cores)
        if (khz * 10 >= fastest * 9)
            ++count;
    return juce::jmax (1, count);
}

void prepareRealtimeWorker() {} // JUCE already asked for SCHED_RR

// Holds a systemd sleep/idle inhibitor for the session. The inhibitor runs
// `cat` on a pipe we hold open, so it also ends if LiveRack crashes (the pipe
// closes, cat exits, systemd-inhibit releases the lock). No systemd: no-op.
struct LatencyCriticalActivity::Impl
{
    int writeFd = -1;
    pid_t child = 0;
};

LatencyCriticalActivity::LatencyCriticalActivity (const char* reason) : impl (std::make_unique<Impl>())
{
    int fds[2];
    if (::pipe (fds) != 0)
        return;

    const std::string why = std::string ("--why=") + reason;
    const char* argv[] = { "systemd-inhibit", "--what=sleep:idle", "--who=LiveRack", why.c_str(),
                           "--mode=block", "cat", nullptr };

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init (&actions);
    posix_spawn_file_actions_adddup2 (&actions, fds[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose (&actions, fds[1]);

    if (posix_spawnp (&impl->child, "systemd-inhibit", &actions, nullptr, const_cast<char**> (argv), environ) == 0)
        impl->writeFd = fds[1];
    else
        ::close (fds[1]);

    posix_spawn_file_actions_destroy (&actions);
    ::close (fds[0]);
}

LatencyCriticalActivity::~LatencyCriticalActivity()
{
    if (impl->writeFd >= 0)
    {
        ::close (impl->writeFd); // EOF: cat exits, the inhibitor is released
        ::waitpid (impl->child, nullptr, 0);
    }
}

std::vector<std::unique_ptr<juce::AudioIODeviceType>> createAudioDeviceTypes()
{
    std::vector<std::unique_ptr<juce::AudioIODeviceType>> types;
    // ALSA hardware first: one device per interface, nothing between us and
    // the driver. JACK covers PipeWire setups (and JACK's own aggregation).
    if (auto* alsa = juce::AudioIODeviceType::createAudioIODeviceType_ALSA())
        types.emplace_back (alsa);
    if (auto* jack = juce::AudioIODeviceType::createAudioIODeviceType_JACK())
        types.emplace_back (jack);
    return types;
}

juce::String realtimeWarning()
{
    rlimit limit {};
    if (::geteuid() == 0 || (::getrlimit (RLIMIT_RTPRIO, &limit) == 0 && limit.rlim_cur > 0))
        return {};

    return "Real-time priority is not allowed for this user, so audio can drop out under load. "
           "Add yourself to the 'audio' group (sudo usermod -aG audio $USER) and make sure "
           "/etc/security/limits.d grants it rtprio 95, then log in again.";
}
} // namespace lr::platform
