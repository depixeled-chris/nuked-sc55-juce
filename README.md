# nuked-sc55-juce

A JUCE-based standalone host for the [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) Roland Sound Canvas and JV-880 hardware emulator.

This project gives you a clean GUI to play MIDI files or stream live MIDI input through the cycle-accurate Nuked-SC55 emulator core, with a system-aware MIDI input picker that does **not** truncate the device list (unlike many older WinMM-based players).

> **Source only.** Because of the license terms attached to Nuked-SC55, no precompiled binaries are distributed from this repository. Clone and build it yourself. See **License** below for the full explanation.

---

## What's in the box

### `NukedSC55JUCE` — the main standalone player

A Winamp/WMP-style player UI:

- **Menu bar** (File / Edit / Help) with Open MIDI, Preferences, About.
- **Now Playing** label showing the current file.
- **Seek bar** with current/total time labels. Drag to scrub, release to seek; sustained notes are silenced automatically.
- **Transport**: Play/Pause toggle, Stop (rewinds to 0), master Volume slider.
- **Preferences dialog** (Edit → Preferences) for all configuration:
  - Emulator **Model** selector covering every Romset Nuked-SC55 supports (SC-55mk2, SC-55, SC-55st, CM-300/SCC-1, SCB-55, RLP-3237, SC-155, SC-155mk2, JV-880).
  - **ROM directory** picker.
  - **MIDI input device** dropdown listing every system MIDI port (no fixed-size limit, unlike WinMM-based players).
  - Full embedded `AudioDeviceSelectorComponent` for output device, sample rate, buffer size, channel mapping.
- **Auto-loads ROMs on startup** when the saved path is valid. No clicks required for the common case.
- **Settings persistence** across launches (ROM directory, model, MIDI input, last-opened MIDI file, master gain).
- **Lenient SMF parser** that handles real-world MIDI files with trailing bytes (JUCE's strict parser rejects these).
- **Status bar** showing live engine diagnostics: native sample rate, output rate, peak level, audio FIFO fullness.

### `NukedTestRunner` — headless renderer for validation

A separate console executable that reads the same saved settings as the GUI app and renders a MIDI file offline through the emulator directly. Writes a `test-output.wav` and detailed `test-runner.log`. Useful for:

- Validating that ROMs load correctly without firing up the GUI.
- Sanity-checking emulator output on a known-good MIDI.
- CI / automated testing where no audio device is available.
- Diagnosing GUI vs. core problems by comparing the two outputs.

Override settings at the command line:

```
NukedTestRunner.exe --rom-dir="C:/path/to/roms" --model=0 --midi="C:/song.mid"
```

### What this project does *not* (yet) do

- VST3 / AU / CLAP plugin variant.
- Multi-instance polyphony stacking (Nuked has a 24-voice cap per instance; jcmoyer's fork supports stacking).
- Reverb / chorus parameter controls.
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
| Distribute compiled binaries you built yourself | **Murky.** The "may not be sold" clause is about selling, but the "commercial activity" clause is broad. We don't do this from the official repo. |
| Sell binaries on a store, bundle in commercial software | **No** |
| Use it for music you write and sell | Yes. Your music is your work; this app is just a renderer. |

### Why this matters for the repo

Because of the combined-work license conflict (Nuked-SC55's non-commercial clause + JUCE's GPLv3-or-paid licensing), this repository ships **source only**. Building binaries is the user's responsibility. Files in `src/` and the `CMakeLists.txt` itself are MIT-licensed (see `LICENSE`); the combined binary inherits Nuked-SC55's restrictions in practice.

If you want to push for binary distribution, the right path is asking nukeykt (Nuked-SC55's author) for written permission to redistribute non-commercial binaries. He's responsive on his issue tracker.

---

## ROM situation — read this too

The Nuked-SC55 emulator is a CPU + DSP simulator. It needs the original Roland firmware ROMs to actually do anything. **Those ROMs are not in this repo and never will be.** They're copyrighted Roland firmware and we don't have permission to redistribute them.

Legitimate ways to obtain the ROMs:

1. Buy an SC-55 / SC-55mk2 / CM-300 / etc. and dump the chips yourself. Used units run $80–$200 on eBay; chip dumpers (e.g. TL866II Plus) are ~$50.
2. Some Roland soft-synth installations have shipped equivalent ROM data in encrypted form, but extracting and using it outside that product is on you, legally.

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

Full list and exact chip-to-filename mappings are in [`external/Nuked-SC55/doc/USAGE.md`](https://github.com/jcmoyer/Nuked-SC55/blob/master/doc/USAGE.md). Your local checkout pins a specific submodule commit, so check `documentation/USAGE.md` inside the submodule for that exact version.

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
2. Go to **Edit → Preferences...**
3. **Model:** pick the unit you have ROMs for (defaults to SC-55mk2).
4. **ROM directory:** click **Browse...** and pick the directory containing your dumped ROM files (e.g. `rom1.bin`, `waverom1.bin`, …).
5. *(Optional)* **MIDI input:** pick a system MIDI port if you want to drive the emulator from a DAW or external player (e.g. a loopMIDI virtual port).
6. *(Optional)* **Audio device:** verify your output device is correct (the picker is at the bottom of the Preferences dialog).
7. Close Preferences. The engine auto-loads as soon as a valid ROM directory is set; the status bar will read "Engine: ... @ 66207 Hz".
8. **File → Open MIDI...**, pick a `.mid` file.
9. Click **Play**.

On subsequent launches, ROMs auto-load from the saved path and the last-opened MIDI file is restored. Just hit Play.

If you hear nothing:
- Check the status bar at the bottom: it shows engine state and live diagnostics (`peak` should rise above zero when audio is being produced; `fifo` should be at a healthy non-zero percentage).
- Verify the output device in **Edit → Preferences → Audio device** — pick the right one if JUCE picked a virtual/silent default.
- Try the headless **`NukedTestRunner.exe`** to verify the emulator works in isolation; if it produces a non-silent WAV, the problem is in the GUI's audio path, not the core.

---

## Project layout

```
nuked-sc55-juce/
├── CMakeLists.txt              # Top-level build script
├── LICENSE                     # MIT for wrapper code
├── NOTICE                      # Third-party license notices
├── README.md                   # You are here
├── .gitignore                  # Excludes build artifacts AND *.bin (ROMs)
├── external/
│   └── Nuked-SC55/             # Git submodule — jcmoyer's fork of nukeykt/Nuked-SC55
└── src/
    ├── Main.cpp                # JUCEApplication entry, DocumentWindow
    ├── MainComponent.h/.cpp    # Top-level UI host: menu bar, layout, audio callback
    ├── NukedEngine.h/.cpp      # Pimpl facade over the Nuked Emulator class:
    │                           #   - dedicated emulator worker thread
    │                           #   - lock-free MIDI input + audio output FIFOs
    │                           #   - atomic master gain, peak meter, diagnostics
    ├── MidiFilePlayer.h/.cpp   # SMF playback: play/pause/stop/seek, lenient
    │                           #   parser, atomic loadFile semantics
    ├── SeekBar.h/.cpp          # Position slider + current/total time labels
    ├── PlayerControls.h/.cpp   # Transport buttons + master volume
    ├── PreferencesDialog.h/.cpp# Modal config: model, ROMs, MIDI input, audio
    └── TestRunner.cpp          # Headless validation executable (separate target)
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

The emulator runs at a fixed native rate (66207 Hz for SC-55mk2 / SC-55st / SC-155mk2, 64000 Hz for SC-55mk1 / CM-300 / SCB-55 / RLP-3237 / SC-155 / JV-880). These are *the actual hardware DAC rates* derived from the MCU's clock divider. They look weird because they aren't musical-standard rates, but that's the chip.

Sample-rate conversion to the audio device rate is done with `juce::LagrangeInterpolator` in the audio callback, with **leftover-sample tracking** between blocks so no input frames get dropped (which caused audible pitch jitter in earlier versions). A polyphase resampler would be a quality upgrade but is not strictly required for normal listening.

---

## Roadmap

### Done

- [x] Standalone JUCE GUI with Winamp/WMP-style player layout
- [x] All Romsets supported by Nuked-SC55 (mk1, mk2, ST, CM-300, SCB-55, RLP-3237, SC-155, SC-155mk2, JV-880)
- [x] Live MIDI input + Standard MIDI File playback with seek / pause / stop
- [x] Auto-load ROMs on startup; settings persistence
- [x] Master volume control
- [x] Headless `NukedTestRunner` for offline WAV rendering
- [x] Lenient SMF parser (tolerates files with trailing bytes that JUCE's parser rejects)
- [x] Leftover-aware resampler (kills the periodic jitter / pitch drift)

### Next

- [ ] VST3 / AU / CLAP plugin variants
- [ ] Multi-instance polyphony stacking (jcmoyer's fork supports it)
- [ ] Render-to-WAV in the GUI app (the core path is already in `NukedTestRunner`)
- [ ] Higher-quality polyphase resampler
- [ ] Reverb / chorus parameter controls (Nuked's MCU exposes these)
- [ ] Playlist support
- [ ] LCD display recreation
- [ ] DAW-friendly tempo sync (when running as a plugin)
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

- **nukeykt** for the original [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) emulator. That's the real achievement; the wrapper is trivial. The emulator is years of reverse engineering.
- **jcmoyer** for the [maintained fork](https://github.com/jcmoyer/Nuked-SC55) that exposes the emulator as a library with a clean backend API. Without that work, this wrapper would be much harder.
- **linoshkmalayil** for the [GUI-Float fork](https://github.com/linoshkmalayil/Nuked-SC55-GUI-Float) that informed a lot of the UX decisions here.
- **Raw Material Software** for JUCE.
- **Roland** for designing the SC-55 in 1991 and shipping the synth that all this work exists to preserve.
