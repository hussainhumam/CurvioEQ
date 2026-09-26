# Settings and data root

JSON settings plus where all user files live (portable vs installed).

## Files

| File | Role |
|------|------|
| `settingsstore.cpp` | `settings.json` load/save, Start with Windows Run key |
| `apppaths.cpp` | Data directory |
| `settingsdialog.cpp` | Devices, I/O, keybinds entry |
| `setupdialog.cpp` | First-run sink + output |

## `AppPaths::dataRoot()`

1. If `portable.txt` exists next to the exe (or parent of `bin/`), that folder is the root.
2. Else `%AppData%/CurvioEQ` (with a few legacy `PerAppEQ` search paths).

`settingsFilePath()` → `dataRoot()/settings.json`. Creating data root happens on first `dataRoot()` if no file exists yet.

Other files in the same root:

| File | Feature |
|------|---------|
| `settings.json` | This module |
| `presets.json` | [presets.md](presets.md) |
| `startuppresets.json` | [startuppresets.md](startuppresets.md) |
| `addons.json` | [../vst3/addons.md](../vst3/addons.md) |
| `soundmods/` | Game file mods |
| `welcome.shown` | First-run marker |

## `AppSettings` (high-signal fields)

| Field | Meaning |
|-------|---------|
| `setupCompleted` | Wizard done |
| `eqOutputDeviceId` / `Name` | Where you hear EQ |
| `routingSinkDeviceId` / `Name` | Dry app destination |
| `muteRoutingSink` | Mute sink while EQ is on |
| `spectrumLimiterDb` | Session ceiling UI/DSP |
| `startWithWindows` | HKCU Run → `CurvioEQ.exe --startup` |
| `engineIo()` helpers | Sample rate, buffer, precision, drift, … |
| `vst3ExtraFolders` | Extra VST scan paths |
| Keybind strings | Global hotkeys |

`SettingsStore::applyStartWithWindows` is **CurvioEQ at logon**, not [app startup presets](startuppresets.md).

## How to extend

- New persisted field: `AppSettings` + parse/write in `settingsstore.cpp` (watch the version branch if you add a format version).
- New standalone JSON (like startup presets) if the blob should not bloat `settings.json`.

## Do not

- Write settings next to the exe unless `portable.txt` is present.
- Store per-app EQ drafts here; those are RAM snapshots in [eqsessions.md](eqsessions.md).
