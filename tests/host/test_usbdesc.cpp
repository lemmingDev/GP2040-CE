// Host byte test for Task 6 (S3 USB-webconfig plan): HID-family configuration
// descriptors with the RNDIS function appended.
//
// Per driver the test asserts:
//  1. the plain descriptor is untouched (1 interface, base wTotalLength),
//  2. the _with_net descriptor adds exactly TUD_RNDIS_DESC_LEN bytes, bumps
//     bNumInterfaces by 2 and wTotalLength by the same amount,
//  3. the appended bytes open with the RNDIS IAD class triple,
//  4. the plain prefix is byte-identical (toggle-Off output unchanged).
//
// NOTE on bNumInterfaces: the task brief/plan text says "==2"; the correct
// USB value is 3 (1 HID + 2 RNDIS). TUD_RNDIS_DESCRIPTOR emits two interfaces
// (comm + data; cf. NetDriver ITF_NUM_TOTAL == 2), so the asserts below check
// plain + 2. A value of 2 would describe a malformed configuration.
//
// Include order matters: HIDDescriptors.h must come first. Its LSB/MSB +
// GAMEPAD_* defines are unguarded while PS3/PS4 guard LSB/MSB with #ifndef
// (and spell them with extra parens, which would trip -Werror on redefine),
// and repeat GAMEPAD_* with identical values (benign).

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "tusb.h"

// HID first (see note above).
#include "drivers/hid/HIDDescriptors.h"
#include "drivers/switch/SwitchDescriptors.h"
#include "drivers/ps3/PS3Descriptors.h"
#include "drivers/ps4/PS4Descriptors.h"
#include "drivers/keyboard/KeyboardDescriptors.h"

static uint16_t cfgTotalLength(const uint8_t *cfg) {
    return (uint16_t)((uint16_t)cfg[2] | ((uint16_t)cfg[3] << 8));
}

static void checkRndisIad(const uint8_t *withNet, size_t baseSize) {
    const uint8_t *iad = withNet + baseSize;
    assert(iad[0] == 8);  // bLength: Interface Association Descriptor
    assert(iad[1] == TUSB_DESC_INTERFACE_ASSOCIATION);
    assert(iad[2] == 1);  // first free interface after the HID function
    assert(iad[3] == 2);  // RNDIS = comm + data interfaces
    assert(iad[4] == TUD_RNDIS_ITF_CLASS);
    assert(iad[5] == TUD_RNDIS_ITF_SUBCLASS);
    assert(iad[6] == TUD_RNDIS_ITF_PROTOCOL);
}

static void checkAppend(const uint8_t *plain, size_t plainSize,
                        const uint8_t *withNet, size_t withNetSize,
                        uint16_t baseTotal, const char *name) {
    // Plain descriptor untouched: single interface, base wTotalLength, and
    // the array length matches wTotalLength.
    assert(plain[4] == 1);
    assert(cfgTotalLength(plain) == baseTotal);
    assert(plainSize == baseTotal);
    // Appended shape: exactly one RNDIS block, two more interfaces.
    assert(withNetSize == plainSize + TUD_RNDIS_DESC_LEN);
    assert(withNet[4] == plain[4] + 2);
    assert(cfgTotalLength(withNet) == (uint16_t)(baseTotal + TUD_RNDIS_DESC_LEN));
    // Toggle-Off prefix byte-identical: everything except the two fields the
    // append legitimately changes (wTotalLength bytes 2-3, bNumInterfaces
    // byte 4) matches the plain descriptor.
    assert(std::memcmp(plain, withNet, 2) == 0);
    assert(std::memcmp(plain + 5, withNet + 5, plainSize - 5) == 0);
    checkRndisIad(withNet, plainSize);
    std::printf("%s: plain=%u with_net=%u ifaces %u->%u OK\n", name,
                (unsigned int)plainSize, (unsigned int)withNetSize,
                (unsigned int)plain[4], (unsigned int)withNet[4]);
}

int main() {
    checkAppend(hid_configuration_descriptor, sizeof(hid_configuration_descriptor),
                hid_configuration_descriptor_with_net,
                sizeof(hid_configuration_descriptor_with_net),
                CONFIG1_DESC_SIZE, "HID");
    checkAppend(switch_configuration_descriptor, sizeof(switch_configuration_descriptor),
                switch_configuration_descriptor_with_net,
                sizeof(switch_configuration_descriptor_with_net),
                0x29, "Switch");
    checkAppend(ps3_configuration_descriptor, sizeof(ps3_configuration_descriptor),
                ps3_configuration_descriptor_with_net,
                sizeof(ps3_configuration_descriptor_with_net),
                0x29, "PS3");
    checkAppend(ps3_alt_configuration_descriptor, sizeof(ps3_alt_configuration_descriptor),
                ps3_alt_configuration_descriptor_with_net,
                sizeof(ps3_alt_configuration_descriptor_with_net),
                0x29, "PS3-alt");
    checkAppend(ps4_configuration_descriptor, sizeof(ps4_configuration_descriptor),
                ps4_configuration_descriptor_with_net,
                sizeof(ps4_configuration_descriptor_with_net),
                PS4_CONFIG1_DESC_SIZE, "PS4");
    checkAppend(keyboard_configuration_descriptor, sizeof(keyboard_configuration_descriptor),
                keyboard_configuration_descriptor_with_net,
                sizeof(keyboard_configuration_descriptor_with_net),
                CONFIG_TOTAL_LEN, "Keyboard");
    std::printf("usbdesc: all HID-family RNDIS appends OK\n");
    return 0;
}
