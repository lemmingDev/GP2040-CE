# GP-Link companion hardware notes — boards, level shifting, RX protection

Companion-side hardware reference for GP-Link. The ESP32 is the
reference platform; other boards are porting targets with the verdicts
below. All 2 Mbaud figures below were verified on-bench unless marked.

## 1. Board matrix (companion duty)

| Board | ADC / analog out | UARTs | Logic | Verdict |
|---|---|---|---|---|
| ESP32 DevKit / generic ESP32 | 18-ch 12-bit (ADC1 32–39 + ADC2); DAC 25/26 | 3 (UART0 = console) | 3.3V, not 5V-tolerant | Good. ADC1 only with WiFi on (ADC2 goes blind — hardware, unfixable) |
| Mega 2560 | 16-ch 10-bit, no DAC (PWM only) | 4 (Serial0 = console) | 5V → divide Mega TX | Good. Level-shift + short RP2040→Mega wire (or buffer) |
| Pico / generic RP2040 | ADC0–2 + ADC3 = VSYS÷3 only (supply monitor, not a free input) | 2, console is USB-CDC so both free | 3.3V | Good. Only 3 external ADC pins — the "4th analog" is VSYS-only, don't promise it |
| Bluepill (STM32F103C8) | 10-ch 12-bit, no DAC | 3, USB separate | 3.3V, mostly 5V-tolerant FT pins | Good. Clone quality varies; check FT-vs-analog per pin; ST-Link flashing |
| Pico W | same as Pico | same | same | Usable with caveats. GPIO29/ADC3 shared with WiFi SPI (needs guard); ADC0–2 unaffected |
| Uno | 6-ch 10-bit, no DAC | 1, hard-shared with USB | 5V → divide | Poor. No free UART at 2Mbaud with USB attached (SoftwareSerial can't); usable only with USB detached |

## 2. Level shifting at 2 Mbaud (verified, with one correction)

RP2040 GPIO is not 5V tolerant (abs max 3.8V), so a 5V companion TX
into the RP2040 RX must be divided down.

- The earlier 470Ω/1kΩ pick (3.40V) is safe (1.4V of VIH margin,
  τ≈18ns ≈ 8% of the 500ns bit) but sits 0.2V under the recommended
  max, and USB-rail tolerance eats it (3.57V at 5.25V in).
- **Use 1kΩ/1.2kΩ (2.73V) instead**: comfortable margins in both
  directions, τ≈30ns ≈ 13%, still clean at 2 Mbaud.
- Keep the divider off GPIO26–29 (ADC-shared pins back-feed the rail;
  use digital-only 0–22 for the link).
- Reverse direction (3V3→5V): fine into STM32 FT pins (~1.2V margin);
  into AVR it is spec-marginal (3.0V worst-case threshold) but works in
  practice — add one 74AHCT125/TXU gate for guaranteed margin in noisy
  builds.
- Skip BSS138 boards (too slow for 2M) and TXB auto-shifters
  (oscillate on cable); the divider wins, TXU0204 if you want an IC.
- RP2040 pin capacitance is officially unspecified (flagged) — the
  bound above assumes worst-case 55pF and still passes.

## 3. RX protection rule of thumb

Anything above 3.3V nominal on an RP2040 RX pin gets a divider (5V
companions) or nothing (3.3V companions: ESP32, Pico/W, Blue Pill UART
pins connect direct). When in doubt, divide — a resistive divider
cannot damage anything; 5V direct eventually will.
