// extras/esp32-companion/src/companion_pins.cpp
#include "companion_pins.h"
#include "gplink.h"

namespace {
// Output-only, PWM + strapping: devkit GPIO2 (blue LED clamps it near 0 V,
// measured; unusable as input, ideal as LED/player-light output).
const uint8_t LED_OUT = GPLINK_PINCAP_OUTPUT | GPLINK_PINCAP_PWM |
                        GPLINK_PINCAP_STRAPPING;
const CompanionPin kPinsDevkit[] = {
    {0, (uint8_t)(IO | ADC | STRAP)},
    {2, LED_OUT},
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
const CompanionPin kPinsR32[] = {
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
#ifdef COMPANION_BOARD_R32
const CompanionPin *kActive = kPinsR32;
const size_t kActiveCount = sizeof(kPinsR32) / sizeof(kPinsR32[0]);
#else
const CompanionPin *kActive = kPinsDevkit;
const size_t kActiveCount = sizeof(kPinsDevkit) / sizeof(kPinsDevkit[0]);
#endif
} // namespace

const CompanionPin *companionPins(size_t *countOut) {
    if (countOut) *countOut = kActiveCount;
    return kActive;
}

const CompanionPin *companionPinLookup(uint8_t gpio) {
    for (size_t i = 0; i < kActiveCount; i++) {
        if (kActive[i].gpio == gpio) return &kActive[i];
    }
    return nullptr;
}
