#ifndef _TRANSPORT_ROUTER_H_
#define _TRANSPORT_ROUTER_H_

#include "enums.pb.h"

// Phase 0: exactly one active transport, chosen by input mode.
// Future (parked): USB+BT simultaneously with runtime selection.
enum class ActiveTransport : uint8_t { USB, BLUETOOTH };

class TransportRouter {
public:
    static ActiveTransport getActive(InputMode mode) {
        return mode == INPUT_MODE_BLUETOOTH ? ActiveTransport::BLUETOOTH : ActiveTransport::USB;
    }
};

#endif
