// extras/esp32-companion/tests/test_companion_pins.cpp
// Host validation of the Classic-ESP32 PIN_CAPS table: every entry must
// offer input or output, strapping/ADC/input-only verdicts must match the
// researched per-pin rules, and reserved pins must be absent.
#include "companion_pins.h"
#include "gplink.h"
#include <cassert>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static bool has(uint8_t gpio, uint8_t bit) {
    const CompanionPin *p = companionPinLookup(gpio);
    return p && (p->caps & bit);
}

int main() {
    size_t count = 0;
    const CompanionPin *pins = companionPins(&count);
    CHECK(pins != nullptr);
    CHECK(count == 24);

    // Every entry offers input or output; reserved bit never set.
    for (size_t i = 0; i < count; i++) {
        CHECK(pins[i].caps & (GPLINK_PINCAP_INPUT | GPLINK_PINCAP_OUTPUT));
        CHECK((pins[i].caps & 0x80) == 0);
        // Table sorted by GPIO number (stable discovery order).
        if (i > 0) CHECK(pins[i - 1].gpio < pins[i].gpio);
    }

    // Reserved / absent GPIOs must not be advertised.
    const uint8_t absent[] = {1, 3, 6, 7, 8, 9, 10, 11, 20, 24, 28, 29, 30, 31, 37, 38};
    for (size_t i = 0; i < sizeof(absent); i++) CHECK(companionPinLookup(absent[i]) == nullptr);

    // Input-only, no pull: 34-36, 39 (ADC1, WiFi-safe).
    const uint8_t inputOnly[] = {34, 35, 36, 39};
    for (size_t i = 0; i < sizeof(inputOnly); i++) {
        CHECK(has(inputOnly[i], GPLINK_PINCAP_INPUT));
        CHECK(has(inputOnly[i], GPLINK_PINCAP_ADC));
        CHECK(!has(inputOnly[i], GPLINK_PINCAP_OUTPUT));
        CHECK(!has(inputOnly[i], GPLINK_PINCAP_PULL));
    }

    // ADC1 full-IO: 32, 33.
    CHECK(has(32, GPLINK_PINCAP_OUTPUT | GPLINK_PINCAP_ADC));
    CHECK(has(33, GPLINK_PINCAP_OUTPUT | GPLINK_PINCAP_ADC));

    // ADC2 (WiFi-blocked, still capable): 0, 2, 4, 12-15, 25-27.
    const uint8_t adc2[] = {0, 2, 4, 12, 13, 14, 15, 25, 26, 27};
    for (size_t i = 0; i < sizeof(adc2); i++) CHECK(has(adc2[i], GPLINK_PINCAP_ADC));

    // Strapping: 0, 2, 5, 12, 15 — and only those.
    const uint8_t straps[] = {0, 2, 5, 12, 15};
    for (size_t i = 0; i < count; i++) {
        bool expected = false;
        for (size_t k = 0; k < sizeof(straps); k++) expected |= (pins[i].gpio == straps[k]);
        CHECK(((pins[i].caps & GPLINK_PINCAP_STRAPPING) != 0) == expected);
    }

    // Nothing on ESP32 is 5 V tolerant.
    for (size_t i = 0; i < count; i++) CHECK(!(pins[i].caps & GPLINK_PINCAP_FIVE_VOLT));

    if (failures == 0) printf("test_companion_pins PASS\n");
    return failures ? 1 : 0;
}
