#include "MainComponent.h"
#include "PreferencesDialog.h"

namespace
{
    const juce::String kPropsRomDir   = "romDirectory";
    const juce::String kPropsModel    = "modelIndex";
    const juce::String kPropsMidiIn   = "midiInputIdentifier";
    const juce::String kPropsLastFile = "lastMidiFile";
    const juce::String kPropsGain     = "masterGain";

    static std::unique_ptr<juce::FileLogger> gLog;
    static void initLogger()
    {
        if (gLog != nullptr) return;
        auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        auto logFile = exeDir.getChildFile ("nuked-sc55-juce.log");
        logFile.deleteFile();
        gLog = std::make_unique<juce::FileLogger> (logFile, "Nuked SC-55 JUCE log", 0);
    }
    static void L (const juce::String& s) { if (gLog) gLog->logMessage (s); }

    enum MenuId : int
    {
        FileOpenMidi = 1000,
        FileExit,
        EditPreferences = 2000,
        HelpAbout = 3000,
    };
}

MainComponent::MainComponent()
{
    initLogger();
    L ("=== App starting ===");

    juce::PropertiesFile::Options opts;
    opts.applicationName     = "NukedSC55JUCE";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.folderName          = "NukedSC55JUCE";
    props.setStorageParameters (opts);

    auto* user = props.getUserSettings();
    currentModelIndex = user->getIntValue (kPropsModel, 0);
    currentRomDir     = juce::File (user->getValue (kPropsRomDir, juce::String()));
    const auto savedGain  = (float) user->getDoubleValue (kPropsGain, 0.8);
    const auto savedMidi  = user->getValue (kPropsMidiIn, juce::String());
    const auto savedFile  = user->getValue (kPropsLastFile, juce::String());

    setSize (980, 360);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (menuBar);

    addAndMakeVisible (nowPlayingLabel);
    nowPlayingLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    nowPlayingLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    nowPlayingLabel.setJustificationType (juce::Justification::centredLeft);
    nowPlayingLabel.setText ("(no file loaded)", juce::dontSendNotification);

    addAndMakeVisible (seekBar);
    seekBar.attachToPlayer (&midiFilePlayer);

    addAndMakeVisible (controls);
    controls.onPlayPause   = [this] { doPlayPause(); };
    controls.onStop        = [this] { doStop(); };
    controls.onGainChanged = [this] (float g)
    {
        engine.setMasterGain (g);
        props.getUserSettings()->setValue (kPropsGain, (double) g);
    };
    controls.setGain (savedGain);
    engine.setMasterGain (savedGain);

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    // MIDI in → engine
    midiFilePlayer.setMessageSink ([this] (const juce::MidiMessage& m)
    {
        engine.postMidiMessage (m);
    });

    setAudioChannels (0, 2);

    // Restore MIDI input device (if previously selected and still present)
    if (savedMidi.isNotEmpty()) openMidiInput (savedMidi);

    // Restore last opened MIDI file
    if (savedFile.isNotEmpty())
    {
        juce::File f (savedFile);
        if (f.existsAsFile())
        {
            juce::String err;
            if (midiFilePlayer.loadFile (f, err))
            {
                lastMidiFile = f;
                nowPlayingLabel.setText (midiFilePlayer.getDescription(), juce::dontSendNotification);
                seekBar.attachToPlayer (&midiFilePlayer); // re-attach to refresh length
            }
            else
            {
                L ("Restore last file failed: " + err);
            }
        }
    }

    autoLoadRomsIfPossible();
    startTimerHz (10);
}

MainComponent::~MainComponent()
{
    midiFilePlayer.stop();
    closeMidiInput();
    engine.stop();
    shutdownAudio();
}

void MainComponent::prepareToPlay (int samplesPerBlock, double sampleRate)
{
    outputSampleRate = sampleRate;
    resampleRatio = engine.isReady()
        ? (double) engine.getNativeSampleRate() / outputSampleRate
        : 1.0;
    interpL.reset();
    interpR.reset();

    const int needed = (int) std::ceil (samplesPerBlock * resampleRatio) + 16;
    scratchL.allocate ((size_t) needed, true);
    scratchR.allocate ((size_t) needed, true);
    scratchCapacity = needed;

    // Leftover buffer holds resampler "carry" between blocks — usually <8 samples.
    leftoverL.allocate (64, true);
    leftoverR.allocate (64, true);
    leftoverCount = 0;

    L ("prepareToPlay: samplesPerBlock=" + juce::String (samplesPerBlock)
       + " sampleRate=" + juce::String (sampleRate)
       + " engine.ready=" + juce::String ((int) engine.isReady())
       + " ratio=" + juce::String (resampleRatio, 4));
}

void MainComponent::releaseResources()
{
    scratchCapacity = 0;
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    if (! engine.isReady() || ! engine.isRunning()) return;

    const int numOut = info.numSamples;
    const int needed = (int) std::ceil (numOut * resampleRatio) + 4;
    if (needed > scratchCapacity)
    {
        scratchL.allocate ((size_t) needed, true);
        scratchR.allocate ((size_t) needed, true);
        scratchCapacity = needed;
    }

    // Build the resampler input as [leftover from last block | fresh from engine].
    // Then ask process() to produce numOut and track how many inputs it consumed,
    // so the next block continues seamlessly.
    if (leftoverCount > 0)
    {
        std::memcpy (scratchL.getData(), leftoverL.getData(), (size_t) leftoverCount * sizeof (float));
        std::memcpy (scratchR.getData(), leftoverR.getData(), (size_t) leftoverCount * sizeof (float));
    }
    const int toFetch = juce::jmax (0, needed - leftoverCount);
    engine.pullSamples (scratchL.getData() + leftoverCount,
                        scratchR.getData() + leftoverCount,
                        toFetch);
    const int totalIn = leftoverCount + toFetch;

    auto* outL = info.buffer->getWritePointer (0, info.startSample);
    auto* outR = info.buffer->getNumChannels() > 1
                 ? info.buffer->getWritePointer (1, info.startSample)
                 : outL;

    const int consumedL = interpL.process (resampleRatio, scratchL.getData(), outL, numOut);
    const int consumedR = interpR.process (resampleRatio, scratchR.getData(), outR, numOut);
    const int consumed  = juce::jmin (consumedL, consumedR);

    const int newLeftover = juce::jmax (0, juce::jmin (totalIn - consumed, 64));
    if (newLeftover > 0)
    {
        std::memcpy (leftoverL.getData(), scratchL.getData() + consumed,
                     (size_t) newLeftover * sizeof (float));
        std::memcpy (leftoverR.getData(), scratchR.getData() + consumed,
                     (size_t) newLeftover * sizeof (float));
    }
    leftoverCount = newLeftover;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (28, 28, 32));

    // Subtle separator under the now-playing block
    g.setColour (juce::Colour::fromRGB (60, 60, 64));
    auto nb = nowPlayingLabel.getBounds();
    g.fillRect (nb.getX(), nb.getBottom() + 4, nb.getWidth(), 1);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    menuBar.setBounds (area.removeFromTop (24));

    area.reduce (16, 12);

    nowPlayingLabel.setBounds (area.removeFromTop (32));
    area.removeFromTop (16);

    seekBar.setBounds (area.removeFromTop (48));
    area.removeFromTop (12);

    controls.setBounds (area.removeFromTop (44));
    area.removeFromTop (8);

    statusLabel.setBounds (area.removeFromBottom (20));
}

//==============================================================================
// Menu
//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu m;
    if (topLevelMenuIndex == 0) // File
    {
        m.addItem (FileOpenMidi, "Open MIDI...");
        m.addSeparator();
        m.addItem (FileExit, "Exit");
    }
    else if (topLevelMenuIndex == 1) // Edit
    {
        m.addItem (EditPreferences, "Preferences...");
    }
    else if (topLevelMenuIndex == 2) // Help
    {
        m.addItem (HelpAbout, "About");
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    switch (id)
    {
        case FileOpenMidi:     pickMidiFile(); break;
        case FileExit:         juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case EditPreferences:  openPreferences(); break;
        case HelpAbout:
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon,
                "Nuked SC-55 JUCE",
                "JUCE host for the Nuked-SC55 emulator.\n"
                "github.com/depixeled-chris/nuked-sc55-juce");
            break;
    }
}

//==============================================================================
void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    engine.postMidiMessage (m);
}

void MainComponent::timerCallback()
{
    juce::String s;
    if (! engine.isReady())
    {
        s = "Engine: not ready - load ROMs via Preferences.";
    }
    else
    {
        s << "Engine: SC-55mk2 @ "
          << engine.getNativeSampleRate() << " Hz | output "
          << (int) outputSampleRate       << " Hz | peak "
          << juce::String (engine.getPeakLevel(), 3)
          << " | fifo " << engine.getAudioFifoUsedPercent() << "%";
    }
    statusLabel.setText (s, juce::dontSendNotification);

    const bool fileReady = engine.isReady() && midiFilePlayer.getLengthSeconds() > 0.0;
    controls.setEnabledTransport (fileReady);
    controls.setPlayingState (midiFilePlayer.isPlaying());
}

//==============================================================================
void MainComponent::pickMidiFile()
{
    auto initial = lastMidiFile.existsAsFile()
                   ? lastMidiFile.getParentDirectory()
                   : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    auto chooser = std::make_shared<juce::FileChooser> (
        "Open MIDI file", initial, "*.mid;*.midi;*.kar");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;

        L ("pickMidiFile selected: " + f.getFullPathName());
        juce::String err;
        if (! midiFilePlayer.loadFile (f, err))
        {
            L ("loadFile failed: " + err);
            statusLabel.setText ("Failed to load: " + err, juce::dontSendNotification);
            return;
        }
        lastMidiFile = f;
        props.getUserSettings()->setValue (kPropsLastFile, f.getFullPathName());
        nowPlayingLabel.setText (midiFilePlayer.getDescription(), juce::dontSendNotification);
        seekBar.attachToPlayer (&midiFilePlayer); // refresh length
        L ("loadFile OK: " + midiFilePlayer.getDescription());
    });
}

void MainComponent::doPlayPause()
{
    if (midiFilePlayer.isPlaying())
    {
        L ("pause");
        midiFilePlayer.pause();
    }
    else
    {
        L ("play (from " + juce::String (midiFilePlayer.getPositionSeconds(), 2) + "s)");
        midiFilePlayer.play();
    }
}

void MainComponent::doStop()
{
    L ("stop");
    midiFilePlayer.stop();
}

void MainComponent::openPreferences()
{
    PreferencesDialog::Initial init {
        deviceManager,
        NukedEngine::getModelDisplayNames(),
        currentModelIndex,
        currentRomDir,
        currentMidiInIdentifier
    };

    PreferencesDialog::launch (
        this, init,
        [this] (int modelIdx)
        {
            currentModelIndex = modelIdx;
            props.getUserSettings()->setValue (kPropsModel, modelIdx);
            juce::String err;
            if (! loadRomsForCurrentSettings (err))
                statusLabel.setText ("ROM load failed: " + err, juce::dontSendNotification);
        },
        [this] (const juce::File& dir)
        {
            currentRomDir = dir;
            juce::String err;
            if (loadRomsForCurrentSettings (err))
                props.getUserSettings()->setValue (kPropsRomDir, dir.getFullPathName());
            else
                statusLabel.setText ("ROM load failed: " + err, juce::dontSendNotification);
        },
        [this] (const juce::String& id)
        {
            if (id.isEmpty())
            {
                closeMidiInput();
                props.getUserSettings()->setValue (kPropsMidiIn, juce::String());
            }
            else
            {
                openMidiInput (id);
            }
        });
}

void MainComponent::autoLoadRomsIfPossible()
{
    if (! currentRomDir.isDirectory()) return;
    juce::String err;
    if (! loadRomsForCurrentSettings (err))
        L ("Auto-load ROMs failed: " + err);
}

bool MainComponent::loadRomsForCurrentSettings (juce::String& err)
{
    midiFilePlayer.stop();
    engine.stop();

    const auto model = NukedEngine::modelFromIndex (currentModelIndex);
    L ("Loading ROMs: " + currentRomDir.getFullPathName() + ", model=" + juce::String (currentModelIndex));

    if (! engine.loadRoms (currentRomDir, model, err))
    {
        L ("loadRoms failed: " + err);
        return false;
    }

    engine.start();
    resampleRatio = (double) engine.getNativeSampleRate() / juce::jmax (1.0, outputSampleRate);
    interpL.reset();
    interpR.reset();
    L ("loadRoms OK, native=" + juce::String (engine.getNativeSampleRate()));
    return true;
}

void MainComponent::openMidiInput (const juce::String& identifier)
{
    closeMidiInput();
    currentMidiIn = juce::MidiInput::openDevice (identifier, this);
    if (currentMidiIn != nullptr)
    {
        currentMidiIn->start();
        currentMidiInIdentifier = identifier;
        props.getUserSettings()->setValue (kPropsMidiIn, identifier);
    }
}

void MainComponent::closeMidiInput()
{
    if (currentMidiIn != nullptr)
    {
        currentMidiIn->stop();
        currentMidiIn.reset();
    }
    currentMidiInIdentifier.clear();
}
