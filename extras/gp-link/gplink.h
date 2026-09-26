// extras/gp-link/gplink.h
#pragma once
#include <stddef.h>
#include <stdint.h>

#define GPLINK_VERSION_MAJOR 1
#define GPLINK_VERSION_MINOR 0
#define GPLINK_VERSION_BYTE 0x10
#define GPLINK_MAX_PAYLOAD 240
#define GPLINK_ENCODED_MAX 256

// v1 message types
#define GPLINK_TYPE_HELLO 0x01
#define GPLINK_TYPE_INPUT_STATE 0x02
#define GPLINK_TYPE_HEARTBEAT 0x03
#define GPLINK_TYPE_RUMBLE_SET 0x04
#define GPLINK_TYPE_PLAYER_LED_SET 0x05
#define GPLINK_TYPE_BATTERY_REPORT 0x06
#define GPLINK_TYPE_GPIO_CONFIG 0x07
#define GPLINK_TYPE_GPIO_READ 0x08
#define GPLINK_TYPE_GPIO_WRITE 0x09
#define GPLINK_TYPE_DEBUG_TEXT 0x0A
#define GPLINK_TYPE_FEATURE_REQ 0x0B
#define GPLINK_TYPE_FEATURE_ACK 0x0C
#define GPLINK_TYPE_PIN_CAPS_REQ 0x0D
#define GPLINK_TYPE_PIN_CAPS_RSP 0x0E
#define GPLINK_TYPE_HTTP_REQ 0x10
#define GPLINK_TYPE_HTTP_RESP 0x11
#define GPLINK_TYPE_HTTP_FRAG 0x12
#define GPLINK_TYPE_GPIO_NAK 0x13
#define GPLINK_TYPE_ANALOG_READ 0x14
#define GPLINK_TYPE_ANALOG_CONFIG 0x15
#define GPLINK_TYPE_TEST_CONFIGURE 0x16
#define GPLINK_TYPE_TEST_RESULT 0x17

// Test-engine functions (TEST_CONFIGURE) and statuses (TEST_RESULT).
// Companion-local #defines defer to these via #ifndef guards.
#define GPLINK_TEST_FN_HOLD_LOW 0x00
#define GPLINK_TEST_FN_HOLD_HIGH 0x01
#define GPLINK_TEST_FN_TOGGLE 0x02
#define GPLINK_TEST_FN_SWEEP_UP 0x03
#define GPLINK_TEST_FN_SWEEP_DOWN 0x04
#define GPLINK_TEST_FN_TRIANGLE 0x05
#define GPLINK_TEST_FN_DRIVE_LOW 0x0A
#define GPLINK_TEST_FN_DRIVE_HIGH 0x0B
#define GPLINK_TEST_FN_DRIVE_CYCLE 0x0C
#define GPLINK_TEST_FN_DRIVE_PWM 0x0D
#define GPLINK_TEST_FN_STOP 0xFF
#define GPLINK_TEST_STATUS_RUNNING 0
#define GPLINK_TEST_STATUS_DONE 1
#define GPLINK_TEST_STATUS_ABORTED 2
#define GPLINK_TEST_STATUS_BAD_PIN 3
#define GPLINK_TEST_STATUS_UNSUPPORTED 4
#define GPLINK_TEST_STATUS_BUSY 5
// FEATURE_REQ/ACK identity feature id + version bound.
#define GPLINK_FEATURE_IDENTITY 0x01
#define GPLINK_TEST_VER_MAX 24

// HELLO device capability bits (spec section 4)
#define GPLINK_CAP_RUMBLE (1u << 0)
#define GPLINK_CAP_LEDS (1u << 1)
#define GPLINK_CAP_BATTERY (1u << 2)
#define GPLINK_CAP_IMU (1u << 3)
#define GPLINK_CAP_MULTI_PAD (1u << 4)
#define GPLINK_CAP_DISPLAY (1u << 5)
#define GPLINK_CAP_HTTP_TUNNEL (1u << 6)
#define GPLINK_CAP_COMPANION_GPIO (1u << 7)

// PIN_CAPS_RSP per-pin capability bits (spec section 6c)
#define GPLINK_PINCAP_INPUT (1u << 0)
#define GPLINK_PINCAP_OUTPUT (1u << 1)
#define GPLINK_PINCAP_PULL (1u << 2)
#define GPLINK_PINCAP_ADC (1u << 3)
#define GPLINK_PINCAP_PWM (1u << 4)
#define GPLINK_PINCAP_STRAPPING (1u << 5)
#define GPLINK_PINCAP_FIVE_VOLT (1u << 6)
#define GPLINK_PINCAP_ADC_SAFE (1u << 7)

// GPIO_CONFIG pull values and flags byte (v1.1; unpackers accept 4-byte
// legacy payloads with flags defaulting to 0)
#define GPLINK_GPIO_PULL_NONE 0
#define GPLINK_GPIO_PULL_UP 1
#define GPLINK_GPIO_PULL_DOWN 2
#define GPLINK_GPIO_FLAG_INVERTED (1u << 0)

size_t gplink_encode(uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t *out);
size_t gplink_encode_seq(uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t seq, uint8_t *out);

struct gplink_decoder {
    uint8_t buf[GPLINK_ENCODED_MAX];
    uint16_t len;
};

struct gplink_frame {
    uint8_t type;
    uint8_t seq;
    uint8_t payload[GPLINK_MAX_PAYLOAD];
    uint8_t len;
};

void gplink_decoder_init(gplink_decoder *d);
bool gplink_feed(gplink_decoder *d, uint8_t byte, gplink_frame *out);

// v1 message payload pack/unpack (all multi-byte fields little-endian;
// each unpacker returns false unless type and exact length match).
size_t gplink_pack_input_state(uint8_t devid, uint32_t buttons, uint8_t dpad, uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry, uint8_t lt, uint8_t rt, uint8_t aux, uint8_t *payload_out);
size_t gplink_pack_hello(uint8_t major, uint8_t minor, uint32_t caps, uint8_t devcount, uint8_t *payload_out);
size_t gplink_pack_gpio_config(uint8_t devid, uint8_t pin, uint8_t dir, uint8_t pull, uint8_t flags, uint8_t *payload_out);
size_t gplink_pack_gpio_mask(uint8_t devid, uint64_t mask, uint8_t *payload_out);
size_t gplink_pack_gpio_nak(uint8_t devid, uint8_t pin, uint8_t code, uint8_t *payload_out);
size_t gplink_pack_rumble(uint8_t devid, uint8_t weak, uint8_t strong, uint16_t duration_ms, uint8_t *payload_out);
size_t gplink_pack_player_led(uint8_t devid, uint8_t mask, uint8_t *payload_out);
size_t gplink_pack_battery(uint8_t devid, uint8_t pct, uint8_t charging, uint8_t *payload_out);
bool gplink_unpack_input_state(const gplink_frame *f, uint8_t *devid, uint32_t *buttons, uint8_t *dpad, uint16_t *lx, uint16_t *ly, uint16_t *rx, uint16_t *ry, uint8_t *lt, uint8_t *rt, uint8_t *aux);
bool gplink_unpack_hello(const gplink_frame *f, uint8_t *major, uint8_t *minor, uint32_t *caps, uint8_t *devcount);
bool gplink_unpack_gpio_config(const gplink_frame *f, uint8_t *devid, uint8_t *pin, uint8_t *dir, uint8_t *pull, uint8_t *flags);
// Analog channel config/report (spec section 6d). Values are normalized
// full-range u16 (companion scales its native ADC width itself).
size_t gplink_pack_analog_config(uint8_t devid, uint8_t pin, uint8_t enable, uint8_t *payload_out);
bool gplink_unpack_analog_config(const gplink_frame *f, uint8_t *devid, uint8_t *pin, uint8_t *enable);
size_t gplink_pack_analog_read(uint8_t devid, uint8_t count, const uint8_t *pins, const uint16_t *values, uint8_t *payload_out);
bool gplink_unpack_analog_read(const gplink_frame *f, uint8_t *devid, uint8_t *count, uint8_t *pins_out, uint16_t *values_out, uint8_t maxCount);
bool gplink_unpack_gpio_mask(const gplink_frame *f, uint8_t *devid, uint64_t *mask);
bool gplink_unpack_gpio_nak(const gplink_frame *f, uint8_t *devid, uint8_t *pin, uint8_t *code);
bool gplink_unpack_rumble(const gplink_frame *f, uint8_t *devid, uint8_t *weak, uint8_t *strong, uint16_t *duration_ms);
bool gplink_unpack_player_led(const gplink_frame *f, uint8_t *devid, uint8_t *mask);
bool gplink_unpack_battery(const gplink_frame *f, uint8_t *devid, uint8_t *pct, uint8_t *charging);

// Pin-capability discovery for board-generic companions.
size_t gplink_pack_pin_caps_req(uint8_t *payload_out);
size_t gplink_pack_pin_caps_rsp(const char *name, uint8_t name_len, uint8_t count, const uint8_t *pins, const uint8_t *caps, uint8_t *payload_out);
bool gplink_unpack_pin_caps_rsp(const gplink_frame *f, char *name_out, uint8_t *name_len, uint8_t *count, uint8_t *pins_out, uint8_t *caps_out);
// Companion test engine (session-only pin exerciser; see companion README).
// Layouts: CONFIGURE [testId pin fn p1LE p2LE] (7 B), RESULT [testId status
// valueLE countLE] (6 B), FEATURE_REQ [feature] (1 B), FEATURE_ACK identity
// [feature verLen ver radio] (3+verLen B).
size_t gplink_pack_test_configure(uint8_t testId, uint8_t pin, uint8_t function, uint16_t param1, uint16_t param2, uint8_t *payload_out);
bool gplink_unpack_test_configure(const gplink_frame *f, uint8_t *testId, uint8_t *pin, uint8_t *function, uint16_t *param1, uint16_t *param2);
size_t gplink_pack_test_result(uint8_t testId, uint8_t status, uint16_t value, uint16_t count, uint8_t *payload_out);
bool gplink_unpack_test_result(const gplink_frame *f, uint8_t *testId, uint8_t *status, uint16_t *value, uint16_t *count);
size_t gplink_pack_feature_req(uint8_t feature, uint8_t *payload_out);
size_t gplink_pack_feature_ack_identity(const char *version, uint8_t verLen, uint8_t radio, uint8_t *payload_out);
bool gplink_unpack_feature_ack_identity(const gplink_frame *f, uint8_t *feature, char *ver_out, uint8_t *ver_len, uint8_t verMax, uint8_t *radio);
// ver_out must hold verMax+1 bytes (NUL terminator); versions longer than
// verMax (or GPLINK_TEST_VER_MAX) are rejected.
