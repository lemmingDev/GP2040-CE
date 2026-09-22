/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2023 Brian S. Stephan <bss@incorporeal.org>
 */

#ifndef TYPES_H_
#define TYPES_H_

// common types
#define	Pin_t		int32_t		// signed to accommodate for -1
#define Mask_t		uint64_t

#if defined(ESP_PLATFORM) && !defined(NUM_BANK0_GPIOS)
// S3 has no Pico SDK: NUM_BANK0_GPIOS normally comes from the board header
// (S3 BoardConfig defines 49), but this header can precede it in the include
// order, so keep a fallback. Values must match: 49 keeps the S3 0-48 tables
// (Mask_t is uint64_t), the GpioMappings proto, and every NUM_BANK0_GPIOS
// loop consistent. Pico is untouched — its value comes from the Pico SDK,
// so this block never fires there.
#define NUM_BANK0_GPIOS 49
#endif

#endif
