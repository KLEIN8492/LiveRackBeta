#include "Core/ShowModel.h"

using namespace lr;

class ShowModelTests final : public juce::UnitTest
{
public:
    ShowModelTests() : juce::UnitTest ("ShowModel", "Core") {}

    void runTest() override
    {
        beginTest ("channel lists parse and format");
        {
            expect (show::parseChannels ("0, 1,5") == std::vector<int> { 0, 1, 5 });
            expect (show::parseChannels ("").empty());
            expectEquals (show::formatChannels ({ 2, 3 }), juce::String ("2,3"));
        }

        beginTest ("routing description is 1-based");
        {
            auto s = show::createEmpty();
            auto a = show::addRack (s, "Kick", 1);
            show::setChannels (a, ids::inputs, { 0 });
            show::setChannels (a, ids::outputs, { 2, 3 });
            expect (show::describeRouting (a, s).startsWith ("In 1"));
            expect (show::describeRouting (a, s).endsWith ("Out 3-4"));

            auto bus = show::addRack (s, "Drums", 2);
            bus.setProperty (ids::sources, a[ids::id], nullptr);
            expect (show::describeRouting (bus, s).startsWith ("Bus of Kick"));
        }

        beginTest ("save/load round trip keeps racks, slots and plugin info");
        {
            auto s = show::createEmpty();
            auto vox = show::addRack (s, "Lead Vox", 1);
            vox.setProperty (ids::critical, true, nullptr);
            show::setChannels (vox, ids::inputs, { 0 });

            juce::PluginDescription desc;
            desc.name = "Defeedback";
            desc.pluginFormatName = "VST3";
            desc.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/Defeedback.vst3";
            desc.uniqueId = 1234;
            auto slot = show::addSlot (vox, desc);
            slot.setProperty (ids::state, juce::Base64::toBase64 ("hello"), nullptr);

            auto file = juce::File::createTempFile ("liverack");
            expect (show::saveToFile (s, file));
            auto loaded = show::loadFromFile (file);
            file.deleteFile();

            expect (loaded.isValid());
            auto loadedRack = show::getRacks (loaded).getChild (0);
            expectEquals (loadedRack[ids::name].toString(), juce::String ("Lead Vox"));
            expect ((bool) loadedRack[ids::critical]);

            auto loadedSlot = loadedRack.getChild (0);
            juce::PluginDescription d2;
            expect (show::getPluginDescription (loadedSlot, d2));
            expectEquals (d2.name, juce::String ("Defeedback"));
            expectEquals (d2.fileOrIdentifier, desc.fileOrIdentifier);
            expectEquals (loadedSlot[ids::state].toString(), slot[ids::state].toString());
            expect (show::findSlot (loaded, slot[ids::id].toString()).isValid());
        }
    }
};

static ShowModelTests showModelTests;
