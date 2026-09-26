# App routing and capture

CurvioEQ does not insert into the app. It **moves the app’s playback** to a virtual sink, **captures that process**, EQs it, and plays on a second device.

```
App ──route──► routing sink (often muted)
    ──process loopback──► EqAudioSession DSP ──► EQ output device
```

## Files

| File | Role |
|------|------|
| `audiopolicyrouter.cpp` | Per-process render device (AudioPolicyConfig) |
| `processtreeutil.cpp` | Children of the clicked PID |
| `processloopbackcapture.cpp` | ActivateAudioInterface / process loopback |
| `sinkmutemanager.cpp` | Mute routing sink while EQ runs |
| `eqaudiosession.cpp` | `routeProcessTreeToDevice` then capture |

Settings for the two devices: [../ui/settings.md](../ui/settings.md). Sessions: [../ui/eqsessions.md](../ui/eqsessions.md).

## `AudioPolicyRouter`

| Function | Use |
|----------|-----|
| `isRoutingSupported()` | Hide routing UI if the OS API is missing |
| `listRenderDevices()` / `listRenderDevicesExcluding` | Settings + per-app “Output device” menu |
| `routeProcessToDevice(pid, deviceId)` | One process |
| `routeProcessTreeToDevice(rootPid, deviceId)` | **What EQ start uses** (Discord-style multi-process) |
| `clearProcessTreeRouting` | On session stop |
| `persistedRenderDeviceId(pid)` | Per-app override from the list menu |
| `clearAllPersistedRouting` | `CurvioEQ.exe --clear-all-routing` |

Routing sink and EQ output **must differ**. `EqSessionManager::resolveDevices` errors and can open Settings.

## Capture

`ProcessLoopbackCapture` records the process (and tree, as configured) after it is playing on the sink. `AudioEngine::startSession` builds `EqAudioSession` with sink id + output id.

## How to extend

- Per-app output (not the global EQ device): already on the session-list “Output device” menu via this router.
- New OS routing APIs: keep the same function names so `eqaudiosession.cpp` stays the only call site for “EQ is on”.

## Do not

- Route the app to the **EQ output** (you would hear dry + wet or lose loopback isolation).
- Enable EQ without a configured sink/output pair.
- Assume one PID is the whole app; use the process-tree helpers for hosts like Discord.
