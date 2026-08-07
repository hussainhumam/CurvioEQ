# CurvioEQ

<p align="center">
  <img src="resources/app_icon.png" alt="CurvioEQ" width="128" height="128">
</p>

**Per-app equalizer for Windows** — real-time EQ for individual applications without touching system-wide audio.

Captures audio from a running application via process loopback, applies a 10-band EQ, and plays the result on a device you choose. The target app's original audio is routed to a separate playback device (typically a virtual cable you install yourself).

## Screenshot

<p align="center">
  <img src="Screenshot 2026-08-07 073614.png" >
</p>


## Download

**Windows 10+** — get the latest installer from [GitHub Releases](https://github.com/hussainhumam/CurvioEQ/releases/latest).

> Requires a virtual audio device for the routing sink (for example VB-Cable, Voicemeeter, or Steam Streaming Speakers).

## Features

- Per-application EQ — pick any running app and enable EQ without affecting system-wide audio
- 10-band equalizer with presets
- Real-time spectrum analyzer
- EarTrumpet-style per-app output routing to hide original audio while EQ replay plays on your headphones or speakers
- System tray integration and optional autostart
- Single-instance app with settings persisted under `%AppData%/CurvioEQ/`

## How it works

```
Target app ──route──► Routing sink (virtual device you pick)
Target app ──loopback──► EQ ──► EQ output (headphones / speakers you pick)
```

CurvioEQ does **not** bundle or install a virtual audio device. Install one yourself (for example VB-Cable or Voicemeeter), then select it under **Settings → Routing sink**.
Note: Steam Streaming Speakers also work.

## Setup

1. Install a virtual playback device if you do not already have one.
2. Open **Settings** and choose:
   - **Routing sink** — where the target app's unprocessed audio goes
   - **EQ output device** — where you hear the EQ'd audio
3. The two devices must be different.
4. Select an app from the list and click **Enable EQ**. Click **Disable EQ** when done.

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

## Installer

Build a shareable Windows installer:

```bat
build_installer.bat
```

Output: `dist/CurvioEQ-Setup.exe` (includes app, Qt runtime, and optional desktop shortcut).

See [`installer/README.md`](installer/README.md) for details.

## Settings file

`%AppData%/CurvioEQ/settings.json` stores routing sink, EQ output, and startup preferences.

## CLI flags

| Flag | Purpose |
|------|---------|
| `--startup` | Start hidden to the system tray (used by Windows autostart only) |
| `--clear-all-routing` | Clear all Windows per-app output overrides (utility) |
| `--test-route <pid> <deviceId>` | Test routing for a process (utility) |

## License

Proprietary — all rights reserved. See [LICENSE](LICENSE).
