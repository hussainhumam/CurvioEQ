# EQ sessions

One WASAPI/process-loopback session per PID. UI state per session lives in `EqSessionManager` snapshots; audio lives in `AudioEngine` / `EqAudioSession`.

## Files

| File | Role |
|------|------|
| `eqsessionmanager.cpp` | Enable/disable, drafts, live pushes |
| `../audio/audioengine.cpp` | `startSession` / `stopSession` |
| `../audio/eqaudiosession.cpp` | Capture → DSP chain → render |

List UI: [sessionlist.md](sessionlist.md). Routing: [../audio/routing.md](../audio/routing.md).

## Snapshot (`EqSessionSnapshot`)

Per PID: `eq`, `virtualSurround`, `dynamicRange`, `audioChainOrder`, `labelColor`, `active`, `hasStoredGains`.

RAM only. Process exit clears restore-worthiness (label color). Not written to `settings.json`.

## Enable

**`enableForProcess(pid)`** — Enable EQ using:

- EQ from a stored draft if `hasStoredGains`, else current UI
- Surround / dynamics / chain always from **current UI** (legacy path)

**`enableForProcess(pid, EqSessionStartSettings)`** — All four sections from the struct. Use this for [startup presets](startuppresets.md) so a background app does not steal the selected app’s HRTF/dynamics.

Both allocate a label color, `resolveDevices()` (EQ output + routing sink), then `AudioEngine::startSession(...)`.

If the session is already active, both return `true` without restarting. To **replace** settings on a live session, call `saveDraftForProcess` (it pushes EQ/surround/dynamics/chain into the engine when `active`).

## Live edits

While sliders move, MainWindow calls `scheduleLiveGainsForProcess` / `saveDraftForProcess` / `applyLiveBalance` for the **selected** PID. Other running sessions keep their snapshots.

`applySnapshotToUi(pid, ...)` loads a snapshot into the sliders when selection changes.

## Disable / restore

- `disableForProcess` — stop engine session, keep draft, `active = false`
- `restoreForProcess` — `canRestoreProcess` (draft + valid color, not running) then `enableForProcess(pid)` (UI mix, not startup-preset settings)
- Tray uses restore; see [tray.md](tray.md)

## How to extend

- Per-session extras: add fields to `EqSessionSnapshot` and `EqSessionStartSettings`, pass them through `startSession` / `EqAudioSession` if they must be audible.
- Do not store snapshots only in the list model; the manager is the source of truth for “EQ on”.

## Do not

- Call the no-arg `enableForProcess` when you already have a preset snapshot.
- Assume two PIDs of the same exe share a snapshot (they do not; startup presets re-apply by exe on each new PID).
