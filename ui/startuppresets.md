# Start at app startup

Bind a **named preset** to an app’s `.exe`. While CurvioEQ is running, when that exe appears as an audio session, EQ starts with that preset.

Taskbar menus for *other* apps are not supported. Explorer right-click on an `.exe` is.

## Files

| File | Role |
|------|------|
| `startuppresetstore.cpp` | `exe → presetId` JSON |
| `explorerstartupverb.cpp` | HKCU Explorer verb |
| `presetpanelcontroller.cpp` | `ensureNamedPreset()` |
| `sessionlistcontroller.cpp` | Checkable list menu |
| `singleinstanceserver.cpp` | Second-instance payload |
| `main.cpp` | `--start-at-app-startup` |
| `mainwindow.cpp` | Bind, auto-enable, IPC handler |

Related: [presets.md](presets.md), [eqsessions.md](eqsessions.md), [sessionlist.md](sessionlist.md). Exe keys must match [../vst3/addons.md](../vst3/addons.md) (`AppIconProvider::normalizeExePath`).

## Call flow

```
List menu / Explorer
        │
        ▼
ensureNamedPreset()     ── dirty or no named preset → SavePresetDialog
        │                   cancel → abort
        ▼
StartupPresetStore::setBinding(exe, presetId)
        │
        ├── if that exe is already in the running-apps list → enableEqWithPreset(pid)
        └── later: applyStartupPresetsToVisibleSessions() on list refresh / 500 ms poll
                    → enableForProcess(pid, EqSessionStartSettings from preset)
```

Explorer (app already running):

1. `CurvioEQ.exe --start-at-app-startup "C:\...\app.exe"`
2. If a instance exists: IPC `SAS|<utf8 path>` then `startAtAppStartupRequested` → `handleStartAtAppStartup`
3. Else: first instance shows the window, then the same handler (queued)

## Functions to call

**Bind from UI (already have a PID)**

- `bindStartupPresetForExe(exePath)` in `MainWindow` — save-if-needed, persist, enable matching PIDs.
- Unbind: `StartupPresetStore::removeBinding(exe)` (list menu uncheck). Does **not** disable EQ.

**Named preset**

- `PresetPanelController::ensureNamedPreset(&preset)` — if the last applied/saved preset is still clean and exists, returns it; otherwise runs the save dialog.
- `markDirty()` / `markClean(id)` — live EQ/HRTF/dynamics/chain edits vs load/save.

**Auto-enable**

- `applyStartupPresetsToVisibleSessions()` — for each visible PID whose exe is bound and EQ is not running. One attempt per PID (`m_startupApplyAttempted`); missing preset id removes the binding.
- `enableEqWithPreset(pid, preset)` — fills `EqSessionStartSettings` from `hasEq` / `hasSurround` / `hasDynamics` / `hasAudioChain` (missing sections from current UI). Does not push the preset into the sliders unless that PID is selected.

**Identity**

- `AppIconProvider::executablePathForProcess(pid)`
- `AppIconProvider::normalizeExePath(path)` — native separators, absolute, lower-case. Same helper as VST addons.

## On disk

`AppPaths::dataRoot()` / `startuppresets.json`:

```json
{ "bindings": [ { "exe": "c:\\...\\discord.exe", "presetId": "uuid-or-builtin-id" } ] }
```

Explorer verb (registered every launch, HKCU):

`Software\Classes\exefile\shell\CurvioEQ.StartAtAppStartup`

Command: `"CurvioEQ.exe" --start-at-app-startup "%1"`

Uninstall deletes that key (`installer/CurvioEQ.iss`).

## IPC

| Payload | Meaning |
|---------|---------|
| `show` | Raise window (`showRequested`) |
| `SAS|<path>` | Bind startup preset for that exe |

Pipe `|` is invalid in Windows file names.

## How to extend

- Extra per-exe flags: add fields next to `presetId` in the JSON array, load/save in `StartupPresetStore`.
- Do not key by PID. Processes die; exe path (or AUMID later) is the stable id.
- Auto-enable must use `enableForProcess(pid, settings)`, not the no-arg overload, or surround/dynamics/chain come from the **global** UI.

## Do not

- Expect this to appear on Discord/Chrome taskbar jump lists.
- Apply the bound preset to the sliders when a *different* app is selected.
- Retry a failed enable every 500 ms for the same PID (error spam). New PID after relaunch is fine.
- Confuse this with `SettingsStore::applyStartWithWindows` (CurvioEQ at logon).
