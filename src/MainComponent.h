#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "NukedEngine.h"
#include "MidiFilePlayer.h"

class MainComponent : public juce::AudioAppComponent,
                      private juce::ComboBox::Listener,
                      private juce::Button::Listener,
                      private juce::MidiInputCallback,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    // juce::AudioAppComponent
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // juce::Component
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void comboBoxChanged (juce::ComboBox* box) override;
    void buttonClicked   (juce::Button*   button) override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;

    void rebuildMidiDeviceMenu();
    void openMidiInput (const juce::String& identifier);
    void closeMidiInput();
    void pickRomDirectory();
    void loadRoms();
    void pickMidiFile();
    void rebuildAudioSettings();

    // Engine
    NukedEngine engine;

    // Resampler state (Nuked native rate → audio device rate)
    juce::LagrangeInterpolator interpL, interpR;
    double outputSampleRate = 0.0;
    double resampleRatio    = 1.0;
    juce::HeapBlock<float> scratchL, scratchR;
    int scratchCapacity = 0;

    // UI
    juce::Label    headerLabel { {}, "Nuked SC-55 / JV-880 — JUCE host" };
    juce::Label    statusLabel;

    juce::Label    modelLabel { {}, "Model:" };
    juce::ComboBox modelBox;

    juce::Label    romLabel { {}, "ROM directory:" };
    juce::TextButton romBrowseButton { "Browse..." };
    juce::Label    romPathLabel;
    juce::TextButton loadRomsButton { "Load ROMs" };

    juce::Label    midiInLabel { {}, "MIDI input:" };
    juce::ComboBox midiInBox;

    juce::TextButton openFileButton { "Open .mid..." };
    juce::Label    midiFileLabel;
    juce::TextButton playButton  { "Play" };
    juce::TextButton stopButton  { "Stop" };
    juce::TextButton gsResetButton { "GS Reset" };

    juce::Label                 audioSettingsLabel { {}, "Audio device:" };
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSelector;

    // Subsystems
    juce::AudioDeviceManager       midiDeviceManager;
    std::unique_ptr<juce::MidiInput> currentMidiIn;
    juce::String                   currentMidiInIdentifier;

    MidiFilePlayer midiFilePlayer;
    juce::File     currentRomDir;
    juce::File     lastMidiFile;
    juce::ApplicationProperties props;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
