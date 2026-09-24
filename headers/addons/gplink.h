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
    uint8_t txSeq;          // next outbound sequence number (== frames sent)
    bool txFuncOk;          // TX pin still muxed to UART (not stolen post-setup)
    bool rxFuncOk;          // RX pin still muxed to UART
    uint32_t uartFr;        // raw UART flag register (TXFE/RXFE/BUSY inspection)
    uint8_t loopTest;       // setup-time SIO continuity test: 0=not run, 1=pass, 2=fail
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

private:
    void sendHello();
    void sendInputState(const GamepadState &state);
    void sendHeartbeat();
    void pumpRx(uint32_t now);
    bool sendFrame(uint8_t type, const uint8_t *payload, size_t payloadLen);

    bool started;               // UART init succeeded
    uint8_t uartInst;           // active UART instance (for reinit deinit)
    uint8_t txPin;              // active TX pin (for mux diagnostics)
    uint8_t rxPin;              // active RX pin (for mux diagnostics)
    uint8_t loopTest;           // setup-time continuity result (mirrors status)
    uint32_t processCalls;      // process() invocations (dispatch watchdog)
    uint32_t rxBytes;           // total bytes drained from RX FIFO
    gplink_decoder decoder;     // streaming COBS decoder for inbound bytes
    gplink_link link;           // liveness / heartbeat / sequence tracking
    uint8_t txSeq;              // next outbound sequence number
    GamepadState lastSent;      // change detection for INPUT_STATE
    bool haveLastSent;
    uint32_t ignoredFrames;     // inbound frames parsed but not acted on (v1 scope)
    uint32_t seqGaps;           // inbound sequence gaps observed
};

GPLinkAddon *GPLink_GetAddon(); // null until the addon is constructed

#endif // GPLINK_H_
