/*
 * SPDX-License-Identifier: MIT
 * Shared flash layout for GP2040-CE Bluetooth pairing data.
 *
 * GP2040-CE reserves 0x1F8000-0x1FFFFF for its 32 KB EEPROM on the
 * standard 2 MB Pico flash layout. The two sectors immediately before
 * that region are kept separate for the two Bluetooth modes; BTstack
 * uses the following two-sector region for its link-key database.
 */
#ifndef BLUETOOTH_FLASH_LAYOUT_H
#define BLUETOOTH_FLASH_LAYOUT_H

#define HIDBT_PAIRING_FLASH_OFFSET    0x1F4000
#define SWITCHBT_PAIRING_FLASH_OFFSET 0x1F5000
#define BTSTACK_FLASH_OFFSET          0x1F6000
#define BTSTACK_FLASH_SIZE            0x2000

#define BT_PAIRING_FLASH_SECTOR_SIZE  0x1000
#define BT_PAIRING_MAGIC_SWITCH       0x53574254  // "SWBT"
#define BT_PAIRING_MAGIC_HID          0x48494443  // "HIDC"

#endif
