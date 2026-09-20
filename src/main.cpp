/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

// Pi Pico includes
#if defined(PICO_BOARD)
#include "pico/multicore.h"
#elif defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// GP2040 includes
#include "gp2040.h"
#include "gp2040aux.h"
#include "hal_time.h"

#include <cstdlib>

// Custom implementation of __gnu_cxx::__verbose_terminate_handler() to reduce binary size
namespace __gnu_cxx {
void __verbose_terminate_handler()
{
	abort();
}
}

static GP2040 * gp2040Core0 = nullptr;
static GP2040Aux * gp2040Core1 = nullptr;

// Launch our second core with additional modules loaded in
#if defined(PICO_BOARD)
void core1() {
	multicore_lockout_victim_init(); // block core 1

	// Create GP2040 w/ Additional Modules for Core 1	
	gp2040Core1->setup();
	gp2040Core1->run();
}
#elif defined(ESP_PLATFORM)
// S3: no core1 companion in Phase 1 (the FreeRTOS aux task lands in Task 4);
// core1() is omitted so the uncompiled GP2040Aux methods are never referenced.
#endif

#if defined(ESP_PLATFORM)
extern "C" void app_main() {
#else
int main() {
#endif
	// Create GP2040 Main Core (core0), Core1 is dependent on Core0
	gp2040Core0 = new GP2040();
#if defined(PICO_BOARD)
	gp2040Core1 = new GP2040Aux();
#elif defined(ESP_PLATFORM)
	// Phase 1 single-core bring-up: no aux object yet (Task 4).
	gp2040Core1 = nullptr;
#endif

	// Create GP2040 Main Core - Setup Core0
	gp2040Core0->setup();

	// Create GP2040 Thread for Core1
#if defined(PICO_BOARD)
	multicore_launch_core1(core1);

	// Sync Core0 and Core1
	while(gp2040Core1->ready() == false ) {
		__asm volatile ("nop\n");
	}
#else
	// Phase 1 single-core bring-up: aux/display/LED task lands in Task 4.
	// Core1 objects exist but are not started yet.
	(void)gp2040Core1;
	hal::sleepMs(10);
#endif
	gp2040Core0->run();
#if defined(ESP_PLATFORM)
	vTaskDelete(nullptr);
#endif

#if defined(PICO_BOARD)
	return 0;
#endif
}
