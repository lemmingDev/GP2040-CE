#!/bin/bash
# Regression guard for ESP32-S3 port invariants proven on hardware.
# Each check below exists because violating it broke real hardware behavior
# (see docs/s3-port-constraints.md). Runs in CI without any toolchain.
# Only TRACKED first-party files are scanned (git ls-files): vendored code
# under lib/ and esp32-s3/components/, and gitignored build outputs, are out.
# Exit nonzero with FAIL lines on violation, else prints PASS lines.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
fail=0
pass() { echo "PASS: $1"; }
failmsg() { echo "FAIL: $1"; fail=1; }

# Tracked first-party sources (excludes lib/*, esp32-s3/components/*).
CODE="$(git ls-files 'src/*.cpp' 'src/*.h' 'headers/*.h' 'hal_esp32s3/*' 'esp32-s3/*.txt' 'esp32-s3/*.csv' 'esp32-s3/*.defaults' 'esp32-s3/*.md' 'esp32-s3/main/*' 'esp32-s3/shims/*' 'configs/*' 2>/dev/null)"

# 1. Non-blocking USB pump: bare tud_task() blocks indefinitely on FreeRTOS
#    when no USB events pend, wedging polled input in quiet modes (Switch).
#    The S3 gamepad loop must pump with tud_task_ext(0, false) + yield.
if grep -q "tud_task_ext(0, false)" src/gp2040.cpp; then
    pass "S3 pump uses tud_task_ext(0, false)"
else
    failmsg "S3 pump uses tud_task_ext(0, false)"
fi
# Every remaining bare tud_task() call must live under a PICO_BOARD guard
# (bare pump is correct on Pico, which has no RTOS to block on).
bad_tud=""
for f in $CODE; do
    case "$f" in *.cpp|*.h) ;;
        *) continue ;;
    esac
    [ -f "$f" ] || continue
    n=0
    while IFS= read -r line; do
        n=$((n + 1))
        stripped="$(echo "$line" | sed 's,//.*,,' )"
        case "$stripped" in
            *tud_task_ext*) continue ;;
            *tud_task*\(\)*)
                ctx="$(sed -n "$((n > 6 ? n - 6 : 1)),${n}p" "$f")"
                case "$ctx" in
                    *PICO_BOARD*|*"#else"*) ;;
                    *) bad_tud="$bad_tud $f:$n" ;;
                esac
                ;;
        esac
    done < "$f"
done
if [ -z "$bad_tud" ]; then
    pass "no unguarded bare tud_task() calls"
else
    failmsg "unguarded bare tud_task() calls:$bad_tud"
fi

# 2. OTG PHY init: raw TinyUSB does not enable clocks/PHY/pins on S3;
#    without usb_new_phy the device silently never enumerates.
if grep -q "usb_new_phy" src/gp2040.cpp; then
    pass "S3 USB OTG PHY init present"
else
    failmsg "S3 USB OTG PHY init present"
fi

# 3. Custom partition table: the gpconfig partition (persistence) only
#    exists if the build uses esp32-s3/partitions.csv, not the IDF default.
if grep -q "^CONFIG_PARTITION_TABLE_CUSTOM=y" esp32-s3/sdkconfig.defaults; then
    pass "custom partition table selected"
else
    failmsg "custom partition table selected"
fi

# 4. Pin validity window: S3 routable GPIOs are 0-48 except USB 19/20.
#    The RP2040-era 30-pin window silently disables high-pin peripherals.
if grep -q "pin <= 48" headers/helper.h; then
    pass "S3 isValidPin covers 0-48 minus USB"
else
    failmsg "S3 isValidPin covers 0-48 minus USB"
fi

# 5. RTOS tick trap: pdMS_TO_TICKS(1) is ZERO ticks at the default 100 Hz
#    tick rate (a mere yield that still starves IDLE). One-tick waits must
#    spell vTaskDelay(1). Comments stripped before matching.
bad_tick=""
for f in $CODE; do
    case "$f" in *.cpp|*.h) ;;
        *) continue ;;
    esac
    [ -f "$f" ] || continue
    stripped_all="$(sed 's,//.*,,' "$f")"
    case "$stripped_all" in
        *pdMS_TO_TICKS\(1\)*)
            bad_tick="$bad_tick $f" ;;
    esac
done
if [ -z "$bad_tick" ]; then
    pass "no pdMS_TO_TICKS(1) one-tick waits in code"
else
    failmsg "pdMS_TO_TICKS(1) one-tick waits in code:$bad_tick"
fi

# 6. FlashPROM commit: esp_timer_stop on a never-started timer aborts boot
#    (ESP_ERR_INVALID_STATE); the stop must stay is_active-guarded.
if grep -q "esp_timer_is_active" lib/FlashPROM/src/FlashPROM_esp32.cpp; then
    pass "FlashPROM timer-stop guard present"
else
    failmsg "FlashPROM timer-stop guard present"
fi

# 7. PS3 report length: the HID descriptor declares 49 input bytes for
#    Report ID 1 but sizeof(PS3Report) is 51; sending 51 wedges the S3 IN
#    endpoint (no completions) and the host drops the excess. Interrupt
#    sends must use PS3_INPUT_REPORT_LEN.
if grep -q "define PS3_INPUT_REPORT_LEN" headers/drivers/ps3/PS3Descriptors.h && \
   grep -q "tud_hid_report(0, report, PS3_INPUT_REPORT_LEN)" src/drivers/ps3/PS3Driver.cpp && \
   ! grep -q "tud_hid_report(0, report, report_size)" src/drivers/ps3/PS3Driver.cpp; then
    pass "PS3 interrupt send uses descriptor length (49)"
else
    failmsg "PS3 interrupt send uses descriptor length (49)"
fi

# 8. 1000 Hz loop: the gamepad loop yields one RTOS tick per iteration, so
#    the tick rate IS the report rate. Anything below 1000 wastes the 1 ms
#    USB poll interval with up to 10 ms of input latency.
if grep -q "^CONFIG_FREERTOS_HZ=1000" esp32-s3/sdkconfig.defaults; then
    pass "FreeRTOS tick pinned to 1000 Hz"
else
    failmsg "FreeRTOS tick pinned to 1000 Hz"
fi

if [ "$fail" -ne 0 ]; then
    echo "s3-guards: FAILURES present (see docs/s3-port-constraints.md)"
    exit 1
fi
echo "s3-guards: all PASS"
