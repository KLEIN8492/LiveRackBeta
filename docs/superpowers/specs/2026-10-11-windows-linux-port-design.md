# LiveRack on Windows and Linux — design

## Intent

Windows and Linux rigs run real shows (Dante Virtual Soundcard, console/interface
ASIO, several interfaces at once), and builds are shared with other engineers.
Success = the same LiveRack (engine, safety features, UI, show files) on all three
OSes, built and unit-tested on each by CI, with the lowest latency each OS allows.

## Approach

A small platform layer (`Source/Platform/`) behind one header, one implementation
file per OS. Everything above it — engine, plans, watchdog, clock bridges, show
model, UI — stays shared and unchanged. Portable JUCE facilities are used where
they are good enough (memory-mapped file, child processes, real-time threads).

| | macOS | Windows | Linux |
|---|---|---|---|
| Audio drivers | Core Audio | ASIO (up to 16 drivers at once, JUCE limit) + WASAPI exclusive | ALSA hardware devices + JACK/PipeWire |
| Plugin formats | VST3, AU | VST3 | VST3, LV2 |
| Performance cores | `hw.perflevel0` | `GetLogicalProcessorInformationEx` efficiency class | cores at the top `cpuinfo_max_freq` |
| Stay fast/awake | `NSActivityLatencyCritical` | 1 ms timer, no power throttling, no sleep | `systemd-inhibit` held while running |
| Worker threads | workgroup join | MMCSS "Pro Audio" | `SCHED_RR` via JUCE (needs rtprio: `audio` group) |
| Crash guard waits on | `kqueue` | process handle | `pidfd` (falls back to polling) |
| Fatal stall | `abort()` (crash report) | `TerminateProcess` (no WER dialog) | `abort()` |
| Package | `.app` (zip) | portable folder (zip) | portable folder (tar.gz) |

## Interfaces with several driver types

`DeviceHub` holds every driver type the OS offers. An interface is identified by
a name that is unique across types: on macOS the plain device name (old shows keep
working); elsewhere `"<driver>: <device>"`, e.g. `ASIO: Dante Virtual Soundcard`,
`WASAPI: Focusrite USB Audio`, `ALSA: Scarlett 18i20`. Each open interface still
has its own engine and callback; clock bridges join them whatever their drivers.

- **WASAPI** lists inputs and outputs as separate endpoints. An input and an output
  whose hardware name (the last parenthesised part) matches exactly one of each are
  offered as one interface, opened as one device.
- **ASIO** drivers load when a device object is created, even just to read channel
  names. While any ASIO interface is open, ASIO drivers are not probed; their last
  known channel names are reused.

## Failure handling

- Windows: Windows Error Reporting dialogs are disabled for the process. A crash
  must end the process at once so the guard relaunches it, rather than waiting
  on a dialog nobody at FOH will see.
- Linux: if the user has no real-time priority allowance, a banner says so and how
  to fix it (`audio` group / limits), instead of silently running at normal
  priority.

## Testing

GitHub Actions on macOS, Windows and Ubuntu: configure, build everything, run the
`Core` tests, and upload the packaged build. Device and real-plugin tests need
hardware and stay manual. ThreadSanitizer stays a macOS/Linux local build.

## Licensing (when sharing builds)

JUCE 8 is AGPLv3 unless you hold a JUCE licence; the bundled ASIO SDK is GPLv3 or
Steinberg's proprietary licence; VST3 SDK is MIT. Sharing binaries under AGPLv3
means offering the source to recipients.

## Out of scope

Installers and code signing (Windows SmartScreen and macOS Gatekeeper will warn),
DirectSound, AAX/CLAP hosting, ARM Windows/Linux builds.
