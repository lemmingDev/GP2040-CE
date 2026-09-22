#pragma once

// True iff both subnets are valid IPv4 /24 private-network addresses and differ.
// String-only (no IDF headers) so host tests compile this TU directly.
bool s3_validateSubnets(const char *apSubnet, const char *usbSubnet);

#if defined(ESP_PLATFORM)

// S3 RNDIS <-> esp_netif glue (S3 USB-webconfig plan, Task 3).
//
// Compiled into the S3 firmware but inert until a later task adds the USB
// descriptor + bring-up: no descriptor opens the RNDIS interface yet, so the
// TinyUSB callbacks below never fire and s3_usbnet_start() is never called.
// Deliberately NOT a reuse of lib/rndis/rndis.c: that TU depends on Pico-SDK
// headers (get_absolute_time()), drives raw lwIP (netif_add/ethernet_input)
// instead of esp_netif, and pumps its own tud_task() loop; only its shape
// (recv_cb -> netif input, link-output -> tud_network_xmit, init_cb reset)
// is mirrored here, mapped onto the esp_netif driver model.

#include <cstdint>

#include "esp_netif.h"

// Frame view passed as the `ref` argument of tud_network_xmit() by the
// future esp_netif transmit function; tud_network_xmit_cb() copies it into
// the USB transfer. The frame must stay alive until xmit_cb() runs.
struct s3_usbnet_frame_t {
    const uint8_t *data;
    uint16_t len;
};

// Override the default device MAC (02:02:84:6A:96:01, locally administered;
// last byte differs from Pico lib/rndis ...:00 so both boards on one PC
// never clash). A null pointer keeps the current MAC.
void s3_usbnet_init(const uint8_t mac[6]);

// Attach/detach the esp_netif the bring-up task (later task) creates for USB.
// Called from webconfig bring-up; until start() runs, recv_cb() refuses
// frames and init_cb() is a no-op.
void s3_usbnet_start(esp_netif_t *netif);
void s3_usbnet_stop();

#endif  // defined(ESP_PLATFORM)
