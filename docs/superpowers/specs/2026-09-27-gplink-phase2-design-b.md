# GP-Link Phase 2 Pad Merge — Design B (Core Hook) — REJECTED

Date: 2026-09-27. Status: **rejected — do not build.** Kept as a record of
the considered-and-dismissed option.
Parent: same as Design A. Reason: `preprocess()` merging (Design A as
revised 2026-09-27) delivers SOCD/invert/4-way/dpad-mode with zero base
touch — the sole justification for this hook evaporated on verification
(`gamepad_usb_host.cpp:19-21`, `keyboard_host.cpp:19-21`). An opt-in
variant is likewise moot: there is no hook to opt into; gating (if ever
wanted) belongs on a merge-toggle bool, not on core machinery.

## 1. Goal

Give merged companion inputs the full local-button treatment (SOCD
cleaner, invert, 4-way filter, dpad-mode→stick conversion) by merging
**before** `Gamepad::process()` instead of after it. Everything Design A
does for buttons/sticks/triggers/aux/flags/link-loss applies unchanged;
only the insertion point (and therefore dpad semantics) differs.

## 2. Non-goals (state plainly — B does NOT buy these)

- Local turbo/macro on companion inputs: `TurboInput`/`InputMacro` run in
  `ProcessAddons`, after `read()`+`process()` regardless. Unchanged from A.
- Multi-pad, new UI/proto, output-mirror changes: same as A (none).

## 3. The hook (only base change)

- **Insert at end of `Gamepad::read()`** (`src/gamepad.cpp:320-418`),
  after `state.dpadOriginal = state.dpad` (`:376`) and the analog-stick
  defaults (`:386-417`): call the addon's merge (e.g.
  `GPLinkAddon::mergeIntoGamepad(*this)` via `GPLink_GetAddon()` with
  null + availability guards), ~5–15 lines.
- **Declare** in `headers/gamepad.h` (~10–20 lines): the merge entry +
  any companion-contribution storage (or reuse the addon's own members —
  preferred, keeps core side stateless).
- Total base diff: **~20–40 lines** in upstream-owned `gamepad.cpp` /
  `gamepad.h`. This is a core input-pipeline change and will get the
  hardest review — the proposal must justify it by the SOCD requirement
  alone.

## 4. Semantics deltas vs Design A (everything else identical)

- Companion dpad **gets** SOCD cleaning (`runSOCDCleaner`,
  `gamepad.cpp:298`), invert, 4-way filter, and dpad-mode→stick
  conversion (`:301-317`) — indistinguishable from local buttons.
- Buttons join before hotkey masking (`gamepad.h:100-104`) identically
  to local buttons (same as A in effect).
- Sticks still **override** (u16 can't OR); still gated on link-alive
  with MID parking; triggers assign + `hasAnalogTriggers`; aux masked
  to Function; flags set by producer. Same code as A, new call site.
- `dpadOriginal`: OR it in the same hook call (after `:376`) to preserve
  hotkey-physical parity (`pressedHotkey`, `gamepad.h:92-95`).

## 5. Explicitly rejected variant

**Hook at end of `Gamepad::process()`**: semantically ~identical to A
(post-SOCD) while still touching core — all cost, no benefit. Do not do
this; if the hook isn't at end-of-`read()`, do Design A instead.

## 6. Verification (extends Design A §5)

- All Design A tests, plus: hold Up+Down on the companion with each SOCD
  mode (Up/Down/Left/Right priority, neutral) and confirm cleaned output
  identical to local buttons; LS/RS dpad modes convert companion dpad to
  sticks; invert switch flips companion dpad.
- `cmake --build` clean; confirm no behavior change with link down
  (hook must no-op when addon absent/disabled/unstarted).

## 7. PR posture

Lead with the SOCD requirement as the sole justification for touching
core; keep the diff squeaky (no drive-by refactors); reference the five
addon-injector precedents to show what *didn't* need core changes, so
reviewers see B as a deliberate exception rather than sprawl.
