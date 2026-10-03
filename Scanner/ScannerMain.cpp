// LiveRackScanner <formatName> <fileOrIdentifier> <outputXml>
//
// Loads one plugin file and writes the descriptions it contains. Runs as a
// throwaway child process so a crashing plugin can't take the app down.
// Exit codes: 0 = found plugins, 3 = loaded but empty, 1/2 = failure.

#include <juce_audio_processors/juce_audio_processors.h>
#include <thread>

int main (int argc, char* argv[])
{
    if (argc < 4)
        return 2;

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Process::setDockIconVisible (false);

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    const juce::String formatName = juce::CharPointer_UTF8 (argv[1]);
    const juce::String fileOrId = juce::CharPointer_UTF8 (argv[2]);
    const juce::File output { juce::String (juce::CharPointer_UTF8 (argv[3])) };

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
