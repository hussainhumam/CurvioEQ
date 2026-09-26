# System tray

`QSystemTrayIcon` menu: show window, per-app EQ rows, quit. Not a Windows jump list.

## Files

| File | Role |
|------|------|
| `traycontroller.cpp` | Icon, menu, messages |
| `mainwindow.cpp` | `onTrayToggleEq`, `updateEqControlState` → `updateEqSessions` |

Sessions: [eqsessions.md](eqsessions.md).

## Setup

`TrayController::setup()` after the window exists. `isAvailable()` is false if the OS has no tray.

## Session rows

`updateEqSessions(EqSessionManager::configuredTraySessions())` rebuilds Enable/Disable actions.

Each row: `toggleEqForProcessRequested(pid)` → `MainWindow::onTrayToggleEq`:

- Running → `disableForProcess`
- Else → `restoreForProcess` (needs an in-memory snapshot with color + `hasStoredGains`) + attach VST addons

That restore path is **not** the [startup preset](startuppresets.md) bind. If the process is new, startup auto-enable on the list poll is what reapplies a named preset.

## Other signals

`showWindowRequested`, `quitRequested`, `updateRequested`, `logMessage`. Balloon helpers: `showCriticalMessage`, `showUpdateAvailableMessage`.

## How to extend

- Extra tray items: add in `setup()`, keep EQ rows in `updateEqSessions` so they stay in sync with the manager.
- Do not use the tray controller for Explorer `.exe` verbs ([startuppresets.md](startuppresets.md)).

## Do not

- Expect restore to work after a CurvioEQ restart (snapshots are RAM-only). Use startup-preset bindings for “this exe, this preset, next launch”.
