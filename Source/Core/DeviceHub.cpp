#include "DeviceHub.h"
#include "Platform/Platform.h"
#include <algorithm>

namespace lr
{
namespace
{
// "Speakers (Focusrite USB Audio)" -> "Focusrite USB Audio": the hardware a
// WASAPI endpoint belongs to (the final parenthesised group, nesting allowed:
// "Speakers (Realtek(R) Audio)" -> "Realtek(R) Audio").
juce::String hardwareOf (const juce::String& endpoint)
{
    if (! endpoint.endsWithChar (')'))
        return {};

    int depth = 0;
    for (int i = endpoint.length() - 1; i >= 0; --i)
    {
        const auto c = endpoint[i];
        if (c == ')') ++depth;
        if (c == '(' && --depth == 0)
            return endpoint.substring (i + 1, endpoint.length() - 1).trim();
    }
    return {};
}
} // namespace

std::vector<EndpointGroup> groupEndpoints (const juce::StringArray& ins, const juce::StringArray& outs, bool pairByHardware)
{
    std::vector<EndpointGroup> groups;

    for (auto& n : outs)
        if (ins.contains (n))
            groups.push_back ({ n, n, n });

    juce::StringArray lonelyIns, lonelyOuts;
    for (auto& n : ins)  if (! outs.contains (n)) lonelyIns.add (n);
    for (auto& n : outs) if (! ins.contains (n))  lonelyOuts.add (n);

    if (pairByHardware)
    {
        auto count = [] (const juce::StringArray& list, const juce::String& hw)
        {
            int c = 0;
            for (auto& n : list) c += hardwareOf (n) == hw ? 1 : 0;
            return c;
        };

        juce::StringArray pairedIns, pairedOuts;
        for (auto& out : lonelyOuts)
        {
            const auto hw = hardwareOf (out);
            // Ambiguous (e.g. Speakers + S/PDIF on one chip): list endpoints separately.
            if (hw.isEmpty() || count (lonelyOuts, hw) != 1 || count (lonelyIns, hw) != 1)
                continue;

            for (auto& in : lonelyIns)
                if (hardwareOf (in) == hw)
                {
                    groups.push_back ({ hw, in, out });
                    pairedIns.add (in);
                    pairedOuts.add (out);
                }
        }

        for (auto& n : pairedIns)  lonelyIns.removeString (n);
        for (auto& n : pairedOuts) lonelyOuts.removeString (n);
    }

    for (auto& n : lonelyOuts) groups.push_back ({ n, {}, n });
    for (auto& n : lonelyIns)  groups.push_back ({ n, n, {} });
    return groups;
}

DeviceHub::DeviceHub()
    : types (platform::createAudioDeviceTypes())
{
    for (auto& t : types)
        t->addListener (this);
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
    for (auto& t : types)
        t->removeListener (this);
    while (! open.empty())
        closeOne (open.back()->name);
    boost.stop();
}

// ---------------------------------------------------------------------------

void DeviceHub::rescan()
{
    std::vector<Info> found;
    for (auto& t : types)
        scanType (*t, types.size() > 1, found);

    available = std::move (found);
    scanned = true;
}

// Lists one driver type's interfaces (see groupEndpoints).
void DeviceHub::scanType (juce::AudioIODeviceType& type, bool qualify, std::vector<Info>& found)
{
    type.scanForDevices();

    juce::StringArray ins, outs;
    for (auto& n : type.getDeviceNames (true))
        if (platform::offerDevice (type, n)) ins.add (n);
    for (auto& n : type.getDeviceNames (false))
        if (platform::offerDevice (type, n)) outs.add (n);

    auto candidates = groupEndpoints (ins, outs, platform::driverLabel (type) == "WASAPI");
    for (auto& c : candidates)
        if (c.in == c.out || c.in.isEmpty() || c.out.isEmpty())
            c.shown = platform::shortDeviceName (type, c.shown);

    // Creating an ASIO device object loads its driver. Never do that next to
    // a running ASIO interface: reuse what the last scan learned instead.
    bool typeRunning = false;
    for (auto& o : open)
        typeRunning |= o->type == &type;
    const bool mayProbe = ! (typeRunning && platform::probingLoadsDriver (type));

    for (auto& c : candidates)
    {
        Info info;
        info.type = &type;
        info.inputDevice = c.in;
        info.outputDevice = c.out;
        info.name = qualify ? platform::driverLabel (type) + ": " + c.shown : c.shown;

        for (int n = 2; std::any_of (found.begin(), found.end(), [&] (const Info& f) { return f.name == info.name; }); ++n)
            info.name = (qualify ? platform::driverLabel (type) + ": " + c.shown : c.shown) + " " + juce::String (n);

        if (auto* o = deviceFor (info.name))
        {
            info.inputs = o->getInputChannelNames();
            info.outputs = o->getOutputChannelNames();
        }
        else if (! mayProbe)
        {
            for (auto& old : available)
                if (old.name == info.name)
                {
                    info.inputs = old.inputs;
                    info.outputs = old.outputs;
                }
        }
        else if (std::unique_ptr<juce::AudioIODevice> probe { type.createDevice (c.out, c.in) })
        {
            // Unopened device object: only used to read channel names.
            info.inputs = probe->getInputChannelNames();
            info.outputs = probe->getOutputChannelNames();
        }

        found.push_back (std::move (info));
    }
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

    std::unique_ptr<juce::AudioIODevice> device (info->type->createDevice (info->outputDevice, info->inputDevice));
    if (device == nullptr)
    {
        errors[name] = "Could not create device";
        return false;
    }

    // All channels, counted on the device itself (the catalogue may hold
    // names from an earlier scan).
    juce::BigInteger ins, outs;
    ins.setRange (0, device->getInputChannelNames().size(), true);
    outs.setRange (0, device->getOutputChannelNames().size(), true);

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

    // The open device is the authority on its channels (routing menus read these).
    for (auto& a : available)
        if (a.name == name)
        {
            a.inputs = device->getInputChannelNames();
            a.outputs = device->getOutputChannelNames();
        }

    entry->type = info->type;
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
    bool smallBlocks = false;
    for (auto& o : open)
        smallBlocks |= o->device->getCurrentBufferSizeSamples() <= boostMaxBlock;

    if (boostEnabled && smallBlocks)
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

    const int spare = platform::performanceCoreCount() - expected - (boosting ? 1 : 0);
    return juce::jmax (0, spare / expected);
}

} // namespace lr
