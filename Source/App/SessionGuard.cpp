#include "SessionGuard.h"
#include <spawn.h>
#include <unistd.h>

extern char** environ;

namespace lr
{
namespace
{
constexpr int tickMs = 250;
constexpr int debounceMs = 500;
constexpr int fullSaveEveryMs = 30000;
constexpr int stallTicks = 4; // ~1 s of the audio thread stuck inside a callback
} // namespace

SessionGuard::SessionGuard() = default;

SessionGuard::~SessionGuard()
{
    stopTimer();
    watchedShow.removeListener (this);
}

juce::File SessionGuard::getSupportDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support/LiveRack");
}

void SessionGuard::begin()
{
    dir.createDirectory();

    crashed = runningMarker.existsAsFile();
    if (crashed)
        suspects = InflightLog::readSuspects (inflightFile);

    inflight.open (inflightFile);
    runningMarker.replaceWithText (juce::String (getpid()));
    spawnGuard();
}

void SessionGuard::spawnGuard()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto guard = exe.getSiblingFile ("LiveRackGuard");
    const auto bundle = juce::File::getSpecialLocation (juce::File::currentApplicationFile);

    if (! guard.existsAsFile() || ! bundle.getFileName().endsWith (".app"))
        return;

    const auto pid = juce::String (getpid());
    std::string g = guard.getFullPathName().toStdString();
    std::string b = bundle.getFullPathName().toStdString();
    std::string m = runningMarker.getFullPathName().toStdString();
    std::string p = pid.toStdString();

    const char* args[] = { g.c_str(), "--pid", p.c_str(), "--app", b.c_str(), "--marker", m.c_str(), nullptr };

    posix_spawnattr_t attr;
    posix_spawnattr_init (&attr);
    posix_spawnattr_setflags (&attr, POSIX_SPAWN_SETSID); // survive our death

    pid_t child = 0;
    posix_spawn (&child, g.c_str(), nullptr, &attr, const_cast<char**> (args), environ);
    posix_spawnattr_destroy (&attr);
}

void SessionGuard::watch (PluginHost& h, AudioEngine& e)
{
    host = &h;
    engine = &e;

    watchedShow.removeListener (this);
    watchedShow = host->getShow();
    watchedShow.addListener (this);

    lastFullSave = juce::Time::getMillisecondCounter();
    startTimer (tickMs);
}

void SessionGuard::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& prop)
{
    if (prop != ids::state) // plugin state is captured by the save itself
        markDirty();
}

void SessionGuard::markDirty()
{
    dirty = true;
    lastChange = juce::Time::getMillisecondCounter();
}

void SessionGuard::saveNow()
{
    if (host == nullptr)
        return;

    // The host may have been given a new show (File > Open).
    if (host->getShow() != watchedShow)
    {
        watchedShow.removeListener (this);
        watchedShow = host->getShow();
        watchedShow.addListener (this);
    }

    host->captureStates();
    show::saveToFile (host->getShow(), getAutosaveFile());
    dirty = false;
    lastFullSave = juce::Time::getMillisecondCounter();
}

void SessionGuard::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();

    if ((dirty && now - lastChange > (juce::uint32) debounceMs) || now - lastFullSave > (juce::uint32) fullSaveEveryMs)
        saveNow();

    checkForStall();
}

void SessionGuard::checkForStall()
{
    if (engine == nullptr || host == nullptr)
        return;

    const auto beat = engine->heartbeat.load();
    const bool stuck = engine->inCallback.load() && beat == lastHeartbeat;
    lastHeartbeat = beat;

    stalledTicks = stuck ? stalledTicks + 1 : 0;
    if (stalledTicks < stallTicks)
        return;

    // A plugin is hung inside processBlock. Nothing in-process can recover the
    // audio thread, so mark the culprit(s) and restart via the guard.
    auto show = host->getShow();
    for (auto rack : show::getRacks (show))
        for (auto slot : rack)
            if (auto rt = host->getSlot (slot[ids::id].toString()); rt != nullptr && rt->inProcess.load())
                slot.setProperty (ids::faultBypassed, true, nullptr);

    show::saveToFile (show, getAutosaveFile()); // no captureStates: plugins may be wedged
    std::abort();
}

void SessionGuard::endCleanly()
{
    stopTimer();
    inflight.close();
    runningMarker.deleteFile();
    getSupportDirectory().getChildFile ("running.relaunches").deleteFile();
}

} // namespace lr
