# Changelog

All notable changes to CurvioEQ are documented here.

## [Unreleased]

## [1.1.1] - 2026-08-17

Audio stability patch. No new features.

### Fixed

- Crackling, dropouts, and uneven playback with EQ on, including when every band is at 0 dB
- Capture and output falling out of step: leftover loopback packets could pile up, and the mixer could write more than one device period at a time
- Underruns repeating the last sample (buzz/crackle); they now play silence
- When the session buffer is too full, a whole chunk is skipped instead of splicing audio mid-write
- Stereo apps on a surround output no longer copy the front pair into rear/center/LFE
- Several apps on the same color label sounding louder or more compressed than a single app; mix level is scaled with session count and the limiter ramps instead of clamping instantly
- Stopping or disabling EQ while sliders move could race the mixer; session rings stay valid until the mixer drops them, and gain/surround updates stay locked with session lifetime

### Improved

- Per-app capture runs EQ, optional surround, resample, then mix-format in one chunk, then hands a lock-free ring to a shared mixer
- Startup DSP checks (EQ, resampler, ring buffer) are printed in the in-app log under the spectrum, not only in a debugger terminal
- Release builds still fail if those DSP checks fail
- Unused audio helpers and dead session fields removed

## [1.1.0] - 2026-08-15

### Added

- Global keybinds (Settings → Keybinds): disable all EQ, mute output device, mute by color label 1–8
- System tray per-app Enable/Disable EQ rows for each configured session
- Master **All** band slider; EQ range expanded to ±20 dB
- Presets panel 2×2 button grid
- Restore EQ routing when a target app exits and is re-enabled

### Improved

- Audio path: batched ring buffer I/O, shorter mixer lock, preallocated capture buffers, EQ coefficient double-buffering, cheaper soft clip, smaller ring buffer (2048 frames)
- Float loopback format fallback for broader device compatibility
- Running Apps list excludes CurvioEQ itself

### Fixed

- Re-enable EQ from tray or main window without losing color assignment when the process is still running

### Notes for v1.0.0 users

Existing settings are preserved. New keybind fields default to off/empty until configured under Settings → Keybinds.

## [1.0.0] - 2026-08-07

First public release.

- Windows 10+
- Per-app 10-band EQ with multi-app support, presets, spectrum analyzer, and system tray integration
- Requires a virtual audio device for the routing sink (VB-Cable, Voicemeeter, or Steam Streaming Speakers)
