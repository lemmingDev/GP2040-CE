#include "hal_time.h"

#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace hal {
void sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
} // namespace hal

#endif
