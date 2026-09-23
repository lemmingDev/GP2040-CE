#pragma once

#include <stdint.h>
#include "tusb.h"
#include "drivers/xboxog/xid/xid_driver.h"

#define XboxOriginalReport USB_XboxGamepad_InReport_t
#define XboxOriginalReportOut USB_XboxGamepad_OutReport_t

static const uint8_t xboxoriginal_string_language[]     = { 0x09, 0x04 };
static const uint8_t xboxoriginal_string_manufacturer[] = "";
static const uint8_t xboxoriginal_string_product[]      = "";
static const uint8_t xboxoriginal_string_version[]      = "1.0";

static const uint8_t *xboxoriginal_string_descriptors[] __attribute__((unused)) =
{
	xboxoriginal_string_language,
	xboxoriginal_string_manufacturer,
	xboxoriginal_string_product,
	xboxoriginal_string_version
};

static const uint8_t *xboxoriginal_device_descriptor __attribute__((unused)) = (const uint8_t*)&XID_DESC_DEVICE;

static const uint8_t *xboxoriginal_configuration_descriptor __attribute__((unused)) = (const uint8_t*)&XID_DESC_CONFIGURATION;

// S3 USB-webconfig (Task 7): XID function byte-identical to
// XID_DESC_CONFIGURATION, followed by the RNDIS function
// (TUD_RNDIS_DESCRIPTOR argument order: itf, str, ep_notif, notif_size,
// epout, epin, epsize). The XID macros are re-emitted under the same #if
// guards as xid_driver.h so the prefix stays byte-identical under any flag
// combo; only the config header (wTotalLength bytes 2-3, bNumInterfaces byte
// 4) differs. EP audit is against the current flags (Duke only: OUT 0x01 /
// IN 0x81): the first-free rule gives notif intr 0x83, bulk OUT 0x04, bulk
// IN 0x85 — re-audit if XID_STEELBATTALION/XID_XREMOTE/MSC_XMU are ever
// enabled. RNDIS takes interfaces XID_ITF_NUM_TOTAL..+1, hence
// bNumInterfaces +2 and wTotalLength + TUD_RNDIS_DESC_LEN (66). Drivers
// return this array iff s3_usb_network_active() holds; toggle-Off keeps the
// plain pointer.
#define XID_CONFIG_TOTAL_LEN_WITH_NET (XID_CONFIG_TOTAL_LEN + TUD_RNDIS_DESC_LEN)
static const uint8_t xboxoriginal_configuration_descriptor_with_net[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, XID_ITF_NUM_TOTAL + 2, 0, XID_CONFIG_TOTAL_LEN_WITH_NET, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

#if (XID_DUKE >= 1)
    TUD_XID_DUKE_DESCRIPTOR(ITF_NUM_XID_DUKE, ITF_NUM_XID_DUKE + 1, 0x80 | (ITF_NUM_XID_DUKE + 1)),
#endif

#if (XID_STEELBATTALION >= 1)
    TUD_XID_SB_DESCRIPTOR(ITF_NUM_XID_STEELBATTALION, ITF_NUM_XID_STEELBATTALION + 1, 0x80 | (ITF_NUM_XID_STEELBATTALION + 1)),
#endif

#if (XID_XREMOTE >= 1)
    TUD_XID_XREMOTE_DESCRIPTOR(ITF_NUM_XID_XREMOTE, 0x80 | (ITF_NUM_XID_XREMOTE + 1)),
#endif

#if (CFG_TUD_MSC >= 1)
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, ITF_NUM_MSC + 1, 0x80 | (ITF_NUM_MSC + 1), 64),
#endif

  // RNDIS function (IAD + comm + data interfaces)
  TUD_RNDIS_DESCRIPTOR(XID_ITF_NUM_TOTAL, 0, 0x83, 8, 0x04, 0x85, 64)
};