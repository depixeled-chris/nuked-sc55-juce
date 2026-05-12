#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Horizontal peak-level meter with peak-hold decay. Pulls a single
// 0.0..1.0+ level value from a caller-supplied function at ~30Hz.

class LevelMeter : public juce::Component,
                   private juce::Timer
{
public:
    LevelMeter();

    void setPeakSource (std::function<float()> sourceFn);

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    std::function<float()> peakSource;
    float currentLevel = 0.0f;
    float peakHold     = 0.0f;
    int   holdCounter  = 0;
};
