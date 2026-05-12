#include "MainComponent.h"

namespace
{
    const juce::String kPropsRomDir   = "romDirectory";
    const juce::String kPropsModel    = "modelIndex";
    const juce::String kPropsMidiIn   = "midiInputIdentifier";
    const juce::String kPropsLastFile = "lastMidiFile";

    static std::unique_ptr<juce::FileLogger> gLog;

    static void initLogger()
    {
        if (gLog != nullptr) return;
        auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        auto logFile = exeDir.getChildFile ("nuked-sc55-juce.log");
        logFile.deleteFile();
        gLog = std::make_unique<juce::FileLogger> (logFile, "Nuked SC-55 JUCE log", 0);
    }

    static void L (const juce::String& s)
    {
        if (gLog != nullptr) gLog->logMessage (s);
    }
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

    setSize (960, 580);

    addAndMakeVisible (headerLabel);
    headerLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible (modelLabel);
    addAndMakeVisible (modelBox);
    int idx = 1;
    for (auto& name : NukedEngine::getModelDisplayNames())
        modelBox.addItem (name, idx++);
    auto* user = props.getUserSettings();
    modelBox.setSelectedItemIndex (user->getIntValue (kPropsModel, 0));
    modelBox.addListener (this);

    addAndMakeVisible (romLabel);
    addAndMakeVisible (romBrowseButton);
    romBrowseButton.addListener (this);
    addAndMakeVisible (romPathLabel);
    romPathLabel.setColour (juce::Label::backgroundColourId, juce::Colour::fromRGB (40, 40, 44));
    romPathLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    if (auto dir = user->getValue (kPropsRomDir, juce::String()); dir.isNotEmpty())
    {
        currentRomDir = juce::File (dir);
        romPathLabel.setText (currentRomDir.getFullPathName(), juce::dontSendNotification);
    }
    else
    {
        romPathLabel.setText ("(none — click Browse to choose a folder containing your dumped ROMs)",
                              juce::dontSendNotification);
    }

    addAndMakeVisible (loadRomsButton);
    loadRomsButton.addListener (this);

    addAndMakeVisible (midiInLabel);
    addAndMakeVisible (midiInBox);
    midiInBox.addListener (this);
    rebuildMidiDeviceMenu();

    addAndMakeVisible (openFileButton);
    openFileButton.addListener (this);
    addAndMakeVisible (midiFileLabel);
    midiFileLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    midiFileLabel.setText ("(no MIDI file loaded)", juce::dontSendNotification);

    addAndMakeVisible (playButton);
    playButton.addListener (this);
    playButton.setEnabled (false);

    addAndMakeVisible (stopButton);
    stopButton.addListener (this);
    stopButton.setEnabled (false);

    addAndMakeVisible (gsResetButton);
    gsResetButton.addListener (this);

    addAndMakeVisible (audioSettingsButton);
    audioSettingsButton.addListener (this);

    // 2 inputs minimum/max=0, 2 outputs minimum/max=2: we're an output-only host.
    setAudioChannels (0, 2);

    // Hook the MIDI file player's output into the engine
    midiFilePlayer.setMessageSink ([this] (const juce::MidiMessage& m)
    {
        engine.postMidiMessage (m);
    });

    // Try to restore previously-used MIDI input
    if (auto saved = user->getValue (kPropsMidiIn, juce::String()); saved.isNotEmpty())
        openMidiInput (saved);

    if (auto lastFile = user->getValue (kPropsLastFile, juce::String()); lastFile.isNotEmpty())
    {
        juce::File f (lastFile);
        if (f.existsAsFile())
        {
            juce::String err;
            if (midiFilePlayer.loadFile (f, err))
            {
                lastMidiFile = f;
                midiFileLabel.setText (midiFilePlayer.getDescription(), juce::dontSendNotification);
                playButton.setEnabled (engine.isReady());
            }
        }
    }

    statusLabel.setText ("Ready. Choose ROMs to begin.", juce::dontSendNotification);
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
    if (engine.isReady())
        resampleRatio = (double) engine.getNativeSampleRate() / outputSampleRate;
    else
        resampleRatio = 1.0;

    interpL.reset();
    interpR.reset();

    const int needed = (int) std::ceil (samplesPerBlock * resampleRatio) + 16;
    scratchL.allocate ((size_t) needed, true);
    scratchR.allocate ((size_t) needed, true);
    scratchCapacity = needed;

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

    // Diagnostic: log first call and one in every 100
    static std::atomic<int> audioCallCount { 0 };
    const int c = audioCallCount.fetch_add (1);
    if (c == 0 || c % 200 == 0)
    {
        L ("audioCallback#" + juce::String (c)
           + " numSamples=" + juce::String (info.numSamples)
           + " chans=" + juce::String (info.buffer->getNumChannels())
           + " engine.ready=" + juce::String ((int) engine.isReady())
           + " engine.running=" + juce::String ((int) engine.isRunning())
           + " samples=" + juce::String ((juce::int64) engine.getTotalSamplesProduced())
           + " fifo=" + juce::String (engine.getAudioFifoUsedPercent()) + "%");
    }

    if (! engine.isReady() || ! engine.isRunning())
        return;

    const int   numOut = info.numSamples;
    const float ratio  = (float) resampleRatio;
    const int   needed = (int) std::ceil (numOut * resampleRatio) + 4;

    if (needed > scratchCapacity)
    {
        scratchL.allocate ((size_t) needed, true);
        scratchR.allocate ((size_t) needed, true);
        scratchCapacity = needed;
    }

    engine.pullSamples (scratchL.getData(), scratchR.getData(), needed);

    auto* outL = info.buffer->getWritePointer (0, info.startSample);
    auto* outR = info.buffer->getNumChannels() > 1
                 ? info.buffer->getWritePointer (1, info.startSample)
                 : outL;

    interpL.process (ratio, scratchL.getData(), outL, numOut);
    interpR.process (ratio, scratchR.getData(), outR, numOut);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (28, 28, 32));
    g.setColour (juce::Colour::fromRGB (60, 60, 64));
    g.fillRect (0, 50, getWidth(), 1);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    headerLabel.setBounds (area.removeFromTop (28));
    area.removeFromTop (12);

    auto row = [&area] (int h) { return area.removeFromTop (h).reduced (0, 4); };

    {
        auto r = row (28);
        modelLabel.setBounds (r.removeFromLeft (60));
        modelBox.setBounds   (r);
    }

    {
        auto r = row (28);
        romLabel.setBounds (r.removeFromLeft (110));
        loadRomsButton.setBounds (r.removeFromRight (110));
        r.removeFromRight (6);
        romBrowseButton.setBounds (r.removeFromRight (90));
        r.removeFromRight (6);
        romPathLabel.setBounds (r);
    }

    {
        auto r = row (28);
        midiInLabel.setBounds (r.removeFromLeft (110));
        midiInBox.setBounds   (r);
    }

    {
        auto r = row (28);
        openFileButton.setBounds (r.removeFromLeft (110));
        r.removeFromLeft (6);
        gsResetButton.setBounds (r.removeFromRight (90));
        r.removeFromRight (6);
        stopButton.setBounds (r.removeFromRight (80));
        r.removeFromRight (6);
        playButton.setBounds (r.removeFromRight (80));
        r.removeFromRight (12);
        midiFileLabel.setBounds (r);
    }

    area.removeFromTop (8);
    audioSettingsButton.setBounds (area.removeFromTop (32).reduced (0, 4));

    statusLabel.setBounds (area.removeFromBottom (22));
}

void MainComponent::comboBoxChanged (juce::ComboBox* box)
{
    if (box == &modelBox)
    {
        props.getUserSettings()->setValue (kPropsModel, modelBox.getSelectedItemIndex());
        statusLabel.setText ("Model changed — click Load ROMs to apply.", juce::dontSendNotification);
    }
    else if (box == &midiInBox)
    {
        const int idx = midiInBox.getSelectedItemIndex();
        if (idx <= 0)
        {
            closeMidiInput();
            props.getUserSettings()->setValue (kPropsMidiIn, juce::String());
            return;
        }
        const auto devices = juce::MidiInput::getAvailableDevices();
        if (idx - 1 < devices.size())
            openMidiInput (devices[idx - 1].identifier);
    }
}

void MainComponent::buttonClicked (juce::Button* button)
{
    if (button == &romBrowseButton) {
        pickRomDirectory();
    } else if (button == &loadRomsButton) {
        loadRoms();
    } else if (button == &openFileButton) {
        pickMidiFile();
    } else if (button == &playButton) {
        L ("Play clicked. file events=" + juce::String (midiFilePlayer.getDescription()));
        midiFilePlayer.start();
        L ("After start: isPlaying=" + juce::String ((int) midiFilePlayer.isPlaying()));
    } else if (button == &stopButton) {
        L ("Stop clicked.");
        midiFilePlayer.stop();
    } else if (button == &gsResetButton) {
        L ("GS Reset clicked.");
        engine.postSystemReset (true);
    } else if (button == &audioSettingsButton) {
        auto* sel = new juce::AudioDeviceSelectorComponent (
            deviceManager, 0, 0, 2, 2,
            /*showMidiInputs*/ false,
            /*showMidiOutputs*/ false,
            /*showChannelsAsStereoPairs*/ true,
            /*hideAdvancedOptionsWithButton*/ false);
        sel->setSize (600, 500);
        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (sel);
        opts.dialogTitle = "Audio Settings";
        opts.dialogBackgroundColour = juce::Colour::fromRGB (28, 28, 32);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = true;
        opts.resizable = true;
        opts.launchAsync();
    }
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    engine.postMidiMessage (m);
}

void MainComponent::timerCallback()
{
    rebuildMidiDeviceMenu();

    // Periodic state dump for debugging (once a second when running)
    static int tickCount = 0;
    if (engine.isReady() && (++tickCount % 10) == 0)
    {
        L ("[tick] samples=" + juce::String ((juce::int64) engine.getTotalSamplesProduced())
           + " midi=" + juce::String ((juce::int64) engine.getTotalMidiBytesProcessed()) + "B"
           + " fifo=" + juce::String (engine.getAudioFifoUsedPercent()) + "%"
           + " filePlaying=" + juce::String ((int) midiFilePlayer.isPlaying())
           + " filePos=" + juce::String (midiFilePlayer.getPositionSeconds(), 2) + "s");
    }

    juce::String s;
    if (! engine.isReady())
    {
        s = "Engine: not ready - load ROMs to begin.";
    }
    else
    {
        s << "Engine: "  << NukedEngine::displayName (engine.getCurrentModel())
          << " @ "        << engine.getNativeSampleRate() << " Hz"
          << " | output " << (int) outputSampleRate         << " Hz"
          << " | file "   << (midiFilePlayer.isPlaying() ? "playing" : "stopped")
          << " | samples " << (juce::int64) engine.getTotalSamplesProduced()
          << " | peak " << juce::String (engine.getPeakLevel(), 4)
          << " | midi-in " << (juce::int64) engine.getTotalMidiBytesProcessed() << " B"
          << " | fifo "    << engine.getAudioFifoUsedPercent() << "%";
    }
    statusLabel.setText (s, juce::dontSendNotification);

    const bool fileReady = engine.isReady() && midiFilePlayer.getLengthSeconds() > 0.0;
    playButton.setEnabled (fileReady && ! midiFilePlayer.isPlaying());
    stopButton.setEnabled (fileReady && midiFilePlayer.isPlaying());
}

void MainComponent::rebuildMidiDeviceMenu()
{
    const auto devices = juce::MidiInput::getAvailableDevices();

    // Only rebuild if the device list changed
    if (midiInBox.getNumItems() == devices.size() + 1)
        return;

    midiInBox.clear (juce::dontSendNotification);
    midiInBox.addItem ("(none)", 1);
    for (int i = 0; i < devices.size(); ++i)
        midiInBox.addItem (devices[i].name, i + 2);

    // Restore selection
    if (currentMidiInIdentifier.isNotEmpty())
    {
        for (int i = 0; i < devices.size(); ++i)
        {
            if (devices[i].identifier == currentMidiInIdentifier)
            {
                midiInBox.setSelectedItemIndex (i + 1, juce::dontSendNotification);
                return;
            }
        }
    }
    midiInBox.setSelectedItemIndex (0, juce::dontSendNotification);
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

void MainComponent::pickRomDirectory()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Select ROM directory",
        currentRomDir.exists() ? currentRomDir : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        juce::String());

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectDirectories;

    chooser->launchAsync (flags, [this, chooser] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();
        if (dir.isDirectory())
        {
            currentRomDir = dir;
            romPathLabel.setText (dir.getFullPathName(), juce::dontSendNotification);
            // Do NOT persist yet — only save to config after Load ROMs succeeds.
        }
    });
}

void MainComponent::loadRoms()
{
    if (! currentRomDir.isDirectory())
    {
        statusLabel.setText ("Pick a ROM directory first.", juce::dontSendNotification);
        return;
    }

    const auto model = NukedEngine::modelFromIndex (modelBox.getSelectedItemIndex());

    midiFilePlayer.stop();
    engine.stop();

    L ("loadRoms: dir=" + currentRomDir.getFullPathName() + " modelIdx=" + juce::String (modelBox.getSelectedItemIndex()));

    juce::String err;
    if (! engine.loadRoms (currentRomDir, model, err))
    {
        L ("loadRoms FAILED: " + err);
        statusLabel.setText ("ROM load failed: " + err, juce::dontSendNotification);
        return;
    }

    // Only persist the directory once we know it actually contains a working romset.
    props.getUserSettings()->setValue (kPropsRomDir, currentRomDir.getFullPathName());

    L ("loadRoms OK, nativeRate=" + juce::String (engine.getNativeSampleRate()));

    engine.start();
    L ("engine.start() called, isRunning=" + juce::String ((int) engine.isRunning()));

    resampleRatio = (double) engine.getNativeSampleRate() / juce::jmax (1.0, outputSampleRate);
    interpL.reset();
    interpR.reset();
    L ("resampleRatio set to " + juce::String (resampleRatio, 4) + " (output rate " + juce::String (outputSampleRate) + ")");

    statusLabel.setText ("ROMs loaded. Engine running.", juce::dontSendNotification);
}

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

        juce::String err;
        if (! midiFilePlayer.loadFile (f, err))
        {
            statusLabel.setText ("Failed to load: " + err, juce::dontSendNotification);
            return;
        }
        lastMidiFile = f;
        props.getUserSettings()->setValue (kPropsLastFile, f.getFullPathName());
        midiFileLabel.setText (midiFilePlayer.getDescription(), juce::dontSendNotification);
    });
}

void MainComponent::rebuildAudioSettings()
{
    // Hook reserved for v0.2: try to negotiate native sample rate from device.
}
