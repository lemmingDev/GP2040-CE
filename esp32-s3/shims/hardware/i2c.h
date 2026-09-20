// S3-only hardware/i2c.h shim (Task 5). Intentionally empty: no header
// parsed on the S3 path (display.h, OneBitDisplay.h) uses any i2c_* symbol
// in declarations — the real backend talks to driver/i2c_master.h directly
// from lib/PicoPeripherals/peripheral_i2c_s3.cpp. This file exists only so
// those UNTOUCHED Pico includes resolve.
#pragma once
