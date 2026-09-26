# Spectrum UI (output ceiling)

Right-hand analyzer (`SpectrumWidget` / `SpectrumPlotArea`). The **dotted line + badge** is the per-session **output ceiling**, not the mix-bus limiter.

DSP: [../audio/spectrumceilinglimiter.md](../audio/spectrumceilinglimiter.md).

## Files

| File | Role |
|------|------|
| `spectrumwidget.cpp` | Plot, badge, drag, `limiterCeilingChanged` |
| `spectrumanalyzer.cpp` | FFT bars for the display |

## Ceiling control

- Range: `0.0` … `-48` dB (`AppConstants::kSpectrumLimiterMaxDb` / `kSpectrumLimiterMinDb`)
- `0.0 dB` ≈ bypass (no extra cut)
- Drag the dotted line **inside the plot** or the badge
- Persist: `spectrumLimiterDb` in [settings.md](settings.md) on `limiterCeilingEditFinished`

Live apply (MainWindow):

`limiterCeilingChanged` → `AudioEngine::setOutputLimiterThreshold(MixLimiter::dbToLinear(db))`

(`MixLimiter::dbToLinear` is only the dB→linear helper; the session DSP class is `SpectrumCeilingLimiter`.)

## Badge layout

`layoutHandle()` / `handleRect()`:

- Center Y on `dbToY(m_ceilingDb)`
- Clamp to the **widget** plus `kHandleWidgetOverflow`, not the inner plot rect, so 0 dB / −48 dB stay on the line
- Plot margins (`kPlotTopMargin` / `kPlotBottomMargin`) give hang room past the graph

`handleCalloutPath()`: one outline — triangle tip at `ceilingY` on the left; only the **right** corners are rounded.

Hit-test is the whole badge. `QLabel` is transparent text on top. Scale tick `0` is skipped if it would overlap the badge.

## How to extend

- Styling: `handleCalloutPath` + paint in `SpectrumPlotArea::paintEvent`
- Grab feel: `kSpectrumLimiterGrabPx`, `isNearCeiling` / `isNearHandle`

## Do not

- Put the number inside the triangle or point the triangle at the label
- Clamp the badge fully inside `plotRect()` (0 dB line is on the plot top)
- Wire this control to `MixLimiter` on the mix bus
