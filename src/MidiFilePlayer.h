#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>

// Plays a Standard MIDI File by dispatching events to a sink callback in
// wall-clock time. Tempo events embedded in the file are honored via JUCE's
// MidiFile::convertTimestampTicksToSeconds() pass.

class MidiFilePlayer : private juce::HighResolutionTimer
{
public:
    using MessageSink = std::function<void (const juce::MidiMessage&)>;

    MidiFilePlayer();
    ~MidiFilePlayer() override;

    // Loads a Standard MIDI File. Returns false (and populates errorOut) on
    // parse failure. On success, the player is left in a stopped state at
    // position 0.
    bool loadFile (const juce::File& f, juce::String& errorOut);

    void setMessageSink (MessageSink sink) { messageSink = std::move (sink); }

    void start();
    void stop();
    bool isPlaying() const { return playing.load (std::memory_order_acquire); }

    double getLengthSeconds() const { return lengthSeconds; }
    double getPositionSeconds() const;

    juce::String getDescription() const { return description; }

private:
    void hiResTimerCallback() override;

    juce::MidiMessageSequence merged;
    double lengthSeconds = 0.0;
    int    nextEventIndex = 0;
    double playStartTimeMs = 0.0;

    MessageSink messageSink;
    std::atomic<bool> playing { false };
    juce::String description;
};
