#include "MidiFilePlayer.h"

namespace
{
    // JUCE's MidiFile::readFrom rejects files that have any trailing bytes past
    // the declared track chunks. Many real-world SMFs include trailing padding
    // or garbage. This helper walks chunks and computes the exact byte length
    // up to the last valid track, then hands that prefix to juce::MidiFile.
    static bool readSMFLeniently (const juce::File& f, juce::MidiFile& out)
    {
        std::unique_ptr<juce::FileInputStream> stream (f.createInputStream());
        if (stream == nullptr || ! stream->openedOk()) return false;

        juce::MemoryBlock data;
        stream->readIntoMemoryBlock (data);

        const auto*  d    = static_cast<const uint8_t*> (data.getData());
        const size_t size = data.getSize();
        if (size < 14) return false;
        if (d[0] != 'M' || d[1] != 'T' || d[2] != 'h' || d[3] != 'd') return false;

        // Header chunk: 'MThd' + uint32 length (big-endian) + payload.
        const uint32_t headerLen = ((uint32_t) d[4] << 24) | ((uint32_t) d[5] << 16)
                                 | ((uint32_t) d[6] <<  8) |  (uint32_t) d[7];
        size_t pos = 8 + (size_t) headerLen;

        // Walk MTrk chunks until we can't.
        while (pos + 8 <= size)
        {
            if (d[pos] != 'M' || d[pos+1] != 'T' || d[pos+2] != 'r' || d[pos+3] != 'k')
                break; // non-track chunk — stop here
            const uint32_t chunkLen = ((uint32_t) d[pos+4] << 24) | ((uint32_t) d[pos+5] << 16)
                                    | ((uint32_t) d[pos+6] <<  8) |  (uint32_t) d[pos+7];
            const size_t chunkEnd = pos + 8 + chunkLen;
            if (chunkEnd > size) break;
            pos = chunkEnd;
        }
        if (pos == 8 + (size_t) headerLen) return false; // no usable tracks

        juce::MemoryInputStream mem (d, pos, false);
        return out.readFrom (mem);
    }
}

MidiFilePlayer::MidiFilePlayer() = default;

MidiFilePlayer::~MidiFilePlayer()
{
    stop();
}

bool MidiFilePlayer::loadFile (const juce::File& f, juce::String& err)
{
    // Parse into local temporaries first. Only commit to member state once
    // we know the parse succeeded — that way a bad file doesn't destroy the
    // currently-loaded one.
    if (! f.existsAsFile())
    {
        err = "File does not exist: " + f.getFullPathName();
        return false;
    }

    juce::MidiFile newMidiFile;
    if (! readSMFLeniently (f, newMidiFile))
    {
        err = "Failed to parse MIDI file (not a Standard MIDI File?): "
              + f.getFullPathName();
        return false;
    }
    newMidiFile.convertTimestampTicksToSeconds();

    juce::MidiMessageSequence newMerged;
    for (int t = 0; t < newMidiFile.getNumTracks(); ++t)
        newMerged.addSequence (*newMidiFile.getTrack (t), 0.0);
    newMerged.updateMatchedPairs();

    if (newMerged.getNumEvents() == 0)
    {
        err = "MIDI file has no playable events.";
        return false;
    }

    // OK — commit.
    stop();
    merged          = std::move (newMerged);
    nextEventIndex  = 0;
    lengthSeconds   = merged.getEndTime();
    currentPath     = f.getFullPathName();
    description     = f.getFileName()
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
