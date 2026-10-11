#include <juce_core/juce_core.h>
#include "Core/DeviceHub.h"
#include "Platform/Platform.h"

using namespace lr;

namespace
{
// Just a type name: what the naming rules look at.
struct FakeType final : juce::AudioIODeviceType
{
    explicit FakeType (const juce::String& name) : juce::AudioIODeviceType (name) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { return {}; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice*, bool) const override { return -1; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return nullptr; }
};
} // namespace

class DeviceNamingTests final : public juce::UnitTest
{
public:
    DeviceNamingTests() : juce::UnitTest ("Device naming", "Core") {}

    const EndpointGroup* find (const std::vector<EndpointGroup>& groups, const juce::String& shown)
    {
        for (auto& g : groups)
            if (g.shown == shown)
                return &g;
        return nullptr;
    }

    void runTest() override
    {
        beginTest ("a name in both lists is one duplex interface (Core Audio, ASIO, ALSA)");
        {
            auto groups = groupEndpoints ({ "Dante Virtual Soundcard", "Mic" }, { "Dante Virtual Soundcard", "Phones" }, false);
            expectEquals ((int) groups.size(), 3);
            auto* dvs = find (groups, "Dante Virtual Soundcard");
            expect (dvs != nullptr && dvs->in == dvs->out);
            auto* mic = find (groups, "Mic");
            expect (mic != nullptr && mic->out.isEmpty());
            auto* phones = find (groups, "Phones");
            expect (phones != nullptr && phones->in.isEmpty());
        }

        beginTest ("WASAPI endpoints of one hardware pair into one interface");
        {
            auto groups = groupEndpoints ({ "Analogue 1 + 2 (Focusrite USB Audio)" },
                                          { "Speakers (Focusrite USB Audio)" }, true);
            expectEquals ((int) groups.size(), 1);
            expectEquals (groups[0].shown, juce::String ("Focusrite USB Audio"));
            expectEquals (groups[0].in, juce::String ("Analogue 1 + 2 (Focusrite USB Audio)"));
            expectEquals (groups[0].out, juce::String ("Speakers (Focusrite USB Audio)"));
        }

        beginTest ("nested parentheses: the whole last group is the hardware");
        {
            auto groups = groupEndpoints ({ "Microphone (Realtek(R) Audio)" }, { "Speakers (Realtek(R) Audio)" }, true);
            expectEquals ((int) groups.size(), 1);
            expectEquals (groups[0].shown, juce::String ("Realtek(R) Audio"));
        }

        beginTest ("ambiguous hardware stays as separate endpoints");
        {
            auto groups = groupEndpoints ({ "Microphone (Realtek Audio)" },
                                          { "Speakers (Realtek Audio)", "Digital Output (Realtek Audio)" }, true);
            expectEquals ((int) groups.size(), 3);
            for (auto& g : groups)
                expect (g.in.isEmpty() || g.out.isEmpty());
        }

        beginTest ("without hardware pairing, separate endpoints never pair");
        {
            auto groups = groupEndpoints ({ "In (X)" }, { "Out (X)" }, false);
            expectEquals ((int) groups.size(), 2);
        }

        beginTest ("ALSA: only direct hardware is offered, shown without the description");
        {
            FakeType alsa ("ALSA");
            const juce::String hw = "Scarlett 18i20 USB, USB Audio; Direct hardware device without any conversions";
            expect (platform::offerDevice (alsa, hw));
            expect (! platform::offerDevice (alsa, "Default ALSA Output"));
            expect (! platform::offerDevice (alsa, "Pulseaudio output"));
            expect (! platform::offerDevice (alsa, "Scarlett 18i20 USB, USB Audio; Front output / input"));
            expectEquals (platform::shortDeviceName (alsa, hw), juce::String ("Scarlett 18i20 USB, USB Audio"));
        }

        beginTest ("driver labels and which drivers load on probe");
        {
            FakeType wasapi ("Windows Audio (Exclusive Mode)"), asio ("ASIO"), jack ("JACK"), core ("CoreAudio");
            expectEquals (platform::driverLabel (wasapi), juce::String ("WASAPI"));
            expectEquals (platform::driverLabel (asio), juce::String ("ASIO"));
            expectEquals (platform::driverLabel (jack), juce::String ("JACK"));
            expectEquals (platform::driverLabel (core), juce::String ("Core Audio"));
            expect (platform::probingLoadsDriver (asio));
            expect (! platform::probingLoadsDriver (wasapi));
            expect (platform::offerDevice (asio, "Dante Virtual Soundcard"));
            expectEquals (platform::shortDeviceName (asio, "Dante Virtual Soundcard"), juce::String ("Dante Virtual Soundcard"));
        }

        beginTest ("this machine reports at least one performance core");
        {
            expect (platform::performanceCoreCount() >= 1);
        }
    }
};

static DeviceNamingTests deviceNamingTests;
