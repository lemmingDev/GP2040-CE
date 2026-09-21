#include "drivermanager.h"

#if defined(PICO_BOARD)
#include "drivers/net/NetDriver.h"
#include "drivers/astro/AstroDriver.h"
#include "drivers/egret/EgretDriver.h"
#endif
#include "drivers/hid/HIDDriver.h"
#if defined(PICO_BOARD) || defined(ESP_PLATFORM)
#include "drivers/switch/SwitchDriver.h"
#include "drivers/ps3/PS3Driver.h"
#include "drivers/keyboard/KeyboardDriver.h"
#include "drivers/ps4/PS4Driver.h"
#include "drivers/xbone/XBOneDriver.h"
#include "drivers/mdmini/MDMiniDriver.h"
#include "drivers/neogeo/NeoGeoDriver.h"
#include "drivers/pcengine/PCEngineDriver.h"
#include "drivers/egret/EgretDriver.h"
#include "drivers/astro/AstroDriver.h"
#include "drivers/psclassic/PSClassicDriver.h"
#include "drivers/xboxog/XboxOriginalDriver.h"
#endif
#if defined(PICO_BOARD)
#include "drivers/keyboard/KeyboardDriver.h"
#include "drivers/mdmini/MDMiniDriver.h"
#include "drivers/neogeo/NeoGeoDriver.h"
#include "drivers/pcengine/PCEngineDriver.h"
#include "drivers/psclassic/PSClassicDriver.h"
#include "drivers/ps3/PS3Driver.h"
#include "drivers/ps4/PS4Driver.h"
#include "drivers/switch/SwitchDriver.h"
#include "drivers/switchpro/SwitchProDriver.h"
#include "drivers/xbone/XBOneDriver.h"
#include "drivers/xboxog/XboxOriginalDriver.h"
#endif
#include "drivers/xinput/XInputDriver.h"
#if defined(PICO_BOARD)
// S3: Bluetooth driver + USB-host manager are Phase 2/3 (no NimBLE/BLE or
// USB-host sources compiled); the S3 setup() switch below has no Bluetooth
// case (Task-2 ruling W3: HID+XInput-only reachable).
#include "drivers/bluetooth/BluetoothDriver.h"
#endif
#include "drivers/switchpro/SwitchProDriver.h"
#include "drivers/p5general/P5GeneralDriver.h"
#include "drivers/sinput/SInputDriver.h"
#if defined(PICO_BOARD)
#include "usbhostmanager.h"
#endif

void DriverManager::setup(InputMode mode) {
#if defined(PICO_BOARD)
    switch (mode) {
        case INPUT_MODE_CONFIG:
            driver = new NetDriver();
            break;
        case INPUT_MODE_ASTRO:
            driver = new AstroDriver();
            break;
        case INPUT_MODE_EGRET:
            driver = new EgretDriver();
            break;
        case INPUT_MODE_KEYBOARD:
            driver = new KeyboardDriver();
            break;
        case INPUT_MODE_GENERIC:
            driver = new HIDDriver();
            break;
        case INPUT_MODE_MDMINI:
            driver = new MDMiniDriver();
            break;
        case INPUT_MODE_NEOGEO:
            driver = new NeoGeoDriver();
            break;
        case INPUT_MODE_PSCLASSIC:
            driver = new PSClassicDriver();
            break;
        case INPUT_MODE_PCEMINI:
            driver = new PCEngineDriver();
            break;
        case INPUT_MODE_PS3:
            driver = new PS3Driver();
            break;
        case INPUT_MODE_PS4:
            driver = new PS4Driver(PS4_CONTROLLER);
            break;
        case INPUT_MODE_PS5:
            driver = new PS4Driver(PS4_ARCADESTICK);
            break;
        case INPUT_MODE_P5GENERAL:
            driver = new P5GeneralDriver();
            break;
        case INPUT_MODE_SWITCH:
            driver = new SwitchDriver();
            break;
        case INPUT_MODE_XBONE:
            driver = new XBOneDriver();
            break;
        case INPUT_MODE_XBOXORIGINAL:
            driver = new XboxOriginalDriver();
            break;
        case INPUT_MODE_XINPUT:
            driver = new XInputDriver();
            break;
        case INPUT_MODE_BLUETOOTH:
            driver = new BluetoothDriver();
            break;
        case INPUT_MODE_SWITCH_PRO:
            driver = new SwitchProDriver();
            break;
        case INPUT_MODE_SINPUT:
            driver = new SInputDriver();
            break;
        default:
            return;
    }
#elif defined(ESP_PLATFORM)
    // S3 Phase 1 Task 6: device-driver parity (USB device only). Bluetooth
    // stays OUT per YAGNI — no NimBLE/BLE sources are compiled in this phase
    // (Ruling W3); CONFIG/Net is webconfig (Phase 3). Unsupported stored
    // modes fall back to HID so the driver is never null.
    switch (mode) {
        case INPUT_MODE_XINPUT:
            driver = new XInputDriver();
            break;
        case INPUT_MODE_SWITCH:
            driver = new SwitchDriver();
            break;
        case INPUT_MODE_PS3:
            driver = new PS3Driver();
            break;
        case INPUT_MODE_KEYBOARD:
            driver = new KeyboardDriver();
            break;
        case INPUT_MODE_PS4:
            driver = new PS4Driver(PS4_CONTROLLER);
            break;
        case INPUT_MODE_PS5:
            driver = new PS4Driver(PS4_ARCADESTICK);
            break;
        case INPUT_MODE_XBONE:
            driver = new XBOneDriver();
            break;
        case INPUT_MODE_MDMINI:
            driver = new MDMiniDriver();
            break;
        case INPUT_MODE_NEOGEO:
            driver = new NeoGeoDriver();
            break;
        case INPUT_MODE_PCEMINI:
            driver = new PCEngineDriver();
            break;
        case INPUT_MODE_EGRET:
            driver = new EgretDriver();
            break;
        case INPUT_MODE_ASTRO:
            driver = new AstroDriver();
            break;
        case INPUT_MODE_PSCLASSIC:
            driver = new PSClassicDriver();
            break;
        case INPUT_MODE_XBOXORIGINAL:
            driver = new XboxOriginalDriver();
            break;
        case INPUT_MODE_SWITCH_PRO:
            driver = new SwitchProDriver();
            break;
        case INPUT_MODE_P5GENERAL:
            driver = new P5GeneralDriver();
            break;
        case INPUT_MODE_SINPUT:
            driver = new SInputDriver();
            break;
        case INPUT_MODE_GENERIC:
        default:
            driver = new HIDDriver();
            mode = INPUT_MODE_GENERIC;
            break;
    }
#endif

    // Initialize our chosen driver
    driver->initialize();
    inputMode = mode;
}
