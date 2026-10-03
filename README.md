# LiveRack

A native macOS live plugin host for FOH: any Core Audio device (Dante Virtual
Soundcard, AVB, USB) → racks of VST3 / AU plugins → back to the console, with
no buffering added beyond the device buffer.

## Build

```bash
brew install cmake
git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git External/JUCE
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j10
open build/LiveRack_artefacts/Release/LiveRack.app
```

Optional VST2 hosting: add `-DLIVERACK_VST2_SDK_PATH=/path/to/vst2sdk`.

## Tests

```bash
build/LiveRackTests_artefacts/Release/LiveRackTests              # engine unit tests
build/LiveRackTests_artefacts/Release/LiveRackTests RealPlugins  # loads installed plugins
```

## First run

1. **Settings** → pick the audio device, sample rate and buffer (64 or 32 samples live).
2. **Settings** → *Scan VST3* (and *Scan Audio Units* if needed). Each plugin is
   tested in its own process; Waves shells take a few seconds each.
3. **Racks** → *+ Mono rack* per channel. Set Input/Output, add plugins.
4. Tick **Critical** on racks that must never drop out (lead vocal): any plugin
   fault makes the rack pass its clean input instead.

## Safety features

| Situation | What happens |
|---|---|
| Plugin outputs NaN/Inf or runaway level | Plugin bypassed; critical rack → dry input |
| Rack too slow for the buffer (8 blocks in a row) | Slowest plugin bypassed; critical rack → dry |
| Plugin outputs silence despite input for 2 s | Row turns amber "No output" |
| Plugin hangs the audio thread for 1 s | App saves and restarts, that plugin bypassed |
| App crashes | Relaunches in a few seconds with the show restored; the plugin that was running is bypassed |
| Plugin crashes while scanning | Only the scanner process dies; plugin is blacklisted |
| Accidental Cmd-Q | Confirmation dialog |
| **BYPASS ALL** | Panic: every rack passes dry, audio keeps flowing |

Shows autosave to `~/Library/Application Support/LiveRack/autosave.liverack`;
File → Save Show As… writes a `.liverack` file you can keep per gig.

## Known limits (v1)

- No scenes/snapshots, MIDI/OSC control, or delay compensation between racks yet.
- FabFilter's "aumf" (MIDI-effect) AU variants output silence in JUCE hosts; use
  their VST3s (the plugin browser prefers VST3 automatically).
- Built ad-hoc signed; on another Mac, right-click → Open the first time.
