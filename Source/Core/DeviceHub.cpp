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

// Watchdog. An interface whose callbacks stop for 1.5 s while not inside a
// block (driver restarted, clock lost, rate changed elsewhere) is reopened.
// A hang *inside* a block is a plugin, handled by the session's stall
// detector. Interfaces that failed to open are retried every 3 s.
void DeviceHub::timerCallback()
{
    constexpr int tickMs = 250, deadMs = 1500, retryMs = 3000;
    bool changed = false;

    std::vector<juce::String> dead;
    for (auto& o : open)
    {
        auto& w = watch[o->name];
        const auto beat = o->engine->heartbeat.load();
        const bool idle = beat == w.beat && ! o->engine->inCallback.load();
        w.beat = beat;
        w.stalledMs = idle ? w.stalledMs + tickMs : 0;

        if (w.stalledMs >= deadMs || ! o->device->isPlaying())
            dead.push_back (o->name);
    }

    for (auto& name : dead)
    {
        closeOne (name);
        watch.erase (name);
        ++restarts[name];
        if (openOne (name))
            errors.erase (name);
        else
            errors[name] = "Stopped delivering audio; reopening failed: " + getError (name);
        changed = true;
    }

    const auto now = juce::Time::getMillisecondCounter();
    if (openingAllowed && now - lastRetry >= (juce::uint32) retryMs)
    {
        lastRetry = now;
        for (auto& name : wanted)
            if (deviceFor (name) == nullptr && findAvailable (name) != nullptr && openOne (name))
                changed = true;
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
            if (deviceFor (n) == nullptr)
                openOne (n);

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

    // Share the performance cores: one per audio thread, one for the boost.
    const int interfaces = (int) open.size() + 1;
    const bool willBoost = boostEnabled && device->getCurrentBufferSizeSamples() <= boostMaxBlock;
    const int spare = performanceCores() - interfaces - (willBoost ? 1 : 0);
    entry->engine->setMaxWorkers (juce::jmax (0, spare / interfaces));

    entry->device = std::move (device);
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
}

} // namespace lr
