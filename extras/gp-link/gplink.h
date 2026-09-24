// extras/gp-link/gplink.h
#pragma once
#include <stddef.h>
#include <stdint.h>

#define GPLINK_VERSION_MAJOR 1
#define GPLINK_VERSION_MINOR 0
#define GPLINK_VERSION_BYTE 0x10
#define GPLINK_MAX_PAYLOAD 240
#define GPLINK_ENCODED_MAX 256

size_t gplink_encode(uint8_t type, const uint8_t *payload, uint8_t payload_len, uint8_t *out);

struct gplink_decoder {
    uint8_t buf[GPLINK_ENCODED_MAX];
    uint16_t len;
};

struct gplink_frame {
    uint8_t type;
    uint8_t payload[GPLINK_MAX_PAYLOAD];
    uint8_t len;
};

void gplink_decoder_init(gplink_decoder *d);
bool gplink_feed(gplink_decoder *d, uint8_t byte, gplink_frame *out);
