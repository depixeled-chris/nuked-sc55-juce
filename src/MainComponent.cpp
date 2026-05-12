#include "MainComponent.h"

namespace
{
    const juce::String kPropsRomDir   = "romDirectory";
    const juce::String kPropsModel    = "modelIndex";
    const juce::String kPropsMidiIn   = "midiInputIdentifier";
    const juce::String kPropsLastFile = "lastMidiFile";
}

MainComponent::MainComponent()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "NukedSC55JUCE";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.folderName          = "NukedSC55JUCE";
    props.setStorageParameters (opts);

    setSize (840, 560);

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

    addAndMakeVisible (audioSettingsLabel);

    // 2 inputs minimum/max=0, 2 outputs minimum/max=2: we're an output-only host.
    setAudioChannels (0, 2);

    audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        deviceManager, 0, 0, 2, 2,
        /*showMidiInputs*/ false,
        /*showMidiOutputs*/ false,
        /*showChannelsAsStereoPairs*/ true,
        /*hideAdvancedOptionsWithButton*/ true);
    addAndMakeVisible (*audioSelector);

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
}

void MainComponent::releaseResources()
{
    scratchCapacity = 0;
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
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
    audioSettingsLabel.setBounds (area.removeFromTop (24));
    if (audioSelector)
        audioSelector->setBounds (area.removeFromTop (260));

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
    if (button == &romBrowseButton)
        pickRomDirectory();
    else if (button == &loadRomsButton)
        loadRoms();
    else if (button == &openFileButton)
        pickMidiFile();
    else if (button == &playButton)
        midiFilePlayer.start();
    else if (button == &stopButton)
        midiFilePlayer.stop();
    else if (button == &gsResetButton)
        engine.postSystemReset (true);
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    engine.postMidiMessage (m);
}

void MainComponent::timerCallback()
{
    rebuildMidiDeviceMenu();

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
          << " | file "   << (midiFilePlayer.isPlaying() ? "playing" : "stopped");
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
            props.getUserSettings()->setValue (kPropsRomDir, dir.getFullPathName());
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

    juce::String err;
    if (! engine.loadRoms (currentRomDir, model, err))
    {
        statusLabel.setText ("ROM load failed: " + err, juce::dontSendNotification);
        return;
    }

    engine.start();
    resampleRatio = (double) engine.getNativeSampleRate() / juce::jmax (1.0, outputSampleRate);
    interpL.reset();
    interpR.reset();

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
