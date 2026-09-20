// S3 PeripheralI2C backend (Task 5): identical class interface over the IDF
// I2C master driver. One master bus per block (I2C0 -> I2C_NUM_0,
// I2C1 -> I2C_NUM_1), lazily created by setConfig(); per-transaction device
// handles are cached (4 slots, round-robin eviction) because the IDF API
// binds the 7-bit address at add-device time while our interface passes it
// per call. Timeouts: 1 s per transaction (S3_I2C_TIMEOUT_MS), the stand-in
// for the Pico blocking calls' indefinite wait. isBlock (no-STOP chaining)
// cannot be expressed per call in this API, so every transaction terminates
// with STOP; no S3 consumer chains (TinySSD1306 passes false; readRegister
// uses transmit_receive for a true repeated start).
#include "peripheral_i2c.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "periph_i2c";

PeripheralI2C::PeripheralI2C() {
    for (uint8_t b = 0; b < NUM_I2CS; b++) {
        _busHandles[b] = nullptr;
    }
    for (uint8_t i = 0; i < S3_I2C_MAX_DEVS; i++) {
        _devAddr[i] = 0xFF;
        _devHandle[i] = nullptr;
    }
}

void PeripheralI2C::setConfig(uint8_t block, int8_t sda, int8_t scl, uint32_t speed) {
    if ((block < NUM_I2CS) && (sda > -1) && (scl > -1)) {
        _block = block;
        _SDA = sda;
        _SCL = scl;
        _Speed = (int32_t)speed;
        configured = true;
        setup();
    } else {
        // currently not supported
    }
}

void PeripheralI2C::setup() {
    // Drop cached device handles: pins/speed may have changed under us.
    for (uint8_t i = 0; i < S3_I2C_MAX_DEVS; i++) {
        if (_devHandle[i] != nullptr) {
            i2c_master_bus_rm_device(_devHandle[i]);
            _devHandle[i] = nullptr;
            _devAddr[i] = 0xFF;
        }
    }
    _devUsed = 0;

    const bool mismatch = (_busHandles[_block] == nullptr) ||
                          (_busSDA[_block] != _SDA) || (_busSCL[_block] != _SCL) ||
                          (_busSpeed[_block] != _Speed);
    if (mismatch) {
        if (_busHandles[_block] != nullptr) {
            ESP_ERROR_CHECK(i2c_del_master_bus(_busHandles[_block]));
            _busHandles[_block] = nullptr;
        }
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = (i2c_port_t)_block;
        bus_cfg.sda_io_num = (gpio_num_t)_SDA;
        bus_cfg.scl_io_num = (gpio_num_t)_SCL;
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = 1; // Pico setup() pulls up too.
        esp_err_t err = i2c_new_master_bus(&bus_cfg, &_busHandles[_block]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_new_master_bus(%d) failed: %s", _block, esp_err_to_name(err));
            _bus = nullptr;
            return;
        }
        _busSDA[_block] = _SDA;
        _busSCL[_block] = _SCL;
        _busSpeed[_block] = _Speed;
    }
    _bus = _busHandles[_block];

    // reset the bus before using it
    clear();
}

i2c_master_dev_handle_t PeripheralI2C::devFor(uint8_t address) {
    if (_bus == nullptr) {
        return nullptr;
    }
    for (uint8_t i = 0; i < S3_I2C_MAX_DEVS; i++) {
        if ((_devHandle[i] != nullptr) && (_devAddr[i] == address)) {
            return _devHandle[i];
        }
    }
    const uint8_t slot = (uint8_t)(_devUsed % S3_I2C_MAX_DEVS);
    _devUsed++;
    if (_devHandle[slot] != nullptr) {
        i2c_master_bus_rm_device(_devHandle[slot]);
        _devHandle[slot] = nullptr;
    }
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = (uint32_t)_Speed;
    if (i2c_master_bus_add_device(_bus, &dev_cfg, &_devHandle[slot]) != ESP_OK) {
        _devHandle[slot] = nullptr;
        _devAddr[slot] = 0xFF;
        return nullptr;
    }
    _devAddr[slot] = address;
    return _devHandle[slot];
}

int16_t PeripheralI2C::read(uint8_t address, uint8_t *data, uint16_t len, bool isBlock) {
    if ((_exclusiveAddress > -1) && (_exclusiveAddress != address)) return -1;
    (void)isBlock; // No-STOP chaining has no per-call expression here; STOP always terminates (see file note).

    i2c_master_dev_handle_t dev = devFor(address);
    if (dev == nullptr) return -1;
    esp_err_t err = i2c_master_receive(dev, data, (size_t)len, pdMS_TO_TICKS(S3_I2C_TIMEOUT_MS));
    return (err == ESP_OK) ? (int16_t)len : -1;
}

int16_t PeripheralI2C::readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len) {
    if ((_exclusiveAddress > -1) && (_exclusiveAddress != address)) return -1;

    i2c_master_dev_handle_t dev = devFor(address);
    if (dev == nullptr) return 0;
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, data, (size_t)len,
                                                pdMS_TO_TICKS(S3_I2C_TIMEOUT_MS));
    return (err == ESP_OK) ? 1 : 0; // Pico returns (registerCheck >= 0): 1/0, not a count.
}

int16_t PeripheralI2C::write(uint8_t address, uint8_t *data, uint16_t len, bool isBlock) {
    if ((_exclusiveAddress > -1) && (_exclusiveAddress != address)) return -1;
    (void)isBlock; // See read(): STOP always terminates.

    i2c_master_dev_handle_t dev = devFor(address);
    if (dev == nullptr) return -1;
    esp_err_t err = i2c_master_transmit(dev, data, (size_t)len, pdMS_TO_TICKS(S3_I2C_TIMEOUT_MS));
    return (err == ESP_OK) ? (int16_t)len : -1;
}

uint8_t PeripheralI2C::test(uint8_t address) {
    if ((_bus == nullptr) || (address > 0x7F)) return 0;
    return (i2c_master_probe(_bus, address, pdMS_TO_TICKS(50)) == ESP_OK) ? 1 : 0;
}

void PeripheralI2C::clear() {
    // reset the bus
    test(0xFF);
}

std::map<uint8_t,bool> PeripheralI2C::scan() {
    std::map<uint8_t,bool> result;

    for (uint8_t addr = 0; addr < (1 << 7); ++addr) {
        if (test(addr)) {
            result.insert({addr, true});
        }
    }

    return result;
}

#endif // defined(ESP_PLATFORM)
