#include "MidiFilePlayer.h"

MidiFilePlayer::MidiFilePlayer() = default;

MidiFilePlayer::~MidiFilePlayer()
{
    stop();
}

bool MidiFilePlayer::loadFile (const juce::File& f, juce::String& err)
{
    stop();

    std::unique_ptr<juce::FileInputStream> stream (f.createInputStream());
    if (stream == nullptr || ! stream->openedOk())
    {
        err = "Could not open file: " + f.getFullPathName();
        return false;
    }

    juce::MidiFile midiFile;
    if (! midiFile.readFrom (*stream))
    {
        err = "Failed to parse MIDI file (not a Standard MIDI File?)";
        return false;
    }

    midiFile.convertTimestampTicksToSeconds();

    merged.clear();
    for (int t = 0; t < midiFile.getNumTracks(); ++t)
        merged.addSequence (*midiFile.getTrack (t), 0.0);
    merged.updateMatchedPairs();

    lengthSeconds  = merged.getEndTime();
    nextEventIndex = 0;
    description    = f.getFileName()
                     + " - " + juce::String (merged.getNumEvents()) + " events, "
                     + juce::String (lengthSeconds, 1) + "s";
    return true;
}

void MidiFilePlayer::start()
{
    if (merged.getNumEvents() == 0)
        return;

    if (nextEventIndex >= merged.getNumEvents())
        nextEventIndex = 0;

    playStartTimeMs = juce::Time::getMillisecondCounterHiRes()
                      - (nextEventIndex > 0 ? merged.getEventPointer (nextEventIndex - 1)->message.getTimeStamp() * 1000.0 : 0.0);
    playing.store (true, std::memory_order_release);
    startTimer (2); // 2 ms tick — well below human-audible jitter for MIDI
}

void MidiFilePlayer::stop()
{
    playing.store (false, std::memory_order_release);
    stopTimer();
}

double MidiFilePlayer::getPositionSeconds() const
{
    if (! playing.load (std::memory_order_acquire))
    {
        if (nextEventIndex == 0) return 0.0;
        return merged.getEventPointer (nextEventIndex - 1)->message.getTimeStamp();
    }
    return (juce::Time::getMillisecondCounterHiRes() - playStartTimeMs) * 0.001;
}

void MidiFilePlayer::hiResTimerCallback()
{
    if (! playing.load (std::memory_order_acquire))
        return;

    const double now = (juce::Time::getMillisecondCounterHiRes() - playStartTimeMs) * 0.001;
    const int total = merged.getNumEvents();

    while (nextEventIndex < total)
    {
        auto* evt = merged.getEventPointer (nextEventIndex);
        if (evt->message.getTimeStamp() > now)
            break;

        // Skip meta events — they're not playable MIDI bytes.
        if (! evt->message.isMetaEvent() && messageSink)
            messageSink (evt->message);

        ++nextEventIndex;
    }

    if (nextEventIndex >= total)
    {
        // Reached end. Send an "all notes off" sweep to be safe.
        if (messageSink)
        {
            for (int ch = 1; ch <= 16; ++ch)
                messageSink (juce::MidiMessage::allNotesOff (ch));
        }
        stop();
    }
}
