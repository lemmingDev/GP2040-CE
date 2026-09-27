# GP-Link Analog Parity with Upstream "Analog Fixes"

Date: 2026-09-26. Context: upstream/main merged into `feature/GP-Link` as
1195703f (10 commits incl. `666def43` Analog fixes). This spec mirrors the
relevant upstream improvements into the GP-Link analog pipeline without
touching any upstream file — all changes land in `src/addons/gplink.cpp`,
`headers/addons/gplink.h`, `proto/config.proto` (additive fields only),
`src/config_utils.cpp`, `src/webconfig.cpp`, and `www/src/Addons/GPLinkAnalog.tsx`.

## 1. What upstream changed (source of truth, already in tree)

`src/addons/analog.cpp` + `headers/addons/analog.h` + `AnalogOptions` proto:

1. **Smoothing rework.** Strength scale 0–10 (was factor/1000, 0–100);
   `α = powf(10.0f, -0.5f * strength)` computed at setup; strength outside
   (0, 10] disables smoothing; formula rewritten as
   `prev + α(new − prev)` (algebraically identical to ours).
2. **EMA seeding.** `x_ema_initialized`/`y_ema_initialized` flags; first
   sample seeds history (which starts at CENTER 0.5, not 0.0). No more
   ramp-from-corner on enable.
3. **Calibration rework.** `readPin(pin, center, minimum, maximum)` with
   asymmetric spans, applied only when `min < center < max <= ADC_MAX`;
   16-sample `readCalibrationSample` helper; centers uint16→uint32; new
   proto `joystick_min/max_*` (defaults 0/4095).
4. **Deadzone guards.** Setup clamps `in` to [0, 0.99] and `out` to
   [in+0.01, 1.0]; snap condition `<` → `<=`; circularity cap scales with
   `error_rate`; `error_rate` clamped to (0, 1].
5. **Pin validation.** `isAdcPin()` forces non-ADC pins to −1.
6. **UI.** Sliders everywhere: smoothing 0–10, deadzones 0–99/100 with
   outer min bound to inner+1; defaults factor 2, inner 5, outer 95.

## 2. Parity table (ours today vs upstream now)

| Feature | Upstream now | GP-Link now | Gap |
|---|---|---|---|
| Smoothing formula | `prev + α(new−prev)` | `α·new + (1−α)·prev` | none (identical math) |
| Smoothing scale | strength 0–10, `α=10^(−s/2)` | `α=factor/1000`, 0–100 | **scale + range differ** |
| EMA init | seeded from first sample, history starts CENTER | history 0.0, always filtered | **ramp on enable** |
| Smoothing default | 2 | 5 | default only |
| Stick calibration | asymmetric min/center/max map | centers only | **stick min/max missing** (triggers already have min/max) |
| Deadzone guards | setup-clamped bands, UI outer-min=inner+1 | runtime `den>0` passthrough only | UI can express `den≤0` (silently linear) |
| Circularity | cap × error_rate | fixed radius | no error_rate concept — N/A by design (companion normalizes) |
| Pin validation | `isAdcPin` → −1 | pin −1 = unmapped convention | equivalent outcome, no change |
| Calibration sampling | 16-sample average | companion 8× oversample + EMA at source | equivalent-or-better at source; RP2040 side N/A |

## 3. Phase 1 — small, safe, no UI shape change

- **EMA seeding.** Add per-axis initialized flags (mirror upstream); first
  shaped sample seeds history; init history to MID (`0x7FFF`, the 0.5
  equivalent). Keeps formula and order (post-invert, pre-stick-dz).
- **Smoothing scale.** Switch to strength 0–10 with
  `α = powf(10.0f, -0.5f * strength)` evaluated per poll (we read options
  live rather than caching at setup — same math, note the difference);
  strength outside (0, 10] disables. UI slider 0–100 → 0–10, defaults
  5 → 2 (fresh configs only; saved values persist).
- **Snap `<=`.** One-character match with upstream (`<` → `<=`).
- **UI outer floor.** Outer slider min follows inner (`min = inner+1`,
  mirror official) instead of the fixed 50 — **decision required**, see §5.

## 4. Phase 2 — stick min/max calibration (mirrors `readPin`)

- **Proto.** Per-stick-axis min/max (4× uint32, LT/RT pattern). Field 32 is
  reserved (do-not-reuse note in file); next free is 40.
- **Firmware.** Asymmetric span map in normalized integer domain, guarded
  by `min < center < max` (mirror the upstream guard); degenerate window
  falls back to centers-only (same fallback philosophy as triggers).
- **UI.** Per-stick Set-min/Set-max capture buttons reusing
  `/api/getGPLinkAnalogValues` (same one-click pattern as trigger
  rest/full, same ±1% margin + cross-clamp). Defaults min 0 / max 65535
  (identity).
- **Validation.** min < center < max enforced in yup (mirror firmware guard).

## 5. Open decisions (need answers before building)

1. **Outer floor: fixed 50 vs dynamic `inner+1`?** Current tree uses the
   fixed 0–50 / 50–100 split per earlier direction. Official binds outer
   min to inner+1 dynamically. Dynamic matches upstream; fixed is simpler
   and already built. Pick one.
2. **Smoothing migration.** Existing saved factors >10 (e.g. 30–50) become
   *disabled* under the new scale (upstream behavior) instead of heavy.
   Accept silent-disable (upstream parity), or clamp into 0–10 on load?
   Recommend: accept upstream behavior + note in reply; factor 5 maps
   0.005 → 0.0032 (close, no surprise).
3. **Phase 2 scope.** Full per-axis min/max + capture UI, or triggers-only
   (status quo)? Recommend full — the trigger implementation is the
   template and the endpoint already serves what capture needs.

## 6. Explicitly out of scope

- `error_rate` gain (no source equivalent — companion sends normalized).
- Companion-side 16-sample calibration (source already 8× + EMA).
- Touching any upstream file (`analog.*`, `Analog.tsx`, official proto).

## 7. Verification per phase

- `cmake --build` clean (`-Wall -Werror`), web `npm run build` clean.
- Flash RP2040AdvancedBreakoutBoardUSBPassthrough; DAC sweep 25→32:
  Phase 1 — enabling smoothing shows no startup ramp; factor mapping
  sanity (10 ≈ frozen-ish, 1 ≈ light); deadzone edge cases (inner=outer,
  outer=100) stay linear.
- Phase 2 — capture rest/full extremes, confirm saturation + identity
  defaults; `min>=max` blocked inline.
