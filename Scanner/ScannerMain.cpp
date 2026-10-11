// LiveRackScanner <formatName> <fileOrIdentifier> <outputXml>
//
// Loads one plugin file and writes the descriptions it contains. Runs as a
// throwaway child process so a crashing plugin can't take the app down.
// Exit codes: 0 = found plugins, 3 = loaded but empty, 1/2 = failure.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Platform/Platform.h"
#include <thread>

int main (int argc, char* argv[])
{
    const auto args = lr::platform::commandLineArguments (argc, argv);
    if (args.size() < 3)
        return 2;

    // A plugin that crashes here must kill only this process, at once (on
    // Windows, not hang on an error dialog until the scan times out).
    lr::platform::configureCrashHandling();

    juce::ScopedJuceInitialiser_GUI juceInit;
   #if JUCE_MAC
    juce::Process::setDockIconVisible (false);
   #endif

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    const juce::String formatName = args[0];
    const juce::String fileOrId = args[1];
    const juce::File output { args[2] };

    juce::AudioPluginFormat* format = nullptr;
    for (auto* f : formats.getFormats())
        if (f->getName() == formatName)
            format = f;

    if (format == nullptr)
        return 2;

    juce::OwnedArray<juce::PluginDescription> found;

    if (formatName == "AudioUnit")
    {
        // AUv3 components finish loading asynchronously and need the message
        // thread free, so scan on a worker and keep the run loop turning.
        std::atomic<bool> done { false };
        std::thread worker ([&]
        {
            format->findAllTypesForFile (found, fileOrId);
            done = true;
        });

        while (! done)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        worker.join();
    }
    else
    {
        // VST3 plugins (Waves shells in particular) touch AppKit while loading,
        // which is only legal on the main thread.
        format->findAllTypesForFile (found, fileOrId);
    }

    if (found.isEmpty())
        return 3;

    juce::XmlElement root ("PLUGINS");
    for (auto* d : found)
        root.addChildElement (d->createXml().release());

    return root.writeTo (output) ? 0 : 1;
}
