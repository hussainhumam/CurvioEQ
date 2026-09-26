# Changelog

All notable changes to CurvioEQ are documented here.

## [1.3.2] - 2026-09-26

Patch on 1.3.1: rewritten audio engine path, full Settings I/O panel, **Start at app startup**, limiter badge on the ceiling line.

Routing is unchanged: app → routing sink, process loopback → EQ → output device.

### Engine — `EngineIoSettings`

New `audio/engineiosettings.h`. One struct is copied into `AudioEngine`, `EqAudioSession`, `WasapiRenderer::open`, and `ProcessLoopbackCapture`.

Enums: `ProcessingPrecision` (Float32 / Float64), `ResampleQuality` (Fast / Balanced / Maximum), `ChannelLayout` (Auto / Stereo / 5.1 / 7.1), `OutputFormat` (Auto / Float32 / PCM16), `ThreadPriority` (RealtimeAudio / Normal), `ShareMode` (PreferShared / Exclusive), `BufferSizePreset` (Low=16 / Balanced=64 / Safe=512 / Custom).

Helpers: `requestedBufferFrames()`, `safetyExtraFrames()`, `mixChannelCountForDevice()`, `affinityCoreOrNone()`, `useDoublePrecision()`, `useRealtimeAudio()`, `preferExclusive()`.

`AudioEngine::setEngineSettings` stores the block; `MainWindow::applySettings` compares previous vs next `engineIo()` and calls `EqSessionManager::restartActiveSessions()` when I/O changed.

### Engine — WASAPI (`wasapirenderer`, `wasapilowlatency.h`, `wasapierror.h`)

- `WasapiRenderer::open(deviceId, EngineIoSettings, error)` instead of a device-id-only open
- Shared: `IAudioClient3::GetSharedModeEnginePeriod` + `InitializeSharedAudioStream` via `initializeSharedSnappedPeriod` / `snapEnginePeriod` (period clamped to min/max and snapped to `fundamentalPeriod`)
- If Client3 is missing or init fails: classic `IAudioClient::Initialize` with `AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUTOCONVERTPCM`
- Exclusive: event-callback init first, then non-event Initialize, then **Shared fallback** with a reduced `EngineIoSettings`
- Output format: Mix format vs requested Float32 / PCM16; failed `IsFormatSupported` / Initialize falls back
- `preferredFrameCount()` is the **engine period**; `bufferFrameCount()` / `prerollFrameCount()` / `lastPaddingFrames()` / `lastWaitTimedOut()` / `deviceLost()` / `generation()` / `readClock()` feed mix and clock sync
- Upmix/downmix uses `mixChannelCountForDevice` vs device channel count
- Capture: loopback client takes the same I/O settings; period/chunk from `AppConstants::captureChunkFramesForPeriod` / `effectiveRingFrames`

### Engine — mix, clock, threads

- `audio/audioclock.h` — QPC + device position
- `ClockSync`: `observeCapture` / `observeRender`; PLL `rateRatio` only if `driftCorrection`; otherwise `rateRatio == 1`. `trimmedWriteFrames` drops up to 1/8 of a write when fill is above `highFillFrames`
- Ring target / high fill: `ringTargetFillFramesForPeriod` / `ringHighFillFramesForPeriod` from requested buffer **and** actual period (not a fixed 512-frame world)
- `requestedEnginePeriodFrames`: for buffers ≥ 16, period request is `max(buffer/4, 16)`
- Mix loop: wait on render event; mix `periodFrames`; catch-up capped at `periodFrames * 3`; `m_mixerRingUnderruns` / wait-timeout / padding-full logged
- `AudioThreadUtils`: `_MM_FLUSH_ZERO` + `_MM_DENORMALS_ZERO`; `AvSetMmThreadCharacteristicsW(L"Pro Audio")` when realtime; `SetThreadAffinityMask` when affinity is not Auto
- `AudioEngine::rebuildAfterInvalidation` / `deviceInvalidated` honor `autoRecovery`

### Engine — DSP precision and session

- `EqAudioSession` holds `EngineIoSettings m_ioSettings`
- `EqProcessor` / `DynamicsProcessor` / `LoudnessProcessor` / `SpectrumCeilingLimiter` / `MixLimiter`: `setUseDoublePrecision` (64-bit accumulators on the session/mix path). WASAPI, VST, HRTF stay float32
- `Resampler` quality enum wired from `ResampleQuality`
- `EqAudioSession::logMeasuredLatency`: period + ringFill + preroll + HRTF IR + limiter OLA (`kHop` if ceiling engaged) + VST `latencySamples()`
- `setSessionBalance(pid, balance)` live path for the new L/R control (`EqState::balance`, `kMinBalance`−`kMaxBalance`)

New/split headers: `engineiosettings.h`, `audioclock.h`, `wasapilowlatency.h`, `wasapierror.h`.

### Settings — persist and UI

`AppSettings` gains: `sampleRate`, `bufferFrames`, `processingPrecision`, `resampleQuality`, `channelLayout`, `outputFormat`, `driftCorrection` (default **false**), `safetyBufferAuto`, `safetyBufferFrames`, `threadPriority`, `cpuAffinityAuto`, `cpuAffinityCore`, `shareMode`, `autoRecovery`.

JSON keys include `bufferPreset` (low/balanced/safe/custom) plus the enum strings (`float32`/`float64`, `fast`/`balanced`/`maximum`, `stereo`/`surround51`/`surround71`, `pcm16`, `realtime`/`normal`, `shared`/`exclusive`).

`SettingsDialog`: combo + spin rows for rate/buffer/safety/affinity; `populateAudioIoControls` / `engineSettingsFromControls` / `setAudioIoDefaults`. `AppConstants` clamps: rate 8000–384000, buffer 1–65536, listed rates `{44100,48000,96000,192000}`.

### Start at app startup — implementation

- `StartupPresetStore` (`startuppresets.json`): `{ "bindings": [ { "exe", "presetId" } ] }`. Key = `AppIconProvider::normalizeExePath` (absolute, native separators, lower-case). Same helper as `Vst3AddonStore`
- `PresetPanelController`: `m_cleanPresetId` / `m_dirty`; `markDirty` / `markClean` / `ensureNamedPreset` (save dialog if dirty or no named preset)
- `SessionListController`: checkable **Start at app startup**; `startupPresetToggled(pid, enable)`; `setStartupPresetBoundQuery`; `processIds()`
- `MainWindow::bindStartupPresetForExe` / `enableEqWithPreset` / `applyStartupPresetsToVisibleSessions` (`m_startupApplyAttempted` so a failed enable is not retried every 500 ms for the same PID)
- `EqSessionManager::enableForProcess(pid, EqSessionStartSettings)` + `startPreparedSession` — all four sections from the preset; missing `has*` flags filled from current UI. No-arg `enableForProcess` unchanged (EQ draft + UI surround/dynamics/chain)
- `ExplorerStartupVerb::registerVerb`: HKCU `exefile\shell\CurvioEQ.StartAtAppStartup`, command `"CurvioEQ.exe" --start-at-app-startup "%1"`, `SHChangeNotify`. Inno `uninsdeletekey` on that key
- `SingleInstanceServer`: payload `show` vs `SAS|<utf8 path>`; `startAtAppStartupRequested`
- `main.cpp`: parse `--start-at-app-startup`, forward to existing instance or `QTimer::singleShot` → `handleStartAtAppStartup`

### Spectrum limiter UI — implementation

- `handleCalloutPath`: one `QPainterPath`; tip at `dbToY(m_ceilingDb)` clamped to body; only **right** corners `quadTo` radius
- `layoutHandle` / `handleRect`: `y = ceilingY - height/2`; clamp to widget ± `kHandleWidgetOverflow` (6), not `plot.top/bottom`
- Plot margins `kPlotTopMargin` 12 / `kPlotBottomMargin` 16 so 0 dB / −48 dB can sit on the line
- DSP class still `SpectrumCeilingLimiter` on `EqAudioSession`; UI still `MixLimiter::dbToLinear` for the dB→linear convert only

### Smaller UI / fixes — technical

- `EqState::balance`; slider under bands; `EqSessionManager::applyLiveBalance`
- Session-list context menu: extra `addSeparator()` calls removed
- Startup enable no longer always overwrote surround/dynamics/chain from global UI (`enableForProcess` surround readers)
- Second-instance Explorer bind no longer only sent `"show"`
- Mix catch-up `min(available, period*3)`; underrun atomics
- Shared `normalizeExePath`; unused includes (`QScrollBar`, `QCursor` on plot, unused `log.h` on session manager)
- clangd: `CMAKE_EXPORT_COMPILE_COMMANDS`, `.clangd` compilation database (so Qt headers resolve)

Routing sink and EQ output must still differ. Multi-process apps still EQ from the main process (full process tree). Built-in tone curves are unchanged.

## [1.3.1] - 2026-09-05

Patch on 1.3.0: **Add-ons** (VST3), a spectrum **ceiling limiter**, clip frequency finder, and a **portable** zip.

### Add-ons

VST3 plugins as per-app inserts (up to 4). Menu **Add-ons** — same dropdown style as Settings / Edit.

- Select a running app, then **Add** from installed VST3 plugins
- **Configure** opens the plugin editor; **Delete** removes it
- New add-ons append at the end of that app’s chain; reorder them with EQ / surround / dynamics / loudness in **Audio chain**
- Scans `C:\Program Files\Common Files\VST3` plus extra folders in Settings
- Saved per app exe in `addons.json`
- Audio only while EQ is on. A crashing plugin can take CurvioEQ down.

### Spectrum ceiling limiter

The spectrum is also a limiter: drag the ceiling label (or the dashed line). Only frequency bands that poke through the line are limited; quieter bands stay as they are. 0 dB bypasses. Mix-bus clip safety is unchanged. The ceiling is saved in settings.

### Clip frequency finder

Right-click a running app → **Record clip** / **Stop and analyze**. Logs the main frequencies in that app’s audio (about 15 s cap).

### Portable build

GitHub Releases now include `CurvioEQ-1.3.1-portable.zip` next to the installer.

- Unzip and run `bin\CurvioEQ.exe`
- `portable.txt` in the unzipped folder keeps settings, add-ons, and sound mods **in that folder** instead of `%AppData%\CurvioEQ`
- The installed copy still uses AppData

### Also in 1.3.1

- App volume slider can go to **150%** (boost on the EQ output above 100%)
- Right-scale on the spectrum (0 to −48); grab cursor on the ceiling label

## [1.3.0] - 2026-09-03

CurvioEQ 1.3.0 is a big quality-of-life and mixing release on top of 1.2.1. There is a new **Edit** menu with EQ Undo / Redo. Advanced EQ is faster to edit. Presets can store more than EQ. HRTF, Dynamics, and processing order can follow the app you have selected. The main window, keybinds, online headphone lists, and updates are cleaned up. Built-in tone curves are unchanged.

### Edit menu (new)

1.2.1 had no EQ undo. **Edit** is a new menu:

- **Undo** — Ctrl+Z — steps back the last EQ change (Simple sliders or Advanced filters)
- **Redo** — Ctrl+Y (also Ctrl+Shift+Z) — steps forward again
- A continuous drag is one step, not dozens
- Shortcuts use the physical Z and Y keys, so they work on Arabic and other layouts, and while a frequency / gain / Q box is focused

### Advanced EQ

Advanced mode is built around the frequency-response graph.

**Selecting and moving dots**
- Click a dot to select it. The selected handle is larger and highlighted.
- Drag a box around several dots to select them as a group.
- Drag any selected dot and the whole group moves together, so a mid-range bump or a high-end shelf can be shifted without rebuilding each filter.
- Filters still cannot cross each other on the frequency axis, so the curve stays ordered from low to high.

**Adding a filter with a preview**
- Hover empty space on the graph. A ghost peaking filter appears at the pointer.
- A faint second curve shows how the full response would look if you added that filter — boost, cut, and how it blends with what is already there — before you commit.
- Right-click **Add dot** to place it (up to the existing filter limit). Default new filters are peaking at standard Q.

**Removing filters**
- **Delete** or **Backspace** removes every selected dot. No need to right-click each one.
- Right-click **Remove dot** still removes the handle under the cursor (or the current selection).
- Right-click **Reset** still zeros Advanced EQ.

**Readout at the bottom left**
- The graph prints a live line in the lower left, for example: `Peaking    Fc  3500 Hz    Gain  +4.0 dB    Q  1.41`
- It follows what you are hovering, dragging, or about to add: filter type (peaking / low shelf / high shelf), center frequency, gain, and Q.
- Hovering an existing handle still shows the same values on the handle itself.

**Slider tips**
- Sliders and the graph show the value in a tip while you drag (dB, %, dynamics mode).

Simple mode is unchanged: ten bands, master **All** slider. Switching Simple ↔ Advanced still keeps each mode’s last curve; a loaded Advanced/AutoEQ preset still cannot drop to Simple until you reset to flat.

### Presets

**Save what you actually want**
- Save is no longer a name-only prompt. You get checkboxes: **EQ** (on by default), **HRTF**, **Dynamics**, **Audio chain**, and **All**.
- OK stays off until there is a name and at least one section. Default names are still “My preset” / “My Advanced preset”.
- Load applies **only** the sections that were saved. Unsaved parts of that app stay as they are. Example: an HRTF-only preset does not flatten EQ or flip Simple/Advanced.

**What a click will change**
- User-saved items show a suffix such as `EQ+HRTF` or `EQ+Dynamics+Chain`. Advanced EQ still shows **· Advanced**.
- Hover a list row for “Includes: …”.

**Per app, not the whole PC**
- HRTF, Dynamics, and chain stored in a preset apply to the **currently selected app**.
- Other apps keep their own EQ, surround, dynamics, and order.
- **Audio → Audio chain** in the menu is still the program-wide default for a newly enabled app. A preset can override that default for one app.

**Favorites and sections**
- Right-click a preset to add or remove a favorite (yellow star). Favorites sit at the top.
- List order: **Favorites**, **Generic**, **Gaming**, **Saved**.
- Export / Delete stay on the right-click menu for saved presets.

**Imports and built-ins**
- AutoEQ, Squiglink, and OPRA stay **EQ-only** (measurement data, not a full mix). No extra save dialog on import.
- Built-ins (Flat, Bass Boost, Treble Boost, Vocal Boost, Rock, Electronic, Warm & Smooth, Speech Clarity, Pop / Balanced, FPS Footsteps, Competitive Shooter, Battle Royale, RPG Immersive, Racing Engine) are the **original 10-band curves**. They do not turn HRTF or Dynamics on, and they do not change chain order.

### Audio chain

You can reorder the live path: **EQ**, **Virtual surround (HRTF)**, **Dynamics**, **Loudness**. First in the list runs first. A stage that is turned off is skipped.

Why order matters:
- **HRTF then EQ** — game/multichannel audio is spatialized first, then you EQ the headphone stereo image. Typical for competitive: localize, then carve footsteps.
- **EQ then HRTF** — you tone-shape the mix, then spatialize it. Often nicer for music and cinematic games.
- **Dynamics / loudness later** — they ride the already-shaped signal. Swapping them with EQ changes punch vs how even the level feels.

Sample-rate conversion still happens after this list. You do not place it in the chain.

### HRTF / virtual surround

The surround block is rebuilt to take less vertical space and to apply immediately.

- Header: **Enable HRTF** and **Reset**.
- One row: **Preset** (Default / Wide / Close) and **Strength** (0–100%, tip while dragging).
- Speaker levels in a room layout with **You** in the center: front L/C/R, side L/R, rear L/R. LFE is shown but unused for headphones.
- **Wide** — more outside-the-head, stronger left/right (world audio, footsteps).
- **Close** — more in-head / cockpit.
- **Default** — in between.
- There is **no Apply button**. Enable, preset, strength, and speaker levels go to the selected app as you change them.
- Reset restores default speaker levels (and the usual HRTF defaults) for that app.

### Dynamics and loudness

Same processors as 1.2.1, easier to save per app and to slot in the chain.

- Toward **Open** — more contrast; quiet stays quiet; distance and explosion punch stay intact.
- Toward **Tight** — quiet detail comes up relative to peaks (footsteps vs gunfire).
- **Loudness** aims at a target (shown in LUFS). **0 is off**. Useful for playlists; leave it off for competitive if you still want level to mean “how far.”
- Dynamics **Reset** is sized with the other compact headers.

### Running apps

- **Enable EQ** and **Disable EQ** buttons are gone. Toggle with **double-click** or **right-click**. **Disable all** remains next to **Refresh**.
- Each row shows the **output device** under the app name, plus **EQ on** / **muted** when those apply.
- The list refreshes much more often (about every half second instead of every five seconds), so device and mute changes show up quickly.
- If an app has several audio sessions, CurvioEQ is less likely to pick a microphone or Steam Streaming Speakers row as “the” device.

### Keybinds

The old “type into a shortcut box” editor is gone.

- Click the field, press the combination, done. **Clear** wipes it.
- The bind is the **physical key**, not the letter on the cap. Switching Windows to Arabic (or any layout) does not steal the shortcut.
- Holding a key does not repeat Disable all / mute / color mute.
- Same actions as before: disable all EQ, mute the EQ output, mute by color label.

### Online headphone presets

Opening **Online presets** used to treat Refresh like a full redownload, so AutoEQ / Squiglink / OPRA indexes fetched again even when you already had them.

- **Refresh** — rebuild the list from files already on disk. No network. Browse immediately.
- **Redownload presets** — clear the cache and fetch indexes again when you want new measurements.
- Source filter (All / AutoEQ / Squiglink / OPRA), 500-at-a-time scroll, and import-as-Advanced-EQ behavior from 1.2.1 are unchanged.

### Updates and installer

- Menu **Update** checks GitHub Releases on launch. It reads **Up to date** when you are current.
- If a newer version exists: tray notification; one click downloads `CurvioEQ-Setup.exe` from the official GitHub release and runs it.
- After updating, a one-time **What’s new** dialog (from the changelog). **Changelog** next to Update opens it again.
- The installer can **close a running CurvioEQ** so files replace instead of failing with “cannot open .exe for writing”, then offers to launch.

### Optimizations

- Disabled HRTF, Dynamics, or Loudness are **skipped** in the chain — they do not extra-process the buffer.
- EQ, HRTF, Dynamics, and chain changes go to the **live** session. Capture/render does not restart.
- Running Apps list is about **half the old list code**, which is why the faster refresh is practical.
- The spectrum analyzer takes **one** before/after pass per chunk instead of two copies.
- Unused FFT code is **not compiled** into the app (it was never on the live headphone path).

### Bug fixes

- Online **Refresh** no longer redownloads the entire catalog.
- New Edit **Undo / Redo** shortcuts keep working while a gain/frequency box is focused, and on non-QWERTY layouts
- Global keybinds survive layout switches; they do not auto-repeat on key-repeat.
- A preset **without EQ** does not flatten bands or force Simple/Advanced.
- A preset’s HRTF / Dynamics / chain does **not** rewrite the program-wide defaults for every other app.
- Multi-session apps are less likely to show the wrong output device.
- Installing over a running copy can succeed because the installer asks Windows to close CurvioEQ.
- Leftover Enable/Disable EQ and Apply HRTF controls no longer fight double-click toggle and live HRTF.

### Removed dead code and leftovers

- Unused FFT implementation (`realfft`)
- Old surround-processor source file; 7.1 channel names stay
- Unused LFE low-pass leftover in the HRTF path (LFE is already silent for headphones)
- Enable EQ, Disable EQ, and Apply HRTF buttons from the main window
- Unused EQ helpers (advanced-mode flag setter, bulk filter copy)
- Unused spectrum “push before” / “push after” APIs (one combined push now)
- Unused sound-mod format-handler registration leftovers
- Trimmed Running Apps empty-state / timer duplication

Built-in preset names and 10-band values are the same as 1.2.1. Settings files from 1.2.1 still load; older presets without section flags stay EQ-only.

## [1.2.1] - 2026-08-26

Advanced EQ, online headphone presets, DSP stabilization, and a quicker per-app EQ toggle.

### Added

- **Advanced mode** — parametric EQ so you can shape the curve precisely (peaking / low shelf / high shelf, Frequency, Gain, Q, and a live frequency-response graph). Simple mode keeps the 10-band sliders.
- **Online presets…** — browse **AutoEQ**, **Squiglink**, and **OPRA** databases, with a source filter (All / AutoEQ / Squiglink / OPRA). Squiglink FR measurements import as flat-target Advanced EQ. The list loads **500** profiles at a time and appends more when you scroll.
- Double-click an app in **Running Apps** to toggle EQ on or off.

### Changed

- Advanced presets persist parametric filters (presets.json v2); Simple presets remain compatible. AutoEQ import prefers ParametricEQ.txt in Advanced mode.
- Advanced EQ filter picker is a compact dropdown; add/remove is on the graph right-click menu (**Add dot** / **Remove dot**). Hovering a handle shows Type / Fc / Gain / Q.
- Switching Advanced → Simple is blocked for loaded Advanced/AutoEQ presets (info dialog); Simple→Advanced peeks, and flat/zero EQ can still switch back. Advanced-saved presets are labeled **· Advanced** and open Advanced mode automatically.
- Preset **Export** / **Delete** moved to the presets list right-click context menu.
- Running Apps hint text mentions the double-click shortcut.
- Installer version bumped to `1.2.1` in Inno Setup script.
- Manifest assembly version bumped to `1.2.1.0`.
- GitHub publish script default/version usage updated to `1.2.1`.

### Fixed

- Bug fixes, DSP stabilization, and compatibility fixes across the EQ pipeline (lock-free coefficient swaps, gain ramps, Simple/Advanced session state).
- Switching Simple ↔ Advanced no longer rewrites EQ parameters via cascade↔parallel conversion; each mode keeps its own last values.
- Advanced EQ curve handles now sit on the composite frequency-response line (and drag follows that curve).
- Advanced EQ handles no longer jump / mark the curve as edited when the window is resized.

## [1.2.0] - 2026-08-19

Major routing-model release focused on reliability, determinism, and maintainability.
This version removes the unstable same-device hide/duck path and standardizes CurvioEQ
on virtual sink routing for all sessions.

### Breaking / Behavioral changes

- Removed the same-device replay + session-hide routing path.
- CurvioEQ now operates in a single routing model:
  - app audio is routed to a **routing sink** (VB-Cable / Voicemeeter / Steam Streaming Speakers)
  - process loopback is captured, EQ is applied, and output is rendered to the selected EQ device
- First-run setup and Settings now require and expose routing sink + EQ output configuration.
- Main-window routing mode toggle and related state text were removed to avoid mismatched runtime states.

### Audio architecture changes

- `EqAudioSession` was refocused to virtual-sink-only execution:
  - removed all same-device hide logic, duck verification branches, and fallback control flow
  - startup/teardown paths are shorter and easier to reason about
  - routing maintenance now handles only process-tree sink reroute validation
- Added integrated **dynamic range** and **loudness** stages to the live per-app audio chain:
  - optional per-session dynamic range compression
  - optional loudness rider stage to improve low-volume intelligibility
  - both stages run in the same real-time session pipeline as EQ/surround
- Session lifecycle behavior is more deterministic:
  - explicit early validation for required sink configuration
  - explicit fail messages when process-tree route does not stick
  - unchanged process-loopback capture and render thread model, with less branching in hot paths

### DSP / runtime optimizations

- Reduced memory traffic in per-chunk processing:
  - dynamics and loudness stages now process in-place on the active write buffer
  - removed redundant intermediate copy buffers previously used by those stages
- Capture-buffer layout in `EqAudioSession` simplified:
  - dropped now-unused per-chunk dynamic/loudness scratch vectors
  - preserved preallocation behavior for capture/resample/mix buffers
- Kept ring-buffer and clock-sync behavior stable while reducing per-chunk copy overhead.

### Stability and error handling

- Removed a class of startup failures caused by same-device hide readback validation.
- Removed watchdog/hide-loss branches tied to duck state transitions.
- Preserved existing protections around route verification, process exit detection, and renderer shutdown.

### UI / UX changes

- **Per-app controls**
  - enabling EQ for multiple apps no longer requires picking a different color each time
  - color remains optional for grouping/quick identification, not a blocker for multi-app EQ
- **System tray / context menu**
  - tray menu now exposes direct **Enable EQ** / **Disable EQ** actions for faster per-app control
  - reduced clicks to toggle EQ without reopening the main window
- **Settings dialog**
  - now consistently presents routing sink + EQ output controls
  - removed mode-selection controls and mode-dependent visibility logic
- **First-run setup**
  - now collects both routing sink and EQ output on initial configuration
  - updated onboarding copy to match virtual-sink-only behavior
- **Main window**
  - removed routing mode toggle row
  - removed routing status text that represented old mode state
  - added **Clear log** button for quick log reset during testing/debugging

### Dead code and leftover cleanup

- Removed obsolete settings and migration paths tied to runtime routing mode selection:
  - `audioRoutingMode` no longer read/written by app settings
  - settings now persist only active routing-sink/output behavior used by runtime
- Trimmed `AudioSessionVolume` to current usage:
  - removed unused duck/probe/device-scoped session volume APIs introduced for same-device hiding
  - removed unused `setMute(...)` wrapper
  - kept `toggleMute(...)` path used by app-level mute keybind behavior
- Removed orphaned local debug harness:
  - deleted `tools/test_loopback.cpp`

### Packaging / release metadata

- Installer version bumped to `1.2.0` in Inno Setup script.
- Manifest assembly version bumped to `1.2.0.0`.
- GitHub publish script default/version usage updated to `1.2.0`.

### Documentation updates

- `README.md` rewritten to reflect virtual-sink-only architecture and setup.
- Removed outdated references to legacy mode toggling and same-device hide behavior.
- Updated setup guidance to emphasize sink/output separation and routing expectations.
- Dynamic range + loudness processing is now explicitly documented in the runtime pipeline.

### Internal quality notes

- Net reduction in branching complexity across session startup and hot-path chunk processing.
- Smaller maintenance surface for future DSP and routing changes.
- Clearer one-path mental model for debugging app routing issues in production.

## [1.1.1] - 2026-08-17

Audio stability patch. No new features.

### Fixed

- Crackling, dropouts, and uneven playback with EQ on, including when every band is at 0 dB
- Capture and output falling out of step: leftover loopback packets could pile up, and the mixer could write more than one device period at a time
- Underruns repeating the last sample (buzz/crackle); they now play silence
- When the session buffer is too full, a whole chunk is skipped instead of splicing audio mid-write
- Stereo apps on a surround output no longer copy the front pair into rear/center/LFE
- Several apps on the same color label sounding louder or more compressed than a single app; mix level is scaled with session count and the limiter ramps instead of clamping instantly
- Stopping or disabling EQ while sliders move could race the mixer; session rings stay valid until the mixer drops them, and gain/surround updates stay locked with session lifetime

### Improved

- Per-app capture runs EQ, optional surround, resample, then mix-format in one chunk, then hands a lock-free ring to a shared mixer
- Startup DSP checks (EQ, resampler, ring buffer) are printed in the in-app log under the spectrum, not only in a debugger terminal
- Release builds still fail if those DSP checks fail
- Unused audio helpers and dead session fields removed

## [1.1.0] - 2026-08-15

### Added

- Global keybinds (Settings → Keybinds): disable all EQ, mute output device, mute by color label 1–8
- System tray per-app Enable/Disable EQ rows for each configured session
- Master **All** band slider; EQ range expanded to ±20 dB
- Presets panel 2×2 button grid
- Restore EQ routing when a target app exits and is re-enabled

### Improved

- Audio path: batched ring buffer I/O, shorter mixer lock, preallocated capture buffers, EQ coefficient double-buffering, cheaper soft clip, smaller ring buffer (2048 frames)
- Float loopback format fallback for broader device compatibility
- Running Apps list excludes CurvioEQ itself

### Fixed

- Re-enable EQ from tray or main window without losing color assignment when the process is still running

### Notes for v1.0.0 users

Existing settings are preserved. New keybind fields default to off/empty until configured under Settings → Keybinds.

## [1.0.0] - 2026-08-07

First public release.

- Windows 10+
- Per-app 10-band EQ with multi-app support, presets, spectrum analyzer, and system tray integration
- Requires a virtual audio device for the routing sink (VB-Cable, Voicemeeter, or Steam Streaming Speakers)
