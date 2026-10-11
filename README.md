# LiveRack

A native live plugin host for FOH on macOS, Windows and Linux: any audio
interface (Dante Virtual Soundcard, console ASIO/USB, AVB, MADI) → racks of
plugins → back to the console, with no buffering added beyond the device buffer.

| | Audio drivers (several interfaces at once) | Plugins |
|---|---|---|
| macOS 13+ (Apple Silicon) | Core Audio | VST3, Audio Unit |
| Windows 10/11 (x64) | ASIO, WASAPI exclusive | VST3 |
| Linux (x64) | ALSA hardware, JACK / PipeWire | VST3, LV2 |

Every push is built and unit-tested on all three by GitHub Actions
(`.github/workflows/build.yml`); the packaged builds are attached to each run.

## Build

All platforms need CMake 3.25+ and JUCE 8.0.15 in `External/JUCE`:

```bash
git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git External/JUCE
```

**macOS** (Xcode command line tools, `brew install cmake`):

```bash
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j10
open build/LiveRack_artefacts/Release/LiveRack.app
```

**Windows** (Visual Studio 2022 with "Desktop development with C++"), from a
Developer PowerShell:

```powershell
cmake -B build
cmake --build build --config Release --parallel
build\LiveRack_artefacts\Release\LiveRack.exe
```

**Linux** (Debian/Ubuntu package names):

```bash
sudo apt install build-essential cmake libasound2-dev libjack-jackd2-dev \
  libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev \
  libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
build/LiveRack_artefacts/Release/LiveRack
```

Optional VST2 hosting: add `-DLIVERACK_VST2_SDK_PATH=/path/to/vst2sdk`.

## Tests

```bash
build/LiveRackTests_artefacts/Release/LiveRackTests              # engine unit tests
build/LiveRackTests_artefacts/Release/LiveRackTests RealPlugins  # loads installed plugins
LIVERACK_SOAK_SECONDS=600 build/LiveRackTests_artefacts/Release/LiveRackTests Soak
```

## First run

1. **Settings** → pick the audio device, sample rate and buffer (64 or 32 samples live).
2. **Settings** → *Scan VST3* (and *Scan Audio Units* on macOS, *Scan LV2* on
   Linux). Each plugin is tested in its own process; Waves shells take a few
   seconds each. A show only loads plugins scanned on that computer.
3. **Racks** → *+ Mono rack* per channel. Set Input/Output, add plugins.
4. Tick **Critical** on racks that must never drop out (lead vocal): any plugin
   fault makes the rack pass its clean input instead.

### Windows

- Interfaces appear as `ASIO: <driver>` and `WASAPI: <device>`. Prefer ASIO
  (Dante Virtual Soundcard in ASIO mode, the console's or interface's driver);
  several ASIO drivers can run at once if the drivers allow it. WASAPI exclusive
  is for devices without ASIO and has a larger minimum buffer (~3–10 ms).
- Unsigned build: SmartScreen asks once (More info → Run anyway).
- Settings and shows live in `%APPDATA%\LiveRack`.

### Linux

- Run `install.sh` from the package: installs to `~/.local/opt/LiveRack`, adds a
  launcher, and tells you if real-time priority still needs enabling (`audio`
  group + rtprio limit). LiveRack shows a banner until it's granted.
- Interfaces appear as `ALSA: <card>` (direct hardware access, lowest latency;
  the card must not be held by PipeWire/PulseAudio) and `JACK: <server>` (with
  JACK or PipeWire's JACK support, which then owns the hardware).
- Settings and shows live in `~/.config/LiveRack`.

## Safety features

| Situation | What happens |
|---|---|
| Plugin outputs NaN/Inf or runaway level | Plugin bypassed; critical rack → dry input |
| Rack too slow for the buffer (8 blocks in a row) | Slowest plugin bypassed; critical rack → dry |
| Plugin outputs silence despite input for 2 s | Row turns amber "NO OUTPUT" |
| Plugin bypassed, faulted, or rack falls back to dry | Latency stays identical (bypass / dry paths are delayed to match) |
| Racks summed on the console | ALIGN ALL RACKS (default on) gives every rack the same latency |
| Rack crosses two interfaces | Clock bridge; latency locks after 10 s warm-up, never shifts mid-show |
| Re-routing a rack to another interface | Two-phase swap: its plugins never run on two audio threads |
| Interface stops delivering audio (driver restart, lost clock) | Watchdog reopens it within ~1.5 s, then backs off 1–8 s between retries |
| Plugin hangs the audio thread for 1 s | App saves and restarts, that plugin bypassed |
| App crashes | Relaunches in a few seconds with the show restored; the plugin that was running is bypassed |
| Autosave unreadable | Loads the rolling backup (≤ 5 min old) and says so |
| Plugin crashes while scanning | Only the scanner process dies; plugin is blacklisted |
| Accidental Cmd-Q | Confirmation dialog |
| **BYPASS ALL** | Panic: every rack passes dry, audio keeps flowing |

Verified by: unit tests (impulse timing through bypass/fault/fallback, bridge
drift ±500 ppm), a stress test swapping plans hundreds of times a second while
two interfaces run, and ThreadSanitizer (0 races, 60 s soak).

Shows autosave to `autosave.liverack` in the settings folder
(`~/Library/Application Support/LiveRack` on macOS); File → Save Show As…
writes a `.liverack` file you can keep per gig.

## Known limits (v1)

- No scenes/snapshots, MIDI/OSC control, or delay compensation between racks yet.
- FabFilter's "aumf" (MIDI-effect) AU variants output silence in JUCE hosts; use
  their VST3s (the plugin browser prefers VST3 automatically).
- Built ad-hoc signed; on another Mac, right-click → Open the first time.
- Shows made on another OS keep their VST3 plugins (matched to the local
  install; AU and LV2 exist on one OS only) but not their routing when
  interface names differ: re-pick inputs/outputs.
- Sharing builds: JUCE 8 is AGPLv3 unless you hold a JUCE licence, and the
  bundled ASIO SDK is GPLv3 (or Steinberg's licence). Under AGPLv3, people you
  give a build to must be able to get the source.
