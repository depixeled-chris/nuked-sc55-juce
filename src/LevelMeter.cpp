#include "LevelMeter.h"

LevelMeter::LevelMeter()
{
    setOpaque (false);
}

void LevelMeter::setPeakSource (std::function<float()> fn)
{
    peakSource = std::move (fn);
    if (peakSource) startTimerHz (30); else stopTimer();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);

    g.setColour (juce::Colour::fromRGB (20, 20, 24));
    g.fillRoundedRectangle (b, 2.0f);

    const float w   = b.getWidth();
    const float lvl = juce::jlimit (0.0f, 1.0f, currentLevel);

    if (lvl > 0.0f)
    {
        auto fill = b;
        fill.setWidth (w * lvl);

        // Gradient: green → amber → red as level approaches 1.0
        juce::ColourGradient grad (juce::Colour::fromRGB ( 60, 200,  80), b.getX(),                   b.getY(),
                                   juce::Colour::fromRGB (230,  80,  60), b.getRight(),               b.getY(), false);
        grad.addColour (0.65, juce::Colour::fromRGB (220, 200,  60));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 2.0f);
    }

    // Peak-hold tick
    if (peakHold > 0.005f)
    {
        const float px = b.getX() + w * juce::jlimit (0.0f, 1.0f, peakHold);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (px - 1.0f, b.getY(), 2.0f, b.getHeight()));
    }
}

void LevelMeter::timerCallback()
{
    if (! peakSource) return;
    const float p = peakSource();

    // Decay current
    currentLevel = juce::jmax (currentLevel * 0.85f, p);

    // Hold the peak for ~1s, then drop
    if (p > peakHold)
    {
        peakHold    = p;
        holdCounter = 30;
    }
    else if (--holdCounter <= 0)
    {
        peakHold *= 0.95f;
    }

    repaint();
}
