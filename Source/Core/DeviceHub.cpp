#include "DeviceHub.h"
#include <sys/sysctl.h>

namespace lr
{
namespace
{
int performanceCores()
{
    int count = 0;
    size_t size = sizeof (count);
    if (sysctlbyname ("hw.perflevel0.physicalcpu", &count, &size, nullptr, 0) == 0 && count > 0)
        return count;
    return juce::SystemStats::getNumPhysicalCpus();
}
} // namespace

DeviceHub::DeviceHub()
    : type (juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio())
{
    type->addListener (this);
    startTimer (250);
}

int DeviceHub::getRestarts (const juce::String& name) const
{
    auto it = restarts.find (name);
    return it != restarts.end() ? it->second : 0;
}

namespace
{
constexpr int retryCapMs = 8000;

// Reopen back-off: the first failure retries at once (driver hiccup), then
// 1, 2, 4, 8 s. Capped low: when a lost clock comes back, audio should too.
int retryDelayMs (int failures)
{
    return failures <= 1 ? 0 : juce::jmin (retryCapMs, 1000 << juce::jmin (failures - 2, 3));
}

bool reached (juce::uint32 now, juce::uint32 due) noexcept { return (juce::int32) (now - due) >= 0; }
} // namespace

void DeviceHub::scheduleRetry (const juce::String& name, juce::uint32 now)
{
    auto& r = retries[name];
    ++r.failures;
    r.due = now + (juce::uint32) retryDelayMs (r.failures);
}

// Watchdog. An interface whose callbacks stop for 1.5 s while not inside a
// block (driver restarted, clock lost, rate changed elsewhere) is reopened,
// with back-off so a dead clock doesn't hammer Core Audio and rebuild every
// plan forever. A hang *inside* a block is a plugin, handled by the
// session's stall detector. 10 s of healthy audio resets the back-off.
void DeviceHub::timerCallback()
{
    constexpr int tickMs = 250, deadMs = 1500, healthyMs = 10000;
    const auto now = juce::Time::getMillisecondCounter();
    bool changed = false;

    std::vector<juce::String> dead;
    for (auto& o : open)
    {
        auto& w = watch[o->name];
        const auto beat = o->engine->heartbeat.load();
        // Not playing counts as idle rather than dead at once: JUCE may be
        // restarting the device itself.
        const bool idle = (beat == w.beat && ! o->engine->inCallback.load()) || ! o->device->isPlaying();
        w.beat = beat;
        w.stalledMs = idle ? w.stalledMs + tickMs : 0;

        if (w.stalledMs >= deadMs)
            dead.push_back (o->name);
        else if (! idle && now - o->openedAt > (juce::uint32) healthyMs)
            retries.erase (o->name);
    }

    for (auto& name : dead)
    {
        closeOne (name);
        watch.erase (name);
        ++restarts[name];
        scheduleRetry (name, now);
        errors[name] = "Stopped delivering audio";
        changed = true;
    }

    if (openingAllowed)
    {
        for (auto& name : wanted)
        {
            if (deviceFor (name) != nullptr || findAvailable (name) == nullptr)
                continue;
            if (auto it = retries.find (name); it != retries.end() && ! reached (now, it->second.due))
                continue;

            if (openOne (name))
            {
                changed = true;
            }
            else
            {
                scheduleRetry (name, now);
                errors[name] = getError (name) + " (retrying in "
                             + juce::String (retryDelayMs (retries[name].failures) / 1000) + " s)";
            }
        }
    }

    if (changed)
    {
        updateBoost();
        if (onChanged)
            onChanged();
    }
}

DeviceHub::~DeviceHub()
{
    stopTimer();
    type->removeListener (this);
    while (! open.empty())
        closeOne (open.back()->name);
    boost.stop();
}

// ---------------------------------------------------------------------------

void DeviceHub::rescan()
{
    type->scanForDevices();
    available.clear();

    auto ins = type->getDeviceNames (true);
    auto outs = type->getDeviceNames (false);
    juce::StringArray names (outs);
    names.addArray (ins);
    names.removeDuplicates (false);

    for (auto& name : names)
    {
        Info info;
        info.name = name;

        if (auto* o = deviceFor (name))
        {
            info.inputs = o->getInputChannelNames();
            info.outputs = o->getOutputChannelNames();
        }
        else
        {
            // Unopened device object: only used to read channel names.
            std::unique_ptr<juce::AudioIODevice> probe (type->createDevice (outs.contains (name) ? name : juce::String(),
                                                                            ins.contains (name) ? name : juce::String()));
            if (probe != nullptr)
            {
                info.inputs = probe->getInputChannelNames();
                info.outputs = probe->getOutputChannelNames();
            }
        }

        available.push_back (std::move (info));
    }

    scanned = true;
}

const std::vector<DeviceHub::Info>& DeviceHub::getAvailable()
{
    if (! scanned)
        rescan();
    return available;
}

const DeviceHub::Info* DeviceHub::findAvailable (const juce::String& name)
{
    for (auto& info : getAvailable())
        if (info.name == name)
            return &info;
    return nullptr;
}

void DeviceHub::audioDeviceListChanged()
{
    retries.clear(); // interfaces came or went: try them afresh
    rescan();
    reconcile();
    if (onChanged)
        onChanged();
}

// ---------------------------------------------------------------------------

void DeviceHub::setFormat (double newRate, int newBlock)
{
    sampleRate = newRate;
    blockSize = newBlock;

    std::vector<juce::String> names;
    for (auto& o : open)
        names.push_back (o->name);
    for (auto& n : names)
        closeOne (n);

    reconcile();
}

int DeviceHub::getMaxOpenBlockSize() const
{
    int m = 0;
    for (auto& o : open)
        m = juce::jmax (m, o->device->getCurrentBufferSizeSamples());
    return m > 0 ? m : blockSize;
}

void DeviceHub::setWanted (const std::set<juce::String>& names)
{
    if (names == wanted)
        return;
    wanted = names;
    reconcile();
}

void DeviceHub::reconcile()
{
    std::vector<juce::String> toClose;
    for (auto& o : open)
        if (wanted.count (o->name) == 0 || findAvailable (o->name) == nullptr)
            toClose.push_back (o->name);
    for (auto& n : toClose)
        closeOne (n);

    if (openingAllowed)
        for (auto& n : wanted)
            if (deviceFor (n) == nullptr && ! openOne (n))
                scheduleRetry (n, juce::Time::getMillisecondCounter());

    updateBoost();
}

bool DeviceHub::openOne (const juce::String& name)
{
    errors.erase (name);
    const auto* info = findAvailable (name);
    if (info == nullptr)
    {
        errors[name] = "Not connected";
        return false;
    }

    // Same name for input and output => one device (no combiner).
    std::unique_ptr<juce::AudioIODevice> device (type->createDevice (info->outputs.isEmpty() ? juce::String() : name,
                                                                     info->inputs.isEmpty() ? juce::String() : name));
    if (device == nullptr)
    {
        errors[name] = "Could not create device";
        return false;
    }

    juce::BigInteger ins, outs;
    ins.setRange (0, info->inputs.size(), true);
    outs.setRange (0, info->outputs.size(), true);

    const auto err = device->open (ins, outs, sampleRate, blockSize);
    if (err.isNotEmpty())
    {
        errors[name] = err;
        return false;
    }

    auto entry = std::make_unique<Open>();
    entry->name = name;
    entry->engine = std::make_unique<AudioEngine>();
    entry->engine->setInflightLog (inflight);
    entry->engine->setGlobalBypass (globalBypass);

    // Its share of the cores, counting this one; updateBoost() re-shares
    // every engine once the open set settles.
    const bool willBoost = boost.isRunning() || (boostEnabled && device->getCurrentBufferSizeSamples() <= boostMaxBlock);
    entry->engine->setMaxWorkers (workerShare ((int) open.size() + 1, willBoost));

    entry->device = std::move (device);
    entry->openedAt = juce::Time::getMillisecondCounter();
    entry->device->start (entry->engine.get());
    open.push_back (std::move (entry));
    return true;
}

void DeviceHub::closeOne (const juce::String& name)
{
    for (auto it = open.begin(); it != open.end(); ++it)
    {
        if ((*it)->name != name)
            continue;

        (*it)->device->stop();
        (*it)->device->close();
        (*it)->device.reset();
        open.erase (it); // engine (and its plans) go after the device has stopped
        break;
    }
    updateBoost();
}

AudioEngine* DeviceHub::engineFor (const juce::String& name) const
{
    for (auto& o : open)
        if (o->name == name)
            return o->engine.get();
    return nullptr;
}

juce::AudioIODevice* DeviceHub::deviceFor (const juce::String& name) const
{
    for (auto& o : open)
        if (o->name == name)
            return o->device.get();
    return nullptr;
}

juce::String DeviceHub::getError (const juce::String& name) const
{
    auto it = errors.find (name);
    return it != errors.end() ? it->second : juce::String();
}

// ---------------------------------------------------------------------------

void DeviceHub::setInflightLog (InflightLog* log)
{
    inflight = log;
    for (auto& o : open)
        o->engine->setInflightLog (log);
}

void DeviceHub::setGlobalBypass (bool b)
{
    globalBypass = b;
    for (auto& o : open)
        o->engine->setGlobalBypass (b);
}

void DeviceHub::setBoostEnabled (bool b)
{
    boostEnabled = b;
    updateBoost();
}

void DeviceHub::updateBoost()
{
    bool small = false;
    for (auto& o : open)
        small |= o->device->getCurrentBufferSizeSamples() <= boostMaxBlock;

    if (boostEnabled && small)
        boost.start();
    else
        boost.stop();

    const int share = workerShare ((int) open.size(), boost.isRunning());
    for (auto& o : open)
        o->engine->setMaxWorkers (share);
}

// Real-time workers per engine so that, summed over every interface, audio
// threads + workers + boost never exceed the performance cores (the audio
// thread spin-waits on its workers, so an oversubscribed worker costs
// deadline). Wanted-but-missing interfaces are counted too, so one coming
// back finds its share free.
int DeviceHub::workerShare (int interfaces, bool boosting)
{
    int expected = interfaces;
    int wantedAvailable = 0;
    for (auto& name : wanted)
        if (findAvailable (name) != nullptr)
            ++wantedAvailable;
    expected = juce::jmax (1, expected, wantedAvailable);

    const int spare = performanceCores() - expected - (boosting ? 1 : 0);
    return juce::jmax (0, spare / expected);
}

} // namespace lr
