#include "SeekBar.h"

SeekBar::SeekBar()
{
    addAndMakeVisible (slider);
    slider.setRange (0.0, 1.0, 0.0);
    slider.setColour (juce::Slider::backgroundColourId, juce::Colour::fromRGB (50, 50, 56));
    slider.setColour (juce::Slider::trackColourId,      juce::Colour::fromRGB (120, 160, 220));
    slider.setColour (juce::Slider::thumbColourId,      juce::Colour::fromRGB (220, 220, 230));
    slider.addListener (this);

    auto setupTimeLabel = [] (juce::Label& l)
    {
        l.setFont (juce::FontOptions (13.0f));
        l.setColour (juce::Label::textColourId, juce::Colour::fromRGB (180, 180, 190));
    };
    addAndMakeVisible (currentLabel);
    addAndMakeVisible (totalLabel);
    setupTimeLabel (currentLabel);
    setupTimeLabel (totalLabel);
    currentLabel.setJustificationType (juce::Justification::centredLeft);
    totalLabel  .setJustificationType (juce::Justification::centredRight);
    currentLabel.setText ("00:00", juce::dontSendNotification);
    totalLabel  .setText ("00:00", juce::dontSendNotification);
}

void SeekBar::attachToPlayer (MidiFilePlayer* p)
{
    player = p;
    if (player != nullptr)
    {
        slider.setRange (0.0, juce::jmax (1.0, player->getLengthSeconds()), 0.0);
        slider.setValue (0.0, juce::dontSendNotification);
        totalLabel.setText (formatTime (player->getLengthSeconds()), juce::dontSendNotification);
        startTimerHz (15);
    }
    else
    {
        stopTimer();
    }
}

void SeekBar::resized()
{
    auto b = getLocalBounds();
    auto bottom = b.removeFromBottom (16);
    currentLabel.setBounds (bottom.removeFromLeft  (60));
    totalLabel  .setBounds (bottom.removeFromRight (60));
    slider.setBounds (b.reduced (0, 2));
}

void SeekBar::paint (juce::Graphics&) {}

void SeekBar::sliderValueChanged (juce::Slider*)
{
    if (dragging)
        currentLabel.setText (formatTime (slider.getValue()), juce::dontSendNotification);
}

void SeekBar::sliderDragStarted (juce::Slider*)  { dragging = true; }

void SeekBar::sliderDragEnded (juce::Slider*)
{
    dragging = false;
    if (player != nullptr)
        player->seek (slider.getValue());
}

void SeekBar::timerCallback()
{
    if (player == nullptr || dragging)
        return;

    const double len = juce::jmax (1.0, player->getLengthSeconds());
    if (std::abs (slider.getMaximum() - len) > 0.5)
    {
        slider.setRange (0.0, len, 0.0);
        totalLabel.setText (formatTime (player->getLengthSeconds()), juce::dontSendNotification);
    }

    const double pos = player->getPositionSeconds();
    slider.setValue (pos, juce::dontSendNotification);
    currentLabel.setText (formatTime (pos), juce::dontSendNotification);
}

juce::String SeekBar::formatTime (double seconds)
{
    if (seconds < 0 || ! std::isfinite (seconds)) seconds = 0;
    const int total = (int) seconds;
    const int mins  = total / 60;
    const int secs  = total % 60;
    return juce::String::formatted ("%02d:%02d", mins, secs);
}
