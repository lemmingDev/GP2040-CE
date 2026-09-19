#if defined(ESP_PLATFORM)
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

// Phase 1: caller maps S3 GPIO to adc_channel_t (e.g. GPIO4 -> ADC_CHANNEL_3);
// no mapping table here (YAGNI).

static adc_oneshot_unit_handle_t adc1 = nullptr;

void halAdcInit() {
    adc_oneshot_unit_init_cfg_t cfg = {};
    cfg.unit_id = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&cfg, &adc1));
}

uint16_t halAdcRead(uint8_t gpioPin, adc_channel_t channel) {
    (void)gpioPin;
    adc_oneshot_chan_cfg_t cfg = {};
    cfg.atten = ADC_ATTEN_DB_11;
    cfg.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1, channel, &cfg));
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1, channel, &raw));
    return (uint16_t)raw;
}
#endif
