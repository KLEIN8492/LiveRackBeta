#include <juce_audio_utils/juce_audio_utils.h>
#include "SessionGuard.h"
#include "DiagnosticsLog.h"
#include "Core/MacActivity.h"
#include "Host/PluginHost.h"
#include "Host/OutOfProcessScanner.h"
#include "UI/MainComponent.h"
#include "UI/SettingsWindow.h"
#include "UI/PluginBrowser.h"

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
        setResizeLimits (1120, 600, 10000, 10000);
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
    // A --snapshot copy is read-only, so it may run next to a live session.
    bool moreThanOneInstanceAllowed() override
    {
        return juce::JUCEApplicationBase::getCommandLineParameters().contains ("--snapshot");
    }

    void initialise (const juce::String& commandLine) override
    {
        // --snapshot <dir>: render each view to PNG without opening audio, then quit.
        snapshotDir = commandLine.fromFirstOccurrenceOf ("--snapshot", false, false).trim().unquoted();
        snapshotMode = commandLine.contains ("--snapshot");

        // Snapshot mode must not touch the live session's files (running
        // marker, crash log, autosave) or start a guard.
        if (! snapshotMode)
        {
            session.begin();
            latencyCritical = std::make_unique<LatencyCriticalActivity> ("LiveRack is processing live audio");
        }

        juce::PropertiesFile::Options options;
        options.applicationName = "LiveRack";
        options.filenameSuffix = ".settings";
        options.folderName = "LiveRack";
        options.osxLibrarySubFolder = "Application Support";
        appProperties.setStorageParameters (options);

        auto& settings = *appProperties.getUserSettings();
        hub.setOpeningAllowed (! snapshotMode);
        hub.setFormat (settings.getDoubleValue ("sampleRate", 48000.0), settings.getIntValue ("bufferSize", 64));
        hub.setBoostEnabled (settings.getBoolValue ("lowLatencyBoost", true));
        hub.setInflightLog (&session.getInflightLog());

        host = std::make_unique<PluginHost> (hub, session.getInflightLog());
        host->setAlignRacks (settings.getBoolValue ("alignRacks", true));
        host->loadKnownPlugins (session.getPluginListFile());

        const auto scannerExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("LiveRackScanner");
        host->getKnownPlugins().setCustomScanner (std::make_unique<OutOfProcessScanner> (scannerExe));
        host->getKnownPlugins().addChangeListener (this);

        auto show = show::loadFromFile (session.getAutosaveFile());
        bool fromBackup = false;
        if (! show.isValid() && session.getAutosaveFile().existsAsFile())
        {
            show = show::loadFromFile (session.getBackupFile());
            fromBackup = show.isValid();
        }
        if (! show.isValid())
            show = show::createEmpty();
        migrate (show);

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
        if (! snapshotMode)
            session.watch (*host, hub);
        if (! snapshotMode)
            diagnostics = std::make_unique<DiagnosticsLog> (hub, SessionGuard::getSupportDirectory().getChildFile ("diagnostics.log"));

        ctx = std::make_unique<AppContext> (AppContext { *host, hub, session, settings, [this] { showSettings(); } });
        auto* content = new MainComponent (*ctx);
        mainComponent = content;
        window = std::make_unique<MainWindow> ("LiveRack", content);

        if (fromBackup)
        {
            content->showBanner ("The autosaved show was unreadable. Restored the backup (up to 5 minutes older).", theme::warn);
        }
        else if (session.previousRunCrashed())
        {
            content->showBanner (suspectNames.isEmpty()
                                     ? juce::String ("Recovered after an unexpected exit. Your show was restored.")
                                     : "Recovered after a crash. Bypassed for safety: " + suspectNames.joinIntoString (", ")
                                           + ". Re-arm them when ready.",
                                 theme::warn);
        }

        commands.registerAllCommandsForTarget (this);
        window->addKeyListener (commands.getKeyMappings());
        setApplicationCommandManagerToWatch (&commands);
        juce::PopupMenu appleMenu;
        appleMenu.addCommandItem (&commands, cmdSettings);
        juce::MenuBarModel::setMacMainMenu (this, &appleMenu);

        if (snapshotMode)
            juce::Timer::callAfterDelay (3000, [this]
            {
                auto dir = juce::File (snapshotDir);
                dir.createDirectory();
                mainComponent->saveSnapshot (dir.getChildFile ("main.png"));
                showSettings();
                settingsWindow->getContent().saveSnapshots (dir);

                // Plugin browser: all, then one category (scratch settings file).
                for (int category : { -1, (int) PluginCategory::reverb })
                {
                    juce::TemporaryFile scratch;
                    juce::PropertiesFile props (scratch.getFile(), {});
                    props.setValue ("browserCategory", category);
                    theme::LookAndFeel lf;
                    PluginBrowser browser (host->getKnownPlugins(), props, {});
                    browser.setLookAndFeel (&lf);
                    auto image = browser.createComponentSnapshot (browser.getLocalBounds(), true, 2.0f);
                    browser.setLookAndFeel (nullptr);
                    auto file = dir.getChildFile (category < 0 ? "browser-all.png" : "browser-reverb.png");
                    file.deleteFile();
                    juce::FileOutputStream out (file);
                    if (out.openedOk())
                        juce::PNGImageFormat().writeImageToStream (image, out);
                }

                juce::JUCEApplication::quit();
            });
    }

    void shutdown() override
    {
        // A second launch is handed to the running copy and shuts down without
        // ever initialising: it must not touch the live session's files.
        if (host == nullptr)
            return;

        juce::MenuBarModel::setMacMainMenu (nullptr);

        if (! snapshotMode)
        {
            session.saveNow (true); // audio is about to stop: safe to ask every plugin
            host->saveKnownPlugins (session.getPluginListFile());
        }
        host->getKnownPlugins().removeChangeListener (this);

        diagnostics.reset();
        settingsWindow.reset();
        window.reset();
        ctx.reset();
        hub.setWanted ({});   // stop every interface before the plugins go
        host.reset();

        if (! snapshotMode)
            session.endCleanly();
        latencyCritical.reset();
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
    // ---- commands & menu ----
    enum CommandIDs { cmdSettings = 0x2001, cmdNew, cmdOpen, cmdSave, cmdSaveAs };

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }

    void getAllCommands (juce::Array<juce::CommandID>& c) override
    {
        juce::JUCEApplication::getAllCommands (c);
        c.addArray ({ cmdSettings, cmdNew, cmdOpen, cmdSave, cmdSaveAs });
    }

    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info) override
    {
        const auto cmd = juce::ModifierKeys::commandModifier;
        switch (id)
        {
            case cmdSettings: info.setInfo ("Settings...", "Interfaces, routing, plugins, performance", "App", 0);
                              info.addDefaultKeypress (',', cmd); break;
            case cmdNew:      info.setInfo ("New Show", {}, "File", 0);        info.addDefaultKeypress ('n', cmd); break;
            case cmdOpen:     info.setInfo ("Open Show...", {}, "File", 0);    info.addDefaultKeypress ('o', cmd); break;
            case cmdSave:     info.setInfo ("Save Show", {}, "File", 0);       info.addDefaultKeypress ('s', cmd); break;
            case cmdSaveAs:   info.setInfo ("Save Show As...", {}, "File", 0);
                              info.addDefaultKeypress ('s', cmd | juce::ModifierKeys::shiftModifier); break;
            default:          juce::JUCEApplication::getCommandInfo (id, info); break;
        }
    }

    bool perform (const InvocationInfo& info) override
    {
        switch (info.commandID)
        {
            case cmdSettings: showSettings(); return true;
            case cmdNew:      confirmNewShow(); return true;
            case cmdOpen:     openShowFile(); return true;
            case cmdSave:     currentFile.existsAsFile() ? saveTo (currentFile) : saveAs(); return true;
            case cmdSaveAs:   saveAs(); return true;
            default:          return juce::JUCEApplication::perform (info);
        }
    }

    juce::StringArray getMenuBarNames() override { return { "File" }; }

    juce::PopupMenu getMenuForIndex (int, const juce::String&) override
    {
        juce::PopupMenu m;
        m.addCommandItem (&commands, cmdNew);
        m.addCommandItem (&commands, cmdOpen);
        m.addSeparator();
        m.addCommandItem (&commands, cmdSave);
        m.addCommandItem (&commands, cmdSaveAs);
        m.addSeparator();
        m.addCommandItem (&commands, cmdSettings);
        return m;
    }

    void menuItemSelected (int, int) override {}

    void showSettings()
    {
        if (settingsWindow == nullptr)
        {
            settingsWindow = std::make_unique<SettingsWindow> (*ctx);
            settingsWindow->addKeyListener (commands.getKeyMappings());
        }
        settingsWindow->setVisible (true);
        settingsWindow->toFront (true);
    }

    // v1 shows stored bare channel numbers for the one device in device.xml.
    void migrate (juce::ValueTree show)
    {
        juce::String in, out;
        if (auto xml = juce::parseXML (session.getDeviceFile()))
        {
            in = xml->getStringAttribute ("audioInputDeviceName");
            out = xml->getStringAttribute ("audioOutputDeviceName");
        }
        show::migrateLegacyPorts (show, in, out);
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
                migrate (loaded);
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
    DeviceHub hub;
    juce::ApplicationCommandManager commands;
    std::unique_ptr<PluginHost> host;
    std::unique_ptr<AppContext> ctx;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<SettingsWindow> settingsWindow;
    MainComponent* mainComponent = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<DiagnosticsLog> diagnostics;
    std::unique_ptr<LatencyCriticalActivity> latencyCritical;
    juce::File currentFile;
    juce::String snapshotDir;
    bool snapshotMode = false;
};
} // namespace lr

START_JUCE_APPLICATION (lr::LiveRackApplication)
