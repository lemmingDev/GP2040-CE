// extras/esp32-companion/src/companion_pins.h
// Classic-ESP32 companion GPIO table for PIN_CAPS discovery.
// Freestanding (stdint only) so host tests can validate it.
// Source: per-pin research (strapping verdicts, ADC tables); bit meanings
// are GPLINK_PINCAP_* in extras/gp-link/gplink.h (spec section 6c).
#pragma once
#include <stddef.h>
#include <stdint.h>

struct CompanionPin {
    uint8_t gpio; // ESP32 GPIO number (source-local numbering)
    uint8_t caps; // GPLINK_PINCAP_* bitmask
};

const CompanionPin *companionPins(size_t *countOut);
const CompanionPin *companionPinLookup(uint8_t gpio);
