#include "NukedEngine.h"

#include <atomic>
#include <thread>
#include <chrono>
#include <array>

// Nuked backend headers. These are C++20 and pull in <span> and <filesystem>.
#include "backend/emu.h"
#include "backend/rom.h"
#include "backend/rom_io.h"
#include "backend/audio.h"

namespace
{
    constexpr float kSampleScale = 1.0f / 536870912.0f; // matches Normalize() in Nuked's audio.h

    int nativeRateForModel (NukedEngine::Model m)
    {
        // From Nuked-SC55 USAGE.md / standard frontend:
        //   SC-55mk2 / SC-55st / SC-155mk2 produce ~66207 Hz
        //   everything else                produces  64000 Hz
        switch (m)
        {
            case NukedEngine::Model::SC55mk2:
            case NukedEngine::Model::SC155mk2:
                return 66207;
            default:
                return 64000;
        }
    }

    Romset toNukedRomset (NukedEngine::Model m)
    {
        switch (m)
        {
            case NukedEngine::Model::SC55mk2:  return Romset::MK2;
            case NukedEngine::Model::SC55:     return Romset::MK1;
            case NukedEngine::Model::SC55st:   return Romset::ST;
            case NukedEngine::Model::CM300:    return Romset::CM300;
            case NukedEngine::Model::SCB55:    return Romset::SCB55;
            case NukedEngine::Model::RLP3237:  return Romset::RLP3237;
            case NukedEngine::Model::SC155:    return Romset::SC155;
            case NukedEngine::Model::SC155mk2: return Romset::SC155MK2;
            case NukedEngine::Model::JV880:    return Romset::JV880;
        }
        return Romset::MK2;
    }
}

//==============================================================================
// Pimpl
//==============================================================================

class NukedEngine::Impl
{
public:
    Impl()
    {
        // Audio ringbuffer — ~0.5 seconds at native rates.
        constexpr int kAudioSlots = 1 << 15; // 32768 stereo frames
        audioL.allocate ((size_t) kAudioSlots, true);
        audioR.allocate ((size_t) kAudioSlots, true);
        audioFifo = std::make_unique<juce::AbstractFifo> (kAudioSlots);

        // MIDI ringbuffer
        constexpr int kMidiSlots = 1 << 13; // 8192 bytes
        midiBuf.allocate ((size_t) kMidiSlots, true);
        midiFifo = std::make_unique<juce::AbstractFifo> (kMidiSlots);
    }

    ~Impl()
    {
        stop();
    }

    bool loadRoms (const juce::File& dir, NukedEngine::Model model, juce::String& err)
    {
        stop();
        ready.store (false, std::memory_order_release); // honest until proven ready
        emu.reset();

        if (! dir.isDirectory())
        {
            err = "ROM directory does not exist: " + dir.getFullPathName();
            return false;
        }

        const Romset romset = toNukedRomset (model);
        const std::filesystem::path basePath = dir.getFullPathName().toStdString();

        RomsetInfo info;
        SetRomsetFilenames (info, basePath, romset, ROMLOCATION_ALL);

        RomLoadStatusSet loadStatus{};
        LoadRomset (info, &loadStatus);

        // Stricter than IsCompleteRomset: every REQUIRED slot must have
        // non-empty rom_data after LoadRomset. IsCompleteRomset only checks
        // that paths are set (which SetRomsetFilenames does unconditionally),
        // so it returns true even when files don't exist on disk.
        juce::StringArray missing;
        for (size_t i = 0; i < ROMLOCATION_COUNT; ++i)
        {
            const auto loc = (RomLocation) i;
            if (! IsRequiredRom (romset, loc))
                continue;
            if (info.rom_data[i].empty())
                missing.add (juce::String (ToCString (loc)));
        }
        if (! missing.isEmpty())
        {
            err = "Required ROM file(s) not found in '"
                  + juce::String (basePath.string()) + "': "
                  + missing.joinIntoString (", ")
                  + ". Did you pick a subdirectory by mistake?";
            return false;
        }

        emu = std::make_unique<Emulator>();
        if (! emu->Init ({ /* lcd_backend = */ nullptr, /* nvram_filename = */ {} }))
        {
            err = "Emulator::Init failed.";
            emu.reset();
            return false;
        }

        // RomsetInfo must outlive the emulator: it holds the std::vector<uint8_t>
        // buffers that the emulator may reference by pointer / span.
        romsetInfo = std::move (info);

        if (! emu->LoadRoms (romset, romsetInfo))
        {
            err = "Emulator::LoadRoms failed.";
            emu.reset();
            return false;
        }

        emu->Reset();
        emu->GetPCM().enable_oversampling = true; // match the native-rate values we report
        emu->SetSampleCallback (&Impl::sampleCallbackTrampoline, this);

        totalSamples.store (0);
        totalMidiBytes.store (0);

        currentModel = model;
        nativeRate = nativeRateForModel (model);
        ready.store (true, std::memory_order_release);
        return true;
    }

    int getNativeSampleRate() const { return nativeRate; }
    bool isReady() const { return ready.load (std::memory_order_acquire); }
    NukedEngine::Model getCurrentModel() const { return currentModel; }

    void postSystemReset (bool gs)
    {
        if (! isReady()) return;
        pendingReset.store (gs ? 1 : 2, std::memory_order_release);
    }

    void postMidiMessage (const juce::MidiMessage& msg)
    {
        if (! isReady()) return;
        const auto* data = msg.getRawData();
        const int   size = msg.getRawDataSize();
        if (size <= 0) return;

        const auto scope = midiFifo->write (size);
        if (scope.blockSize1 + scope.blockSize2 < size)
            return; // queue full — drop message
        for (int i = 0; i < scope.blockSize1; ++i)
            midiBuf[scope.startIndex1 + i] = data[i];
        for (int i = 0; i < scope.blockSize2; ++i)
            midiBuf[scope.startIndex2 + i] = data[scope.blockSize1 + i];
    }

    void start()
    {
        if (! isReady())  return;
        if (running.load (std::memory_order_acquire)) return;

        running.store (true, std::memory_order_release);
        emuThread = std::thread ([this] { runLoop(); });
    }

    void stop()
    {
        running.store (false, std::memory_order_release);
        if (emuThread.joinable())
            emuThread.join();
    }

    bool isRunning() const { return running.load (std::memory_order_acquire); }

    uint64_t getTotalSamplesProduced() const   { return totalSamples.load (std::memory_order_relaxed); }
    uint64_t getTotalMidiBytesProcessed() const{ return totalMidiBytes.load (std::memory_order_relaxed); }
    float    getPeakLevel() const              { return (float) peakAbs.load (std::memory_order_relaxed) * kSampleScale; }
    int      getAudioFifoUsedPercent() const
    {
        const int total = audioFifo ? audioFifo->getTotalSize() : 1;
        const int used  = audioFifo ? audioFifo->getNumReady()  : 0;
        return (int) (100.0 * (double) used / (double) total);
    }

    int pullSamples (float* outL, float* outR, int numFrames)
    {
        if (! isReady())
        {
            juce::FloatVectorOperations::clear (outL, numFrames);
            juce::FloatVectorOperations::clear (outR, numFrames);
            return 0;
        }

        const auto scope = audioFifo->read (numFrames);
        const int delivered = scope.blockSize1 + scope.blockSize2;
        const float g = masterGain.load (std::memory_order_relaxed);

        for (int i = 0; i < scope.blockSize1; ++i)
        {
            outL[i] = audioL[scope.startIndex1 + i] * g;
            outR[i] = audioR[scope.startIndex1 + i] * g;
        }
        for (int i = 0; i < scope.blockSize2; ++i)
        {
            outL[scope.blockSize1 + i] = audioL[scope.startIndex2 + i] * g;
            outR[scope.blockSize1 + i] = audioR[scope.startIndex2 + i] * g;
        }

        if (delivered < numFrames)
        {
            juce::FloatVectorOperations::clear (outL + delivered, numFrames - delivered);
            juce::FloatVectorOperations::clear (outR + delivered, numFrames - delivered);
        }
        return delivered;
    }

    void  setMasterGain (float g) { masterGain.store (juce::jlimit (0.0f, 4.0f, g), std::memory_order_relaxed); }
    float getMasterGain() const   { return masterGain.load (std::memory_order_relaxed); }

private:
    void runLoop()
    {
        using namespace std::chrono_literals;

        while (running.load (std::memory_order_acquire))
        {
            // Drain MIDI input
            drainMidi();

            // Apply pending system reset, if any
            if (auto resetFlag = pendingReset.exchange (0, std::memory_order_acq_rel))
            {
                emu->PostSystemReset (resetFlag == 1
                                      ? EMU_SystemReset::GS_RESET
                                      : EMU_SystemReset::GM_RESET);
            }

            // Throttle: if the audio ringbuffer is mostly full, sleep instead
            // of generating more samples. The audio thread will drain it.
            constexpr int kHighWaterMark = (1 << 15) * 3 / 4;
            if (audioFifo->getNumReady() > kHighWaterMark)
            {
                std::this_thread::sleep_for (500us);
                continue;
            }

            // Step the emulator. Each Step() may or may not produce a sample
            // (depending on internal cycle counters). Loop a small batch so
            // we don't pay the syscall cost per single step.
            for (int i = 0; i < 64; ++i)
                emu->Step();
        }
    }

    void drainMidi()
    {
        const int avail = midiFifo->getNumReady();
        if (avail <= 0) return;

        const auto scope = midiFifo->read (avail);
        for (int i = 0; i < scope.blockSize1; ++i)
            emu->PostMIDI (midiBuf[scope.startIndex1 + i]);
        for (int i = 0; i < scope.blockSize2; ++i)
            emu->PostMIDI (midiBuf[scope.startIndex2 + i]);

        totalMidiBytes.fetch_add ((uint64_t) avail, std::memory_order_relaxed);
    }

    static void sampleCallbackTrampoline (void* userdata, const AudioFrame<int32_t>& f)
    {
        static_cast<Impl*> (userdata)->onSample (f);
    }

    void onSample (const AudioFrame<int32_t>& f)
    {
        totalSamples.fetch_add (1, std::memory_order_relaxed);

        // Diagnostic: track absolute peak ever observed (saturates at INT32_MAX/2).
        const int32_t absL = f.left  < 0 ? -f.left  : f.left;
        const int32_t absR = f.right < 0 ? -f.right : f.right;
        const int32_t abs  = absL > absR ? absL : absR;
        int32_t prev = peakAbs.load (std::memory_order_relaxed);
        while (abs > prev
               && ! peakAbs.compare_exchange_weak (prev, abs,
                                                   std::memory_order_relaxed,
                                                   std::memory_order_relaxed))
        { /* retry */ }

        const auto scope = audioFifo->write (1);
        if (scope.blockSize1 == 1)
        {
            audioL[scope.startIndex1] = (float) f.left  * kSampleScale;
            audioR[scope.startIndex1] = (float) f.right * kSampleScale;
        }
        else if (scope.blockSize2 == 1)
        {
            audioL[scope.startIndex2] = (float) f.left  * kSampleScale;
            audioR[scope.startIndex2] = (float) f.right * kSampleScale;
        }
        // If FIFO full, drop the sample. Throttle in runLoop() prevents this in practice.
    }

    std::unique_ptr<Emulator> emu;
    RomsetInfo romsetInfo; // must outlive emu — holds ROM byte vectors
    NukedEngine::Model currentModel = NukedEngine::Model::SC55mk2;
    int  nativeRate = 0;
    std::atomic<bool> ready { false };
    std::atomic<bool> running { false };
    std::atomic<int>  pendingReset { 0 }; // 0 = none, 1 = GS, 2 = GM
    std::atomic<uint64_t> totalSamples { 0 };
    std::atomic<uint64_t> totalMidiBytes { 0 };
    std::atomic<int32_t>  peakAbs { 0 };
    std::atomic<float>    masterGain { 1.0f };

    juce::HeapBlock<float>  audioL, audioR;
    std::unique_ptr<juce::AbstractFifo> audioFifo;

    juce::HeapBlock<uint8_t> midiBuf;
    std::unique_ptr<juce::AbstractFifo> midiFifo;

    std::thread emuThread;
};

//==============================================================================
// Forwarding facade
//==============================================================================

NukedEngine::NukedEngine() : impl (std::make_unique<Impl>()) {}
NukedEngine::~NukedEngine() = default;

juce::StringArray NukedEngine::getModelDisplayNames()
{
    return {
        "Roland SC-55mk2 (v1.01)",
        "Roland SC-55 (v1.21)",
        "Roland SC-55st (v1.01)",
        "Roland CM-300 / SCC-1",
        "Roland SCB-55",
        "Roland RLP-3237",
        "Roland SC-155",
        "Roland SC-155mk2",
        "Roland JV-880",
    };
}

NukedEngine::Model NukedEngine::modelFromIndex (int i)
{
    switch (i)
    {
        case 0: return Model::SC55mk2;
        case 1: return Model::SC55;
        case 2: return Model::SC55st;
        case 3: return Model::CM300;
        case 4: return Model::SCB55;
        case 5: return Model::RLP3237;
        case 6: return Model::SC155;
        case 7: return Model::SC155mk2;
        case 8: return Model::JV880;
        default: return Model::SC55mk2;
    }
}

int NukedEngine::indexFromModel (Model m)
{
    switch (m)
    {
        case Model::SC55mk2:  return 0;
        case Model::SC55:     return 1;
        case Model::SC55st:   return 2;
        case Model::CM300:    return 3;
        case Model::SCB55:    return 4;
        case Model::RLP3237:  return 5;
        case Model::SC155:    return 6;
        case Model::SC155mk2: return 7;
        case Model::JV880:    return 8;
    }
    return 0;
}

juce::String NukedEngine::displayName (Model m)
{
    return getModelDisplayNames()[indexFromModel (m)];
}

bool NukedEngine::loadRoms (const juce::File& dir, Model model, juce::String& err)
{
    return impl->loadRoms (dir, model, err);
}

bool  NukedEngine::isReady() const             { return impl->isReady(); }
int   NukedEngine::getNativeSampleRate() const { return impl->getNativeSampleRate(); }
NukedEngine::Model NukedEngine::getCurrentModel() const { return impl->getCurrentModel(); }

void NukedEngine::postSystemReset (bool gs)             { impl->postSystemReset (gs); }
void NukedEngine::postMidiMessage (const juce::MidiMessage& m) { impl->postMidiMessage (m); }

void NukedEngine::start() { impl->start(); }
void NukedEngine::stop()  { impl->stop(); }
bool NukedEngine::isRunning() const { return impl->isRunning(); }

int NukedEngine::pullSamples (float* outL, float* outR, int numFrames)
{
    return impl->pullSamples (outL, outR, numFrames);
}

uint64_t NukedEngine::getTotalSamplesProduced() const     { return impl->getTotalSamplesProduced(); }
uint64_t NukedEngine::getTotalMidiBytesProcessed() const  { return impl->getTotalMidiBytesProcessed(); }
int      NukedEngine::getAudioFifoUsedPercent() const     { return impl->getAudioFifoUsedPercent(); }
float    NukedEngine::getPeakLevel() const                { return impl->getPeakLevel(); }
void     NukedEngine::setMasterGain (float g)             { impl->setMasterGain (g); }
float    NukedEngine::getMasterGain() const               { return impl->getMasterGain(); }
