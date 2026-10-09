# Multi-interface I/O + Settings window — Design

Date: 2026-10-09. Approved in conversation.

## Goal
Pick any channel of any audio interface per rack, input and output independently,
from the rack view; several interfaces in use at once; crossing interfaces is
allowed, seamless, with minimal added latency. No macOS aggregate device.
Settings (devices, routing, plugins, performance) move to a ⌘, window.

## Model
- A channel is a `PortRef { device name, channel index }`.
- RACK `inputs` / `outputs` become `name#ch;name#ch` with the device name
  URL-escaped. Old numeric lists are migrated using the device names stored in
  device.xml (input device for inputs, output device for outputs).
- Interfaces referenced by any rack are opened; unreferenced ones are closed.
  A referenced interface that is not connected shows as MISSING in the rack.

## Engine
- `DeviceHub` opens each interface as its own `AudioIODevice` (same name for
  input and output => one device, never JUCE's combiner), all at the global
  sample rate and buffer size, every channel active.
- Each open interface has its own `AudioEngine` + `RenderPlan`, running in that
  interface's callback thread and workgroup.
- A rack's **home** interface = interface of its first output, else of its
  first input, else (bus) the home of its first source. A rack is processed in
  its home interface's callback, so it writes outputs directly.
- Audio that crosses interfaces (a rack's input channel on another interface,
  a bus source rack homed elsewhere, an output channel on another interface)
  goes through a `ClockBridge`:
  - SPSC ring per stream, producer writes in its callback, consumer reads in
    its callback.
  - Consumer resamples with a PI-controlled ratio that holds the fill level at
    `target = consumer block + max(producer block, consumer block) / 2`;
    ratio clamped to ±1000 ppm. Before first fill it outputs silence; on
    underrun it outputs silence and re-primes; on overrun it skips to target.
  - Added latency ≈ target fill (≈1 ms at 32 smp / 48 kHz).
- Output channels on a non-home interface: the rack's output stream is bridged
  to that interface, which sums it into its outputs.
- Workers per interface = (performance cores − 1 − boost) / open interfaces.
  CpuBoost runs when any open interface's buffer ≤ 128.

## UI
- Main window: rack list + rack view + status strip (device count, SR, buffer,
  worst RTL, DSP, xruns, boost) + BYPASS ALL. No view tabs.
- Rack INPUT / OUTPUT buttons open a cascading menu: every interface on the Mac
  → side menu with its channels (ticked = in use). Bus sources stay in INPUT.
- ⌘, (and LiveRack ▸ Settings…) opens a Settings window with pages:
  AUDIO (sample rate, buffer, per-interface status/latency/xruns),
  ROUTING (patch grid grouped by interface), PLUGINS (scan + list),
  PERFORMANCE (low-latency boost, diagnostics log).

## Testing
- PortRef encoding/migration round trips.
- ClockBridge: ±200 ppm drift, mismatched block sizes (32/64/48), 60 s
  simulated: no underrun after settle, fill within ±1 block of target,
  latency ≈ target, sine continuity.
- Multi-device plan: two fake interfaces driven alternately; same-device rack
  has zero added delay, crossing rack output equals delayed input.
