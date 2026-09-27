# GP-Link Phase 2 Pad Merge — Design A (Preprocess Merge)

Date: 2026-09-27. Status: proposed (competes with Design B).
Parent: `docs/superpowers/specs/2026-09-24-gp-link-design.md` §6 (`INPUT_STATE`),
§7 failsafe. Touch policy: **zero upstream-owned files** — all changes in
`src/addons/gplink.cpp` + `headers/addons/gplink.h` (fork-owned).

Correction (2026-09-27): earlier drafts placed the merge in `process()`
(post-SOCD). Verified against `src/addons/gamepad_usb_host.cpp:19-21` and
`src/addons/keyboard_host.cpp:19-21`: USB pads and keyboard merge in
`preprocess()`, i.e. *before* `gamepad->process()`, so they receive full
SOCD/invert/4-way/dpad-mode treatment plus downstream turbo (which reads
merged state in `ProcessAddons`). Macro *detection* runs before us in load
order (`InputMacro` loads at `gp2040.cpp:128`, GPLink at `:129`), so
companion inputs can't trigger macros, though they coexist with macro
output bits. The merge below follows that precedent — Design B's core
hook is therefore unnecessary unless a requirement surfaces that
preprocess can't meet (none known).

## 1. Goal

Consume companion `INPUT_STATE` frames as real gamepad inputs merged into
`GamepadState`, using only the addon injection pattern every in-tree addon
already uses (PCF8575, keyboard-host, rotary, Wii, USB-host pads, and our
own GPIO path). No core, proto, or webconfig changes for MVP.

## 2. Non-goals

- SOCD cleaning / invert / dpad-mode conversion on merged dpad (post-`process()`
  by construction — same as all addon injectors; see Design B if required).
- Local turbo/macro on companion inputs (runs before us; unchanged).
- Multi-pad (`devid` 1–3): filter `devid == 0` now, same as TX and GPIO arms.
- Any new webconfig UI or proto fields (reuse `GPLinkOptions.enabled`).

## 3. Wire changes: none

Codec already packs/unpacks 17 B `INPUT_STATE` with tests
(`extras/gp-link/gplink.cpp:117-130,210-224`,
`extras/gp-link/tests/test_gplink_messages.cpp:29-52`). TX path
(`GPLinkAddon::sendInputState`, `src/addons/gplink.cpp:487-494`) unchanged.

## 4. Firmware changes (`src/addons/gplink.cpp`, `headers/addons/gplink.h`)

1. **RX case arm** in `pumpRx()` switch (`:510-566`), mirroring the
   `GPIO_READ` arm (`:511-522`): unpack via `gplink_unpack_input_state`,
   accept only `devid == 0`, store to new `lastInputState` member,
   `handledFrames++`, else `ignoredFrames++`.
2. **Members** (`gplink.h`, private): stored companion state struct
   (buttons/dpad/lx/ly/rx/ry/lt/rt/aux), previous-frame buttons mask for
   release handling, all zero-init.
3. **`applyInputState()`** called from a new `GPLinkAddon::preprocess()`
   (not `process()`), so merged inputs flow through `gamepad->process()`
   like USB/keyboard pads. Move the `pumpRx()` drain there too (frames feed
   stores; `process()` keeps consuming stores for GPIO/analog/outputs, so
   nothing else moves). `preprocess()` doesn't run in webconfig mode —
   correct, pad merge is a gamepad-mode feature (`pollConfigMode` coverage
   unchanged).
   Per-field rules (same as before, new call site):
   - `buttons`: symmetric set/clear — `state.buttons =
     (state.buttons & ~lastButtons) | newButtons`, then save mask.
     (Bare OR sticks bits on after link loss; the mask ternary mirrors
     `applyGpioMask` at `:414-446`.)
   - `dpad`: same OR-with-release; also OR `dpadOriginal` (keyboard-host
     precedent, `keyboard_host_listener.cpp:84-85`) so companion dpad can
     participate in hotkeys — call out that companion FN-combos can fire
     local hotkeys (profile load, save, reboot), accepted per precedent.
   - sticks: **override** (`=`, never OR — u16 axes can't OR). Gate on
     link-alive (below); park at MID on timeout (§7 failsafe).
   - triggers: override + set `hasAnalogTriggers = true`
     (`wiiext.cpp:394` precedent). Order vs our analog path (which also
     sets it at `:333`): INPUT_STATE wins, stated in code comment.
   - `aux`: OR `AUX_MASK_FUNCTION`-masked bit only.
   - flags: set `hasAnalogTriggers` on nonzero triggers;
     `hasLeft/RightAnalogStick = true` when merging sticks
     (`DualshockPS4Host.cpp:182-184` precedent).
4. **Link-loss neutral.** Gate stick/trigger/button contributions on
   `gplink_link_alive()` (2 s timeout, `gplink_link.h:16-18`); on
   timeout clear contributed button bits (via saved mask) and park
   sticks MID, triggers 0 (spec §7).
5. **Sticky re-apply.** `gamepad->read()` rebuilds state every poll, so
   re-apply every `process()` like `applyGpioMask`/`applyAnalogAxes`
   (`:599-608` comment applies verbatim).

Estimated diff: ~60–100 lines, all fork-owned. Base files touched: none
(`gp2040.cpp:129` registration already exists).

## 5. Verification

- Host unit test: INPUT_STATE RX arm (valid devid-0 applies, wrong devid
  ignored, truncated frame ignored, release clears bits, timeout parks
  neutral) — extend `extras/gp-link/tests/` or add addon-level test.
- Hardware: companion `a` synth (or Bluepad32 stub) driving buttons +
  sticks; `joy.cpl` shows merge; pull the link (unplug UART) → neutral
  within 2 s; hotkey participation confirmed/denied on purpose.
- Build: `cmake --build` clean, web build clean (no UI change).

## 6. Known limitations (disclose in PR, don't solve here)

- Load order runs GPLink preprocess last, so on ties (USB pad + companion
  driving the same stick) the companion override wins. Document, don't fight.
- Companion FN-combos can fire local hotkeys (profile load, save, reboot):
  accepted per keyboard precedent — call it out, offer the merge toggle as
  the opt-out if testers want it.
- Single pad only (`devid 0`); multi-pad is future spec §4 as written.
- No separate opt-in toggle for MVP: merge rides the existing
  `GPLinkOptions.enabled` switch + link-alive gating + MID-park failsafe
  (nothing sends INPUT_STATE today, so blast radius is zero until M2).
  Design B's "opt-in hook" question is moot — there is no hook to opt into.
