// TestRunner.cpp
//
// Headless validation runner. Reads the same settings the GUI app saves
// (ROM dir, model, last MIDI file) and renders the MIDI file offline
// through Nuked-SC55 directly, writing a WAV and a verbose log.
//
// Run from the command line — no UI, no audio device, fully deterministic.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>

#include "backend/emu.h"
#include "backend/rom.h"
#include "backend/rom_io.h"
#include "backend/audio.h"

namespace
{
    juce::String                     g_logPath;
    std::unique_ptr<juce::FileLogger> g_logger;

    void L (const juce::String& s)
    {
        std::cout << s << std::endl;
        if (g_logger != nullptr) g_logger->logMessage (s);
    }

    Romset romsetFromIndex (int idx)
    {
        switch (idx)
        {
            case 0: return Romset::MK2;
            case 1: return Romset::MK1;
            case 2: return Romset::ST;
            case 3: return Romset::CM300;
            case 4: return Romset::SCB55;
            case 5: return Romset::RLP3237;
            case 6: return Romset::SC155;
            case 7: return Romset::SC155MK2;
            case 8: return Romset::JV880;
            default: return Romset::MK2;
        }
    }

    int nativeRateFor (int modelIdx)
    {
        // mk2 (0), st (2), sc155mk2 (7) → 66207, else 64000
        return (modelIdx == 0 || modelIdx == 2 || modelIdx == 7) ? 66207 : 64000;
    }
}

// --- offline sample collector --------------------------------------------------

struct SampleCollector
{
    std::vector<float> left;
    std::vector<float> right;
    std::atomic<uint64_t> count { 0 };
    double minL =  1e30, maxL = -1e30, minR = 1e30, maxR = -1e30;
    double sumAbs = 0.0;
};

static void sampleCallback (void* userdata, const AudioFrame<int32_t>& f)
{
    auto* c = static_cast<SampleCollector*> (userdata);
    constexpr float scale = 1.0f / 536870912.0f;
    const float L = (float) f.left  * scale;
    const float R = (float) f.right * scale;
    c->left.push_back (L);
    c->right.push_back (R);
    c->count.fetch_add (1, std::memory_order_relaxed);
    if (L < c->minL) c->minL = L;
    if (L > c->maxL) c->maxL = L;
    if (R < c->minR) c->minR = R;
    if (R > c->maxR) c->maxR = R;
    c->sumAbs += std::abs (L) + std::abs (R);
}

// --- main --------------------------------------------------------------------

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit; // for FileLogger etc.

    auto exeDir  = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    auto logFile = exeDir.getChildFile ("test-runner.log");
    logFile.deleteFile();
    g_logger = std::make_unique<juce::FileLogger> (logFile, "TestRunner", 0);
    g_logPath = logFile.getFullPathName();

    L ("=== TestRunner start ===");
    L ("Log file: " + g_logPath);

    // --- read same settings the GUI app saves -----------------------------
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "NukedSC55JUCE";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.folderName          = "NukedSC55JUCE";

    juce::ApplicationProperties props;
    props.setStorageParameters (opts);
    auto* user = props.getUserSettings();

    juce::File romDir   (user->getValue ("romDirectory"));
    int        modelIdx = user->getIntValue ("modelIndex", 0);
    juce::File midiFile (user->getValue ("lastMidiFile"));

    juce::String midiArg, romArg;
    int modelArg = -1;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a (argv[i]);
        if (a.startsWith ("--midi=")) midiArg = a.fromFirstOccurrenceOf ("=", false, false);
        else if (a.startsWith ("--rom-dir=")) romArg = a.fromFirstOccurrenceOf ("=", false, false);
        else if (a.startsWith ("--model="))   modelArg = a.fromFirstOccurrenceOf ("=", false, false).getIntValue();
    }
    if (midiArg.isNotEmpty()) midiFile = juce::File (midiArg);
    if (romArg.isNotEmpty())  romDir   = juce::File (romArg);
    if (modelArg >= 0)         modelIdx = modelArg;

    L ("Settings file: " + user->getFile().getFullPathName());
    L ("ROM dir:   " + (romDir.isDirectory() ? romDir.getFullPathName() : "(not set / not a directory)"));
    L ("Model idx: " + juce::String (modelIdx));
    L ("MIDI file: " + (midiFile.existsAsFile() ? midiFile.getFullPathName() : "(not set / does not exist)"));

    if (! romDir.isDirectory())
    {
        L ("FATAL: ROM directory not set or invalid. Run the GUI app first to pick one, or pass --rom-dir=...");
        return 2;
    }
    if (! midiFile.existsAsFile())
    {
        L ("FATAL: MIDI file not set or invalid. Run the GUI app first to load one, or pass --midi=...");
        return 2;
    }

    // --- load ROMs --------------------------------------------------------
    const Romset romset = romsetFromIndex (modelIdx);
    const int nativeRate = nativeRateFor (modelIdx);
    L ("Native sample rate: " + juce::String (nativeRate));

    RomsetInfo info;
    SetRomsetFilenames (info, romDir.getFullPathName().toStdString(), romset, ROMLOCATION_ALL);
    L ("SetRomsetFilenames done. Expected paths:");
    for (size_t i = 0; i < ROMLOCATION_COUNT; ++i)
    {
        if (! info.rom_paths[i].empty())
            L ("  [" + juce::String (ToCString ((RomLocation) i)) + "] " + juce::String (info.rom_paths[i].string()));
    }

    RomLoadStatusSet loadStatus{};
    LoadRomset (info, &loadStatus);
    L ("LoadRomset complete. Per-slot status:");
    for (size_t i = 0; i < loadStatus.size(); ++i)
    {
        if (loadStatus[i] == RomLoadStatus::Loaded || loadStatus[i] == RomLoadStatus::Failed)
        {
            L ("  [" + juce::String (ToCString ((RomLocation) i)) + "] "
                  + juce::String (ToCString (loadStatus[i]))
                  + " (" + juce::String ((int) info.rom_data[i].size()) + " bytes)");
        }
    }

    RomCompletionStatusSet completion{};
    if (! IsCompleteRomset (info, romset, &completion))
    {
        juce::StringArray missing;
        for (size_t i = 0; i < completion.size(); ++i)
            if (completion[i] == RomCompletionStatus::Missing)
                missing.add (ToCString ((RomLocation) i));
        L ("FATAL: incomplete romset. Missing: " + missing.joinIntoString (", "));
        return 3;
    }
    L ("Romset complete.");

    // --- emulator setup ---------------------------------------------------
    auto emu = std::make_unique<Emulator>();
    if (! emu->Init ({ nullptr, {} })) { L ("FATAL: Emulator::Init failed"); return 4; }
    L ("Emulator::Init OK");

    if (! emu->LoadRoms (romset, info)) { L ("FATAL: LoadRoms failed"); return 5; }
    L ("LoadRoms OK");

    emu->Reset();
    emu->GetPCM().enable_oversampling = true;
    L ("Reset + oversampling enabled");

    SampleCollector collector;
    collector.left.reserve (nativeRate * 60);
    collector.right.reserve (nativeRate * 60);
    emu->SetSampleCallback (&sampleCallback, &collector);
    L ("Sample callback installed");

    // Warm up the emulator: run for ~250ms of native samples so it
    // initializes its PCM state and is ready to play.
    const uint64_t warmupSamples = nativeRate / 4;
    L ("Warming up emulator for " + juce::String ((juce::int64) warmupSamples) + " samples...");
    {
        auto t0 = std::chrono::steady_clock::now();
        while (collector.count.load() < warmupSamples)
            emu->Step();
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (t1 - t0).count();
        L ("Warmup done. " + juce::String ((juce::int64) collector.count.load())
              + " samples in " + juce::String ((juce::int64) ms) + " ms wall-clock");
    }

    // Send a GS reset SysEx — covers MIDIs that omit it
    {
        L ("Sending GS reset SysEx...");
        const uint8_t gsReset[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
        for (auto b : gsReset)
            emu->PostMIDI (b);
        // Run a bit so the reset takes effect
        const uint64_t target = collector.count.load() + nativeRate / 10;
        while (collector.count.load() < target) emu->Step();
        L ("GS reset processed.");
    }

    // --- parse the MIDI file ----------------------------------------------
    juce::MidiFile mf;
    {
        std::unique_ptr<juce::FileInputStream> is (midiFile.createInputStream());
        if (is == nullptr || ! is->openedOk()) { L ("FATAL: open MIDI failed"); return 6; }
        if (! mf.readFrom (*is))                { L ("FATAL: parse MIDI failed"); return 7; }
    }
    mf.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence merged;
    for (int t = 0; t < mf.getNumTracks(); ++t) merged.addSequence (*mf.getTrack (t), 0.0);
    merged.updateMatchedPairs();
    L ("MIDI parsed: " + juce::String (merged.getNumEvents())
          + " events, length " + juce::String (merged.getEndTime(), 2) + " s");

    // --- offline render ---------------------------------------------------
    const double renderSeconds = juce::jmin (merged.getEndTime() + 1.0, 30.0);
    const uint64_t totalSamplesNeeded = (uint64_t) (renderSeconds * nativeRate);
    L ("Rendering " + juce::String (renderSeconds, 2) + " s ("
          + juce::String ((juce::int64) totalSamplesNeeded) + " samples)...");

    const uint64_t startCount = collector.count.load();
    uint64_t midiBytesPosted  = 0;
    int     nextEvent          = 0;
    const int numEvents        = merged.getNumEvents();

    while (collector.count.load() - startCount < totalSamplesNeeded)
    {
        const uint64_t relSamples = collector.count.load() - startCount;
        const double   nowSec     = (double) relSamples / nativeRate;

        // Dispatch any events whose time has come
        while (nextEvent < numEvents)
        {
            auto* evt = merged.getEventPointer (nextEvent);
            if (evt->message.getTimeStamp() > nowSec) break;
            if (! evt->message.isMetaEvent())
            {
                const auto* data = evt->message.getRawData();
                const int    n    = evt->message.getRawDataSize();
                for (int i = 0; i < n; ++i)
                    emu->PostMIDI ((uint8_t) data[i]);
                midiBytesPosted += (uint64_t) n;
            }
            ++nextEvent;
        }
        emu->Step();
    }
    L ("Render done. Events sent: " + juce::String (nextEvent) + "/" + juce::String (numEvents)
          + ", MIDI bytes: " + juce::String ((juce::int64) midiBytesPosted)
          + ", samples produced: " + juce::String ((juce::int64) (collector.count.load() - startCount)));

    // --- statistics -------------------------------------------------------
    const uint64_t n = collector.left.size();
    const double meanAbs = collector.sumAbs / juce::jmax ((uint64_t) 1, n * 2);
    L ("Output statistics:");
    L ("  total stereo frames: " + juce::String ((juce::int64) n));
    L ("  L range: [" + juce::String (collector.minL, 6) + ", " + juce::String (collector.maxL, 6) + "]");
    L ("  R range: [" + juce::String (collector.minR, 6) + ", " + juce::String (collector.maxR, 6) + "]");
    L ("  mean |sample|: " + juce::String (meanAbs, 8));

    const bool nonSilent = (collector.maxL > 1e-5) || (-collector.minL > 1e-5)
                        || (collector.maxR > 1e-5) || (-collector.minR > 1e-5);
    L (nonSilent ? "==> AUDIO PRODUCED (non-silent output)"
                 : "==> SILENCE — emulator generated zero-magnitude output");

    // --- write WAV --------------------------------------------------------
    auto wavFile = exeDir.getChildFile ("test-output.wav");
    wavFile.deleteFile();
    juce::WavAudioFormat wavFmt;
    std::unique_ptr<juce::FileOutputStream> fos (wavFile.createOutputStream());
    if (fos == nullptr) { L ("WARN: could not create WAV output stream"); return 0; }
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wavFmt.createWriterFor (fos.get(), nativeRate, 2, 16, {}, 0));
    if (writer == nullptr) { L ("WARN: WAV writer creation failed"); return 0; }
    fos.release(); // ownership transferred

    juce::AudioBuffer<float> buf (2, (int) n);
    juce::FloatVectorOperations::copy (buf.getWritePointer (0), collector.left.data(),  (int) n);
    juce::FloatVectorOperations::copy (buf.getWritePointer (1), collector.right.data(), (int) n);
    writer->writeFromAudioSampleBuffer (buf, 0, (int) n);
    writer.reset();
    L ("Wrote WAV: " + wavFile.getFullPathName() + " (" + juce::String ((juce::int64) wavFile.getSize()) + " bytes)");

    L ("=== TestRunner end ===");
    g_logger.reset();
    return nonSilent ? 0 : 1;
}
