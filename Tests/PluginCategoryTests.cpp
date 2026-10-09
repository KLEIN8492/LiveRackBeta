#include "Core/PluginCategory.h"
#include <map>

using namespace lr;

namespace
{
juce::PluginDescription plugin (const char* name, const char* category = "Fx", const char* maker = "")
{
    juce::PluginDescription d;
    d.name = name;
    d.category = category;
    d.manufacturerName = maker;
    return d;
}
} // namespace

class PluginCategoryTests final : public juce::UnitTest
{
public:
    PluginCategoryTests() : juce::UnitTest ("PluginCategory", "Core") {}

    void runTest() override
    {
        beginTest ("well-known plugins land in the right category");
        {
            using C = PluginCategory;
            const std::pair<juce::PluginDescription, C> cases[] = {
                { plugin ("De-Feedback", "Fx", "Alpha Labs LLC"), C::feedback },
                { plugin ("X-FDBK Mono"), C::feedback },
                { plugin ("FabFilter Pro-Q 3", "Fx|EQ"), C::eq },
                { plugin ("FabFilter Pro-C 2", "Fx|Dynamics"), C::compressor },
                { plugin ("FabFilter Pro-L 2", "Fx|Dynamics"), C::limiter },
                { plugin ("FabFilter Pro-DS", "Fx|Dynamics"), C::deEsser },
                { plugin ("FabFilter Pro-G", "Fx|Dynamics"), C::gate },
                { plugin ("FabFilter Pro-MB", "Fx|Dynamics"), C::multiband },
                { plugin ("FabFilter Pro-R 2", "Fx|Reverb"), C::reverb },
                { plugin ("FabFilter Timeless 3", "Fx|Delay"), C::delay },
                { plugin ("FabFilter Saturn 2", "Fx|Distortion"), C::saturation },
                { plugin ("L2 Mono"), C::limiter },
                { plugin ("C6 Stereo"), C::multiband },
                { plugin ("F6 Stereo"), C::multiband },
                { plugin ("CLA-2A Mono"), C::compressor },
                { plugin ("RDeEsser Mono"), C::deEsser },
                { plugin ("H-Delay Stereo"), C::delay },
                { plugin ("Abbey Road Plates Stereo"), C::reverb },
                { plugin ("Silk Vocal Live Mono"), C::multiband },
                { plugin ("API 2500", "Fx|Dynamics"), C::compressor },
                { plugin ("UAD Pultec EQP-1A", "Fx|EQ"), C::eq },
                { plugin ("UADx 1176 Rev E Compressor", "Fx|Dynamics"), C::compressor },
                { plugin ("UADx SSL E Channel Strip", "Fx|Channel Strip"), C::channelStrip },
                { plugin ("ValhallaVintageVerb", "Fx|Reverb"), C::reverb },
                { plugin ("ValhallaSpaceModulator", "Fx|Modulation"), C::modulation },
                { plugin ("ValhallaDelay", "Fx|Delay"), C::delay },
                { plugin ("EchoBoy"), C::delay },
                { plugin ("Little AlterBoy"), C::pitch },
                { plugin ("Decapitator"), C::saturation },
                { plugin ("Waves Tune Real-Time Mono", "Fx|Pitch Shift"), C::pitch },
                { plugin ("AmpliTube 5", "Fx|Guitar"), C::ampSim },
                { plugin ("RX 10 Voice De-noise", "Fx|Restoration"), C::restoration },
                { plugin ("Wurli V2", "Instrument|Synth"), C::instrument },
                { plugin ("UAD Marshall Bluesbreaker 1962", "Fx|Guitar"), C::ampSim },
                { plugin ("RX 12 De-reverb", "Fx|Restoration"), C::restoration },
                { plugin ("RX 10 Guitar De-noise", "Fx|Restoration"), C::restoration },
                { plugin ("UAD Chandler Limited Curve Bender", "Fx|EQ"), C::eq },
                { plugin ("UADx 610-B Preamp and EQ", "Fx|Channel Strip"), C::channelStrip },
                { plugin ("TR5 Linear Phase EQ", "Fx|EQ"), C::eq },
                { plugin ("APU Loudness Meter", "Fx|Analyzer"), C::utility },
                { plugin ("UAD MXR Flanger-Doubler", "Fx|Modulation"), C::modulation },
                { plugin ("TR5 Dyna-Mu", "Fx|Dynamics"), C::compressor },
                { plugin ("UAD Friedman Buxom Betty", "Fx|Distortion"), C::ampSim },
                { plugin ("apUnmask", "Fx|Mastering"), C::multiband },
                { plugin ("Mystery Box", "Fx"), C::other },
            };

            for (auto& [desc, expected] : cases)
                expectEquals (juce::String (displayName (classify (desc))), juce::String (displayName (expected)), desc.name);
        }
    }
};

static PluginCategoryTests pluginCategoryTests;

// Dev aid: how the user's real scanned list splits up ("RealPlugins" category).
class PluginCategoryReport final : public juce::UnitTest
{
public:
    PluginCategoryReport() : juce::UnitTest ("PluginCategory report", "RealPlugins") {}

    void runTest() override
    {
        beginTest ("scanned list");
        auto file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Application Support/LiveRack/plugins.xml");
        juce::KnownPluginList list;
        if (auto xml = juce::parseXML (file))
            list.recreateFromXml (*xml);

        std::map<int, int> counts;
        std::map<int, juce::StringArray> samples;
        juce::StringArray others;
        for (auto& d : list.getTypes())
        {
            const auto c = classify (d);
            ++counts[(int) c];
            samples[(int) c].add (d.name);
            if (c == PluginCategory::other)
                others.add (d.name + "  [" + d.category + "]  " + d.manufacturerName);
        }

        for (auto& [c, n] : counts)
        {
            auto& names = samples[c];
            names.sort (true);
            juce::StringArray pick;
            for (int i = 0; i < names.size(); i += juce::jmax (1, names.size() / 9))
                pick.add (names[i]);
            logMessage ("  " + juce::String (n).paddedLeft (' ', 4) + "  " + displayName ((PluginCategory) c) + ":  " + pick.joinIntoString (" | "));
        }
        others.sort (true);
        for (auto& o : others)
            logMessage ("    other: " + o);
    }
};

static PluginCategoryReport pluginCategoryReport;
