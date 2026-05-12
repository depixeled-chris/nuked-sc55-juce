#include "MidiFilePlayer.h"

MidiFilePlayer::MidiFilePlayer() = default;

MidiFilePlayer::~MidiFilePlayer()
{
    stop();
}

bool MidiFilePlayer::loadFile (const juce::File& f, juce::String& err)
{
    // Hard reset of everything regardless of where we were.
    stop();
    merged.clear();
    nextEventIndex = 0;
    lengthSeconds  = 0.0;
    description.clear();
    currentPath.clear();

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

    for (int t = 0; t < midiFile.getNumTracks(); ++t)
        merged.addSequence (*midiFile.getTrack (t), 0.0);
    merged.updateMatchedPairs();

    lengthSeconds = merged.getEndTime();
    currentPath   = f.getFullPathName();
    description   = f.getFileName()
                  + " - " + juce::String (merged.getNumEvents()) + " events, "
                  + juce::String (lengthSeconds, 1) + "s";
    return true;
}

void MidiFilePlayer::play()
{
    if (merged.getNumEvents() == 0)
        return;

    if (nextEventIndex >= merged.getNumEvents())
        nextEventIndex = 0;

    const double posSec = nextEventIndex > 0
        ? merged.getEventPointer (nextEventIndex - 1)->message.getTimeStamp()
        : 0.0;
    playStartTimeMs = juce::Time::getMillisecondCounterHiRes() - posSec * 1000.0;
    playing.store (true, std::memory_order_release);
    startTimer (2);
}

void MidiFilePlayer::pause()
{
    const bool wasPlaying = playing.exchange (false, std::memory_order_acq_rel);
    stopTimer();
    if (wasPlaying)
        silenceAllNotes();
}

void MidiFilePlayer::stop()
{
    pause();
    nextEventIndex = 0;
}

void MidiFilePlayer::seek (double seconds)
{
    seconds = juce::jlimit (0.0, lengthSeconds, seconds);

    // Binary search for first event index >= seconds. MidiMessageSequence is
    // sorted by timestamp so this is well-defined.
    int lo = 0;
    int hi = merged.getNumEvents();
    while (lo < hi)
    {
        const int mid = (lo + hi) / 2;
        if (merged.getEventPointer (mid)->message.getTimeStamp() < seconds)
            lo = mid + 1;
        else
            hi = mid;
    }
    nextEventIndex = lo;

    if (playing.load (std::memory_order_acquire))
    {
        silenceAllNotes();
        playStartTimeMs = juce::Time::getMillisecondCounterHiRes() - seconds * 1000.0;
    }
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

void MidiFilePlayer::silenceAllNotes()
{
    if (! messageSink) return;
    for (int ch = 1; ch <= 16; ++ch)
    {
        messageSink (juce::MidiMessage::controllerEvent (ch, 64,  0));  // Sustain off
        messageSink (juce::MidiMessage::controllerEvent (ch, 123, 0));  // All Notes Off
        messageSink (juce::MidiMessage::controllerEvent (ch, 120, 0));  // All Sound Off
    }
}

void MidiFilePlayer::hiResTimerCallback()
{
    if (! playing.load (std::memory_order_acquire))
        return;

    const double now   = (juce::Time::getMillisecondCounterHiRes() - playStartTimeMs) * 0.001;
    const int    total = merged.getNumEvents();

    while (nextEventIndex < total)
    {
        auto* evt = merged.getEventPointer (nextEventIndex);
        if (evt->message.getTimeStamp() > now)
            break;

        if (! evt->message.isMetaEvent() && messageSink)
            messageSink (evt->message);

        ++nextEventIndex;
    }

    if (nextEventIndex >= total)
        stop();
}
