#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MidiFilePlayer.h"

// Position slider + elapsed/total time labels. Wires to a MidiFilePlayer for
// position updates (auto-poll) and seek (on drag-end).

class SeekBar : public juce::Component,
                private juce::Slider::Listener,
                private juce::Timer
{
public:
    SeekBar();

    void attachToPlayer (MidiFilePlayer* p);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void sliderValueChanged (juce::Slider*)        override;
    void sliderDragStarted  (juce::Slider*)        override;
    void sliderDragEnded    (juce::Slider*)        override;
    void timerCallback()                           override;

    static juce::String formatTime (double seconds);

    juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label  currentLabel, totalLabel;
    MidiFilePlayer* player = nullptr;
    bool dragging = false;
};
