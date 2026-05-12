# nuked-sc55-juce

A JUCE-based standalone host for the [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) Roland Sound Canvas and JV-880 hardware emulator.

This project gives you a clean GUI to play MIDI files or stream live MIDI input through the cycle-accurate Nuked-SC55 emulator core, with a system-aware MIDI input picker that does **not** truncate the device list (unlike many older WinMM-based players).

> **Source only.** Because of the license terms attached to Nuked-SC55, no precompiled binaries are distributed from this repository. Clone and build it yourself. See **License** below for the full explanation.

---

## What's in the box

- **Standalone JUCE app** that hosts the Nuked-SC55 backend on a dedicated thread.
- **MIDI input device picker** that enumerates every system MIDI port, no fixed-size dropdown limits.
- **MIDI file player** built on JUCE's `MidiFile` + `MidiMessageSequence`, with tempo-event handling.
- **Audio output device picker** with sample rate, buffer size, channel selection (JUCE's standard `AudioDeviceSelectorComponent`).
- **Model selector** covering every Romset Nuked-SC55 supports:
  - Roland SC-55mk2 (v1.01)
  - Roland SC-55 (v1.00 / v1.21 / v2.0)
  - Roland SC-55st (v1.01)
  - Roland CM-300 / SCC-1
  - Roland SCB-55
  - Roland RLP-3237
  - Roland SC-155
  - Roland SC-155mk2
  - Roland JV-880

What this project does *not* (yet) do:

- VST3 / AU / CLAP plugin variant (planned for v0.2).
- Multi-instance polyphony stacking (planned for v0.2).
- Render-to-WAV (planned for v0.2 — easy add since the audio path is already in place).
- LCD display recreation (would be cute, not on roadmap).

---

## License situation — read this before building

This is the part everyone skips and then regrets.

### Nuked-SC55 is *not* MIT or BSD-without-strings

Despite being called "open source" in casual conversation, Nuked-SC55's per-file license header reads:

> *Redistributions may not be sold, nor may they be used in a commercial product or activity.*

That clause makes it incompatible with **GPL** (which forbids adding restrictions like non-commercial), incompatible with **JUCE's commercial license** (no sale allowed), and incompatible with shipping a precompiled binary in any commercial context. It does *not* prevent personal use, modification, or non-commercial source distribution, which is what this project sticks to.

### Practical consequences

| You want to... | OK under Nuked's license? |
|---|---|
| Clone, build, and use this app personally | Yes |
| Fork this repo and publish your changes (source) | Yes |
| Distribute compiled binaries you built yourself | **Murky** — the "may not be sold" clause is about selling, but the "commercial activity" clause is broad. We don't do this from the official repo. |
| Sell binaries on a store, bundle in commercial software | **No** |
| Use it for music you write and sell | Yes — your music is your work; this app is just a renderer. |

### Why this matters for the repo

Because of the combined-work license conflict (Nuked-SC55's non-commercial clause + JUCE's GPLv3-or-paid licensing), this repository ships **source only**. Building binaries is the user's responsibility. Files in `src/` and the `CMakeLists.txt` itself are MIT-licensed (see `LICENSE`); the combined binary inherits Nuked-SC55's restrictions in practice.

If you want to push for binary distribution, the right path is asking nukeykt (Nuked-SC55's author) for written permission to redistribute non-commercial binaries. He's responsive on his issue tracker.

---

## ROM situation — read this too

The Nuked-SC55 emulator is a CPU + DSP simulator. It needs the original Roland firmware ROMs to actually do anything. **Those ROMs are not in this repo and never will be** — they are copyrighted Roland firmware, and we don't have permission to redistribute them.

Legitimate ways to obtain the ROMs:

1. Buy an SC-55 / SC-55mk2 / CM-300 / etc. and dump the chips yourself. Used units run $80–$200 on eBay; chip dumpers (e.g. TL866II Plus) are ~$50.
2. Some Roland soft-synth installations have shipped equivalent ROM data in encrypted form — but extracting and using it outside that product is on you, legally.

The "everyone just downloads them from archive.org" path is *technically* infringing. The community largely doesn't care, but this project does not point you at any specific source.

### ROM filename convention

The app expects ROMs to follow Nuked-SC55's "legacy" filename scheme. Place them in a single directory and point the app at it.

| Model | Files expected |
|---|---|
| **SC-55mk2 / SC-155mk2** (v1.01) | `rom1.bin`, `rom2.bin`, `rom_sm.bin`, `waverom1.bin`, `waverom2.bin` |
| **SC-55** (v1.00 / v1.21 / v2.0) | `sc55_rom1.bin`, `sc55_rom2.bin`, `sc55_waverom1.bin`, `sc55_waverom2.bin`, `sc55_waverom3.bin` |
| **SC-55st** (v1.01) | `rom1.bin`, `rom2_st.bin`, `rom_sm.bin`, `waverom1.bin`, `waverom2.bin` |
| **CM-300 / SCC-1** (v1.10 / v1.20) | `cm300_rom1.bin`, `cm300_rom2.bin`, `cm300_waverom1.bin`, `cm300_waverom2.bin`, `cm300_waverom3.bin` |
| **SCB-55** | `scb55_rom1.bin`, `scb55_rom2.bin`, `scb55_waverom1.bin`, `scb55_waverom2.bin` |
| **RLP-3237** | `rlp3237_rom1.bin`, `rlp3237_rom2.bin`, `rlp3237_waverom1.bin` |
| **SC-155** | `sc155_rom1.bin`, `sc155_rom2.bin`, `sc155_waverom1.bin`, `sc155_waverom2.bin`, `sc155_waverom3.bin` |
| **JV-880** (v1.0.0 / v1.0.1) | `jv880_rom1.bin`, `jv880_rom2.bin`, `jv880_waverom1.bin`, `jv880_waverom2.bin` |

Full list and exact chip-to-filename mappings are in [`external/Nuked-SC55/doc/USAGE.md`](https://github.com/jcmoyer/Nuked-SC55/blob/master/doc/USAGE.md) (note: in your local checkout, the submodule pins a specific commit — check the `documentation/USAGE.md` inside the submodule for that exact version).

---

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| **CMake** | 3.22 or newer | https://cmake.org/download |
| **A C++20 compiler** | MSVC 2022, Apple Clang 14+, GCC 11+, or Clang 14+ | C++20 is required because Nuked-SC55 uses `std::span` and `std::filesystem` |
| **JUCE** | 8.0 or newer (8.0.12 is the FetchContent fallback) | Either install locally or let CMake fetch it |
| **Git** with submodule support | any modern version | This project uses a submodule for the Nuked-SC55 backend |

### Optional but recommended: local JUCE install

This project follows the convention of looking for JUCE at:

- **Windows**: `C:/JUCE`
- **macOS / Linux**: `~/JUCE`

If JUCE is present at that path, CMake uses it. If not, CMake will `FetchContent` JUCE 8.0.12 from GitHub on first configure. Both work; local is faster on rebuilds.

To install JUCE locally:

```bash
# Windows (PowerShell, run as admin):
git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git C:/JUCE

# macOS / Linux:
git clone --depth 1 --branch 8.0.12 https://github.com/juce-framework/JUCE.git ~/JUCE
```

### Override the JUCE path

If your JUCE is somewhere else, pass `-DJUCE_LOCAL_PATH=/path/to/JUCE` at CMake configure time.

---

## Build

### Clone with submodules

```bash
git clone --recurse-submodules https://github.com/depixeled-chris/nuked-sc55-juce.git
cd nuked-sc55-juce
```

(Already cloned without `--recurse-submodules`? Run `git submodule update --init --recursive`.)

### Windows (Visual Studio 2022)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The executable lands in `build/NukedSC55JUCE_artefacts/Release/Nuked SC-55 JUCE.exe`.

### Windows (Ninja + MSVC, faster)

```powershell
# From a "Developer PowerShell for VS 2022" prompt
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### macOS

```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Output: `build/NukedSC55JUCE_artefacts/Release/Nuked SC-55 JUCE.app`

### Linux

Install the JUCE Linux dependencies first:

```bash
sudo apt-get install libasound2-dev libjack-jackd2-dev \
    libcurl4-openssl-dev libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev \
    libwebkit2gtk-4.1-dev libglu1-mesa-dev mesa-common-dev
```

Then:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build/NukedSC55JUCE_artefacts/Nuked SC-55 JUCE`

---

## First run

1. Launch the built executable.
2. Click **Browse...** and pick the directory containing your dumped ROMs.
3. Pick a **Model** from the dropdown (defaults to SC-55mk2).
4. Click **Load ROMs**. If filenames match, the engine starts. If you get an "Incomplete ROM set" error, the error message tells you which ROM slots are missing — usually a typo in filenames.
5. (Optional) Pick a **MIDI input** device — anything you send to it is routed live to the emulator.
6. (Optional) Click **Open .mid...** to load a Standard MIDI File, then **Play**.

If you hear nothing:

- Check the **Audio device** section — is the right output device selected? Are channels mapped?
- Click **GS Reset** — some MIDI files expect a GS reset SysEx before playing, and a missing reset is the most common silence cause.
- Check the status bar at the bottom for engine state.

---

## Project layout

```
nuked-sc55-juce/
├── CMakeLists.txt        # Top-level build script
├── LICENSE               # MIT for wrapper code
├── NOTICE                # Third-party license notices
├── README.md             # You are here
├── .gitignore            # Excludes build artifacts AND *.bin (ROMs)
├── external/
│   └── Nuked-SC55/       # Git submodule — jcmoyer's fork of nukeykt/Nuked-SC55
└── src/
    ├── Main.cpp          # JUCEApplication entry point
    ├── MainComponent.h   # Standalone window + UI + audio callback
    ├── MainComponent.cpp
    ├── NukedEngine.h     # JUCE-facing facade for the Emulator class
    ├── NukedEngine.cpp   # Pimpl impl with worker thread + ringbuffers
    ├── MidiFilePlayer.h  # MIDI file → message-sink scheduler
    └── MidiFilePlayer.cpp
```

The `external/Nuked-SC55` submodule is pinned to a specific commit. To update it:

```bash
cd external/Nuked-SC55
git fetch
git checkout <new-commit-or-tag>
cd ../..
git add external/Nuked-SC55
git commit -m "Bump Nuked-SC55 submodule"
```

---

## Threading model

This is the part that breaks the first time someone reaches for the emulator in a tight loop, so it's worth documenting.

```
┌────────────────┐                    ┌──────────────────┐
│  MIDI device   │  raw bytes ─────▶ │ Lock-free MIDI    │
│  callback      │                    │ FIFO (uint8 x N)  │
└────────────────┘                    └──────────────────┘
                                              │
                                              ▼
                                      ┌──────────────────┐
                                      │  Emu worker      │
                                      │  thread          │
                                      │                  │
                                      │  - drain MIDI    │
                                      │  - call Step()   │
                                      │  - back off when │
                                      │    audio FIFO    │
                                      │    is full       │
                                      └──────────────────┘
                                              │
                              sample callback ▼
                                      ┌──────────────────┐
                                      │ Lock-free audio  │
                                      │ FIFO (float x N) │
                                      └──────────────────┘
                                              │
                                              ▼
                                      ┌──────────────────┐
                                      │ Audio thread     │
                                      │ (JUCE)           │
                                      │                  │
                                      │ - pull samples   │
                                      │ - resample to    │
                                      │   device rate    │
                                      └──────────────────┘
```

The emulator runs at a fixed native rate (66207 Hz for SC-55mk2, 64000 Hz for SC-55mk1 / CM-300 / etc.). Sample-rate conversion to the audio device rate is done with `juce::LagrangeInterpolator` in the audio callback. This is acceptable for v0.1; a higher-quality polyphase resampler is a v0.2 task if anyone notices the difference.

---

## Roadmap

### v0.1 (this commit)

- [x] Standalone GUI app
- [x] All Romsets supported by Nuked-SC55
- [x] MIDI input + MIDI file playback
- [x] Settings persistence

### v0.2

- [ ] Render to WAV (offline mode — already easy given the architecture)
- [ ] VST3 / AU / CLAP plugin variants
- [ ] Multi-instance polyphony stacking (jcmoyer's fork supports it)
- [ ] Higher-quality polyphase resampler

### v0.3 and beyond

- [ ] Reverb / chorus controls (exposed by Nuked's MCU)
- [ ] LCD display recreation
- [ ] DAW-friendly tempo sync (host tempo as MIDI tempo)
- [ ] Preset / SysEx bank loader

If you want any of these, open an issue or send a PR.

---

## Contributing

Contributions welcome under the same license terms. Please:

- Keep new code MIT-licensed (in `src/`).
- Don't add files that bundle ROM data or Roland firmware in any form.
- If you bump the Nuked-SC55 submodule, mention which commit in your PR.

Code style: roughly JUCE-flavored C++ (4-space indent, K&R braces, no `using namespace` in headers).

---

## Acknowledgments

- **nukeykt** for the original [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) emulator — the actual achievement here. The wrapper is trivial; the emulator is years of reverse engineering.
- **jcmoyer** for the [maintained fork](https://github.com/jcmoyer/Nuked-SC55) that exposes the emulator as a library with a clean backend API. Without that work, this wrapper would be much harder.
- **linoshkmalayil** for the [GUI-Float fork](https://github.com/linoshkmalayil/Nuked-SC55-GUI-Float) that informed a lot of the UX decisions here.
- **Raw Material Software** for JUCE.
- **Roland** for designing the SC-55 in 1991 and shipping the synth that all this work exists to preserve.
