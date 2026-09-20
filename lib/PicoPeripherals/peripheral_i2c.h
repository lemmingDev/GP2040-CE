#ifndef _PERIPHERAL_I2C_H_
#define _PERIPHERAL_I2C_H_

#include <map>

// I2C block defaults: common scope (as upstream) so BOTH the Pico backend
// and the S3 backend (plus config_utils.cpp on either path) see them;
// board headers (Pico board configs, S3 BoardConfig.h) override before use.
#ifndef I2C0_ENABLED
#define I2C0_ENABLED 0
#endif

#ifndef I2C0_PIN_SDA
#define I2C0_PIN_SDA -1
#endif

#ifndef I2C0_PIN_SCL
#define I2C0_PIN_SCL -1
#endif

#ifndef I2C0_SPEED
#define I2C0_SPEED 400000
#endif

#ifndef I2C1_ENABLED
#define I2C1_ENABLED 0
#endif

#ifndef I2C1_PIN_SDA
#define I2C1_PIN_SDA -1
#endif

#ifndef I2C1_PIN_SCL
#define I2C1_PIN_SCL -1
#endif

#ifndef I2C1_SPEED
#define I2C1_SPEED 400000
#endif

#if defined(ESP_PLATFORM)
// S3 backend (Task 5): same public interface over the IDF I2C master driver
// (driver/i2c_master.h); see peripheral_i2c_s3.cpp. Header dispatch keeps
// the Pico CMake untouched (it still builds peripheral_i2c.cpp only).
#include <stdint.h>
#include "driver/i2c_master.h"

#ifndef NUM_I2CS
#define NUM_I2CS 2
#endif

//#define DEBUG_PERIPHERALI2C

class PeripheralI2C {
public:
    PeripheralI2C();
    ~PeripheralI2C() {}

    bool configured = false;

    i2c_master_bus_handle_t getController() { return _bus; }

    void setConfig(uint8_t block, int8_t sda, int8_t scl, uint32_t speed);

    int16_t read(uint8_t address, uint8_t *data, uint16_t len, bool isBlock=false);
    int16_t readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len);

    int16_t write(uint8_t address, uint8_t *data, uint16_t len, bool isBlock=true);

    uint8_t test(uint8_t address);
    void clear();

    std::map<uint8_t,bool> scan();

    // if this is set to anything other than -1, any r/w operations against the address other than test()/scan() will not be processed
    void setExclusiveUse(int8_t address = -1) { _exclusiveAddress = address; }
private:
    // Stand-in for the Pico blocking calls' indefinite wait: 1 s per
    // transaction (a full 1 KiB SSD1306 frame at 400 kHz takes ~25 ms).
    static const uint32_t S3_I2C_TIMEOUT_MS = 1000;
    // Display probing uses one address; allowance for a few more devices.
    static const uint8_t S3_I2C_MAX_DEVS = 4;

    const uint32_t DEFAULT_SPEED = 400000;

    uint8_t _block = 0;
    int8_t _SDA = -1;
    int8_t _SCL = -1;
    int32_t _Speed = DEFAULT_SPEED;

    i2c_master_bus_handle_t _busHandles[NUM_I2CS] = {nullptr, nullptr};
    int8_t _busSDA[NUM_I2CS] = {-1, -1};
    int8_t _busSCL[NUM_I2CS] = {-1, -1};
    int32_t _busSpeed[NUM_I2CS] = {0, 0};
    i2c_master_bus_handle_t _bus = nullptr;

    uint8_t _devAddr[S3_I2C_MAX_DEVS];
    i2c_master_dev_handle_t _devHandle[S3_I2C_MAX_DEVS];
    uint8_t _devUsed = 0;

    int8_t _exclusiveAddress = -1;

    void setup();
    i2c_master_dev_handle_t devFor(uint8_t address);
};

#else // Pico SDK original (PICO_BOARD and host builds)

#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/platform_defs.h>

class PeripheralI2C {
public:
    PeripheralI2C();
    ~PeripheralI2C() {}

    bool configured = false;

    i2c_inst_t* getController() { return _I2C; }

    void setConfig(uint8_t block, int8_t sda, int8_t scl, uint32_t speed);

    int16_t read(uint8_t address, uint8_t *data, uint16_t len, bool isBlock=false);
    int16_t readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len);

    int16_t write(uint8_t address, uint8_t *data, uint16_t len, bool isBlock=true);

    uint8_t test(uint8_t address);
    void clear();

    std::map<uint8_t,bool> scan();

    // if this is set to anything other than -1, any r/w operations against the address other than test()/scan() will not be processed
    void setExclusiveUse(int8_t address = -1) { _exclusiveAddress = address; }
private:
    const uint32_t DEFAULT_SPEED = 400000;

    uint8_t _SDA;
    uint8_t _SCL;
    i2c_inst_t *_I2C;
    int32_t _Speed;

    i2c_inst_t* _hardwareBlocks[NUM_I2CS] = {i2c0,i2c1};

    int8_t _exclusiveAddress = -1;

    void setup();
};

#endif // Pico SDK original vs S3 backend

#endif // _PERIPHERAL_I2C_H_
