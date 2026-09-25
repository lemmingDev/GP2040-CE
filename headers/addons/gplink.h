#ifndef GPLINK_H_
#define GPLINK_H_

#include "gpaddon.h"
#include "storagemanager.h"
#include "enums.pb.h"
// NOTE: relative path, not "gplink.h": headers/addons is itself on the
// include path, so a bare include self-matches this file and hits the
// include guard instead of the codec header in extras/gp-link.
#include "../extras/gp-link/gplink.h"
#include "gplink_link.h"

#ifndef GPLINK_ENABLED
#define GPLINK_ENABLED 0
#endif

#ifndef GPLINK_ANALOG_ENABLED
#define GPLINK_ANALOG_ENABLED 0
#endif

#ifndef GPLINK_ANALOG_LX_PIN
#define GPLINK_ANALOG_LX_PIN -1
#endif

#ifndef GPLINK_ANALOG_LY_PIN
#define GPLINK_ANALOG_LY_PIN -1
#endif

#ifndef GPLINK_ANALOG_RX_PIN
#define GPLINK_ANALOG_RX_PIN -1
#endif

#ifndef GPLINK_ANALOG_RY_PIN
#define GPLINK_ANALOG_RY_PIN -1
#endif

// Companion GPIO table size (matches GPLinkOptions.gplinkPins max_count).
// Indexed by companion GPIO number (covers ESP32-S3 GPIO 48).
#ifndef GPLINK_PIN_COUNT
#define GPLINK_PIN_COUNT 64
#endif

// Default pull for companion inputs (buttons-to-GND convention).
#ifndef GPLINK_PIN_PULL_DEFAULT
#define GPLINK_PIN_PULL_DEFAULT 1
#endif

#ifndef GPLINK_UART_INSTANCE
#define GPLINK_UART_INSTANCE GPLINK_UART_DEFAULT_INST
#endif

#ifndef GPLINK_TX_PIN
#define GPLINK_TX_PIN GPLINK_UART_DEFAULT_TX
#endif

#ifndef GPLINK_RX_PIN
#define GPLINK_RX_PIN GPLINK_UART_DEFAULT_RX
#endif

#ifndef GPLINK_BAUD_RATE
#define GPLINK_BAUD_RATE GPLINK_UART_DEFAULT_BAUD
#endif

// GPLink Module Name
#define GPLinkName "GPLink"

// Snapshot of addon runtime state for the webconfig status readout.
struct GPLinkStatus {
    bool started;           // UART init succeeded and link is running
    bool linkAlive;         // inbound traffic within the 2 s timeout
    uint32_t seqGaps;       // inbound sequence discontinuities observed
    uint32_t ignoredFrames; // inbound frames parsed but not acted on (v1 scope)
    uint32_t handledFrames; // inbound GPIO_READ frames applied to gamepad state
    uint8_t txSeq;          // next outbound sequence number (== frames sent)
    bool txFuncOk;          // TX pin still muxed to UART (not stolen post-setup)
    bool rxFuncOk;          // RX pin still muxed to UART
    uint32_t uartFr;        // raw UART flag register (TXFE/RXFE/BUSY inspection)
    uint32_t processCalls;  // process() invocations (dispatch watchdog)
    uint32_t rxBytes;       // total bytes drained from RX FIFO
    uint32_t uptimeS;       // getMillis()/1000 at status time (reboot detector)
};

class GPLinkAddon : public GPAddon {
public:
    GPLinkAddon();
    virtual bool available();
    virtual void setup();
    virtual void reinit();
    virtual void preprocess() {}
    virtual void process();
    virtual void postprocess(bool sent) {}
    virtual std::string name() { return GPLinkName; }
    void getStatus(GPLinkStatus &out);
    // Queue a PIN_CAPS_REQ discovery round (RSP arrives via pumpRx in gamepad
    // mode; the /api/testGPLink handler drains synchronously in config mode).
    bool requestCaps();
    // Raw stored value for a companion pin (MID if never received).
    uint16_t getAnalogPinValue(uint8_t pin);

private:
    void sendHello();
    void sendGpioConfigs();
    void sendAnalogConfigs();
    void applyGpioMask(uint64_t mask);
    void applyAnalogPin(uint8_t pin, uint16_t value);
    void applyAnalogAxes();
    void sendOutputMask(uint64_t mask);
    void sendInputState(const GamepadState &state);
    void sendHeartbeat();
    void pumpRx(uint32_t now);
    bool sendFrame(uint8_t type, const uint8_t *payload, size_t payloadLen);

    bool started;               // UART init succeeded
    uint8_t uartInst;           // active UART instance (for reinit deinit)
    uint8_t txPin;              // active TX pin (for mux diagnostics)
    uint8_t rxPin;              // active RX pin (for mux diagnostics)
    uint32_t processCalls;      // process() invocations (dispatch watchdog)
    uint32_t rxBytes;           // total bytes drained from RX FIFO
    uint32_t lastDebugMs;       // last DEBUG_TEXT telemetry beacon
    bool wasAlive;              // link state last poll (CONFIG resend on rise)
    uint32_t lastConfigMs;      // last periodic GPIO_CONFIG refresh
    gplink_decoder decoder;     // streaming COBS decoder for inbound bytes
    gplink_link link;           // liveness / heartbeat / sequence tracking
    uint8_t txSeq;              // next outbound sequence number
    GamepadState lastSent;      // change detection for INPUT_STATE
    bool haveLastSent;
    uint32_t ignoredFrames;     // inbound frames parsed but not acted on (v1 scope)
    uint32_t handledFrames;     // inbound GPIO_READ frames applied to state
    uint32_t seqGaps;           // inbound sequence gaps observed
    uint64_t lastMask;          // last GPIO_READ mask, re-applied every poll
    uint64_t lastOutputMask;    // last GPIO_WRITE mask sent to companion
    uint32_t lastOutputMs;      // last GPIO_WRITE send (5 s backstop)
    uint16_t analogValues[64];  // last ANALOG_READ values by companion pin
    uint8_t lastLedMask;        // last PLAYER_LED_SET mask sent
    uint8_t lastWeak;           // last RUMBLE_SET weak intensity sent
    uint8_t lastStrong;         // last RUMBLE_SET strong intensity sent
    uint32_t lastActMs;         // last actuation send (5 s backstop)
};

GPLinkAddon *GPLink_GetAddon(); // null until the addon is constructed

#endif // GPLINK_H_
