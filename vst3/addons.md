# Per-app VST3 add-ons

Up to a fixed number of VST3 slots **per exe**, plus that app’s audio-chain order. Independent of [named EQ presets](../ui/presets.md) except both key by the same normalized path.

## Files

| File | Role |
|------|------|
| `vst3addonstore.cpp` | `addons.json` |
| `vst3addonmanager.cpp` | Scan, instantiate, `attachToSession` |
| `vst3plugin.cpp` | Host wrapper |
| `../ui/addonspanel.cpp` | Add-ons menu |

Exe key: `AppIconProvider::normalizeExePath` — **same as** [../ui/startuppresets.md](../ui/startuppresets.md).

## Data

`Vst3AppAddons`: `exePath`, `pluginSlots[]` (`uid`, `name`, `vendor`, `path`, `state` blob, `occupied`), `chain`.

`addonsForExe(path)` / `setAddonsForExe`. Empty exe → empty slots.

`Vst3AddonManager::attachToSession(pid, exePath, sampleRate)` loads those plugins into the live `EqAudioSession`. MainWindow does this after enable (list, tray restore, startup preset).

Extra scan folders: `AppSettings.vst3ExtraFolders` ([../ui/settings.md](../ui/settings.md)).

## On disk

`AppPaths::dataRoot()` / `addons.json` — object keyed by normalized exe, slots + chain array.

## How to extend

- More slots: `kAudioChainAddonCount` / chain max in `audio/audiochainorder.h`, JSON, UI menu.
- Keep using `normalizeExePath`; if you invent a second key format, startup presets and addons will disagree.

## Do not

- Store addons by PID.
- Assume a preset’s `hasAudioChain` replaces `addons.json`; chain order can live in both a preset and the addon store — apply order is MainWindow / enable path.
