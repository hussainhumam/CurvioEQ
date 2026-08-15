# Changelog

All notable changes to CurvioEQ are documented here.

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
