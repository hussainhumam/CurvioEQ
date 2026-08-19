# Changelog

All notable changes to CurvioEQ are documented here.

## [Unreleased]

## [1.2.0] - 2026-08-19

Major routing-model release focused on reliability, determinism, and maintainability.
This version removes the unstable same-device hide/duck path and standardizes CurvioEQ
on virtual sink routing for all sessions.

### Breaking / Behavioral changes

- Removed the same-device replay + session-hide routing path.
- CurvioEQ now operates in a single routing model:
  - app audio is routed to a **routing sink** (VB-Cable / Voicemeeter / Steam Streaming Speakers)
  - process loopback is captured, EQ is applied, and output is rendered to the selected EQ device
- First-run setup and Settings now require and expose routing sink + EQ output configuration.
- Main-window routing mode toggle and related state text were removed to avoid mismatched runtime states.

### Audio architecture changes

- `EqAudioSession` was refocused to virtual-sink-only execution:
  - removed all same-device hide logic, duck verification branches, and fallback control flow
  - startup/teardown paths are shorter and easier to reason about
  - routing maintenance now handles only process-tree sink reroute validation
- Added integrated **dynamic range** and **loudness** stages to the live per-app audio chain:
  - optional per-session dynamic range compression
  - optional loudness rider stage to improve low-volume intelligibility
  - both stages run in the same real-time session pipeline as EQ/surround
- Session lifecycle behavior is more deterministic:
  - explicit early validation for required sink configuration
  - explicit fail messages when process-tree route does not stick
  - unchanged process-loopback capture and render thread model, with less branching in hot paths

### DSP / runtime optimizations

- Reduced memory traffic in per-chunk processing:
  - dynamics and loudness stages now process in-place on the active write buffer
  - removed redundant intermediate copy buffers previously used by those stages
- Capture-buffer layout in `EqAudioSession` simplified:
  - dropped now-unused per-chunk dynamic/loudness scratch vectors
  - preserved preallocation behavior for capture/resample/mix buffers
- Kept ring-buffer and clock-sync behavior stable while reducing per-chunk copy overhead.

### Stability and error handling

- Removed a class of startup failures caused by same-device hide readback validation.
- Removed watchdog/hide-loss branches tied to duck state transitions.
- Preserved existing protections around route verification, process exit detection, and renderer shutdown.

### UI / UX changes

- **Per-app controls**
  - enabling EQ for multiple apps no longer requires picking a different color each time
  - color remains optional for grouping/quick identification, not a blocker for multi-app EQ
- **System tray / context menu**
  - tray menu now exposes direct **Enable EQ** / **Disable EQ** actions for faster per-app control
  - reduced clicks to toggle EQ without reopening the main window
- **Settings dialog**
  - now consistently presents routing sink + EQ output controls
  - removed mode-selection controls and mode-dependent visibility logic
- **First-run setup**
  - now collects both routing sink and EQ output on initial configuration
  - updated onboarding copy to match virtual-sink-only behavior
- **Main window**
  - removed routing mode toggle row
  - removed routing status text that represented old mode state
  - added **Clear log** button for quick log reset during testing/debugging

### Dead code and leftover cleanup

- Removed obsolete settings and migration paths tied to runtime routing mode selection:
  - `audioRoutingMode` no longer read/written by app settings
  - settings now persist only active routing-sink/output behavior used by runtime
- Trimmed `AudioSessionVolume` to current usage:
  - removed unused duck/probe/device-scoped session volume APIs introduced for same-device hiding
  - removed unused `setMute(...)` wrapper
  - kept `toggleMute(...)` path used by app-level mute keybind behavior
- Removed orphaned local debug harness:
  - deleted `tools/test_loopback.cpp`

### Packaging / release metadata

- Installer version bumped to `1.2.0` in Inno Setup script.
- Manifest assembly version bumped to `1.2.0.0`.
- GitHub publish script default/version usage updated to `1.2.0`.

### Documentation updates

- `README.md` rewritten to reflect virtual-sink-only architecture and setup.
- Removed outdated references to legacy mode toggling and same-device hide behavior.
- Updated setup guidance to emphasize sink/output separation and routing expectations.
- Dynamic range + loudness processing is now explicitly documented in the runtime pipeline.

### Internal quality notes

- Net reduction in branching complexity across session startup and hot-path chunk processing.
- Smaller maintenance surface for future DSP and routing changes.
- Clearer one-path mental model for debugging app routing issues in production.

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
