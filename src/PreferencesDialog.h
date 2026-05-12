#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class PreferencesDialog : public juce::Component,
                          private juce::Button::Listener,
                          private juce::ComboBox::Listener
{
public:
    struct Initial
    {
        juce::AudioDeviceManager& deviceManager;
        juce::StringArray         modelNames;
        int                       selectedModelIndex = 0;
        juce::File                romDirectory;
        juce::String              selectedMidiInputIdentifier; // empty = none
    };

    // Callbacks back to the host
    std::function<void(int)>            onModelChanged;
    std::function<void(const juce::File&)> onRomDirChanged;
    std::function<void(const juce::String&)> onMidiInputChanged; // identifier or empty

    explicit PreferencesDialog (const Initial& init);
    ~PreferencesDialog() override;

    void resized() override;
    void paint   (juce::Graphics& g) override;

    static void launch (juce::Component* parentForModal, const Initial& init,
                        std::function<void(int)>            onModel,
                        std::function<void(const juce::File&)> onRomDir,
                        std::function<void(const juce::String&)> onMidiIn);

private:
    void buttonClicked   (juce::Button*)   override;
    void comboBoxChanged (juce::ComboBox*) override;
    void rebuildMidiInputs();

    juce::AudioDeviceManager& deviceManager;
    juce::File                currentRomDir;

    juce::Label    modelLabel { {}, "Emulator model:" };
    juce::ComboBox modelBox;

    juce::Label      romLabel { {}, "ROM directory:" };
    juce::TextButton romBrowseButton { "Browse..." };
    juce::Label      romPathLabel;

    juce::Label      midiInLabel { {}, "MIDI input:" };
    juce::ComboBox   midiInBox;

    juce::Label                                            audioLabel { {}, "Audio device:" };
    std::unique_ptr<juce::AudioDeviceSelectorComponent>    audioSelector;
};
