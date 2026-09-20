/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2023 Brian S. Stephan <bss@incorporeal.org>
 */

#ifndef TYPES_H_
#define TYPES_H_

// common types
#define	Pin_t		int32_t		// signed to accommodate for -1
#define Mask_t		uint32_t

#if defined(ESP_PLATFORM) && !defined(NUM_BANK0_GPIOS)
// S3 has no Pico SDK: pin-table count comes from the Task-0 board table
// (configs/ESP32S3DevKitC1/BoardConfig.h), which is still a 30-entry table
// over pins 0-29 (GPIO 30+ await the later S3 table extension). 30 keeps
// Mask_t (uint32_t) bitmasks, the GpioMappings proto, and every
// NUM_BANK0_GPIOS loop intact. Reserved pins inside the range (USB 19/20,
// strapping 0/3) stay RESERVED in the board table and are never init'd.
#define NUM_BANK0_GPIOS 30
#endif

#endif
