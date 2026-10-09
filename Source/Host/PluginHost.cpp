#include "PluginHost.h"
#include "PluginWindow.h"
#include "Core/PlanBuilder.h"
#include "Core/PluginLayout.h"

namespace lr
{
PluginHost::PluginHost (DeviceHub& hub, InflightLog& log)
    : devices (hub), inflight (log)
{
    juce::addDefaultFormatsToManager (formatManager);
    preparedRate = devices.getSampleRate();
    preparedBlock = juce::jmax (devices.getBlockSize(), devices.getMaxOpenBlockSize());

    devices.onChanged = [this, token = std::weak_ptr<bool> (alive)]
    {
        if (! token.expired())
            devicesChanged();
    };

    startTimerHz (4);
}

int PluginHost::getRackLatency (const juce::String& rackId) const
{
    auto it = latencies.find (rackId);
    return it != latencies.end() ? it->second : 0;
}

void PluginHost::setAlignRacks (bool b)
{
    alignRacks = b;
    rebuildPlan();
}

// Plugins may call this from any thread (including audio): just set a flag.
void PluginHost::audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details)
{
    if (details.latencyChanged)
        latencyDirty.store (true);
}

void PluginHost::timerCallback()
{
    bool rebuild = latencyDirty.exchange (false);

    // A bridge that grew its latency during warm-up, or locked it, changes
    // the rack's total latency: realign.
    for (auto& [key, bridge] : bridges)
    {
        const auto s = bridge->getStats();
        auto& last = bridgeState[key];
        if (std::abs (s.target - last.first) > 0.5 || s.locked != last.second)
        {
            last = { s.target, s.locked };
            rebuild = true;
        }
    }

    if (rebuild)
        rebuildPlan();
}

void PluginHost::clearAllPlans()
{
    for (auto& o : devices.getOpen())
        o->engine->clearPlan();
}

bool PluginHost::waitForEngines()
{
    bool ok = true;
    for (auto& o : devices.getOpen())
        ok &= o->engine->waitUntilOldPlansReleased (1000);
    return ok;
}

juce::String PluginHost::getHome (const juce::String& rackId) const
{
    auto it = homes.find (rackId);
    return it != homes.end() ? it->second : juce::String();
}

PluginHost::~PluginHost()
{
    *alive = false;
    stopTimer();
    cancelPendingUpdate();
    for (auto& [id, slot] : slots)
        if (slot->plugin != nullptr)
            slot->plugin->removeListener (this);
    devices.onChanged = nullptr;
    show.removeListener (this);
    windows.clear();

    clearAllPlans();
    waitForEngines();

    for (auto& [id, slot] : slots)
        inflight.releaseRecord (slot->inflightRecord);
}

void PluginHost::loadKnownPlugins (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
        knownPlugins.recreateFromXml (*xml);
}

void PluginHost::saveKnownPlugins (const juce::File& file) const
{
    if (auto xml = knownPlugins.createXml())
    {
        file.getParentDirectory().createDirectory();
        xml->writeTo (file);
    }
}

// ---------------------------------------------------------------------------

void PluginHost::setShow (juce::ValueTree newShow)
{
    show.removeListener (this);
    windows.clear();

    // Take every plugin out of the engines before dropping the old show.
    clearAllPlans();
    waitForEngines();

    for (auto& [id, slot] : slots)
    {
        if (slot->plugin != nullptr)
            slot->plugin->removeListener (this);
        inflight.releaseRecord (slot->inflightRecord);
    }

    slots.clear();
    racks.clear();
    slotErrors.clear();
    excluded.clear();

    show = newShow;
    show.addListener (this);

    for (auto rack : show::getRacks (show))
    {
        racks[rack[ids::id].toString()] = std::make_shared<RackRuntime> (rack[ids::id].toString());
        for (auto slot : rack)
            attachSlot (slot);
    }

    rebuildPlan();
}

void PluginHost::attachSlot (juce::ValueTree slot)
{
    const auto id = slot[ids::id].toString();
    auto rt = std::make_shared<SlotRuntime> (id);
    rt->userBypassed = (bool) slot[ids::bypassed];
    if ((bool) slot[ids::faultBypassed])
        rt->fault = FaultReason::crashSuspect;
    rt->inflightRecord = inflight.allocateRecord (id);

    slots[id] = rt;
    createInstance (slot);
}

void PluginHost::detachSlot (const juce::String& slotId)
{
    closeEditor (slotId);

    if (auto it = slots.find (slotId); it != slots.end())
    {
        if (it->second->plugin != nullptr)
            it->second->plugin->removeListener (this);
        inflight.releaseRecord (it->second->inflightRecord);
        slots.erase (it); // the retired plan keeps it alive until the audio thread lets go
    }

    slotErrors.erase (slotId);
    excluded.erase (slotId);
}

void PluginHost::createInstance (juce::ValueTree slot)
{
    const auto slotId = slot[ids::id].toString();
    juce::PluginDescription desc;

    if (! show::getPluginDescription (slot, desc))
    {
        slotErrors[slotId] = "Missing plugin description";
        return;
    }

    const double rate = preparedRate > 0 ? preparedRate : 48000.0;
    const int block = preparedBlock > 0 ? preparedBlock : 512;
    std::weak_ptr<bool> token = alive;

    formatManager.createPluginInstanceAsync (desc, rate, block,
        [this, token, slotId] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (token.expired())
                return;

            auto slotNode = show::findSlot (show, slotId);
            auto it = slots.find (slotId);
            if (! slotNode.isValid() || it == slots.end())
                return; // removed while loading

            if (instance == nullptr)
            {
                slotErrors[slotId] = error.isNotEmpty() ? error : juce::String ("Plugin could not be loaded");
                notify();
                return;
            }

            const int rackChannels = (int) slotNode.getParent()[ids::channels];
            configure (*instance, rackChannels);
            prepare (*instance);

            const auto state = slotNode[ids::state].toString();
            if (state.isNotEmpty())
            {
                juce::MemoryBlock data;
                if (data.fromBase64Encoding (state))
                    instance->setStateInformation (data.getData(), (int) data.getSize());
            }

            instance->addListener (this);
            it->second->plugin = std::move (instance);
            slotErrors.erase (slotId);
            rebuildPlan();
        });
}

void PluginHost::configure (juce::AudioPluginInstance& plugin, int rackChannels)
{
    negotiateLayout (plugin, rackChannels); // the engine adapts mono <-> stereo if the plugin refuses
}

void PluginHost::prepare (juce::AudioPluginInstance& plugin)
{
    const double rate = preparedRate > 0 ? preparedRate : 48000.0;
    const int block = preparedBlock > 0 ? preparedBlock : 512;

    plugin.releaseResources();
    plugin.setRateAndBufferSizeDetails (rate, block);
    plugin.setNonRealtime (false);
    plugin.prepareToPlay (rate, block);
}

// Takes the given slots out of the running plan, waits until the audio thread
// has released them, re-lays them out and prepares them, then puts them back.
void PluginHost::reconfigureSlots (const std::vector<juce::String>& slotIds)
{
    for (auto& id : slotIds)
        excluded.insert (id);

    rebuildPlan();
    const bool released = waitForEngines();

    for (auto& id : slotIds)
    {
        auto it = slots.find (id);
        auto node = show::findSlot (show, id);
        // If the audio thread is stuck we must not touch a plugin it may still be running.
        if (released && it != slots.end() && it->second->plugin != nullptr && node.isValid())
        {
            configure (*it->second->plugin, (int) node.getParent()[ids::channels]);
            prepare (*it->second->plugin);
        }
        excluded.erase (id);
    }

    rebuildPlan();
}

// ---------------------------------------------------------------------------

void PluginHost::rebuildPlan()
{
    // Open every interface a rack refers to (and close the rest).
    std::set<juce::String> wanted;
    for (auto rack : show::getRacks (show))
        for (auto& prop : { ids::inputs, ids::outputs })
            for (auto& p : show::getPorts (rack, prop))
                wanted.insert (p.device);
    devices.setWanted (wanted);

    // A block size bigger than the plugins were prepared for needs a re-prepare.
    if (devices.getMaxOpenBlockSize() > preparedBlock || ! juce::exactlyEqual (devices.getSampleRate(), preparedRate))
    {
        devicesChanged();
        return;
    }

    PlanSpec spec;
    spec.sampleRate = preparedRate;
    spec.alignRacks = alignRacks;
    for (auto& o : devices.getOpen())
        spec.devices.push_back ({ o->name,
                                  o->device->getInputChannelNames().size(),
                                  o->device->getOutputChannelNames().size(),
                                  o->device->getCurrentBufferSizeSamples() });

    std::set<juce::String> used;
    spec.bridges = [this, &used] (const juce::String& key, int channels, int producerBlock, int consumerBlock)
    {
        used.insert (key);
        auto& b = bridges[key];
        if (b == nullptr)
        {
            b = std::make_shared<ClockBridge> (channels);
            b->configure (producerBlock, consumerBlock);
        }
        return b;
    };

    for (auto rack : show::getRacks (show))
    {
        const auto rackId = rack[ids::id].toString();
        auto& runtime = racks[rackId];
        if (runtime == nullptr)
            runtime = std::make_shared<RackRuntime> (rackId);

        RackSpec rs;
        rs.id = rackId;
        rs.numChannels = (int) rack[ids::channels];
        rs.inputs = show::getPorts (rack, ids::inputs);
        rs.outputs = show::getPorts (rack, ids::outputs);
        for (auto& src : show::parseIds (rack[ids::sources].toString()))
            rs.sources.push_back (src);
        rs.critical = (bool) rack[ids::critical];
        rs.bypassed = (bool) rack[ids::bypassed];
        rs.runtime = runtime;

        for (auto slot : rack)
        {
            const auto slotId = slot[ids::id].toString();
            if (excluded.count (slotId) == 0)
                if (auto it = slots.find (slotId); it != slots.end())
                    rs.slots.push_back (it->second);
        }

        spec.racks.push_back (std::move (rs));
    }

    auto result = buildPlan (spec);
    routingError = result.error;

    if (result.error.isEmpty())
    {
        homes = result.homes;
        latencies = result.latencies;
        alignedLatency = result.alignedLatency;
        for (auto& o : devices.getOpen())
        {
            auto it = result.plans.find (o->name);
            o->engine->setPlan (it != result.plans.end() ? std::move (it->second) : nullptr);
        }

        // Forget bridges no plan uses any more (running plans keep their own refs).
        for (auto it = bridges.begin(); it != bridges.end();)
            it = used.count (it->first) ? std::next (it) : bridges.erase (it);
    }

    notify();
}

void PluginHost::notify()
{
    if (onStateChanged)
        onStateChanged();
}

void PluginHost::handleAsyncUpdate()
{
    rebuildPlan();
}

void PluginHost::devicesChanged()
{
    const double rate = devices.getSampleRate();
    const int block = juce::jmax (devices.getBlockSize(), devices.getMaxOpenBlockSize());

    if (! juce::exactlyEqual (rate, preparedRate) || block != preparedBlock)
    {
        preparedRate = rate;
        preparedBlock = block;

        std::vector<juce::String> all;
        for (auto& [id, slot] : slots)
            all.push_back (id);
        reconfigureSlots (all); // rebuilds the plans
    }
    else
    {
        rebuildPlan();
    }
}

// ---------------------------------------------------------------------------

void PluginHost::valueTreePropertyChanged (juce::ValueTree& node, const juce::Identifier& prop)
{
    if (node.hasType (ids::SLOT))
    {
        auto it = slots.find (node[ids::id].toString());
        if (it == slots.end())
            return;

        if (prop == ids::bypassed)
            it->second->userBypassed = (bool) node[ids::bypassed];

        return; // slot properties never need a new plan
    }

    if (node.hasType (ids::RACK))
    {
        if (prop == ids::channels)
        {
            std::vector<juce::String> rackSlots;
            for (auto slot : node)
                rackSlots.push_back (slot[ids::id].toString());
            reconfigureSlots (rackSlots);
            return;
        }

        if (prop == ids::name)
        {
            notify();
            return;
        }

        triggerAsyncUpdate();
    }
}

void PluginHost::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (ids::SLOT))
    {
        attachSlot (child);
    }
    else if (child.hasType (ids::RACK))
    {
        racks[child[ids::id].toString()] = std::make_shared<RackRuntime> (child[ids::id].toString());
        for (auto slot : child)
            attachSlot (slot);
    }

    triggerAsyncUpdate();
}

void PluginHost::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::SLOT))
    {
        detachSlot (child[ids::id].toString());
    }
    else if (child.hasType (ids::RACK))
    {
        for (auto slot : child)
            detachSlot (slot[ids::id].toString());
        racks.erase (child[ids::id].toString());
    }

    // Rebuild immediately so removed plugins leave the audio path right away.
    rebuildPlan();
}

void PluginHost::valueTreeChildOrderChanged (juce::ValueTree&, int, int)
{
    triggerAsyncUpdate();
}

void PluginHost::valueTreeRedirected (juce::ValueTree&)
{
    triggerAsyncUpdate();
}

// ---------------------------------------------------------------------------

std::shared_ptr<SlotRuntime> PluginHost::getSlot (const juce::String& slotId) const
{
    auto it = slots.find (slotId);
    return it != slots.end() ? it->second : nullptr;
}

std::shared_ptr<RackRuntime> PluginHost::getRack (const juce::String& rackId) const
{
    auto it = racks.find (rackId);
    return it != racks.end() ? it->second : nullptr;
}

juce::String PluginHost::getSlotError (const juce::String& slotId) const
{
    auto it = slotErrors.find (slotId);
    return it != slotErrors.end() ? it->second : juce::String();
}

int PluginHost::getSlotLatencySamples (const juce::String& slotId) const
{
    auto slot = getSlot (slotId);
    return slot != nullptr && slot->plugin != nullptr ? slot->plugin->getLatencySamples() : 0;
}

double PluginHost::getSampleRate() const
{
    return preparedRate > 0 ? preparedRate : 48000.0;
}

void PluginHost::captureStates()
{
    for (auto& [id, slot] : slots)
    {
        if (slot->plugin == nullptr)
            continue;

        auto node = show::findSlot (show, id);
        if (! node.isValid())
            continue;

        juce::MemoryBlock block;
        slot->plugin->getStateInformation (block);
        node.setProperty (ids::state, block.toBase64Encoding(), nullptr);
    }
}

void PluginHost::openEditor (const juce::String& slotId)
{
    if (auto it = windows.find (slotId); it != windows.end())
    {
        it->second->toFront (true);
        return;
    }

    auto slot = getSlot (slotId);
    if (slot == nullptr || slot->plugin == nullptr)
        return;

    auto node = show::findSlot (show, slotId);
    const auto title = node.getParent()[ids::name].toString() + "  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")) + "  "
                     + slot->plugin->getName();

    windows[slotId] = std::make_unique<PluginWindow> (*slot->plugin, title, [this, slotId]
    {
        juce::MessageManager::callAsync ([this, slotId, token = std::weak_ptr<bool> (alive)]
        {
            if (! token.expired())
                closeEditor (slotId);
        });
    });
}

void PluginHost::closeEditor (const juce::String& slotId)
{
    windows.erase (slotId);
}

void PluginHost::rearmSlot (const juce::String& slotId)
{
    if (auto slot = getSlot (slotId))
    {
        slot->resetRequested = true;
        slot->fault = FaultReason::none;
    }

    auto node = show::findSlot (show, slotId);
    if (node.isValid())
        node.setProperty (ids::faultBypassed, false, nullptr);

    notify();
}

void PluginHost::restoreRack (const juce::String& rackId)
{
    if (auto rack = getRack (rackId))
        rack->dryFallback = false;

    // Plugins that caused the fallback stay bypassed until re-armed.
    notify();
}

} // namespace lr
