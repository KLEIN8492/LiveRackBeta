#include "Core/PortRef.h"
#include "Core/ShowModel.h"

using namespace lr;

class PortRefTests final : public juce::UnitTest
{
public:
    PortRefTests() : juce::UnitTest ("PortRef", "Core") {}

    void runTest() override
    {
        beginTest ("encode/decode round trip, names with odd characters");
        {
            std::vector<PortRef> ports { { "Dante Virtual Soundcard", 0 }, { "Weird #;% Name", 7 }, { "MacBook Pro Microphone", 1 } };
            auto text = encodePorts (ports);
            auto back = decodePorts (text);
            expect (back == ports, text);
        }

        beginTest ("empty and malformed input");
        {
            expect (decodePorts ("").empty());
            expect (decodePorts ("garbage").empty());
            expect (decodePorts ("Dev#3;;").size() == 1);
        }

        beginTest ("legacy numeric lists are recognised");
        {
            expect (isLegacyChannelList ("0,1"));
            expect (isLegacyChannelList ("4"));
            expect (! isLegacyChannelList (""));
            expect (! isLegacyChannelList ("Dante#0"));
        }

        beginTest ("migrating a v1 show attaches device names");
        {
            auto s = show::createEmpty();
            auto rack = show::addRack (s, "Vox", 1);
            rack.setProperty (ids::inputs, "1", nullptr);
            rack.setProperty (ids::outputs, "0,1", nullptr);

            const int changed = show::migrateLegacyPorts (s, "Dante Virtual Soundcard", "Dante Virtual Soundcard");
            expectEquals (changed, 1);

            auto ins = show::getPorts (rack, ids::inputs);
            auto outs = show::getPorts (rack, ids::outputs);
            expect (ins == std::vector<PortRef> { { "Dante Virtual Soundcard", 1 } });
            expect (outs.size() == 2 && outs[1] == PortRef { "Dante Virtual Soundcard", 1 });

            expectEquals (show::migrateLegacyPorts (s, "X", "Y"), 0); // idempotent
        }

        beginTest ("legacy lists without a known device are cleared, not guessed");
        {
            auto s = show::createEmpty();
            auto rack = show::addRack (s, "Vox", 1);
            rack.setProperty (ids::inputs, "1", nullptr);
            show::migrateLegacyPorts (s, {}, {});
            expect (show::getPorts (rack, ids::inputs).empty());
        }

        beginTest ("toggle a port");
        {
            std::vector<PortRef> ports;
            togglePort (ports, { "A", 2 });
            togglePort (ports, { "B", 0 });
            togglePort (ports, { "A", 1 });
            expect (ports == std::vector<PortRef> { { "A", 1 }, { "A", 2 }, { "B", 0 } }); // sorted
            togglePort (ports, { "A", 2 });
            expect (ports == std::vector<PortRef> { { "A", 1 }, { "B", 0 } });
        }
    }
};

static PortRefTests portRefTests;
