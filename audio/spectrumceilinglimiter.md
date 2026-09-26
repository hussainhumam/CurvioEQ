# SpectrumCeilingLimiter (session output ceiling)

Per-session peak ceiling **after** the user DSP chain (EQ / HRTF / dynamics / loudness / VST). Not the mix-bus `MixLimiter`.

UI: [../ui/spectrum.md](../ui/spectrum.md).

## Files

| File | Role |
|------|------|
| `spectrumceilinglimiter.cpp` | STFT-ish hop limiter, 48 bands |
| `eqaudiosession.cpp` | `m_outputLimiter`, `setOutputLimiterThreshold` |
| `audioengine.cpp` | `setOutputLimiterThreshold` → every session |

## Behavior

- `setThreshold(linearPeak)` — `1.0` / above `kBypassThreshold` (0.999) skips extra limiting
- UI `0.0 dB` → linear 1.0 via `MixLimiter::dbToLinear` (shared math only)
- `process(interleaved, frames, channels)` on the session thread
- Adds hop OLA latency when engaged (`kHop` = 512); `EqAudioSession::logMeasuredLatency` accounts for it

Constants: `kFftSize` 2048, `kBandCount` 48.

## Call chain

```
SpectrumPlotArea drag
  → SpectrumWidget::limiterCeilingChanged(db)
  → AudioEngine::setOutputLimiterThreshold(MixLimiter::dbToLinear(db))
  → EqAudioSession::setOutputLimiterThreshold
  → SpectrumCeilingLimiter::setThreshold
```

Persist: `AppSettings.spectrumLimiterDb` ([../ui/settings.md](../ui/settings.md)).

## vs MixLimiter

| | Session ceiling | Mix bus |
|--|-----------------|--------|
| Class | `SpectrumCeilingLimiter` | `MixLimiter` |
| Where | Each `EqAudioSession` | Engine mix |
| UI | Spectrum dotted line | Not this badge |

## How to extend

- Algorithm changes stay in `spectrumceilinglimiter.cpp`; keep `setThreshold` linear and thread-safe (`std::atomic`).
- Bypass must remain “threshold ≈ 1” so 0 dB UI does not add hop latency.

## Do not

- Treat mix-bus `MixLimiter` as this control
- Call `setThreshold` with dB; convert first
