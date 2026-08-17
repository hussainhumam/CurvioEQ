# CurvioEQ

<p align="center">
  <img src="resources/app_icon.png" alt="CurvioEQ" width="128" height="128">
</p>

**Per-app equalizer for Windows** — real-time EQ for individual applications without touching system-wide audio.

Captures audio from a running application via process loopback, applies a 10-band EQ, and plays the result on a device you choose. The target app's original audio is routed to a separate playback device (typically a virtual cable you install yourself).

## Screenshot

<p align="center">
  <img src="resources/Screenshot 2026-08-07 073614.png" alt="CurvioEQ main window">
</p>

## Download

**Windows 10+** — get the latest installer from [GitHub Releases](https://github.com/hussainhumam/CurvioEQ/releases/latest).

> Requires a virtual audio device for the routing sink (for example VB-Cable, Voicemeeter, or Steam Streaming Speakers).

See [CHANGELOG.md](CHANGELOG.md) for release history.

## Features

- Per-application EQ — pick any running app and enable EQ without affecting system-wide audio
- **Multi-app EQ** — run EQ on several apps at once; each keeps its own bands and 7.1 settings, mixed to the same EQ output device
- Color labels — default blue chip; use **+** to add more colors when enabling EQ on multiple apps
- Single slider panel — selecting an app loads its EQ/surround settings; non-EQ apps show flat 0 dB bands
- 10-band equalizer (±20 dB) with master **All** slider and presets in a 2×2 grid
- Real-time spectrum analyzer
- EarTrumpet-style per-app output routing to hide original audio while EQ replay plays on your headphones or speakers
- **Global keybinds** — disable all EQ, mute output device, or mute apps by color label (Settings → Keybinds)
- **System tray** — per-app Enable/Disable EQ rows, show window, quit; optional autostart
- Restore EQ routing when a target app exits and is re-enabled
- Single-instance app with settings persisted under `%AppData%/CurvioEQ/`

## How it works

```
App A ──route──► Routing sink
App A ──loopback──► EQ (blue) ──┐
App B ──route──► Routing sink   ├── mix ──► EQ output (headphones / speakers)
App B ──loopback──► EQ (red) ───┘
```

CurvioEQ does **not** bundle or install a virtual audio device. Install one yourself (for example VB-Cable or Voicemeeter), then select it under **Settings → Routing sink**.
Note: Steam Streaming Speakers also work.

## Setup

1. Install a virtual playback device if you do not already have one.
2. Open **Settings** and choose:
   - **Routing sink** — where the target app's unprocessed audio goes
   - **EQ output device** — where you hear the EQ'd audio
3. The two devices must be different.
4. Select an app (blue is selected by default), click **Enable EQ**. Use **+** next to the color chip to add another color for the next app.
5. Click an app in the list to edit its EQ — sliders and 7.1 settings switch to that app's saved values.
6. Use **Disable for app** to stop EQ on the selected app, or **Disable all** to stop every session.
7. Optional: open **Settings → Keybinds** to assign global shortcuts for disable-all, output mute, and per-color label mute.
8. Right-click the tray icon for quick per-app Enable/Disable EQ without opening the main window.

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

`%AppData%/CurvioEQ/settings.json` stores routing sink, EQ output, keybinds, and startup preferences.

## CLI flags

| Flag | Purpose |
|------|---------|
| `--dsp-status` | Print DSP architecture and run verification checks (terminal) |
| `--startup` | Start hidden to the system tray (used by Windows autostart only) |
| `--clear-all-routing` | Clear all Windows per-app output overrides (utility) |
| `--test-route <pid> <deviceId>` | Test routing for a process (utility) |

## License

Proprietary — all rights reserved. See [LICENSE](LICENSE).
