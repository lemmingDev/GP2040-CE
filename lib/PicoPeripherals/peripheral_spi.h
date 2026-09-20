#ifndef _PERIPHERAL_SPI_H_
#define _PERIPHERAL_SPI_H_

#include <map>
#include <stddef.h>
#include <stdint.h>

#define DMA_BUFFER_SIZE 2048 // Each direction gets a 2k buffer

typedef enum {
  SPI_MODE0 = 0,
  SPI_MODE1 = 1,
  SPI_MODE2 = 2,
  SPI_MODE3 = 3,
} SPIMode;

#ifndef SPI0_ENABLED
#define SPI0_ENABLED 0
#endif

#ifndef SPI0_PIN_RX
#define SPI0_PIN_RX -1
#endif

#ifndef SPI0_PIN_CS
#define SPI0_PIN_CS -1
#endif

#ifndef SPI0_PIN_SCK
#define SPI0_PIN_SCK -1
#endif

#ifndef SPI0_PIN_TX
#define SPI0_PIN_TX -1
#endif

#ifndef SPI1_ENABLED
#define SPI1_ENABLED 0
#endif

#ifndef SPI1_PIN_RX
#define SPI1_PIN_RX -1
#endif

#ifndef SPI1_PIN_CS
#define SPI1_PIN_CS -1
#endif

#ifndef SPI1_PIN_SCK
#define SPI1_PIN_SCK -1
#endif

#ifndef SPI1_PIN_TX
#define SPI1_PIN_TX -1
#endif

#if defined(ESP_PLATFORM)
// S3 SPI stub (Task 5): no SPI peripheral backend in Phase 1 (the S3 path
// uses I2C displays only; USB host is Phase 2). Same class name and the
// method set peripheralmanager.cpp uses; everything no-ops so the manager
// compiles untouched. Pico types (spi_order_t) become plain ints.
#ifndef NUM_SPIS
#define NUM_SPIS 2
#endif

class PeripheralSPI {
public:
    PeripheralSPI() {}

    ~PeripheralSPI() {}

    bool configured = false;
    bool initialized = true;

    void* getController() { return nullptr; }

    // Set the configuration for this SPI peripheral instance
    void setConfig(uint8_t block, uint8_t tx, uint8_t rx, uint8_t sck, uint8_t cs) {}

    // Disable SPI instance and all associated system resources (such as DMA channels) associated with that instance
    void deactivate() {}

    // Conditionally transfers and/or receives data of the provided length
    void transfer(const uint8_t *tx, uint8_t *rx, size_t count) {}

    // Reads a byte of data from the SPI bus while sending the transfer value provided
    uint8_t transfer(uint8_t tx) { (void)tx; return 0; }

    // Reads two bytes of data from the SPI bus while sending the transfer value provided
    uint16_t transfer16(uint16_t tx) { (void)tx; return 0; }

    // Activate CS pin for this SPI peripheral instance
    void select(int8_t cs = -1) { (void)cs; }

    // Deactivate currently active CS pin for this SPI peripheral instance
    void deselect() {}

    // Begin a SPI transaction
    void beginTransaction(uint32_t speedMHz, int bitOrder, SPIMode spiMode) {
        (void)speedMHz; (void)bitOrder; (void)spiMode;
    }

    // End a SPI transaction
    void endTransaction() {}
};

#else // Pico SDK original

#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <hardware/sync.h>
#include <hardware/platform_defs.h>

class PeripheralSPI {
public:
    PeripheralSPI();

    ~PeripheralSPI() {
        deselect();
        deactivate();
    }

    bool configured = false;
    bool initialized = true;

    spi_inst_t* getController() { return _SPI; }

    // Set the configuration for this SPI peripheral instance
    void setConfig(uint8_t block, uint8_t tx, uint8_t rx, uint8_t sck, uint8_t cs);

    // Disable SPI instance and all associated system resources (such as DMA channels) associated with that instance
    void deactivate();

    // Conditionally transfers and/or receives data of the provided length
    void transfer(const uint8_t *tx, uint8_t *rx, size_t count);

    // Reads a byte of data from the SPI bus while sending the transfer value provided
    uint8_t transfer(uint8_t tx);

    // Reads two bytes of data from the SPI bus while sending the transfer value provided
    uint16_t transfer16(uint16_t tx);

    // Activate CS pin for this SPI peripheral instance
    void select(int8_t cs = -1);

    // Deactivate currently active CS pin for this SPI peripheral instance
    void deselect();

    // Begin a SPI transaction
    void beginTransaction(uint32_t speedMHz, spi_order_t bitOrder, SPIMode spiMode);

    // End a SPI transaction
    void endTransaction();

private:
    const uint32_t SPI_DEFAULT_SPEED = 1000000; // 1Mhz

    uint8_t _RX; // MISO/POCI gpio pin
    uint8_t _TX; // MOSI/PICO gpio pin
    uint8_t _SCK;
    int _CS; // The HW CS pin
    int _CSActive; // The active CS pin, negative value indicates no selected pin

    spi_inst_t *_SPI; // The RP2040 SPI instance
    uint32_t _Speed;
    SPIMode _SpiMode;
    spi_order_t _BitOrder;
    spi_cpol_t _Cpol;
    spi_cpha_t _Cpha;

    bool _UseDMA;
    int _dmaRxChannel;
    int _dmaTxChannel;
    uint8_t *_dmaRxBuf;
    uint8_t *_dmaTxBuf;

    spi_inst_t* _hardwareBlocks[NUM_SPIS] = {spi0,spi1};

    void setup();

    inline spi_cpol_t get_cpol(SPIMode mode) {
        switch (mode)
        {
            case SPI_MODE0:
                return SPI_CPOL_0;
            case SPI_MODE1:
                return SPI_CPOL_0;
            case SPI_MODE2:
                return SPI_CPOL_1;
            case SPI_MODE3:
                return SPI_CPOL_1;
        }

        // Error
        return SPI_CPOL_0;
    }

    inline spi_cpha_t get_cpha(SPIMode mode) {
        switch (mode)
        {
            case SPI_MODE0:
                return SPI_CPHA_0;
            case SPI_MODE1:
                return SPI_CPHA_1;
            case SPI_MODE2:
                return SPI_CPHA_0;
            case SPI_MODE3:
                return SPI_CPHA_1;
        }

        // Error
        return SPI_CPHA_0;
    }
};

#endif // Pico SDK original vs S3 stub

#endif
