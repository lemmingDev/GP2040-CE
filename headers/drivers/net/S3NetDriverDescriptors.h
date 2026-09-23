/*
 * SPDX-License-Identifier: MIT
 * S3 CONFIG-mode standalone RNDIS descriptors (USB-webconfig pivot).
 * Plain data like every other Descriptors.h: host-testable, no guards.
 * Mirrors Pico NetDriver's proven bytes (MISC class, EP triple).
 */

#pragma once

#include <stdint.h>

// String descriptor indexes (Pico NetDriver order, kept identical).
enum
{
  S3_STRID_LANGID = 0,
  S3_STRID_MANUFACTURER,
  S3_STRID_PRODUCT,
  S3_STRID_SERIAL,
  S3_STRID_INTERFACE,
  S3_STRID_MAC
};

enum
{
  S3_ITF_NUM_CDC = 0,
  S3_ITF_NUM_CDC_DATA,
  S3_ITF_NUM_TOTAL
};

// Single configuration (RNDIS only).
#define S3_RNDIS_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN)

// Pico NetDriver EP triple (proven on Windows).
#define S3_EPNUM_NET_NOTIF 0x81
#define S3_EPNUM_NET_OUT   0x02
#define S3_EPNUM_NET_IN    0x82

static const uint8_t s3net_device_descriptor[] =
{
	0x12,        // bLength
	0x01,        // bDescriptorType (Device)
	0x00, 0x02,  // bcdUSB 2.00
	0xEF,        // bDeviceClass (Miscellaneous)
	0x02,        // bDeviceSubClass (Common Class)
	0x01,        // bDeviceProtocol (Interface Association)
	0x40,        // bMaxPacketSize0 64
	0xFE, 0xCA,  // idVendor 0xCAFE
	0x00, 0x40,  // idProduct 0x4000
	0x01, 0x01,  // bcdDevice 1.01
	0x01,        // iManufacturer (String Index)
	0x02,        // iProduct (String Index)
	0x03,        // iSerialNumber (String Index)
	0x01,        // bNumConfigurations 1
};

static const uint8_t s3net_configuration_descriptor[] =
{
	// Config number, interface count, string index, total length, attribute, power in mA
	TUD_CONFIG_DESCRIPTOR(1, S3_ITF_NUM_TOTAL, 0, S3_RNDIS_CONFIG_TOTAL_LEN, 0, 100),

	// Interface number, string index, EP notification address and size, EP data address (out, in) and size.
	// Endpoint size is a literal 64 (full-speed bulk): our fork defines
	// CFG_TUD_NET_ENDPOINT_SIZE but the managed TinyUSB copy built on S3
	// does not, so the macro is unusable here.
	TUD_RNDIS_DESCRIPTOR(S3_ITF_NUM_CDC, S3_STRID_INTERFACE, S3_EPNUM_NET_NOTIF, 8, S3_EPNUM_NET_OUT, S3_EPNUM_NET_IN, 64),
};
