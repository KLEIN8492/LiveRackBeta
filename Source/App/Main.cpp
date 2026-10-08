#include <juce_audio_utils/juce_audio_utils.h>
#include "SessionGuard.h"
#include "DiagnosticsLog.h"
#include "Host/PluginHost.h"
#include "Host/OutOfProcessScanner.h"
#include "UI/MainComponent.h"

namespace lr
{
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, juce::Component* content)
        : juce::DocumentWindow (name, theme::bg, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (900, 560, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class LiveRackApplication final : public juce::JUCEApplication,
                                  private juce::MenuBarModel,
                                  private juce::ChangeListener
{
public:
    const juce::String getApplicationName() override    { return "LiveRack"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        session.begin();

        juce::PropertiesFile::Options options;
        options.applicationName = "LiveRack";
        options.filenameSuffix = ".settings";
        options.folderName = "LiveRack";
        options.osxLibrarySubFolder = "Application Support";
        appProperties.setStorageParameters (options);

        auto deviceState = juce::parseXML (session.getDeviceFile());
        const auto deviceError = deviceManager.initialise (256, 256, deviceState.get(), true);

        engine.setInflightLog (&session.getInflightLog());
        engine.setLowLatencyBoost (appProperties.getUserSettings()->getBoolValue ("lowLatencyBoost", true));
        host = std::make_unique<PluginHost> (deviceManager, engine, session.getInflightLog());
        host->loadKnownPlugins (session.getPluginListFile());

        const auto scannerExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("LiveRackScanner");
        host->getKnownPlugins().setCustomScanner (std::make_unique<OutOfProcessScanner> (scannerExe));
        host->getKnownPlugins().addChangeListener (this);

        auto show = show::loadFromFile (session.getAutosaveFile());
        if (! show.isValid())
            show = show::createEmpty();

        juce::StringArray suspectNames;
        if (session.previousRunCrashed())
        {
            for (auto& id : session.getSuspectSlots())
            {
                auto slot = show::findSlot (show, id);
                if (! slot.isValid())
                    continue;
                slot.setProperty (ids::faultBypassed, true, nullptr);
                juce::PluginDescription d;
                show::getPluginDescription (slot, d);
                suspectNames.add (d.name + " on " + slot.getParent()[ids::name].toString());
            }

            // Slots marked by the stall detector were saved straight into the show.
            for (auto rack : show::getRacks (show))
                for (auto slot : rack)
                    if ((bool) slot[ids::faultBypassed])
                    {
                        juce::PluginDescription d;
                        show::getPluginDescription (slot, d);
                        suspectNames.addIfNotAlreadyThere (d.name + " on " + rack[ids::name].toString());
                    }
        }

        host->setShow (show);
        deviceManager.addAudioCallback (&engine);
        session.watch (*host, engine);
        diagnostics = std::make_unique<DiagnosticsLog> (deviceManager, engine,
                                                        SessionGuard::getSupportDirectory().getChildFile ("diagnostics.log"));

        ctx = std::make_unique<AppContext> (AppContext { *host, deviceManager, engine, session, *appProperties.getUserSettings() });
        auto* content = new MainComponent (*ctx);
        mainComponent = content;
        window = std::make_unique<MainWindow> ("LiveRack", content);

        if (session.previousRunCrashed())
        {
            content->showBanner (suspectNames.isEmpty()
                                     ? juce::String ("Recovered after an unexpected exit. Your show was restored.")
                                     : "Recovered after a crash. Bypassed for safety: " + suspectNames.joinIntoString (", ")
                                           + ". Re-arm them when ready.",
                                 theme::warn);
        }
        else if (deviceError.isNotEmpty())
        {
            content->showBanner ("Audio device problem: " + deviceError, theme::fault);
        }

        juce::MenuBarModel::setMacMainMenu (this);
    }

    void shutdown() override
    {
        juce::MenuBarModel::setMacMainMenu (nullptr);

        session.saveNow();
        if (auto xml = deviceManager.createStateXml())
            xml->writeTo (session.getDeviceFile());
        host->saveKnownPlugins (session.getPluginListFile());
        host->getKnownPlugins().removeChangeListener (this);

        diagnostics.reset();
        window.reset();
        ctx.reset();
        deviceManager.removeAudioCallback (&engine);
        deviceManager.closeAudioDevice();
        host.reset();

        session.endCleanly();
    }

    void systemRequestedQuit() override
    {
        // A stray Cmd-Q mid-show would kill the audio: always ask.
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                     "Quit LiveRack?",
                                                                     "Audio processing will stop. Your show is saved automatically.",
                                                                     "Quit", "Keep running");
        juce::AlertWindow::showAsync (options, [] (int result)
        {
            if (result == 1)
                juce::JUCEApplication::quit();
        });
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    // ---- menu ----
    enum { newShow = 1, openShow, saveShow, saveShowAs };

    juce::StringArray getMenuBarNames() override { return { "File" }; }

    juce::PopupMenu getMenuForIndex (int, const juce::String&) override
    {
        juce::PopupMenu m;
        m.addItem (newShow, "New Show");
        m.addItem (openShow, "Open Show...");
        m.addSeparator();
        m.addItem (saveShow, "Save Show");
        m.addItem (saveShowAs, "Save Show As...");
        return m;
    }

    void menuItemSelected (int id, int) override
    {
        switch (id)
        {
            case newShow:    confirmNewShow(); break;
            case openShow:   openShowFile(); break;
            case saveShow:   currentFile.existsAsFile() ? saveTo (currentFile) : saveAs(); break;
            case saveShowAs: saveAs(); break;
            default: break;
        }
    }

    void confirmNewShow()
    {
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon,
                                                                     "Start a new show?",
                                                                     "All racks and plugins will be removed.",
                                                                     "New Show", "Cancel");
        juce::AlertWindow::showAsync (options, [this] (int result)
        {
            if (result != 1)
                return;
            currentFile = juce::File();
            loadShow (show::createEmpty());
        });
    }

    void openShowFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open show", showsFolder(), "*.liverack");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File())
                    return;

                auto loaded = show::loadFromFile (file);
                if (! loaded.isValid())
                {
                    mainComponent->showBanner ("Couldn't read " + file.getFileName(), theme::fault);
                    return;
                }
                currentFile = file;
                loadShow (loaded);
            });
    }

    void saveAs()
    {
        chooser = std::make_unique<juce::FileChooser> ("Save show", showsFolder().getChildFile ("Show.liverack"), "*.liverack");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File())
                    return;
                saveTo (file.withFileExtension ("liverack"));
            });
    }

    void saveTo (const juce::File& file)
    {
        host->captureStates();
        if (show::saveToFile (host->getShow(), file))
        {
            currentFile = file;
            window->setName ("LiveRack  -  " + file.getFileNameWithoutExtension());
        }
        else
        {
            mainComponent->showBanner ("Couldn't save " + file.getFullPathName(), theme::fault);
        }
    }

    void loadShow (juce::ValueTree newShow)
    {
        host->setShow (newShow);
        session.saveNow();
        mainComponent->showChanged();
        window->setName (currentFile.existsAsFile() ? "LiveRack  -  " + currentFile.getFileNameWithoutExtension()
                                                    : juce::String ("LiveRack"));
    }

    static juce::File showsFolder()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("LiveRack Shows");
        dir.createDirectory();
        return dir;
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        host->saveKnownPlugins (session.getPluginListFile());
    }

    SessionGuard session;
    juce::ApplicationProperties appProperties;
    juce::AudioDeviceManager deviceManager;
    AudioEngine engine;
    std::unique_ptr<PluginHost> host;
    std::unique_ptr<AppContext> ctx;
    std::unique_ptr<MainWindow> window;
    MainComponent* mainComponent = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<DiagnosticsLog> diagnostics;
    juce::File currentFile;
};
} // namespace lr

START_JUCE_APPLICATION (lr::LiveRackApplication)
