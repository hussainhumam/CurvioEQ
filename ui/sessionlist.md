# Running-apps list

Right-hand list of processes that currently have a WASAPI audio session. Identity in the list is **PID**, not exe.

## Files

| File | Role |
|------|------|
| `sessionlistcontroller.cpp` | Model, 500 ms refresh, context menu |
| `appsessiondelegate.cpp` | Row paint (icon, EQ color chip) |
| `../audio/audiosessionenumerator.cpp` | `listActiveSessions()` |

## Item roles

`RoleProcessId`, output device id/name, display name, `RoleEqActive`, `RoleEqColor`, muted.

EQ colors come from `setEqSessions(EqSessionManager::activeSessionColors())`. The list does not start EQ itself.

## Refresh

`SessionListController::refresh()` rebuilds rows from the enumerator (dedupe by PID).

The timer (`AppConstants::kSessionRefreshIntervalActiveMs`, 500 ms) calls `refresh()` then `refreshRequested`. MainWindow then prunes ended engine sessions and [applies startup presets](startuppresets.md).

Manual **Refresh** goes through `MainWindow::refreshSessionList()`.

## Context menu signals

| Signal | When |
|--------|------|
| `enableEqRequested(pid)` / `disableEqRequested(pid)` | Toggle EQ |
| `startupPresetToggled(pid, enable)` | Start at app startup |
| `soundModsRequested(pid)` | Game sound-mod dialog |
| `recordClipRequested` / `stopClipAnalyzeRequested` | Clip analyzer |
| `appVolumeChanged(pid, percent)` | Per-app volume / mute row |

`setStartupPresetBoundQuery(lambda)` — checkable “Start at app startup” uses exe via `AppIconProvider::executablePathForProcess`.

Double-click toggles EQ (`toggleEqAt`).

## Helpers

- `selectedProcessId()`, `processIds()`, `displayNameForPid(pid)`

## How to extend

- New menu action: add in `showContextMenu`, emit a signal, handle in `MainWindow` (same pattern as startup presets).
- Need a stable app id: resolve exe (or AUMID) at click time; do not persist PID.

## Do not

- Treat display name as unique (two Chrome PIDs).
- Enable EQ from the list without going through `EqSessionManager` (routing + colors + engine).
