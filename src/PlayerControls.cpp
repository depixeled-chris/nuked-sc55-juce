#include "PlayerControls.h"

PlayerControls::PlayerControls()
{
    addAndMakeVisible (playPauseButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (volumeLabel);
    addAndMakeVisible (volumeSlider);

    playPauseButton.addListener (this);
    stopButton.addListener (this);

    volumeLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (180, 180, 190));
    volumeLabel.setJustificationType (juce::Justification::centredRight);

    volumeSlider.setRange (0.0, 1.0, 0.0);
    volumeSlider.setValue (0.8, juce::dontSendNotification);
    volumeSlider.setNumDecimalPlacesToDisplay (2);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    volumeSlider.setColour (juce::Slider::backgroundColourId, juce::Colour::fromRGB (50, 50, 56));
    volumeSlider.setColour (juce::Slider::trackColourId,      juce::Colour::fromRGB (120, 160, 220));
    volumeSlider.setColour (juce::Slider::thumbColourId,      juce::Colour::fromRGB (220, 220, 230));
    volumeSlider.addListener (this);

    setEnabledTransport (false);
}

void PlayerControls::resized()
{
    auto b = getLocalBounds();
    playPauseButton.setBounds (b.removeFromLeft (90).reduced (4, 6));
    stopButton    .setBounds (b.removeFromLeft (70).reduced (4, 6));

    b.removeFromLeft (16);
    volumeSlider.setBounds (b.removeFromRight (260).reduced (4, 8));
    volumeLabel .setBounds (b.removeFromRight (60));
}

void PlayerControls::paint (juce::Graphics&) {}

void PlayerControls::setPlayingState (bool playing)
{
    playPauseButton.setButtonText (playing ? "Pause" : "Play");
}

void PlayerControls::setGain (float g)
{
    volumeSlider.setValue (g, juce::dontSendNotification);
}

void PlayerControls::setEnabledTransport (bool enabled)
{
    playPauseButton.setEnabled (enabled);
    stopButton    .setEnabled (enabled);
}

void PlayerControls::buttonClicked (juce::Button* b)
{
    if (b == &playPauseButton && onPlayPause) onPlayPause();
    else if (b == &stopButton && onStop)      onStop();
}

void PlayerControls::sliderValueChanged (juce::Slider* s)
{
    if (s == &volumeSlider && onGainChanged)
        onGainChanged ((float) volumeSlider.getValue());
}
