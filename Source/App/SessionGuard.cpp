#include "SessionGuard.h"
#include "Platform/Platform.h"

namespace lr
{
namespace
{
constexpr int tickMs = 250;
constexpr int debounceMs = 500;
constexpr int pluginSaveEveryMs = 30000; // plugin edits are captured at most this often
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
    return platform::supportDirectory();
}

void SessionGuard::begin()
{
    dir.createDirectory();

    crashed = runningMarker.existsAsFile();
    if (crashed)
        suspects = InflightLog::readSuspects (inflightFile);

    inflight.open (inflightFile);
    runningMarker.replaceWithText (juce::String (platform::processId()));
    spawnGuard();
}

void SessionGuard::spawnGuard()
{
    const auto guard = platform::helperExecutable ("LiveRackGuard");
    const auto app = platform::relaunchTarget();

    // Only a packaged build relaunches itself (not tests or a bare build dir).
    if (! guard.existsAsFile() || ! app.exists())
        return;

    platform::spawnDetached (guard, { "--pid", juce::String (platform::processId()),
                                      "--app", app.getFullPathName(),
                                      "--marker", runningMarker.getFullPathName() });
}

void SessionGuard::watch (PluginHost& h, DeviceHub& d)
{
    host = &h;
    hub = &d;

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

void SessionGuard::saveNow (bool allPluginStates)
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

    host->captureStates (! allPluginStates);

    // Keep a known-good copy at most 5 minutes old, in case the autosave is
    // ever damaged (the write itself is atomic: temp file + rename).
    const auto autosave = getAutosaveFile(), backup = getBackupFile();
    if (autosave.existsAsFile()
        && (! backup.existsAsFile() || backup.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::minutes (5))
        && show::loadFromFile (autosave).isValid())
        autosave.copyFileTo (backup);

    show::saveToFile (host->getShow(), autosave);
    dirty = false;
    lastFullSave = juce::Time::getMillisecondCounter();
}

void SessionGuard::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();

    // Show edits save after a short debounce. Plugin edits are batched: asking
    // a plugin for its state can stall its audio, so nothing is captured
    // while nothing changed.
    const bool showEdited = dirty && now - lastChange > (juce::uint32) debounceMs;
    const bool pluginsEdited = now - lastFullSave > (juce::uint32) pluginSaveEveryMs && host->hasUncapturedState();

    if (showEdited || pluginsEdited)
        saveNow();

    checkForStall();
}

void SessionGuard::checkForStall()
{
    if (hub == nullptr || host == nullptr)
        return;

    // Stuck = some interface's callback is still inside the same block.
    bool stuck = false;
    for (auto& o : hub->getOpen())
    {
        const auto beat = o->engine->heartbeat.load();
        stuck |= o->engine->inCallback.load() && beat == lastHeartbeat[o->name];
        lastHeartbeat[o->name] = beat;
    }

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
    platform::terminateNow();
}

void SessionGuard::endCleanly()
{
    stopTimer();
    inflight.close();
    runningMarker.deleteFile();
    getSupportDirectory().getChildFile ("running.relaunches").deleteFile();
}

} // namespace lr
