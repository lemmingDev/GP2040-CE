// S3-only hardware/clocks.h shim (Task 5). Intentionally empty:
// clock_get_hz() is not referenced by any S3-parsed TU (AnimationStation
// headers include this file but never call into it). Exists only so those
// UNTOUCHED includes resolve.
#pragma once
