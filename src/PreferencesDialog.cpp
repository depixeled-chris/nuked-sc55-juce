#include "PreferencesDialog.h"

PreferencesDialog::PreferencesDialog (const Initial& init)
    : deviceManager (init.deviceManager),
      currentRomDir (init.romDirectory)
{
    setSize (700, 560);

    auto label = [] (juce::Label& l)
    {
        l.setColour (juce::Label::textColourId, juce::Colour::fromRGB (200, 200, 210));
        l.setFont   (juce::FontOptions (13.0f));
    };

    addAndMakeVisible (modelLabel);   label (modelLabel);
    addAndMakeVisible (modelBox);
    int idx = 1;
    for (auto& name : init.modelNames)
        modelBox.addItem (name, idx++);
    modelBox.setSelectedItemIndex (init.selectedModelIndex, juce::dontSendNotification);
    modelBox.addListener (this);

    addAndMakeVisible (romLabel);     label (romLabel);
    addAndMakeVisible (romBrowseButton);
    romBrowseButton.addListener (this);
    addAndMakeVisible (romPathLabel);
    romPathLabel.setColour (juce::Label::backgroundColourId, juce::Colour::fromRGB (40, 40, 44));
    romPathLabel.setColour (juce::Label::textColourId,       juce::Colour::fromRGB (190, 190, 200));
    romPathLabel.setText (currentRomDir.exists()
                          ? currentRomDir.getFullPathName()
                          : juce::String ("(none — click Browse)"),
                          juce::dontSendNotification);

    addAndMakeVisible (midiInLabel);  label (midiInLabel);
    addAndMakeVisible (midiInBox);
    midiInBox.addListener (this);
    rebuildMidiInputs();
    if (init.selectedMidiInputIdentifier.isNotEmpty())
    {
        const auto devices = juce::MidiInput::getAvailableDevices();
        for (int i = 0; i < devices.size(); ++i)
            if (devices[i].identifier == init.selectedMidiInputIdentifier)
                midiInBox.setSelectedItemIndex (i + 1, juce::dontSendNotification);
    }

    addAndMakeVisible (audioLabel);   label (audioLabel);

    audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        deviceManager, 0, 0, 2, 2,
        /*showMidiInputs*/ false,
        /*showMidiOutputs*/ false,
        /*showChannelsAsStereoPairs*/ true,
        /*hideAdvancedOptionsWithButton*/ false);
    addAndMakeVisible (*audioSelector);
}

PreferencesDialog::~PreferencesDialog() = default;

void PreferencesDialog::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto row = [&area] (int h) { return area.removeFromTop (h).reduced (0, 4); };

    {
        auto r = row (28);
        modelLabel.setBounds (r.removeFromLeft (160));
        modelBox.setBounds (r);
    }
    {
        auto r = row (28);
        romLabel.setBounds (r.removeFromLeft (160));
        romBrowseButton.setBounds (r.removeFromRight (100));
        r.removeFromRight (8);
        romPathLabel.setBounds (r);
    }
    {
        auto r = row (28);
        midiInLabel.setBounds (r.removeFromLeft (160));
        midiInBox.setBounds (r);
    }

    area.removeFromTop (12);
    audioLabel.setBounds (area.removeFromTop (24));
    if (audioSelector)
        audioSelector->setBounds (area);
}

void PreferencesDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (28, 28, 32));
}

void PreferencesDialog::buttonClicked (juce::Button* b)
{
    if (b != &romBrowseButton) return;

    auto chooser = std::make_shared<juce::FileChooser> (
        "Select ROM directory",
        currentRomDir.exists() ? currentRomDir : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        juce::String());

    auto fcFlags = juce::FileBrowserComponent::openMode
                 | juce::FileBrowserComponent::canSelectDirectories;

    chooser->launchAsync (fcFlags, [this, chooser] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();
        if (! dir.isDirectory()) return;

        currentRomDir = dir;
        romPathLabel.setText (dir.getFullPathName(), juce::dontSendNotification);
        if (onRomDirChanged) onRomDirChanged (dir);
    });
}

void PreferencesDialog::comboBoxChanged (juce::ComboBox* box)
{
    if (box == &modelBox)
    {
        if (onModelChanged) onModelChanged (modelBox.getSelectedItemIndex());
    }
    else if (box == &midiInBox)
    {
        const int sel = midiInBox.getSelectedItemIndex();
        juce::String id;
        if (sel > 0)
        {
            const auto devices = juce::MidiInput::getAvailableDevices();
            if (sel - 1 < devices.size())
                id = devices[sel - 1].identifier;
        }
        if (onMidiInputChanged) onMidiInputChanged (id);
    }
}

void PreferencesDialog::rebuildMidiInputs()
{
    midiInBox.clear (juce::dontSendNotification);
    midiInBox.addItem ("(none)", 1);
    const auto devices = juce::MidiInput::getAvailableDevices();
    for (int i = 0; i < devices.size(); ++i)
        midiInBox.addItem (devices[i].name, i + 2);
    midiInBox.setSelectedItemIndex (0, juce::dontSendNotification);
}

void PreferencesDialog::launch (juce::Component* parent, const Initial& init,
                                std::function<void(int)> onModel,
                                std::function<void(const juce::File&)> onRomDir,
                                std::function<void(const juce::String&)> onMidiIn)
{
    auto* dlg = new PreferencesDialog (init);
    dlg->onModelChanged    = std::move (onModel);
    dlg->onRomDirChanged   = std::move (onRomDir);
    dlg->onMidiInputChanged= std::move (onMidiIn);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (dlg);
    opts.dialogTitle = "Preferences";
    opts.dialogBackgroundColour = juce::Colour::fromRGB (28, 28, 32);
    opts.componentToCentreAround = parent;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.launchAsync();
}
