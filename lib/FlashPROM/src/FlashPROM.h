/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2021 Jason Skuby (mytechtoybox.com)
 */

#ifndef FLASHPROM_H_
#define FLASHPROM_H_

#include <stdint.h>
#include <string.h>
#if defined(PICO_BOARD)
#include <pico/lock_core.h>
#include <pico/multicore.h>
#include <hardware/flash.h>
#include <hardware/timer.h>
#endif

#if defined(ESP_PLATFORM)
// S3: no XIP alias; persistence lives in the 32k gpconfig partition.
// (Upstream moved Pico to 32k for the larger post-merge config; S3 matches
// since the partition fits exactly. Erase gpconfig once when updating.)
#define EEPROM_SIZE_BYTES    0x8000
#define EEPROM_ADDRESS_START (0)
#else
#define EEPROM_SIZE_BYTES    0x8000           // Reserve 32k of flash memory (ensure this value is divisible by 256)
#define EEPROM_ADDRESS_START _u(0x101F8000) // The arduino-pico EEPROM lib starts here, so we'll do the same
#endif

// Warning: If the write wait is too long it can stall other processes
#define EEPROM_WRITE_WAIT    50             // Amount of time in ms to wait before blocking core1 and committing to flash

class FlashPROM
{
	public:
		void start();
		void commit();
		void reset();

		static uint8_t writeCache[EEPROM_SIZE_BYTES];
};

inline FlashPROM EEPROM;

#endif
