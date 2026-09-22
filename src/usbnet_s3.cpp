#include "usbnet_s3.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

bool parsePrivateSubnet24(const char *s) {
    if (s == nullptr) {
        return false;
    }
    int octets[4] = {0, 0, 0, 0};
    int end = 0;
    if (std::sscanf(s, "%d.%d.%d.%d%n",
                    &octets[0], &octets[1], &octets[2], &octets[3], &end) != 4) {
        return false;
    }
    if (s[end] != '\0') {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (octets[i] < 0 || octets[i] > 255) {
            return false;
        }
    }
    if (octets[3] != 0) {
        return false;  // host bits must be zero (/24 network address)
    }
    if (octets[0] == 10) {
        return true;
    }
    if (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) {
        return true;
    }
    if (octets[0] == 192 && octets[1] == 168) {
        return true;
    }
    return false;
}

}  // namespace

bool s3_validateSubnets(const char *apSubnet, const char *usbSubnet) {
    if (apSubnet == nullptr || usbSubnet == nullptr) {
        return false;
    }
    if (!parsePrivateSubnet24(apSubnet) || !parsePrivateSubnet24(usbSubnet)) {
        return false;
    }
    return std::strcmp(apSubnet, usbSubnet) != 0;
}

#if defined(ESP_PLATFORM)

// --- S3 RNDIS <-> esp_netif glue (S3 USB-webconfig plan, Task 3) ---
//
// Inert until a later task adds the USB descriptor + bring-up (see header).
// TinyUSB declaration source: lib/tinyusb/src/class/net/net_device.h:63-88.

#include "tusb.h"
#include "esp_netif.h"

// Locally-administered device MAC; ...:01 (Pico lib/rndis uses ...:00).
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x01};

namespace {

esp_netif_t *s_netif = nullptr;

}  // namespace

void s3_usbnet_init(const uint8_t mac[6]) {
    if (mac != nullptr) {
        std::memcpy(tud_network_mac_address, mac, sizeof(tud_network_mac_address));
    }
}

void s3_usbnet_start(esp_netif_t *netif) {
    s_netif = netif;
}

void s3_usbnet_stop() {
    s_netif = nullptr;
}

extern "C" {

// RX path (lib/rndis linkoutput shape, esp_netif direction): deliver the USB
// frame straight into the TCP/IP stack. esp_netif_receive() with eb ==
// nullptr copies the frame, so passing TinyUSB's buffer directly is safe.
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (s_netif == nullptr || src == nullptr || size == 0) {
        return false;
    }
    return esp_netif_receive(s_netif, const_cast<uint8_t *>(src), size, nullptr) == ESP_OK;
}

// TX path: copy the bring-up task's frame into the USB transfer. The future
// esp_netif transmit function calls tud_network_xmit(ref, 0) with ref
// pointing at an s3_usbnet_frame_t once tud_network_can_xmit(len) is true.
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    (void)arg;
    const s3_usbnet_frame_t *frame = static_cast<const s3_usbnet_frame_t *>(ref);
    if (dst == nullptr || frame == nullptr || frame->data == nullptr || frame->len == 0) {
        return 0;
    }
    std::memcpy(dst, frame->data, frame->len);
    return frame->len;
}

// Re-init (bus reset / re-enumeration): this glue holds no queued frames
// (recv_cb delivers synchronously), so there is nothing stale to drop.
// Re-announce the link once bring-up has attached a netif (esp_netif_up +
// DHCP/static GOT_IP handling; base/event args unused by the handler).
void tud_network_init_cb(void) {
    if (s_netif != nullptr) {
        esp_netif_action_connected(s_netif, nullptr, 0, nullptr);
    }
}

}  // extern "C"

#endif  // defined(ESP_PLATFORM)
