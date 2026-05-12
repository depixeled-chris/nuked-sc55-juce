#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class PlayerControls : public juce::Component,
                       private juce::Button::Listener,
                       private juce::Slider::Listener
{
public:
    PlayerControls();

    // Wire callbacks
    std::function<void()>      onPlayPause;
    std::function<void()>      onStop;
    std::function<void(float)> onGainChanged; // 0.0 .. 1.0 linear

    // Sync state from the outside (e.g. when transport ends naturally)
    void setPlayingState (bool playing);
    void setGain (float gain);
    void setEnabledTransport (bool enabled);

    void resized() override;
    void paint   (juce::Graphics& g) override;

private:
    void buttonClicked      (juce::Button* b) override;
    void sliderValueChanged (juce::Slider* s) override;

    juce::TextButton playPauseButton { "Play" };
    juce::TextButton stopButton      { "Stop" };
    juce::Label      volumeLabel     { {}, "Volume" };
    juce::Slider     volumeSlider    { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
};
