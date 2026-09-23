#pragma once

#include <cstdint>

// True iff both subnets are valid IPv4 /24 private-network addresses and differ.
// String-only (no IDF headers) so host tests compile this TU directly.
bool s3_validateSubnets(const char *apSubnet, const char *usbSubnet);

#if defined(ESP_PLATFORM)
#include "esp_netif.h"  // esp_ip4_addr_t + esp_netif_t for the declarations below
#else
// Host-test alias for IDF's esp_ip4_addr_t (esp_netif_ip_addr.h).
// Layout: .addr holds the dotted quad as the composed integer
// (a<<24|b<<16|c<<8|d), i.e. "192.168.5.0" -> 0xC0A80500. NOTE: this is NOT
// the on-device network-byte-order representation (esp_ip4addr_aton and
// ESP_IP4TOADDR both apply htonl, giving 0x0005A8C0 on little-endian) —
// convert with esp_netif_htonl() at the esp_netif boundary (see
// s3_usbnet_bringup and s3_configure_ap).
typedef struct {
    uint32_t addr;
} esp_ip4_addr_t;
#endif

// Parse a /24 private-network subnet string ("192.168.5.0" style) into .addr
// (composed integer, see above — NOT network byte order; callers convert
// with esp_netif_htonl() before storing into esp_netif structures). False on
// garbage/non-private/host bits set (same dotted-quad code as validation);
// `out` untouched on false. String-only (no IDF headers) so host tests
// compile this TU directly.
bool s3_subnetToIp(const char *subnet, esp_ip4_addr_t *out);

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

// USB netif bring-up (S3 USB-webconfig plan, Task 4): creates the RNDIS
// esp_netif with static IP `.1` of `usbSubnet`, starts the DHCP server, and
// attaches it via s3_usbnet_start(). Invalid stored subnets (or a pair that
// fails s3_validateSubnets) fall back to compiled defaults — the boot is
// never failed for config reasons; false is returned only on esp_netif
// errors. Idempotent: a second call while up returns true without touching
// the shared netif state. Called from webconfig bring-up (Task 5 wires the
// call); inert until then.
bool s3_usbnet_bringup(const char *apSubnet, const char *usbSubnet);

// Status for GET /api/getNetworkStatus (Task 4): true / "192.168.5.1"-style
// IP while the USB netif is up, false / "" when down.
bool s3_usbnet_is_up();
const char *s3_usbnet_ip();

#endif  // defined(ESP_PLATFORM)
