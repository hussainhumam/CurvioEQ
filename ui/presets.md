# Presets

Named EQ (and optional surround / dynamics / chain) snapshots. Built-ins plus user list in `presets.json`.

## Files

| File | Role |
|------|------|
| `presetstore.cpp` | Load/save, built-ins, favorites, import/export |
| `presetpanelcontroller.cpp` | List UI, apply, save, dirty flag |
| `savepresetdialog.cpp` | Name + which sections to include |

Used by [startuppresets.md](startuppresets.md) via `ensureNamedPreset()`.

## `EqPreset`

- `id`, `name`
- `eq`, `surround`, `dynamics`, `audioChainOrder`
- `hasEq` / `hasSurround` / `hasDynamics` / `hasAudioChain` — only those sections are applied
- `isBuiltIn`

`addUserPreset` always creates a **new** UUID. There is no in-place overwrite.

## Dirty vs saved

`PresetPanelController` tracks:

- `m_cleanPresetId` — last loaded or saved id
- `m_dirty` — user changed EQ/HRTF/dynamics/chain after that

`MainWindow` calls `markDirty()` from live-edit paths (`beginUserEqEdit`, surround/dynamics/chain, reset, undo/redo). Load/save/AutoEQ call `markClean(id)`.

**`ensureNamedPreset(out)`**

1. If `!dirty` and `presetById(cleanId)` exists → return it (built-in or user).
2. Else `SavePresetDialog`. Cancel → `false`.
3. `addUserPreset` → `markClean(created.id)`.

Clicking **Save** uses the same dialog helper (`saveCurrentPresetInteractive`).

Selecting a list row: `applyPresetToUi` → `presetApplied` → MainWindow writes a draft for the **selected** PID.

## On disk

`AppPaths::dataRoot()` / `presets.json` — user presets + favorite ids. Built-ins live in code (`PresetStore::defaultBuiltIns`).

## How to extend

- New section: add flags + fields on `EqPreset`, JSON in `presetstore.cpp`, checkboxes on `SavePresetDialog`, apply/read in the panel controller, and `hasX` handling in `enableEqWithPreset`.
- Updating an existing user preset in place is not implemented; add an explicit “overwrite” API if you need it.

## Do not

- Treat list selection as “current preset still matches the sliders” without the dirty flag.
- Apply a preset’s missing sections from stale snapshot data; `has*` exists so partial presets leave other UI state alone.
