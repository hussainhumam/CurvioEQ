# CurvioEQ

<p align="center">
  <img src="resources/app_icon.png" alt="CurvioEQ" width="128" height="128">
</p>

**Per-app equalizer for Windows** — real-time EQ for individual applications without touching system-wide audio.

Captures audio from a running application via process loopback, applies a 10-band EQ, and plays the result on your headphones or speakers.

## Screenshot

<p align="center">
  <img src="resources/Screenshot 2026-08-07 073614.png" alt="CurvioEQ main window">
</p>

## Download

**Windows 10+** — get the latest installer from [GitHub Releases](https://github.com/hussainhumam/CurvioEQ/releases/latest).

See [CHANGELOG.md](CHANGELOG.md) for release history.

## Features

- Per-application EQ — pick any running app and enable EQ without affecting system-wide audio
- **Virtual sink routing** — route dry app audio to a separate device (VB-Cable, Voicemeeter, Steam Streaming Speakers, etc.)
- **Multi-app EQ** — run EQ on several apps at once; each keeps its own bands and 7.1 settings, mixed to the same EQ output device
- Color labels — default blue chip; use **+** to add more colors when enabling EQ on multiple apps
- Single slider panel — selecting an app loads its EQ/surround settings; non-EQ apps show flat 0 dB bands
- 10-band equalizer (±20 dB) with master **All** slider and presets in a 2×2 grid
- Real-time spectrum analyzer
- **Global keybinds** — disable all EQ, mute output device, or mute apps by color label (Settings → Keybinds)
- **System tray** — per-app Enable/Disable EQ rows, show window, quit; optional autostart
- Restore EQ routing when a target app exits and is re-enabled
- Single-instance app with settings persisted under `%AppData%/CurvioEQ/`

## How it works

```
App ──route──► Routing sink
App ──loopback──► EQ (ex : blue) ──┐
App B ──route──► Routing sink   ├── mix ──► EQ output (headphones / speakers)
App B ──loopback──► EQ (ex : red) ───┘
```

Install a virtual playback device (VB-Cable, Voicemeeter, Steam Streaming Speakers), then select it under **Settings → Routing sink**. CurvioEQ routes the app there, captures via process loopback, and replays EQ on your output device.

## Setup

1. Install a virtual playback device if needed (VB-Cable, Voicemeeter, Steam Streaming Speakers).
2. On first launch, pick your **routing sink** and **EQ output device**.
3. Open **Settings** and confirm:
   - **Routing sink** — where the target app's unprocessed audio goes
   - **EQ output device** — where you hear the EQ'd audio
   - **Mute routing sink while EQ is active** — recommended; keeps dry app audio from leaking to your headphones
4. The routing sink and EQ output device must be different.
5. Select a running app and click **Enable EQ**.
6. For Discord and other multi-process apps, enable EQ on the main Discord entry — CurvioEQ routes the full process tree.

### General

1. Click an app in the list to edit its EQ — sliders and 7.1 settings switch to that app's saved values.
2. Use **Disable for app** to stop EQ on the selected app, or **Disable all** to stop every session.
3. Optional: open **Settings → Keybinds** for global shortcuts.
4. Right-click the tray icon for quick per-app Enable/Disable EQ.

## Build

Requirements: Windows 10+, Qt 6.5+, MSVC 2022.

```bat
build_app.bat
```

Or with CMake presets (Qt Creator):

```bat
cmake --preset Desktop_Qt_6_11_1_MSVC2022_64bit_Release
cmake --build --preset Desktop_Qt_6_11_1_MSVC2022_64bit_Release
```

Output: `dist/bin/CurvioEQ.exe`

Every build runs DSP verification automatically and prints DSP state in the terminal; the build fails if the audio engine is broken.

Example build output:

```
=== CurvioEQ DSP State ===
  EQ topology      : parallel peaking (10 bands, +/-20 dB)
  ...
DSP state: HEALTHY (all checks passed)
```

To check DSP state manually:

```bat
dist\bin\CurvioEQ_DspVerify.exe
```

Or from Command Prompt:

```bat
CurvioEQ.exe --dsp-status
```

## Installer

Build a shareable Windows installer:

```bat
build_installer.bat
```

Output: `dist/CurvioEQ-Setup.exe` (includes app, Qt runtime, and optional desktop shortcut).

See [`installer/README.md`](installer/README.md) for details.

## Settings file

Settings are stored at `%AppData%/CurvioEQ/settings.json` (version 6). Key fields:

- `setupCompleted` — first-run wizard completed
- `eqOutputDeviceId` / `eqOutputDeviceName` — where EQ audio plays
- `routingSinkDeviceId` / `routingSinkDeviceName` — virtual routing sink
- `muteRoutingSink` — mute the routing sink while EQ is active
