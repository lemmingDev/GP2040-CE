#include "usbnet_s3.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// Parse "A.B.C.D" into octets; false on garbage, out-of-range octets, or
// trailing characters. Shared by validation and s3_subnetToIp so both agree.
bool parseQuad(const char *s, int octets[4]) {
    if (s == nullptr) {
        return false;
    }
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
    return true;
}

// True iff octets are a /24 private-network address (host bits zero).
bool isPrivate24(const int octets[4]) {
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

bool parsePrivateSubnet24(const char *s) {
    int octets[4] = {0, 0, 0, 0};
    return parseQuad(s, octets) && isPrivate24(octets);
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

bool s3_subnetToIp(const char *subnet, esp_ip4_addr_t *out) {
    if (subnet == nullptr || out == nullptr) {
        return false;
    }
    int octets[4] = {0, 0, 0, 0};
    if (!parseQuad(subnet, octets) || !isPrivate24(octets)) {
        return false;
    }
    out->addr = (uint32_t)(((uint32_t)octets[0] << 24) | ((uint32_t)octets[1] << 16) |
                           ((uint32_t)octets[2] << 8) | (uint32_t)octets[3]);
    return true;
}

#if defined(ESP_PLATFORM)

// --- S3 RNDIS <-> esp_netif glue (S3 USB-webconfig plan, Task 3) ---
//
// Inert until a later task adds the USB descriptor + bring-up (see header).
// TinyUSB declaration source: lib/tinyusb/src/class/net/net_device.h:63-88.

#include "tusb.h"
#include "esp_netif.h"
#include "esp_log.h"

#ifndef DEFAULT_USB_SUBNET
#define DEFAULT_USB_SUBNET "192.168.5.0"
#endif

static const char *S3_USBNET_TAG = "usbnet_s3";

// Destination capacity for tud_network_xmit_cb: the ECM/RNDIS driver copies
// into `transmitted[]` (CFG_TUD_NET_PACKET_PREFIX_LEN + CFG_TUD_NET_MTU +
// CFG_TUD_NET_PACKET_PREFIX_LEN) past the packet prefix, so CFG_TUD_NET_MTU
// is the conservative per-frame cap in both ECM and RNDIS modes.
#ifndef CFG_TUD_NET_MTU
#define CFG_TUD_NET_MTU 1514
#endif

// Locally-administered device MAC; ...:01 (Pico lib/rndis uses ...:00).
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x01};

namespace {

esp_netif_t *s_netif = nullptr;

// Task 4 bring-up state (same TU, separate block so Task 3 lines above stay
// untouched): the dotted-quad status string, one single-frame TX staging
// buffer, and an opaque handle for the esp_netif driver slot.
char s_usb_ip_str[16] = "";
uint8_t s_tx_buf[CFG_TUD_NET_MTU];
s3_usbnet_frame_t s_tx_frame = {s_tx_buf, 0};
bool s_tx_busy = false;
uint8_t s_driver_tag = 0;

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

// esp_netif transmit side (the "future transmit function" Task 3's
// s3_usbnet_frame_t anticipates): stage the stack frame in the single TX
// buffer and hand it to TinyUSB. Drops (returns ESP_FAIL, the stack frees
// the frame) when the link is down, TinyUSB cannot accept a frame, or a
// previous frame is still in flight — a later task may add a queue; one
// in-flight frame is enough until the descriptor exposes RNDIS.
esp_err_t s3_usbnet_transmit(void *h, void *buffer, size_t len) {
    (void)h;
    if (buffer == nullptr || len == 0 || len > CFG_TUD_NET_MTU) {
        return ESP_FAIL;
    }
    if (s_netif == nullptr || s_tx_busy || !tud_network_can_xmit((uint16_t)len)) {
        return ESP_FAIL;
    }
    std::memcpy(s_tx_buf, buffer, len);
    s_tx_frame.len = (uint16_t)len;
    s_tx_busy = true;
    tud_network_xmit(&s_tx_frame, 0);
    return ESP_OK;
}

bool s3_usbnet_bringup(const char *apSubnet, const char *usbSubnet) {
    // Lifecycle guard (Task 3 review pointer): creation runs once at boot
    // while TinyUSB callbacks only read s_netif, so a second call returns
    // without touching shared state instead of creating a duplicate netif.
    if (s_netif != nullptr) {
        return true;
    }
    const char *usb = usbSubnet;
    if (!s3_validateSubnets(apSubnet, usbSubnet)) {
        ESP_LOGW(S3_USBNET_TAG, "invalid stored subnets, falling back to defaults");
        usb = DEFAULT_USB_SUBNET;
    }
    esp_ip4_addr_t net{};
    if (!s3_subnetToIp(usb, &net)) {
        ESP_LOGW(S3_USBNET_TAG, "subnet parse failed, falling back to defaults");
        s3_subnetToIp(DEFAULT_USB_SUBNET, &net);  // cannot fail: valid /24 private
    }
    const uint32_t ipComposed = (net.addr & 0xFFFFFF00u) | 0x01u;  // .1 of the /24
    // esp_netif stores network byte order (raw-copied into lwIP, same as
    // IDF's ESP_IP4TOADDR defaults): s3_subnetToIp returns the composed
    // (a<<24|b<<16|c<<8|d) form, so convert at the boundary — the S3 is
    // little-endian. (Without this the netif would come up as 1.5.168.192
    // instead of 192.168.5.1; the status string below prints from the
    // pre-swap composition so it stays truthful.)
    esp_netif_ip_info_t ipInfo{};
    ipInfo.ip.addr = esp_netif_htonl(ipComposed);
    ipInfo.gw.addr = esp_netif_htonl(ipComposed);
    ipInfo.netmask.addr = esp_netif_htonl(0xFFFFFF00u);  // /24
    // AP-netif sequence mirrored with the PUBLIC esp_netif API (start/up are
    // private; drivers drive them via the action_* handlers): create, set
    // static IP, action_start (AUTOUP brings the lwIP netif up; the
    // DHCP_SERVER flag starts the DHCP server inside), then verify link +
    // DHCP state via the public getters. Ethernet stack config: RNDIS
    // carries Ethernet frames.
    esp_netif_inherent_config_t base{};
    base.flags = (esp_netif_flags_t)(ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP);
    std::memcpy(base.mac, tud_network_mac_address, sizeof(base.mac));
    base.ip_info = nullptr;  // set explicitly below, mirroring the AP path
    base.if_key = "USB_DEF";
    base.if_desc = "usb";
    base.route_prio = 10;  // same as the AP netif; STA (100) keeps the default route
    esp_netif_driver_ifconfig_t driver{};
    driver.handle = &s_driver_tag;
    driver.transmit = &s3_usbnet_transmit;
    esp_netif_config_t cfg{};
    cfg.base = &base;
    cfg.driver = &driver;
    cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
    esp_netif_t *netif = esp_netif_new(&cfg);
    if (netif == nullptr) {
        ESP_LOGE(S3_USBNET_TAG, "esp_netif_new failed");
        return false;
    }
    if (esp_netif_set_ip_info(netif, &ipInfo) != ESP_OK) {
        ESP_LOGE(S3_USBNET_TAG, "esp_netif_set_ip_info failed");
        esp_netif_destroy(netif);
        return false;
    }
    esp_netif_action_start(netif, nullptr, 0, nullptr);
    esp_netif_dhcp_status_t dhcpStatus = ESP_NETIF_DHCP_INIT;
    if (!esp_netif_is_netif_up(netif) ||
        esp_netif_dhcps_get_status(netif, &dhcpStatus) != ESP_OK ||
        (dhcpStatus != ESP_NETIF_DHCP_STARTED && esp_netif_dhcps_start(netif) != ESP_OK)) {
        ESP_LOGE(S3_USBNET_TAG, "USB netif start/DHCP failed");
        esp_netif_action_stop(netif, nullptr, 0, nullptr);
        esp_netif_destroy(netif);
        return false;
    }
    s3_usbnet_start(netif);
    std::snprintf(s_usb_ip_str, sizeof(s_usb_ip_str), "%u.%u.%u.%u",
                  (unsigned int)((ipComposed >> 24) & 0xFF), (unsigned int)((ipComposed >> 16) & 0xFF),
                  (unsigned int)((ipComposed >> 8) & 0xFF), (unsigned int)(ipComposed & 0xFF));
    ESP_LOGI(S3_USBNET_TAG, "USB netif up (%s)", s_usb_ip_str);
    return true;
}

bool s3_usbnet_is_up() {
    return s_netif != nullptr;
}

const char *s3_usbnet_ip() {
    return s_usb_ip_str;
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
    if (frame->len > CFG_TUD_NET_MTU) {
        return 0;
    }
    std::memcpy(dst, frame->data, frame->len);
    if (frame == &s_tx_frame) {
        s_tx_busy = false;  // staged frame consumed; transmit may stage the next
    }
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
