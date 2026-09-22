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

# 9. Partition fit: flash layout must fit 8 MB variants. Sum the Size
#    column of esp32-s3/partitions.csv (hex) and fail above 0x800000.
#    (Current: nvs 0x6000 + phy 0x1000 + factory 0x1E0000 + gpconfig
#    0x8000 + www 0x400000 = 0x5EF000.)
part_total=0
while IFS=, read -r part_name part_type part_subtype part_offset part_size part_flags; do
    case "$part_name" in \#*|"") continue ;; esac
    part_size="$(echo "$part_size" | tr -d '[:space:]')"
    case "$part_size" in 0x*|"") ;; *) continue ;; esac
    [ -n "$part_size" ] || continue
    part_total=$((part_total + part_size))
done < esp32-s3/partitions.csv
if [ "$part_total" -le $((0x800000)) ]; then
    pass "partitions fit 8 MB flash (total $part_total bytes)"
else
    failmsg "partitions exceed 8 MB flash (total $part_total bytes)"
fi

# 10. Transport default pinned: S2-hold boots USB-config by design.
if grep -q "DEFAULT_WEBCONFIG_TRANSPORT WEBCONFIG_TRANSPORT_USB" src/config_utils.cpp; then
    pass "webconfig transport default pinned to USB"
else
    failmsg "webconfig transport default pinned to USB"
fi

# 11. No credentials in code or logs: the documented default passphrase
#     lives ONLY in the DEFAULT_AP_PASSPHRASE define; fail if the literal
#     passphrase string appears in any other tracked first-party file,
#     and fail if webconfig_s3.cpp printf/ESP_LOG lines reference
#     apPassphrase (lengths-only logging rule).
cred_hits=""
for f in $CODE; do
    [ -f "$f" ] || continue
    [ "$f" = "src/config_utils.cpp" ] && continue
    if grep -q "gp2040config" "$f"; then
        cred_hits="$cred_hits $f"
    fi
done
if [ -z "$cred_hits" ] && grep -q 'define DEFAULT_AP_PASSPHRASE "gp2040config"' src/config_utils.cpp; then
    pass "default passphrase lives only in DEFAULT_AP_PASSPHRASE"
else
    failmsg "default passphrase lives only in DEFAULT_AP_PASSPHRASE:$cred_hits"
fi
if grep -E "printf|ESP_LOG" src/webconfig_s3.cpp | grep -q "apPassphrase"; then
    failmsg "no apPassphrase in webconfig_s3.cpp log lines"
else
    pass "no apPassphrase in webconfig_s3.cpp log lines"
fi

# 12. STA defaults pinned + no STA passphrase in code/logs: the STA client
#     joins a home network, so the safe default must be radio-off with empty
#     credentials (an Always-on default would join unknown networks on
#     first boot). The passphrase obeys the same lengths-only logging rule as
#     the AP check, and the default literal lives ONLY in its
#     DEFAULT_STA_PASSPHRASE define.
if grep -q "DEFAULT_STA_MODE STA_OFF" src/config_utils.cpp && \
   grep -q 'define DEFAULT_STA_SSID ""' src/config_utils.cpp && \
   grep -q 'define DEFAULT_STA_PASSPHRASE ""' src/config_utils.cpp; then
    pass "STA defaults pinned (off, empty SSID/passphrase)"
else
    failmsg "STA defaults pinned (off, empty SSID/passphrase)"
fi
if grep -E "printf|ESP_LOG" src/webconfig_s3.cpp | grep -q "staPassphrase"; then
    failmsg "no staPassphrase in webconfig_s3.cpp log lines"
else
    pass "no staPassphrase in webconfig_s3.cpp log lines"
fi
sta_cred_hits=""
for f in $CODE; do
    [ -f "$f" ] || continue
    [ "$f" = "src/config_utils.cpp" ] && continue
    if grep -q "DEFAULT_STA_PASSPHRASE" "$f"; then
        sta_cred_hits="$sta_cred_hits $f"
    fi
done
if [ -z "$sta_cred_hits" ]; then
    pass "DEFAULT_STA_PASSPHRASE lives only in config_utils.cpp"
else
    failmsg "DEFAULT_STA_PASSPHRASE lives only in config_utils.cpp:$sta_cred_hits"
fi

if [ "$fail" -ne 0 ]; then
    echo "s3-guards: FAILURES present (see docs/s3-port-constraints.md)"
    exit 1
fi
echo "s3-guards: all PASS"
