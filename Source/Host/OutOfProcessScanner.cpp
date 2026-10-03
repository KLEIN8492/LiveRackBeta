#include "OutOfProcessScanner.h"

namespace lr
{
OutOfProcessScanner::OutOfProcessScanner (juce::File scannerExecutable)
    : scanner (std::move (scannerExecutable)) {}

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                              juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    if (! scanner.existsAsFile())
    {
        // Running outside the bundle (e.g. from the build dir): scan in-process.
        format.findAllTypesForFile (result, fileOrIdentifier);
        return true;
    }

    juce::TemporaryFile output ("scan", 0);

    juce::ChildProcess child;
    if (! child.start (juce::StringArray { scanner.getFullPathName(), format.getName(),
                                           fileOrIdentifier, output.getFile().getFullPathName() }, 0))
        return false;

    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (child.isRunning())
    {
        if (shouldExit() || juce::Time::getMillisecondCounter() > deadline)
        {
            child.kill();
            return shouldExit(); // a cancelled scan isn't the plugin's fault; a timeout is
        }
        juce::Thread::sleep (20);
    }

    // JUCE reports a child killed by a signal as exit code 0, so success is
    // judged by the results file the scanner writes only when it finishes.
    if (child.getExitCode() == 3) // loaded fine but contains nothing we can host
        return true;

    auto xml = juce::parseXML (output.getFile());
    if (xml == nullptr)
        return false;

    for (auto* e : xml->getChildIterator())
    {
        auto desc = std::make_unique<juce::PluginDescription>();
        if (desc->loadFromXml (*e))
            result.add (desc.release());
    }

    return true;
}

} // namespace lr
