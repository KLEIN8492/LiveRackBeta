// Process handling shared by macOS and Linux.
#include "Platform.h"
#include <spawn.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <vector>

extern char** environ;

namespace lr::platform
{
int processId()
{
    return (int) ::getpid();
}

bool spawnDetached (const juce::File& exe, const juce::StringArray& args)
{
    std::vector<std::string> storage;
    storage.push_back (exe.getFullPathName().toStdString());
    for (auto& a : args)
        storage.push_back (a.toStdString());

    std::vector<char*> argv;
    for (auto& s : storage)
        argv.push_back (s.data());
    argv.push_back (nullptr);

    posix_spawnattr_t attr;
    posix_spawnattr_init (&attr);
    posix_spawnattr_setflags (&attr, POSIX_SPAWN_SETSID); // own session: survives our death

    pid_t child = 0;
    const int result = posix_spawn (&child, storage.front().c_str(), nullptr, &attr, argv.data(), environ);
    posix_spawnattr_destroy (&attr);
    return result == 0;
}

juce::StringArray commandLineArguments (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String (juce::CharPointer_UTF8 (argv[i])));
    return args;
}

void configureCrashHandling() {}

void terminateNow()
{
    std::abort(); // a crash report with the hung thread's stack is worth having
}
} // namespace lr::platform
