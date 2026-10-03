# LiveRack — Design Spec

Date: 2026-10-03
Status: approved in conversation (user delegated remaining decisions)

## Goal

A native macOS live plugin host for FOH mixing, in the spirit of Waves SuperRack /
LiveProfessor: take channels from any Core Audio device (Dante Virtual Soundcard,
AVB, USB interface, console USB card), run each through a rack of plugins with the
lowest achievable latency, and return them to the console. Must load essentially any
VST3 or AU plugin, and must keep critical channels (e.g. the lead vocal through a
de-feedback plugin) alive when a plugin misbehaves.

## Non-goals for v1

Scenes/snapshots, MIDI/OSC remote control, per-rack process sandboxing, plugin delay
compensation across racks, VST2 hosting by default, recording/playback.

## Platform & stack

- macOS 13+, Apple Silicon (arm64) primary.
- C++20, **JUCE 8** (fetched by CMake), CMake + Xcode toolchain.
- Plugin formats: **VST3** and **AU** always on. **VST2** behind a CMake option
  `LIVERACK_VST2_SDK_PATH`; off unless the user supplies SDK headers.

## Concepts

- **Device**: the Core Audio device; user picks device, sample rate, buffer size
  (32–1024). Any input/output channel count.
- **Rack**: an ordered chain of plugin slots. Has a name, an *input source*, a
  channel layout (mono or stereo), *output destinations*, a `critical` flag, and a
  rack bypass.
- **Input source** of a rack: either a set of device input channels, or other
  racks (a **bus**: sums the outputs of listed racks).
- **Output destinations**: zero or more device output channel pairs/singles. Several
  racks writing the same output are summed.
- **Slot**: one hosted plugin instance + bypass flag + fault state.
- **Show**: device settings + all racks; saved as a single XML file.

## Architecture

```
UI (message thread) ──edits──► ShowModel (ValueTree)
                                   │ on change
                                   ▼
                           PlanBuilder (message thread)
                                   │ builds immutable RenderPlan
                                   ▼
                   atomic swap ──► AudioEngine (audio thread + worker pool)
                                   │ fault events (lock-free FIFO)
                                   ▼
                           FaultMonitor (message thread) ─► UI warnings
```

### Units

1. **ShowModel** — `juce::ValueTree` holding the show. Serialises to/from XML
   including each plugin's state (base64 `getStateInformation`). Single source of
   truth for the UI.
2. **PluginHost** — `AudioPluginFormatManager` + `KnownPluginList`; creates plugin
   instances (async for AU), owns plugin editor windows.
3. **PluginScanner** — scans in a **child process** (the app binary launched with
   `--scan`). A plugin that crashes or hangs (>30 s) the child is recorded in a
   blacklist and skipped; the main app never loads unscanned code during scanning.
4. **RenderPlan** — immutable, fully preallocated description of one processing
   cycle: rack jobs (plugin pointers, scratch buffers, input channel map, output
   map), grouped into dependency **levels** (level 0 = racks fed by device inputs,
   level n = buses fed by level < n). Built off the audio thread; retired plans are
   freed on the message thread (never on the audio thread).
5. **AudioEngine** — the `AudioIODeviceCallback`. Per block: copy inputs into rack
   scratch buffers → process each level's racks in parallel → sum rack outputs into
   device outputs. Reads the current plan via an atomic pointer swap.
6. **WorkerPool** — N real-time worker threads (N = performance cores − 1) that join
   the device's **audio workgroup** (`os_workgroup`, via JUCE's
   `AudioWorkgroup`), so they're scheduled as real-time on P-cores. Work
   distribution: atomic job index per level; the audio thread participates too.
   Small plans (≤ 2 racks) run single-threaded.
7. **Watchdog** (inside the engine, per rack/slot, audio-thread safe):
   - after each slot: scan for NaN/Inf → fault;
   - sustained peak > +24 dBFS for > 50 ms → fault;
   - rack processing time > 90 % of the block period for 8 consecutive blocks →
     fault.
   On fault: critical rack → output switches to **dry input** (passthrough) and the
   faulty slot is fault-bypassed; non-critical rack → faulty slot fault-bypassed.
   Faults are posted to a lock-free FIFO; the user re-arms a slot from the UI.
8. **StallDetector** — message-thread timer reading an audio-thread heartbeat
   counter. If the callback has not run for 1 s while the device is running
   (plugin hung inside `processBlock`), the app autosaves and aborts so the
   supervisor restarts it.
9. **Supervisor** — `LiveRackGuard`, a tiny helper executable in the app bundle,
   spawned by the app at startup with the app's PID. If the app exits without
   writing a clean-exit marker, the guard relaunches it with `--recover`, which
   reloads the autosaved show and marks the last suspected plugin as bypassed.
10. **Autosave** — after every model change (debounced 500 ms) and on fault, the
    show is written to `~/Library/Application Support/LiveRack/autosave.liverack`.

### Real-time rules (audio thread & workers)

No locks, no allocation, no Objective-C/message calls, no file I/O, no logging.
Plan swap via `std::atomic<RenderPlan*>`; parameter-free communication via atomics
and a single-producer FIFO for fault events.

## UI

- **Top bar**: device name, sample rate, buffer, round-trip latency in ms, DSP load
  %, dropout counter, "Bypass all" panic button, settings.
- **Left**: rack list (name, input→output summary, critical badge, fault light,
  meters).
- **Main**: selected rack — vertical slot chain with plugin name, reported plugin
  latency, bypass, open-editor, fault state + "re-arm"; "+" opens a searchable
  plugin browser.
- **Routing tab**: grid of device inputs × racks and racks × device outputs; bus
  membership via rack-source picker.
- **Settings**: audio device panel, plugin scan (folders, rescan, blacklist).
- Plugin editors open in floating, always-on-top-of-app windows.

## Latency

The host adds **zero** buffering beyond the device buffer. Displayed latency =
device input + output latency + buffer; each slot shows the plugin's own reported
latency, and each rack shows its total.

## Testing

- Unit tests (JUCE `UnitTest`, run by a `LiveRackTests` console target):
  PlanBuilder (levels, bus summing, output summing, cycle rejection), Watchdog
  (NaN, overload, overtime → correct fallback per critical flag), show
  serialisation round-trip.
- Engine test with in-process dummy processors (gain, NaN-generator, sleeper) driven
  by a fake device callback.
- Manual acceptance: load Defeedback.vst3 + a Waves VST3 on a critical mono rack at
  64 samples with Dante VSC/any interface; inject faults via a debug "NaN" test
  plugin; kill -9 the app and confirm auto-relaunch with state restored.

## Phases after v1

2. Scenes/snapshots with MIDI program change recall. 3. MIDI/OSC parameter control.
4. Per-rack sandbox process (shared-memory audio). 5. Optional delay compensation
for buses. 6. VST2 default-on if headers are available.
