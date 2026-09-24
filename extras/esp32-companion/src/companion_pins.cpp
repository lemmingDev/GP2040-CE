// extras/esp32-companion/src/companion_pins.cpp
#include "companion_pins.h"
#include "gplink.h"

namespace {
const uint8_t IO = GPLINK_PINCAP_INPUT | GPLINK_PINCAP_OUTPUT |
                   GPLINK_PINCAP_PULL | GPLINK_PINCAP_PWM;
const uint8_t ADC = GPLINK_PINCAP_ADC;
const uint8_t STRAP = GPLINK_PINCAP_STRAPPING;

// Classic ESP32 (WROOM-32): GPIOs 0-19, 21-23, 25-27, 32-39 exist.
// Absent: 1, 3 (console), 6-11 (flash), 20, 24, 28-31, 37-38.
// Input-only, no pull: 34-36, 39. ADC2 (0,2,4,12-15,25-27) still
// advertises ADC (WiFi-block caveat is operational, not capability).
const CompanionPin kPins[] = {
    {0, (uint8_t)(IO | ADC | STRAP)},
    {2, (uint8_t)(IO | ADC | STRAP)},
    {4, (uint8_t)(IO | ADC)},
    {5, (uint8_t)(IO | STRAP)},
    {12, (uint8_t)(IO | ADC | STRAP)},
    {13, (uint8_t)(IO | ADC)},
    {14, (uint8_t)(IO | ADC)},
    {15, (uint8_t)(IO | ADC | STRAP)},
    {16, IO},
    {17, IO},
    {18, IO},
    {19, IO},
    {21, IO},
    {22, IO},
    {23, IO},
    {25, (uint8_t)(IO | ADC)},
    {26, (uint8_t)(IO | ADC)},
    {27, (uint8_t)(IO | ADC)},
    {32, (uint8_t)(IO | ADC)},
    {33, (uint8_t)(IO | ADC)},
    {34, (uint8_t)(GPLINK_PINCAP_INPUT | ADC)},
    {35, (uint8_t)(GPLINK_PINCAP_INPUT | ADC)},
    {36, (uint8_t)(GPLINK_PINCAP_INPUT | ADC)},
    {39, (uint8_t)(GPLINK_PINCAP_INPUT | ADC)},
};
} // namespace

const CompanionPin *companionPins(size_t *countOut) {
    if (countOut) *countOut = sizeof(kPins) / sizeof(kPins[0]);
    return kPins;
}

const CompanionPin *companionPinLookup(uint8_t gpio) {
    for (size_t i = 0; i < sizeof(kPins) / sizeof(kPins[0]); i++) {
        if (kPins[i].gpio == gpio) return &kPins[i];
    }
    return nullptr;
}
