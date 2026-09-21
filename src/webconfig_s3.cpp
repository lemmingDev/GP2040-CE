// ESP32-S3 webconfig HTTP backend core (s3-webconfig Task 4).
//
// S3-ONLY TU: the whole file is wrapped in #if defined(ESP_PLATFORM) so the
// Pico build never compiles it even if it were listed in Pico SRCS (it is
// not: root CMakeLists.txt lists Pico sources explicitly, and this file is
// only added to esp32-s3/main/CMakeLists.txt SRCS).
//
// Serves the exact Pico URI contract (see src/webconfig.cpp, read-only
// reference — Pico's file stays byte-untouched) via esp_http_server:
//   GET  /api/getFirmwareVersion   POST /api/setConfig
//   GET  /api/getConfig            POST /api/reboot
//   GET  /api/getGamepadOptions    POST /api/resetSettings
//   GET  /api/getPinMappings       POST /api/setGamepadOptions
//   GET  /api/getProfileOptions    POST /api/setPinMappings
//   GET  /api/getKeyMappings       POST /api/setProfileOptions
//   GET  /api/getBootModeOptions   POST /api/setKeyMappings
//                                 POST /api/setBootModeOptions
// Task 6 appends the LED/display/addons/peripherals group (same contract):
//   GET  /api/getLedOptions            POST /api/setLedOptions
//   GET  /api/getDisplayOptions        POST /api/setDisplayOptions
//                                     POST /api/setPreviewDisplayOptions (RAM-only)
//   GET  /api/getAddonsOptions         POST /api/setAddonsOptions
//   GET  /api/getWiiControls           POST /api/setWiiControls
//   GET  /api/getMacroAddonOptions     POST /api/setMacroAddonOptions
//   GET  /api/getPeripheralOptions     POST /api/setPeripheralOptions
//   GET  /api/getI2CPeripheralMap
//   GET  /api/getExpansionPins         POST /api/setExpansionPins
//   GET  /api/getHETriggerCalibrations POST /api/setHETriggerCalibrations
//                                     POST /api/setHETriggerOptions (RAM-only)
//                                     POST /api/getHETriggerVoltage (live ADC)
//   GET  /api/getReactiveLEDs          POST /api/setReactiveLEDs
//   GET  /api/getAnimationProtoOptions POST /api/setAnimationProtoOptions
//                                     POST /api/setAnimationButtonTestMode
//                                     POST /api/setAnimationButtonTestState
//                                     POST /api/clearAnimationButtonTestMode
//   GET  /api/getLightsDataOptions     POST /api/setLightsDataOptions
//   GET  /api/getLightsPresets/0..7
//   GET  /api/getLightsDataPresets     POST /api/setLightsToDefault
//   GET  /api/getSplashImage           POST /api/setSplashImage
//   GET  /api/getBoardDefinition
//   GET  /api/getMemoryReport
//   GET  /api/getUsedPins
//   GET  /api/getHeldPins              POST /api/abortGetHeldPins
//   GET  /api/getJoystickCenter
//   GET  /api/getJoystickCenter2       POST /api/setPS4Options
// plus catch-all static GET serving the React bundle from the SPIFFS `www`
// partition (Task 3) mounted at /www.
//
// Tasks 5-6 append endpoint groups to this file: keep the s3_json_get /
// s3_json_post helper shape stable and register new URIs in
// startWebconfigServer() below.
//
// Verification split (per plan): S3 + Pico builds green here; live-curl
// verification of every endpoint added here is deferred to the end-to-end
// task (the server first starts there).

#if defined(ESP_PLATFORM)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>

#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_adc/adc_oneshot.h"
// Task 7 WiFi-AP bring-up (S3 main REQUIRES gains esp_wifi nvs_flash
// esp_netif in this task, so these includes resolve).
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

// spi_flash_get_chip_size() (IDF spi_flash, always linked) forward-declared
// instead of #including "esp_flash.h": the S3 main component does not list
// esp_flash in REQUIRES and this TU must stay self-contained (brief: modify
// src/webconfig_s3.cpp ONLY, no CMakeLists changes).
extern "C" uint32_t spi_flash_get_chip_size(void);

#include <ArduinoJson.h>

#include "config.pb.h"
#include "config_utils.h"
#include "storagemanager.h"
#include "system.h"
#include "eventmanager.h"
#include "GPRestartEvent.h"
#include "version.h" // esp32-s3/generated/version.h (S3 include dir, not Pico's)

// Task 6 mirrors Pico handler bodies field-for-field, so it needs Pico's
// helper/dependency headers (all S3-clean: they compile into the S3 app).
#include "base64.h"
#include "helper.h" // isValidPin() (S3-aware) + animationstation.h/playerleds.h
#include "peripheralmanager.h"
#include "addons/neopicoleds.h" // NeoPicoLEDAddon + LIGHT_DATA_* board presets
#include "addons/input_macro.h" // MAX_MACRO_LIMIT / MAX_MACRO_INPUT_LIMIT
#include "animationstation/animationstation.h"
#include "hal_gpio.h"
#include "hal_time.h"

#ifndef GP2040_BOARDCONFIG
#define GP2040_BOARDCONFIG "Unknown"
#endif

// Mirrors Pico LWIP_HTTPD_POST_MAX_PAYLOAD_LEN (src/webconfig.cpp:42).
#define S3_POST_MAX_PAYLOAD_LEN (1024 * 16)

static const char *S3_WEBCONFIG_TAG = "webconfig_s3";
static httpd_handle_t s3_httpd = nullptr;

// ---- JSON plumbing (stable shape for Tasks 5-6) ----

static esp_err_t s3_send_json(httpd_req_t *req, const std::string &body, const char *status)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, body.c_str(), body.size());
}

static esp_err_t s3_json_get(httpd_req_t *req, std::string (*fn)())
{
    return s3_send_json(req, fn(), "200 OK");
}

// Reads the full POST body into a heap buffer. A 16 KB stack buffer is NOT
// an option: the httpd task stack is 12288 bytes (see startWebconfigServer).
// Returns true on success; on overflow/IO error sends the 400 response and
// returns false.
static bool s3_recv_body(httpd_req_t *req, std::unique_ptr<char[]> &buf, size_t &len)
{
    len = 0;
    if (req->content_len > S3_POST_MAX_PAYLOAD_LEN)
    {
        s3_send_json(req, "{ \"error\": \"payload too large\" }", "400 Bad Request");
        return false;
    }
    buf.reset(new (std::nothrow) char[S3_POST_MAX_PAYLOAD_LEN + 1]);
    if (!buf)
    {
        s3_send_json(req, "{ \"error\": \"out of memory\" }", "500 Internal Server Error");
        return false;
    }
    size_t remaining = (size_t)req->content_len;
    while (remaining > 0)
    {
        int ret = httpd_req_recv(req, buf.get() + len, remaining);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue;
        }
        if (ret <= 0)
        {
            s3_send_json(req, "{ \"error\": \"failed to read request\" }", "400 Bad Request");
            return false;
        }
        len += (size_t)ret;
        remaining -= (size_t)ret;
    }
    buf[len] = '\0';
    return true;
}

static esp_err_t s3_json_post(httpd_req_t *req, std::string (*fn)(const char *body, size_t len))
{
    std::unique_ptr<char[]> buf;
    size_t len = 0;
    if (!s3_recv_body(req, buf, len))
    {
        return ESP_OK; // error response already sent
    }
    return s3_send_json(req, fn(buf.get(), len), "200 OK");
}

// setConfig needs Pico's 200/400/500 outcome mapping (DataAndStatusCode in
// src/webconfig.cpp), so it uses this status-carrying POST variant instead.
enum class S3HttpStatus
{
    Ok,
    BadRequest,
    InternalError,
};

struct S3DataAndStatus
{
    S3DataAndStatus(std::string &&data, S3HttpStatus status) :
        data(std::move(data)),
        status(status)
    {
    }

    std::string data;
    S3HttpStatus status;
};

static esp_err_t s3_json_post_status(httpd_req_t *req, S3DataAndStatus (*fn)(const char *body, size_t len))
{
    std::unique_ptr<char[]> buf;
    size_t len = 0;
    if (!s3_recv_body(req, buf, len))
    {
        return ESP_OK; // error response already sent
    }
    S3DataAndStatus result = fn(buf.get(), len);
    const char *status = "200 OK";
    if (result.status == S3HttpStatus::BadRequest)
    {
        status = "400 Bad Request";
    }
    else if (result.status == S3HttpStatus::InternalError)
    {
        status = "500 Internal Server Error";
    }
    return s3_send_json(req, result.data, status);
}

// ---- Core endpoints (mirror Pico handler bodies field-for-field) ----

static std::string s3_serialize(const DynamicJsonDocument &doc)
{
    std::string data;
    serializeJson(doc, data);
    return data;
}

// Mirrors Pico getFirmwareVersion() keys (src/webconfig.cpp:2924).
static std::string s3_getFirmwareVersion()
{
    const size_t capacity = JSON_OBJECT_SIZE(10);
    DynamicJsonDocument doc(capacity);
    doc["version"] = GP2040VERSION;
    doc["boardArchitecture"] = GP2040PLATFORM;
    doc["boardBuild"] = GP2040BUILD;
    doc["boardBuildType"] = GP2040CONFIG;
    // Same string as the S3 CMakeLists board definition
    // (GP2040_BOARDCONFIG="ESP32S3DevKitC1").
    doc["boardConfigLabel"] = "ESP32S3DevKitC1";
    // Pico reports its UF2 target basename here (BOARD_CONFIG_FILE_NAME);
    // the S3 app image is gp2040-ce-s3 (esp32-s3/CMakeLists.txt project()).
    doc["boardConfigFileName"] = "gp2040-ce-s3";
    doc["boardConfig"] = GP2040_BOARDCONFIG;
    return s3_serialize(doc);
}

static std::string s3_getConfig()
{
    return ConfigUtils::toJSON(Storage::getInstance().getConfig());
}

// Mirrors Pico setConfig() incl. the 200/400/500 mapping (src/webconfig.cpp:3024).
static S3DataAndStatus s3_setConfig(const char *body, size_t len)
{
    // Store config struct on the heap to avoid stack overflow
    std::unique_ptr<Config> config(new Config);
    *config.get() = Config_init_default;
    if (ConfigUtils::fromJSON(*config.get(), body, len))
    {
        Storage::getInstance().getConfig() = *config.get();
        config.reset();
        if (Storage::getInstance().save(true))
        {
            return S3DataAndStatus(s3_getConfig(), S3HttpStatus::Ok);
        }
        else
        {
            return S3DataAndStatus("{ \"error\": \"internal error while saving config\" }",
                S3HttpStatus::InternalError);
        }
    }
    else
    {
        return S3DataAndStatus("{ \"error\": \"invalid JSON document\" }", S3HttpStatus::BadRequest);
    }
}

// Mirrors Pico resetSettings() (src/webconfig.cpp:3049).
static std::string s3_resetSettings(const char *body, size_t len)
{
    (void)body;
    (void)len;
    Storage::getInstance().ResetSettings();
    const size_t capacity = JSON_OBJECT_SIZE(10);
    DynamicJsonDocument doc(capacity);
    doc["success"] = true;
    return s3_serialize(doc);
}

// MUST MATCH web navigation: 0 GAMEPAD | 1 WEBCONFIG | 2 BOOTSEL
// (BOOT_MODES in src/webconfig.cpp:3067). Mirrors Pico reboot()
// (src/webconfig.cpp:3073): the GPRestartEvent is consumed by GP2040's
// delayed-restart handler, which calls System::reboot() — on S3 that packs
// the mode with Task-2 packBootWord() into RTC memory so it survives
// esp_restart(), and the 500 ms delay lets this response flush first.
static std::string s3_reboot(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);
    uint32_t bootMode = doc["bootMode"];
    System::BootMode systemBootMode = System::BootMode::DEFAULT;
    if (bootMode == 0)
    {
        systemBootMode = System::BootMode::GAMEPAD;
    }
    else if (bootMode == 1)
    {
        systemBootMode = System::BootMode::WEBCONFIG;
    }
    else if (bootMode == 2)
    {
        systemBootMode = System::BootMode::USB;
    }
    EventManager::getInstance().triggerEvent(new GPRestartEvent(systemBootMode));
    doc["success"] = true;
    return s3_serialize(doc);
}

// ---- Task 5: gamepad/pins/profiles/keys/bootmode (mirror Pico field-for-field) ----

// Mirrors Pico MAX_MAPPED_INPUT_MODES (src/webconfig.cpp:44).
#ifndef MAX_MAPPED_INPUT_MODES
#define MAX_MAPPED_INPUT_MODES 8
#endif

// Local read/write helpers mirroring Pico's readDoc/writeDoc overloads
// (src/webconfig.cpp:55-215), renamed s3_ for this TU.
template <typename T, typename K>
static void s3_readDoc(T& var, const DynamicJsonDocument& doc, const K& key)
{
    var = doc[key];
}

template <typename T, typename K0, typename K1>
static void s3_readDoc(T& var, const DynamicJsonDocument& doc, const K0& key0, const K1& key1)
{
    var = doc[key0][key1];
}

template <typename T, typename K>
static void s3_writeDoc(DynamicJsonDocument& doc, const K& key, const T& var)
{
    doc[key] = var;
}

template <typename K>
static void s3_writeDoc(DynamicJsonDocument& doc, const K& key, const bool& var)
{
    doc[key] = var ? 1 : 0;
}

template <typename T, typename K0, typename K1>
static void s3_writeDoc(DynamicJsonDocument& doc, const K0& key0, const K1& key1, const T& var)
{
    doc[key0][key1] = var;
}

template <typename T, typename K0, typename K1, typename K2>
static void s3_writeDoc(DynamicJsonDocument& doc, const K0& key0, const K1& key1, const K2& key2, const T& var)
{
    doc[key0][key1][key2] = var;
}

template <typename T, typename K0, typename K1, typename K2, typename K3>
static void s3_writeDoc(DynamicJsonDocument& doc, const K0& key0, const K1& key1, const K2& key2, const K3& key3, const T& var)
{
    doc[key0][key1][key2][key3] = var;
}

template <typename T, typename K0, typename K1, typename K2, typename K3, typename K4>
static void s3_writeDoc(DynamicJsonDocument& doc, const K0& key0, const K1& key1, const K2& key2, const K3& key3, const K4& key4, const T& var)
{
    doc[key0][key1][key2][key3][key4] = var;
}

// Mirrors Pico save_hotkey()/load_hotkey() (src/webconfig.cpp:291-332).
static void s3_save_hotkey(HotkeyEntry* hotkey, const DynamicJsonDocument& doc, const std::string& hotkey_key)
{
    s3_readDoc(hotkey->auxMask, doc, hotkey_key, "auxMask");
    uint32_t buttonsMask = doc[hotkey_key]["buttonsMask"];
    uint32_t dpadMask = 0;
    if (buttonsMask & GAMEPAD_MASK_DU) {
        dpadMask |= GAMEPAD_MASK_UP;
    }
    if (buttonsMask & GAMEPAD_MASK_DD) {
        dpadMask |= GAMEPAD_MASK_DOWN;
    }
    if (buttonsMask & GAMEPAD_MASK_DL) {
        dpadMask |= GAMEPAD_MASK_LEFT;
    }
    if (buttonsMask & GAMEPAD_MASK_DR) {
        dpadMask |= GAMEPAD_MASK_RIGHT;
    }
    buttonsMask &= ~(GAMEPAD_MASK_DU | GAMEPAD_MASK_DD | GAMEPAD_MASK_DL | GAMEPAD_MASK_DR);
    hotkey->dpadMask = dpadMask;
    hotkey->buttonsMask = buttonsMask;
    s3_readDoc(hotkey->action, doc, hotkey_key, "action");
}

static void s3_load_hotkey(const HotkeyEntry* hotkey, DynamicJsonDocument& doc, const std::string& hotkey_key)
{
    s3_writeDoc(doc, hotkey_key, "auxMask", hotkey->auxMask);
    uint32_t buttonsMask = hotkey->buttonsMask;
    if (hotkey->dpadMask & GAMEPAD_MASK_UP) {
        buttonsMask |= GAMEPAD_MASK_DU;
    }
    if (hotkey->dpadMask & GAMEPAD_MASK_DOWN) {
        buttonsMask |= GAMEPAD_MASK_DD;
    }
    if (hotkey->dpadMask & GAMEPAD_MASK_LEFT) {
        buttonsMask |= GAMEPAD_MASK_DL;
    }
    if (hotkey->dpadMask & GAMEPAD_MASK_RIGHT) {
        buttonsMask |= GAMEPAD_MASK_DR;
    }
    s3_writeDoc(doc, hotkey_key, "buttonsMask", buttonsMask);
    s3_writeDoc(doc, hotkey_key, "action", hotkey->action);
}

// Mirrors Pico setGamepadOptions() (src/webconfig.cpp:682-750): same keys,
// same Storage calls, same GPStorageSaveEvent(true) save semantics.
static std::string s3_setGamepadOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();

    s3_readDoc(gamepadOptions.dpadMode, doc, "dpadMode");
    s3_readDoc(gamepadOptions.inputMode, doc, "inputMode");
    s3_readDoc(gamepadOptions.inputDeviceType, doc, "inputDeviceType");
    s3_readDoc(gamepadOptions.socdMode, doc, "socdMode");
    s3_readDoc(gamepadOptions.switchTpShareForDs4, doc, "switchTpShareForDs4");
    s3_readDoc(gamepadOptions.lockHotkeys, doc, "lockHotkeys");
    s3_readDoc(gamepadOptions.fourWayMode, doc, "fourWayMode");
    s3_readDoc(gamepadOptions.profileNumber, doc, "profileNumber");
    s3_readDoc(gamepadOptions.debounceDelay, doc, "debounceDelay");
    s3_readDoc(gamepadOptions.inputModeB1, doc, "inputModeB1");
    s3_readDoc(gamepadOptions.inputModeB2, doc, "inputModeB2");
    s3_readDoc(gamepadOptions.inputModeB3, doc, "inputModeB3");
    s3_readDoc(gamepadOptions.inputModeB4, doc, "inputModeB4");
    s3_readDoc(gamepadOptions.inputModeL1, doc, "inputModeL1");
    s3_readDoc(gamepadOptions.inputModeL2, doc, "inputModeL2");
    s3_readDoc(gamepadOptions.inputModeR1, doc, "inputModeR1");
    s3_readDoc(gamepadOptions.inputModeR2, doc, "inputModeR2");
    s3_readDoc(gamepadOptions.ps4AuthType, doc, "ps4AuthType");
    s3_readDoc(gamepadOptions.ps5AuthType, doc, "ps5AuthType");
    s3_readDoc(gamepadOptions.xinputAuthType, doc, "xinputAuthType");
    s3_readDoc(gamepadOptions.ps4ControllerIDMode, doc, "ps4ControllerIDMode");
    s3_readDoc(gamepadOptions.usbDescOverride, doc, "usbDescOverride");
    s3_readDoc(gamepadOptions.miniMenuGamepadInput, doc, "miniMenuGamepadInput");
    // Copy USB descriptor strings
    size_t strSize = sizeof(gamepadOptions.usbDescManufacturer);
    strncpy(gamepadOptions.usbDescManufacturer, doc["usbDescManufacturer"], strSize - 1);
    gamepadOptions.usbDescManufacturer[strSize - 1] = '\0';
    strSize = sizeof(gamepadOptions.usbDescProduct);
    strncpy(gamepadOptions.usbDescProduct, doc["usbDescProduct"], strSize - 1);
    gamepadOptions.usbDescProduct[strSize - 1] = '\0';
    strSize = sizeof(gamepadOptions.usbDescVersion);
    strncpy(gamepadOptions.usbDescVersion, doc["usbDescVersion"], strSize - 1);
    gamepadOptions.usbDescVersion[strSize - 1] = '\0';
    s3_readDoc(gamepadOptions.usbOverrideID, doc, "usbOverrideID");
    s3_readDoc(gamepadOptions.usbVendorID, doc, "usbVendorID");
    s3_readDoc(gamepadOptions.usbProductID, doc, "usbProductID");

    // Task 7: S3-only WebConfig AP/transport keys (no Pico equivalent — Pico's
    // setGamepadOptions() reads known keys individually (src/webconfig.cpp),
    // so unknown keys in the shared React bundle's POST are ignored there and
    // Pico behavior is untouched). Assign-only-when-set so partial POSTs keep
    // stored values. Same GPStorageSaveEvent(true) save semantics as above.
    WebConfigOptions& webConfigOptions = Storage::getInstance().getConfig().webConfigOptions;
    // (s3_docToValue is defined further down this TU, so the assign-only-
    // when-set is spelled out explicitly here.)
    if (doc["apEnabled"] != nullptr)
    {
        webConfigOptions.apEnabled = doc["apEnabled"];
    }
    if (doc["apSSID"] != nullptr)
    {
        strncpy(webConfigOptions.apSSID, doc["apSSID"], sizeof(webConfigOptions.apSSID) - 1);
        webConfigOptions.apSSID[sizeof(webConfigOptions.apSSID) - 1] = '\0';
    }
    if (doc["apPassphrase"] != nullptr)
    {
        strncpy(webConfigOptions.apPassphrase, doc["apPassphrase"], sizeof(webConfigOptions.apPassphrase) - 1);
        webConfigOptions.apPassphrase[sizeof(webConfigOptions.apPassphrase) - 1] = '\0';
    }
    if (doc["webconfigTransport"] != nullptr)
    {
        webConfigOptions.webconfigTransport = (WebconfigTransport)doc["webconfigTransport"].as<int>();
    }

    HotkeyOptions& hotkeyOptions = Storage::getInstance().getHotkeyOptions();
    s3_save_hotkey(&hotkeyOptions.hotkey01, doc, "hotkey01");
    s3_save_hotkey(&hotkeyOptions.hotkey02, doc, "hotkey02");
    s3_save_hotkey(&hotkeyOptions.hotkey03, doc, "hotkey03");
    s3_save_hotkey(&hotkeyOptions.hotkey04, doc, "hotkey04");
    s3_save_hotkey(&hotkeyOptions.hotkey05, doc, "hotkey05");
    s3_save_hotkey(&hotkeyOptions.hotkey06, doc, "hotkey06");
    s3_save_hotkey(&hotkeyOptions.hotkey07, doc, "hotkey07");
    s3_save_hotkey(&hotkeyOptions.hotkey08, doc, "hotkey08");
    s3_save_hotkey(&hotkeyOptions.hotkey09, doc, "hotkey09");
    s3_save_hotkey(&hotkeyOptions.hotkey10, doc, "hotkey10");
    s3_save_hotkey(&hotkeyOptions.hotkey11, doc, "hotkey11");
    s3_save_hotkey(&hotkeyOptions.hotkey12, doc, "hotkey12");
    s3_save_hotkey(&hotkeyOptions.hotkey13, doc, "hotkey13");
    s3_save_hotkey(&hotkeyOptions.hotkey14, doc, "hotkey14");
    s3_save_hotkey(&hotkeyOptions.hotkey15, doc, "hotkey15");
    s3_save_hotkey(&hotkeyOptions.hotkey16, doc, "hotkey16");

    ForcedSetupOptions& forcedSetupOptions = Storage::getInstance().getForcedSetupOptions();
    s3_readDoc(forcedSetupOptions.mode, doc, "forcedSetupMode");

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return s3_serialize(doc);
}

// Mirrors Pico getGamepadOptions() (src/webconfig.cpp:752-821).
static std::string s3_getGamepadOptions()
{
    const size_t capacity = JSON_OBJECT_SIZE(500);
    DynamicJsonDocument doc(capacity);

    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
    s3_writeDoc(doc, "dpadMode", gamepadOptions.dpadMode);
    s3_writeDoc(doc, "inputMode", gamepadOptions.inputMode);
    s3_writeDoc(doc, "inputDeviceType", gamepadOptions.inputDeviceType);
    s3_writeDoc(doc, "socdMode", gamepadOptions.socdMode);
    s3_writeDoc(doc, "switchTpShareForDs4", gamepadOptions.switchTpShareForDs4 ? 1 : 0);
    s3_writeDoc(doc, "lockHotkeys", gamepadOptions.lockHotkeys ? 1 : 0);
    s3_writeDoc(doc, "fourWayMode", gamepadOptions.fourWayMode ? 1 : 0);
    s3_writeDoc(doc, "profileNumber", gamepadOptions.profileNumber);
    s3_writeDoc(doc, "debounceDelay", gamepadOptions.debounceDelay);
    s3_writeDoc(doc, "inputModeB1", gamepadOptions.inputModeB1);
    s3_writeDoc(doc, "inputModeB2", gamepadOptions.inputModeB2);
    s3_writeDoc(doc, "inputModeB3", gamepadOptions.inputModeB3);
    s3_writeDoc(doc, "inputModeB4", gamepadOptions.inputModeB4);
    s3_writeDoc(doc, "inputModeL1", gamepadOptions.inputModeL1);
    s3_writeDoc(doc, "inputModeL2", gamepadOptions.inputModeL2);
    s3_writeDoc(doc, "inputModeR1", gamepadOptions.inputModeR1);
    s3_writeDoc(doc, "inputModeR2", gamepadOptions.inputModeR2);
    s3_writeDoc(doc, "ps4AuthType", gamepadOptions.ps4AuthType);
    s3_writeDoc(doc, "ps5AuthType", gamepadOptions.ps5AuthType);
    s3_writeDoc(doc, "xinputAuthType", gamepadOptions.xinputAuthType);
    s3_writeDoc(doc, "ps4ControllerIDMode", gamepadOptions.ps4ControllerIDMode);
    s3_writeDoc(doc, "usbDescOverride", gamepadOptions.usbDescOverride);
    s3_writeDoc(doc, "usbDescManufacturer", gamepadOptions.usbDescManufacturer);
    s3_writeDoc(doc, "usbDescProduct", gamepadOptions.usbDescProduct);
    s3_writeDoc(doc, "usbDescVersion", gamepadOptions.usbDescVersion);
    s3_writeDoc(doc, "usbOverrideID", gamepadOptions.usbOverrideID);
    s3_writeDoc(doc, "miniMenuGamepadInput", gamepadOptions.miniMenuGamepadInput);
    // Write USB Vendor ID and Product ID as 4 character hex strings with 0 padding
    char usbVendorStr[5];
    snprintf(usbVendorStr, 5, "%04X", (unsigned int)gamepadOptions.usbVendorID);
    s3_writeDoc(doc, "usbVendorID", usbVendorStr);
    char usbProductStr[5];
    snprintf(usbProductStr, 5, "%04X", (unsigned int)gamepadOptions.usbProductID);
    s3_writeDoc(doc, "usbProductID", usbProductStr);
    // Task 7: S3-only WebConfig AP/transport keys (no Pico equivalent — Pico's
    // GET simply omits them and the shared React bundle falls back to defaults).
    WebConfigOptions& webConfigOptions = Storage::getInstance().getConfig().webConfigOptions;
    s3_writeDoc(doc, "apEnabled", webConfigOptions.apEnabled);
    s3_writeDoc(doc, "apSSID", webConfigOptions.apSSID);
    s3_writeDoc(doc, "apPassphrase", webConfigOptions.apPassphrase);
    s3_writeDoc(doc, "webconfigTransport", webConfigOptions.webconfigTransport);
    s3_writeDoc(doc, "fnButtonPin", -1);
    GpioMappingInfo* gpioMappings = Storage::getInstance().getGpioMappings().pins;
    for (unsigned int pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
        if (gpioMappings[pin].action == GpioAction::BUTTON_PRESS_FN) {
            s3_writeDoc(doc, "fnButtonPin", pin);
        }
    }

    HotkeyOptions& hotkeyOptions = Storage::getInstance().getHotkeyOptions();
    s3_load_hotkey(&hotkeyOptions.hotkey01, doc, "hotkey01");
    s3_load_hotkey(&hotkeyOptions.hotkey02, doc, "hotkey02");
    s3_load_hotkey(&hotkeyOptions.hotkey03, doc, "hotkey03");
    s3_load_hotkey(&hotkeyOptions.hotkey04, doc, "hotkey04");
    s3_load_hotkey(&hotkeyOptions.hotkey05, doc, "hotkey05");
    s3_load_hotkey(&hotkeyOptions.hotkey06, doc, "hotkey06");
    s3_load_hotkey(&hotkeyOptions.hotkey07, doc, "hotkey07");
    s3_load_hotkey(&hotkeyOptions.hotkey08, doc, "hotkey08");
    s3_load_hotkey(&hotkeyOptions.hotkey09, doc, "hotkey09");
    s3_load_hotkey(&hotkeyOptions.hotkey10, doc, "hotkey10");
    s3_load_hotkey(&hotkeyOptions.hotkey11, doc, "hotkey11");
    s3_load_hotkey(&hotkeyOptions.hotkey12, doc, "hotkey12");
    s3_load_hotkey(&hotkeyOptions.hotkey13, doc, "hotkey13");
    s3_load_hotkey(&hotkeyOptions.hotkey14, doc, "hotkey14");
    s3_load_hotkey(&hotkeyOptions.hotkey15, doc, "hotkey15");
    s3_load_hotkey(&hotkeyOptions.hotkey16, doc, "hotkey16");

    ForcedSetupOptions& forcedSetupOptions = Storage::getInstance().getForcedSetupOptions();
    s3_writeDoc(doc, "forcedSetupMode", forcedSetupOptions.mode);
    return s3_serialize(doc);
}

// Mirrors Pico setPinMappings() (src/webconfig.cpp:1474-1501). The pin loop
// is bounded by NUM_BANK0_GPIOS exactly like Pico's; on S3 that macro is 30
// (headers/types.h), so only pin00..pin29 exist here.
static std::string s3_setPinMappings(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    GpioMappings& gpioMappings = Storage::getInstance().getGpioMappings();

    char pinName[6];
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        snprintf(pinName, 6, "pin%02d", (int)pin);
        // setting a pin shouldn't change a new existing addon/reserved pin
        if (gpioMappings.pins[pin].action != GpioAction::RESERVED &&
                gpioMappings.pins[pin].action != GpioAction::ASSIGNED_TO_ADDON &&
                (GpioAction)doc[pinName]["action"] != GpioAction::RESERVED &&
                (GpioAction)doc[pinName]["action"] != GpioAction::ASSIGNED_TO_ADDON) {
            gpioMappings.pins[pin].action = (GpioAction)doc[pinName]["action"];
            gpioMappings.pins[pin].customButtonMask = (uint32_t)doc[pinName]["customButtonMask"];
            gpioMappings.pins[pin].customDpadMask = (uint32_t)doc[pinName]["customDpadMask"];
        }
    }
    size_t profileLabelSize = sizeof(gpioMappings.profileLabel);
    strncpy(gpioMappings.profileLabel, doc["profileLabel"], profileLabelSize - 1);
    gpioMappings.profileLabel[profileLabelSize - 1] = '\0';
    gpioMappings.enabled = doc["enabled"];

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return s3_serialize(doc);
}

// Mirrors Pico getPinMappings() (src/webconfig.cpp:1503-1572). Serializes
// exactly the entries the S3 build owns: pin00..pin29 explicitly, plus the
// same `#if NUM_BANK0_GPIOS > 32` pin30..pin47 block Pico has — which
// compiles out on S3 (NUM_BANK0_GPIOS=30), so no pin48+ entries are invented.
static std::string s3_getPinMappings()
{
    const size_t capacity = JSON_OBJECT_SIZE(500);
    DynamicJsonDocument doc(capacity);

    GpioMappings& gpioMappings = Storage::getInstance().getGpioMappings();

    const auto writePinDoc = [&](const char* key, const GpioMappingInfo& value) -> void
    {
        s3_writeDoc(doc, key, "action", value.action);
        s3_writeDoc(doc, key, "customButtonMask", value.customButtonMask);
        s3_writeDoc(doc, key, "customDpadMask", value.customDpadMask);
    };

    writePinDoc("pin00", gpioMappings.pins[0]);
    writePinDoc("pin01", gpioMappings.pins[1]);
    writePinDoc("pin02", gpioMappings.pins[2]);
    writePinDoc("pin03", gpioMappings.pins[3]);
    writePinDoc("pin04", gpioMappings.pins[4]);
    writePinDoc("pin05", gpioMappings.pins[5]);
    writePinDoc("pin06", gpioMappings.pins[6]);
    writePinDoc("pin07", gpioMappings.pins[7]);
    writePinDoc("pin08", gpioMappings.pins[8]);
    writePinDoc("pin09", gpioMappings.pins[9]);
    writePinDoc("pin10", gpioMappings.pins[10]);
    writePinDoc("pin11", gpioMappings.pins[11]);
    writePinDoc("pin12", gpioMappings.pins[12]);
    writePinDoc("pin13", gpioMappings.pins[13]);
    writePinDoc("pin14", gpioMappings.pins[14]);
    writePinDoc("pin15", gpioMappings.pins[15]);
    writePinDoc("pin16", gpioMappings.pins[16]);
    writePinDoc("pin17", gpioMappings.pins[17]);
    writePinDoc("pin18", gpioMappings.pins[18]);
    writePinDoc("pin19", gpioMappings.pins[19]);
    writePinDoc("pin20", gpioMappings.pins[20]);
    writePinDoc("pin21", gpioMappings.pins[21]);
    writePinDoc("pin22", gpioMappings.pins[22]);
    writePinDoc("pin23", gpioMappings.pins[23]);
    writePinDoc("pin24", gpioMappings.pins[24]);
    writePinDoc("pin25", gpioMappings.pins[25]);
    writePinDoc("pin26", gpioMappings.pins[26]);
    writePinDoc("pin27", gpioMappings.pins[27]);
    writePinDoc("pin28", gpioMappings.pins[28]);
    writePinDoc("pin29", gpioMappings.pins[29]);
#if NUM_BANK0_GPIOS > 32
    writePinDoc("pin30", gpioMappings.pins[30]);
    writePinDoc("pin31", gpioMappings.pins[31]);
    writePinDoc("pin32", gpioMappings.pins[32]);
    writePinDoc("pin33", gpioMappings.pins[33]);
    writePinDoc("pin34", gpioMappings.pins[34]);
    writePinDoc("pin35", gpioMappings.pins[35]);
    writePinDoc("pin36", gpioMappings.pins[36]);
    writePinDoc("pin37", gpioMappings.pins[37]);
    writePinDoc("pin38", gpioMappings.pins[38]);
    writePinDoc("pin39", gpioMappings.pins[39]);
    writePinDoc("pin40", gpioMappings.pins[40]);
    writePinDoc("pin41", gpioMappings.pins[41]);
    writePinDoc("pin42", gpioMappings.pins[42]);
    writePinDoc("pin43", gpioMappings.pins[43]);
    writePinDoc("pin44", gpioMappings.pins[44]);
    writePinDoc("pin45", gpioMappings.pins[45]);
    writePinDoc("pin46", gpioMappings.pins[46]);
    writePinDoc("pin47", gpioMappings.pins[47]);
#endif

    s3_writeDoc(doc, "profileLabel", gpioMappings.profileLabel);
    doc["enabled"] = gpioMappings.enabled;

    return s3_serialize(doc);
}

// Mirrors Pico setProfileOptions() (src/webconfig.cpp:558-600).
static std::string s3_setProfileOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    ProfileOptions& profileOptions = Storage::getInstance().getProfileOptions();
    GpioMappings& coreMappings = Storage::getInstance().getGpioMappings();
    JsonObject options = doc.as<JsonObject>();
    JsonArray alts = options["alternativePinMappings"];
    int altsIndex = 0;
    char pinName[6];
    for (JsonObject alt : alts) {
        for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
            snprintf(pinName, 6, "pin%02d", (int)pin);
            // setting a pin shouldn't change a new existing addon/reserved pin
            // but if the profile definition is new, we should still capture the addon/reserved state
            if (profileOptions.gpioMappingsSets[altsIndex].pins[pin].action != GpioAction::ASSIGNED_TO_ADDON &&
                    profileOptions.gpioMappingsSets[altsIndex].pins[pin].action != GpioAction::RESERVED &&
                    (GpioAction)alt[pinName]["action"] != GpioAction::RESERVED &&
                    (GpioAction)alt[pinName]["action"] != GpioAction::ASSIGNED_TO_ADDON) {
                profileOptions.gpioMappingsSets[altsIndex].pins[pin].action = (GpioAction)alt[pinName]["action"];
                profileOptions.gpioMappingsSets[altsIndex].pins[pin].customButtonMask = (uint32_t)alt[pinName]["customButtonMask"];
                profileOptions.gpioMappingsSets[altsIndex].pins[pin].customDpadMask = (uint32_t)alt[pinName]["customDpadMask"];
            } else if ((coreMappings.pins[pin].action == GpioAction::RESERVED &&
                        (GpioAction)alt[pinName]["action"] == GpioAction::RESERVED) ||
                    (coreMappings.pins[pin].action == GpioAction::ASSIGNED_TO_ADDON &&
                        (GpioAction)alt[pinName]["action"] == GpioAction::ASSIGNED_TO_ADDON)) {
                profileOptions.gpioMappingsSets[altsIndex].pins[pin].action = (GpioAction)alt[pinName]["action"];
            }
        }
        profileOptions.gpioMappingsSets[altsIndex].pins_count = NUM_BANK0_GPIOS;

        size_t profileLabelSize = sizeof(profileOptions.gpioMappingsSets[altsIndex].profileLabel);
        strncpy(profileOptions.gpioMappingsSets[altsIndex].profileLabel, alt["profileLabel"], profileLabelSize - 1);
        profileOptions.gpioMappingsSets[altsIndex].profileLabel[profileLabelSize - 1] = '\0';
        profileOptions.gpioMappingsSets[altsIndex].enabled = alt["enabled"];

        profileOptions.gpioMappingsSets_count = ++altsIndex;
        if (altsIndex > 4) break;
    }

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));
    return s3_serialize(doc);
}

// Mirrors Pico getProfileOptions() (src/webconfig.cpp:602-680). Same S3
// table-size note as s3_getPinMappings: pin00..pin29 plus Pico's identical
// `#if NUM_BANK0_GPIOS > 32` guard (compiles out on S3).
static std::string s3_getProfileOptions()
{
    const size_t capacity = JSON_OBJECT_SIZE(500);
    DynamicJsonDocument doc(capacity);

    const auto writePinDoc = [&](const int item, const char* key, const GpioMappingInfo& value) -> void
    {
        s3_writeDoc(doc, "alternativePinMappings", item, key, "action", value.action);
        s3_writeDoc(doc, "alternativePinMappings", item, key, "customButtonMask", value.customButtonMask);
        s3_writeDoc(doc, "alternativePinMappings", item, key, "customDpadMask", value.customDpadMask);
    };

    ProfileOptions& profileOptions = Storage::getInstance().getProfileOptions();

    // return an empty list if no profiles are currently set, since we no longer populate by default
    if (profileOptions.gpioMappingsSets_count == 0) {
        doc.createNestedArray("alternativePinMappings");
    }

    for (int i = 0; i < profileOptions.gpioMappingsSets_count; i++) {
        // this looks duplicative, but something in arduinojson treats the doc
        // field string by reference so you can't be "clever" and do an snprintf
        // thing or else you only send the last field in the JSON
        writePinDoc(i, "pin00", profileOptions.gpioMappingsSets[i].pins[0]);
        writePinDoc(i, "pin01", profileOptions.gpioMappingsSets[i].pins[1]);
        writePinDoc(i, "pin02", profileOptions.gpioMappingsSets[i].pins[2]);
        writePinDoc(i, "pin03", profileOptions.gpioMappingsSets[i].pins[3]);
        writePinDoc(i, "pin04", profileOptions.gpioMappingsSets[i].pins[4]);
        writePinDoc(i, "pin05", profileOptions.gpioMappingsSets[i].pins[5]);
        writePinDoc(i, "pin06", profileOptions.gpioMappingsSets[i].pins[6]);
        writePinDoc(i, "pin07", profileOptions.gpioMappingsSets[i].pins[7]);
        writePinDoc(i, "pin08", profileOptions.gpioMappingsSets[i].pins[8]);
        writePinDoc(i, "pin09", profileOptions.gpioMappingsSets[i].pins[9]);
        writePinDoc(i, "pin10", profileOptions.gpioMappingsSets[i].pins[10]);
        writePinDoc(i, "pin11", profileOptions.gpioMappingsSets[i].pins[11]);
        writePinDoc(i, "pin12", profileOptions.gpioMappingsSets[i].pins[12]);
        writePinDoc(i, "pin13", profileOptions.gpioMappingsSets[i].pins[13]);
        writePinDoc(i, "pin14", profileOptions.gpioMappingsSets[i].pins[14]);
        writePinDoc(i, "pin15", profileOptions.gpioMappingsSets[i].pins[15]);
        writePinDoc(i, "pin16", profileOptions.gpioMappingsSets[i].pins[16]);
        writePinDoc(i, "pin17", profileOptions.gpioMappingsSets[i].pins[17]);
        writePinDoc(i, "pin18", profileOptions.gpioMappingsSets[i].pins[18]);
        writePinDoc(i, "pin19", profileOptions.gpioMappingsSets[i].pins[19]);
        writePinDoc(i, "pin20", profileOptions.gpioMappingsSets[i].pins[20]);
        writePinDoc(i, "pin21", profileOptions.gpioMappingsSets[i].pins[21]);
        writePinDoc(i, "pin22", profileOptions.gpioMappingsSets[i].pins[22]);
        writePinDoc(i, "pin23", profileOptions.gpioMappingsSets[i].pins[23]);
        writePinDoc(i, "pin24", profileOptions.gpioMappingsSets[i].pins[24]);
        writePinDoc(i, "pin25", profileOptions.gpioMappingsSets[i].pins[25]);
        writePinDoc(i, "pin26", profileOptions.gpioMappingsSets[i].pins[26]);
        writePinDoc(i, "pin27", profileOptions.gpioMappingsSets[i].pins[27]);
        writePinDoc(i, "pin28", profileOptions.gpioMappingsSets[i].pins[28]);
        writePinDoc(i, "pin29", profileOptions.gpioMappingsSets[i].pins[29]);
#if NUM_BANK0_GPIOS > 32
        writePinDoc(i, "pin30", profileOptions.gpioMappingsSets[i].pins[30]);
        writePinDoc(i, "pin31", profileOptions.gpioMappingsSets[i].pins[31]);
        writePinDoc(i, "pin32", profileOptions.gpioMappingsSets[i].pins[32]);
        writePinDoc(i, "pin33", profileOptions.gpioMappingsSets[i].pins[33]);
        writePinDoc(i, "pin34", profileOptions.gpioMappingsSets[i].pins[34]);
        writePinDoc(i, "pin35", profileOptions.gpioMappingsSets[i].pins[35]);
        writePinDoc(i, "pin36", profileOptions.gpioMappingsSets[i].pins[36]);
        writePinDoc(i, "pin37", profileOptions.gpioMappingsSets[i].pins[37]);
        writePinDoc(i, "pin38", profileOptions.gpioMappingsSets[i].pins[38]);
        writePinDoc(i, "pin39", profileOptions.gpioMappingsSets[i].pins[39]);
        writePinDoc(i, "pin40", profileOptions.gpioMappingsSets[i].pins[40]);
        writePinDoc(i, "pin41", profileOptions.gpioMappingsSets[i].pins[41]);
        writePinDoc(i, "pin42", profileOptions.gpioMappingsSets[i].pins[42]);
        writePinDoc(i, "pin43", profileOptions.gpioMappingsSets[i].pins[43]);
        writePinDoc(i, "pin44", profileOptions.gpioMappingsSets[i].pins[44]);
        writePinDoc(i, "pin45", profileOptions.gpioMappingsSets[i].pins[45]);
        writePinDoc(i, "pin46", profileOptions.gpioMappingsSets[i].pins[46]);
        writePinDoc(i, "pin47", profileOptions.gpioMappingsSets[i].pins[47]);
#endif
        s3_writeDoc(doc, "alternativePinMappings", i, "profileLabel", profileOptions.gpioMappingsSets[i].profileLabel);
        doc["alternativePinMappings"][i]["enabled"] = profileOptions.gpioMappingsSets[i].enabled;
    }

    return s3_serialize(doc);
}

// Mirrors Pico setKeyMappings() (src/webconfig.cpp:1624-1666).
static std::string s3_setKeyMappings(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    KeyboardMapping& keyboardMapping = Storage::getInstance().getKeyboardMapping();

    s3_readDoc(keyboardMapping.keyDpadUp, doc, "Up");
    s3_readDoc(keyboardMapping.keyDpadDown, doc, "Down");
    s3_readDoc(keyboardMapping.keyDpadLeft, doc, "Left");
    s3_readDoc(keyboardMapping.keyDpadRight, doc, "Right");
    s3_readDoc(keyboardMapping.keyButtonB1, doc, "B1");
    s3_readDoc(keyboardMapping.keyButtonB2, doc, "B2");
    s3_readDoc(keyboardMapping.keyButtonB3, doc, "B3");
    s3_readDoc(keyboardMapping.keyButtonB4, doc, "B4");
    s3_readDoc(keyboardMapping.keyButtonL1, doc, "L1");
    s3_readDoc(keyboardMapping.keyButtonR1, doc, "R1");
    s3_readDoc(keyboardMapping.keyButtonL2, doc, "L2");
    s3_readDoc(keyboardMapping.keyButtonR2, doc, "R2");
    s3_readDoc(keyboardMapping.keyButtonS1, doc, "S1");
    s3_readDoc(keyboardMapping.keyButtonS2, doc, "S2");
    s3_readDoc(keyboardMapping.keyButtonL3, doc, "L3");
    s3_readDoc(keyboardMapping.keyButtonR3, doc, "R3");
    s3_readDoc(keyboardMapping.keyButtonA1, doc, "A1");
    s3_readDoc(keyboardMapping.keyButtonA2, doc, "A2");
    s3_readDoc(keyboardMapping.keyButtonA3, doc, "A3");
    s3_readDoc(keyboardMapping.keyButtonA4, doc, "A4");
    s3_readDoc(keyboardMapping.keyButtonE1, doc, "E1");
    s3_readDoc(keyboardMapping.keyButtonE2, doc, "E2");
    s3_readDoc(keyboardMapping.keyButtonE3, doc, "E3");
    s3_readDoc(keyboardMapping.keyButtonE4, doc, "E4");
    s3_readDoc(keyboardMapping.keyButtonE5, doc, "E5");
    s3_readDoc(keyboardMapping.keyButtonE6, doc, "E6");
    s3_readDoc(keyboardMapping.keyButtonE7, doc, "E7");
    s3_readDoc(keyboardMapping.keyButtonE8, doc, "E8");
    s3_readDoc(keyboardMapping.keyButtonE9, doc, "E9");
    s3_readDoc(keyboardMapping.keyButtonE10, doc, "E10");
    s3_readDoc(keyboardMapping.keyButtonE11, doc, "E11");
    s3_readDoc(keyboardMapping.keyButtonE12, doc, "E12");

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return s3_serialize(doc);
}

// Mirrors Pico getKeyMappings() (src/webconfig.cpp:1668-1708).
static std::string s3_getKeyMappings()
{
    const size_t capacity = JSON_OBJECT_SIZE(100);
    DynamicJsonDocument doc(capacity);
    const KeyboardMapping& keyboardMapping = Storage::getInstance().getKeyboardMapping();

    s3_writeDoc(doc, "Up", keyboardMapping.keyDpadUp);
    s3_writeDoc(doc, "Down", keyboardMapping.keyDpadDown);
    s3_writeDoc(doc, "Left", keyboardMapping.keyDpadLeft);
    s3_writeDoc(doc, "Right", keyboardMapping.keyDpadRight);
    s3_writeDoc(doc, "B1", keyboardMapping.keyButtonB1);
    s3_writeDoc(doc, "B2", keyboardMapping.keyButtonB2);
    s3_writeDoc(doc, "B3", keyboardMapping.keyButtonB3);
    s3_writeDoc(doc, "B4", keyboardMapping.keyButtonB4);
    s3_writeDoc(doc, "L1", keyboardMapping.keyButtonL1);
    s3_writeDoc(doc, "R1", keyboardMapping.keyButtonR1);
    s3_writeDoc(doc, "L2", keyboardMapping.keyButtonL2);
    s3_writeDoc(doc, "R2", keyboardMapping.keyButtonR2);
    s3_writeDoc(doc, "S1", keyboardMapping.keyButtonS1);
    s3_writeDoc(doc, "S2", keyboardMapping.keyButtonS2);
    s3_writeDoc(doc, "L3", keyboardMapping.keyButtonL3);
    s3_writeDoc(doc, "R3", keyboardMapping.keyButtonR3);
    s3_writeDoc(doc, "A1", keyboardMapping.keyButtonA1);
    s3_writeDoc(doc, "A2", keyboardMapping.keyButtonA2);
    s3_writeDoc(doc, "A3", keyboardMapping.keyButtonA3);
    s3_writeDoc(doc, "A4", keyboardMapping.keyButtonA4);
    s3_writeDoc(doc, "E1", keyboardMapping.keyButtonE1);
    s3_writeDoc(doc, "E2", keyboardMapping.keyButtonE2);
    s3_writeDoc(doc, "E3", keyboardMapping.keyButtonE3);
    s3_writeDoc(doc, "E4", keyboardMapping.keyButtonE4);
    s3_writeDoc(doc, "E5", keyboardMapping.keyButtonE5);
    s3_writeDoc(doc, "E6", keyboardMapping.keyButtonE6);
    s3_writeDoc(doc, "E7", keyboardMapping.keyButtonE7);
    s3_writeDoc(doc, "E8", keyboardMapping.keyButtonE8);
    s3_writeDoc(doc, "E9", keyboardMapping.keyButtonE9);
    s3_writeDoc(doc, "E10", keyboardMapping.keyButtonE10);
    s3_writeDoc(doc, "E11", keyboardMapping.keyButtonE11);
    s3_writeDoc(doc, "E12", keyboardMapping.keyButtonE12);

    return s3_serialize(doc);
}

// Mirrors Pico getBootModeOptions() (src/webconfig.cpp:1574-1595).
static std::string s3_getBootModeOptions() {
    const size_t capacity = JSON_OBJECT_SIZE(100);
    DynamicJsonDocument doc(capacity);

    BootModeOptions& bootModeOptions = Storage::getInstance().getBootModeOptions();
    auto &mappings = bootModeOptions.inputModeMappings;

    s3_writeDoc(doc, "enabled", bootModeOptions.enabled);
    s3_writeDoc(doc, "webConfigPinMask", bootModeOptions.webConfigPinMask);
    s3_writeDoc(doc, "usbModePinMask", bootModeOptions.usbModePinMask);

    if (bootModeOptions.inputModeMappings_count == 0) {
        doc.createNestedArray("inputModeMappings");
    }
    for (int i = 0; i < bootModeOptions.inputModeMappings_count; i++) {
        s3_writeDoc(doc, "inputModeMappings", i, "pinMask", mappings[i].pinMask);
        s3_writeDoc(doc, "inputModeMappings", i, "inputMode", mappings[i].inputMode);
        s3_writeDoc(doc, "inputModeMappings", i, "profileNumber", mappings[i].profileNumber);
    }

    return s3_serialize(doc);
}

// Mirrors Pico setBootModeOptions() (src/webconfig.cpp:1597-1622).
static std::string s3_setBootModeOptions(const char *body, size_t len) {
    BootModeOptions& bootModeOptions = Storage::getInstance().getBootModeOptions();

    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);
    JsonObject options = doc.as<JsonObject>();

    bootModeOptions.enabled = options["enabled"].as<bool>();
    bootModeOptions.webConfigPinMask = options["webConfigPinMask"].as<int32_t>();
    bootModeOptions.usbModePinMask = options["usbModePinMask"].as<int32_t>();

    JsonArray mappings = options["inputModeMappings"];

    size_t i = 0;
    for (JsonObject mapping : mappings) {
        bootModeOptions.inputModeMappings[i].pinMask = mapping["pinMask"].as<int32_t>();
        bootModeOptions.inputModeMappings[i].inputMode = mapping["inputMode"].as<InputMode>();
        bootModeOptions.inputModeMappings[i].profileNumber = mapping["profileNumber"].as<uint32_t>();
        if (++i >= MAX_MAPPED_INPUT_MODES) {
            break;
        }
    }
    bootModeOptions.inputModeMappings_count = i;

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));
    return s3_serialize(doc);
}

// ---- Task 6: LED/display/addons/peripherals (mirror Pico field-for-field) ----

// Pico readDoc 3-key overload (Task 5 already added the 1- and 2-key shapes;
// setDisplayOptions needs doc["buttonLayoutCustomOptions"]["params"][...]).
template <typename T, typename K0, typename K1, typename K2>
static void s3_readDoc(T& var, const DynamicJsonDocument& doc, const K0& key0, const K1& key1, const K2& key2)
{
    var = doc[key0][key1][key2];
}

// Mirrors Pico docToValue() (src/webconfig.cpp:83-110): assign only when set.
template <typename T>
static void s3_docToValue(T& value, const DynamicJsonDocument& doc, const char* key)
{
    if (doc[key] != nullptr)
    {
        value = doc[key];
    }
}

template <typename T>
static void s3_docToValue(T& value, const DynamicJsonDocument& doc, const char* key0, const char* key1)
{
    if (doc[key0][key1] != nullptr)
    {
        value = doc[key0][key1];
    }
}

template <typename T>
static void s3_docToValue(T& value, const DynamicJsonDocument& doc, const char* key0, const char* key1, const char* key2)
{
    if (doc[key0][key1][key2] != nullptr)
    {
        value = doc[key0][key1][key2];
    }
}

// Mirrors Pico cleanAddonGpioMappings() (src/webconfig.cpp:113-139). One S3
// difference: isValidPin() admits S3 GPIOs up to 48 (minus 19/20) while the
// nanopb pin tables are 48 entries, so table marks are clamped to the array
// (pin 48 stays a legal configured value but cannot be table-marked).
static void s3_cleanAddonGpioMappings(Pin_t& addonPin, Pin_t oldAddonPin)
{
    GpioMappingInfo* gpioMappings = Storage::getInstance().getGpioMappings().pins;
    ProfileOptions& profiles = Storage::getInstance().getProfileOptions();

    // if the new addon pin value is valid, mark it assigned in GpioMappings
    if (isValidPin(addonPin))
    {
        if (addonPin >= 0 && addonPin < 48)
        {
            gpioMappings[addonPin].action = GpioAction::ASSIGNED_TO_ADDON;
            profiles.gpioMappingsSets[0].pins[addonPin].action = GpioAction::ASSIGNED_TO_ADDON;
            profiles.gpioMappingsSets[1].pins[addonPin].action = GpioAction::ASSIGNED_TO_ADDON;
            profiles.gpioMappingsSets[2].pins[addonPin].action = GpioAction::ASSIGNED_TO_ADDON;
        }
    } else {
        // -1 is our de facto value for "not assigned" in addons
        addonPin = -1;
    }

    // either way now, the addon's pin config is set to its real value, if the
    // old value is a real pin (and different), we should unset it
    if (isValidPin(oldAddonPin) && oldAddonPin != addonPin)
    {
        if (oldAddonPin >= 0 && oldAddonPin < 48)
        {
            gpioMappings[oldAddonPin].action = GpioAction::NONE;
            profiles.gpioMappingsSets[0].pins[oldAddonPin].action = GpioAction::NONE;
            profiles.gpioMappingsSets[1].pins[oldAddonPin].action = GpioAction::NONE;
            profiles.gpioMappingsSets[2].pins[oldAddonPin].action = GpioAction::NONE;
        }
    }
}

// Mirrors Pico docToPin() (src/webconfig.cpp:142-172).
static void s3_docToPin(Pin_t& pin, const DynamicJsonDocument& doc, const char* key)
{
    Pin_t oldPin = pin;
    if (doc.containsKey(key))
    {
        pin = doc[key];
        s3_cleanAddonGpioMappings(pin, oldPin);
    }
}

static void s3_docToPin(Pin_t& pin, const DynamicJsonDocument& doc, const char* key0, const char* key1, const char* key2)
{
    Pin_t oldPin = pin;
    if (doc.containsKey(key0) && doc[key0].containsKey(key1) && doc[key0][key1].containsKey(key2))
    {
        pin = doc[key0][key1][key2];
        s3_cleanAddonGpioMappings(pin, oldPin);
    }
}

static int32_t s3_cleanPin(int32_t pin) { return isValidPin(pin) ? pin : -1; }

// Mirrors Pico addUsedPinsArray() (src/webconfig.cpp:400-413).
static void s3_addUsedPinsArray(DynamicJsonDocument& doc)
{
    auto usedPins = doc.createNestedArray("usedPins");

    GpioMappingInfo* gpioMappings = Storage::getInstance().getGpioMappings().pins;
    for (unsigned int pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
        // NOTE: addons in webconfig break by seeing their own pins here; if/when they
        // are refactored to ignore their own pins from this list, we can include them
        if (gpioMappings[pin].action != GpioAction::NONE &&
                gpioMappings[pin].action != GpioAction::ASSIGNED_TO_ADDON) {
            usedPins.add(pin);
        }
    }
}

// ---- S3 ADC access (joystick centers + HE trigger voltage) ----
//
// No hardware/adc.h shim exists on S3, so Pico's adc_gpio_init /
// adc_select_input / adc_read sequence is replaced with the S3 oneshot
// backend (hal_esp32s3/hal_adc_s3.cpp, declared here to keep this TU
// self-contained). Only ADC1 (S3 GPIO 1-10) is mapped: ADC2 shares hardware
// with WiFi, which is always up while webconfig runs.
void halAdcInit();
uint16_t halAdcRead(uint8_t gpioPin, adc_channel_t channel);

static int s3_adcChannelForGpio(Pin_t pin)
{
    // ESP32-S3 ADC1: GPIO1 -> CH0 ... GPIO10 -> CH9 (adc_channel_t 0-9).
    if (pin >= 1 && pin <= 10)
    {
        return pin - 1;
    }
    return -1;
}

static bool s3_adcReady = false;

static void s3_adcInitOnce()
{
    if (!s3_adcReady)
    {
        halAdcInit();
        s3_adcReady = true;
    }
}

// Reads one S3 GPIO through ADC1. Returns false when the pin has no ADC1
// channel; callers keep Pico's zero-fill behavior in that case.
static bool s3_adcReadGpio(Pin_t pin, uint16_t &value)
{
    int channel = s3_adcChannelForGpio(pin);
    if (channel < 0)
    {
        return false;
    }
    s3_adcInitOnce();
    value = halAdcRead((uint8_t)pin, (adc_channel_t)channel);
    return true;
}

// Mirrors Pico setLedOptions() (src/webconfig.cpp:823-861): same keys, same
// brightness percent<->0-255 mapping, same PLED pin handling, same save.
static std::string s3_setLedOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);
    LEDOptions& ledOptions = Storage::getInstance().getLedOptions();

    s3_docToPin(ledOptions.dataPin, doc, "dataPin");
    s3_readDoc(ledOptions.ledFormat, doc, "ledFormat");
    s3_readDoc(ledOptions.turnOffWhenSuspended, doc, "turnOffWhenSuspended");

    s3_readDoc(ledOptions.brightnessMaximum, doc, "brightnessMaximum");
    uint32_t checkedBrightnessMax = std::clamp<uint32_t>(ledOptions.brightnessMaximum, 0, 100);
    ledOptions.brightnessMaximum = int(((float)checkedBrightnessMax * 2.55f) +  + 0.5f); //+0.5 to cause it to round to nearest number
    ledOptions.brightnessMaximum = std::clamp<uint32_t>(ledOptions.brightnessMaximum, 0, 255);

    s3_readDoc(ledOptions.pledType, doc, "pledType");
    if(ledOptions.pledType == PLEDType::PLED_TYPE_PWM)
    {
        s3_docToPin(ledOptions.pledPin1, doc, "pledPin1");
        s3_docToPin(ledOptions.pledPin2, doc, "pledPin2");
        s3_docToPin(ledOptions.pledPin3, doc, "pledPin3");
        s3_docToPin(ledOptions.pledPin4, doc, "pledPin4");
    }
    else
    {
        int32_t resetVal = -1;
        s3_cleanAddonGpioMappings(resetVal, ledOptions.pledPin1);
        s3_cleanAddonGpioMappings(resetVal, ledOptions.pledPin2);
        s3_cleanAddonGpioMappings(resetVal, ledOptions.pledPin3);
        s3_cleanAddonGpioMappings(resetVal, ledOptions.pledPin4);

        ledOptions.pledPin1 = resetVal;
        ledOptions.pledPin2 = resetVal;
        ledOptions.pledPin3 = resetVal;
        ledOptions.pledPin4 = resetVal;
    }

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));
    return s3_serialize(doc);
}

// Mirrors Pico getLedOptions() (src/webconfig.cpp:863-883).
static std::string s3_getLedOptions()
{
    const size_t capacity = JSON_OBJECT_SIZE(500);
    DynamicJsonDocument doc(capacity);
    const LEDOptions& ledOptions = Storage::getInstance().getLedOptions();
    s3_writeDoc(doc, "dataPin", s3_cleanPin(ledOptions.dataPin));
    s3_writeDoc(doc, "ledFormat", ledOptions.ledFormat);
    s3_writeDoc(doc, "turnOffWhenSuspended", ledOptions.turnOffWhenSuspended);

    uint32_t adjustedbrightnessMax = (uint32_t)(((float)ledOptions.brightnessMaximum / 2.55f) + 0.5f); //+0.5 to cause it to round to nearest number
    adjustedbrightnessMax = std::clamp<uint32_t>(adjustedbrightnessMax, 0, 100);
    s3_writeDoc(doc, "brightnessMaximum", adjustedbrightnessMax);

    s3_writeDoc(doc, "pledType", ledOptions.pledType);
    s3_writeDoc(doc, "pledPin1", ledOptions.pledPin1);
    s3_writeDoc(doc, "pledPin2", ledOptions.pledPin2);
    s3_writeDoc(doc, "pledPin3", ledOptions.pledPin3);
    s3_writeDoc(doc, "pledPin4", ledOptions.pledPin4);

    return s3_serialize(doc);
}

// Shared core of setDisplayOptions / setPreviewDisplayOptions: mirrors Pico
// setDisplayOptions(DisplayOptions&) (src/webconfig.cpp:430-470).
static void s3_applyDisplayOptions(DisplayOptions& displayOptions, const DynamicJsonDocument& doc)
{
    s3_readDoc(displayOptions.enabled, doc, "enabled");
    s3_readDoc(displayOptions.flip, doc, "flipDisplay");
    s3_readDoc(displayOptions.invert, doc, "invertDisplay");
    s3_readDoc(displayOptions.buttonLayout, doc, "buttonLayout");
    s3_readDoc(displayOptions.buttonLayoutRight, doc, "buttonLayoutRight");
    s3_readDoc(displayOptions.splashMode, doc, "splashMode");
    s3_readDoc(displayOptions.splashChoice, doc, "splashChoice");
    s3_readDoc(displayOptions.splashDuration, doc, "splashDuration");
    s3_readDoc(displayOptions.displaySaverTimeout, doc, "displaySaverTimeout");
    s3_readDoc(displayOptions.displaySaverMode, doc, "displaySaverMode");
    s3_readDoc(displayOptions.buttonLayoutOrientation, doc, "buttonLayoutOrientation");
    s3_readDoc(displayOptions.turnOffWhenSuspended, doc, "turnOffWhenSuspended");
    s3_readDoc(displayOptions.inputMode, doc, "inputMode");
    s3_readDoc(displayOptions.turboMode, doc, "turboMode");
    s3_readDoc(displayOptions.dpadMode, doc, "dpadMode");
    s3_readDoc(displayOptions.socdMode, doc, "socdMode");
    s3_readDoc(displayOptions.macroMode, doc, "macroMode");
    s3_readDoc(displayOptions.profileMode, doc, "profileMode");
    s3_readDoc(displayOptions.inputHistoryEnabled, doc, "inputHistoryEnabled");
    s3_readDoc(displayOptions.inputHistoryLength, doc, "inputHistoryLength");
    s3_readDoc(displayOptions.inputHistoryCol, doc, "inputHistoryCol");
    s3_readDoc(displayOptions.inputHistoryRow, doc, "inputHistoryRow");
    s3_readDoc(displayOptions.contrast, doc, "displayContrast");

    s3_readDoc(displayOptions.buttonLayoutCustomOptions.paramsLeft.layout, doc, "buttonLayoutCustomOptions", "params", "layout");
    s3_readDoc(displayOptions.buttonLayoutCustomOptions.paramsLeft.common.startX, doc, "buttonLayoutCustomOptions", "params", "startX");
    s3_readDoc(displayOptions.buttonLayoutCustomOptions.paramsLeft.common.startY, doc, "buttonLayoutCustomOptions", "params", "startY");
    s3_readDoc(displayOptions.buttonLayoutCustomOptions.paramsLeft.common.buttonRadius, doc, "buttonLayoutCustomOptions", "params", "buttonRadius");
    s3_readDoc(displayOptions.buttonLayoutCustomOptions.paramsLeft.common.buttonPadding, doc, "buttonLayoutCustomOptions", "params", "buttonPadding");

    s3_readDoc(displayOptions.buttonLayoutCustomOptions.paramsRight.layout, doc, "buttonLayoutCustomOptions", "paramsRight", "layout");
    s3_readDoc(displayOptions.buttonLayoutCustomOptions.paramsRight.common.startX, doc, "buttonLayoutCustomOptions", "paramsRight", "startX");
    s3_readDoc(displayOptions.buttonLayoutCustomOptions.paramsRight.common.startY, doc, "buttonLayoutCustomOptions", "paramsRight", "startY");
    s3_readDoc(displayOptions.buttonLayoutCustomOptions.paramsRight.common.buttonRadius, doc, "buttonLayoutCustomOptions", "paramsRight", "buttonRadius");
    s3_readDoc(displayOptions.buttonLayoutCustomOptions.paramsRight.common.buttonPadding, doc, "buttonLayoutCustomOptions", "paramsRight", "buttonPadding");
}

// Mirrors Pico setDisplayOptions() (src/webconfig.cpp:472-477): save.
static std::string s3_setDisplayOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);
    s3_applyDisplayOptions(Storage::getInstance().getDisplayOptions(), doc);
    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));
    return s3_serialize(doc);
}

// Mirrors Pico setPreviewDisplayOptions() (src/webconfig.cpp:479-483):
// RAM-only live preview, no save.
static std::string s3_setPreviewDisplayOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);
    s3_applyDisplayOptions(Storage::getInstance().getDisplayOptions(), doc);
    return s3_serialize(doc);
}

// Mirrors Pico getDisplayOptions() (src/webconfig.cpp:485-527).
static std::string s3_getDisplayOptions()
{
    const size_t capacity = JSON_OBJECT_SIZE(100);
    DynamicJsonDocument doc(capacity);
    const DisplayOptions& displayOptions = Storage::getInstance().getDisplayOptions();
    s3_writeDoc(doc, "enabled", displayOptions.enabled ? 1 : 0);
    s3_writeDoc(doc, "flipDisplay", displayOptions.flip);
    s3_writeDoc(doc, "invertDisplay", displayOptions.invert ? 1 : 0);
    s3_writeDoc(doc, "buttonLayout", displayOptions.buttonLayout);
    s3_writeDoc(doc, "buttonLayoutRight", displayOptions.buttonLayoutRight);
    s3_writeDoc(doc, "splashMode", displayOptions.splashMode);
    s3_writeDoc(doc, "splashChoice", displayOptions.splashChoice);
    s3_writeDoc(doc, "splashDuration", displayOptions.splashDuration);
    s3_writeDoc(doc, "displaySaverTimeout", displayOptions.displaySaverTimeout);
    s3_writeDoc(doc, "displaySaverMode", displayOptions.displaySaverMode);
    s3_writeDoc(doc, "buttonLayoutOrientation", displayOptions.buttonLayoutOrientation);
    s3_writeDoc(doc, "turnOffWhenSuspended", displayOptions.turnOffWhenSuspended);
    s3_writeDoc(doc, "inputMode", displayOptions.inputMode);
    s3_writeDoc(doc, "turboMode", displayOptions.turboMode);
    s3_writeDoc(doc, "dpadMode", displayOptions.dpadMode);
    s3_writeDoc(doc, "socdMode", displayOptions.socdMode);
    s3_writeDoc(doc, "macroMode", displayOptions.macroMode);
    s3_writeDoc(doc, "profileMode", displayOptions.profileMode);
    s3_writeDoc(doc, "inputHistoryEnabled", displayOptions.inputHistoryEnabled);
    s3_writeDoc(doc, "inputHistoryLength", displayOptions.inputHistoryLength);
    s3_writeDoc(doc, "inputHistoryCol", displayOptions.inputHistoryCol);
    s3_writeDoc(doc, "inputHistoryRow", displayOptions.inputHistoryRow);
    s3_writeDoc(doc, "displayContrast", displayOptions.contrast);

    s3_writeDoc(doc, "buttonLayoutCustomOptions", "params", "layout", displayOptions.buttonLayoutCustomOptions.paramsLeft.layout);
    s3_writeDoc(doc, "buttonLayoutCustomOptions", "params", "startX", displayOptions.buttonLayoutCustomOptions.paramsLeft.common.startX);
    s3_writeDoc(doc, "buttonLayoutCustomOptions", "params", "startY", displayOptions.buttonLayoutCustomOptions.paramsLeft.common.startY);
    s3_writeDoc(doc, "buttonLayoutCustomOptions", "params", "buttonRadius", displayOptions.buttonLayoutCustomOptions.paramsLeft.common.buttonRadius);
    s3_writeDoc(doc, "buttonLayoutCustomOptions", "params", "buttonPadding", displayOptions.buttonLayoutCustomOptions.paramsLeft.common.buttonPadding);

    s3_writeDoc(doc, "buttonLayoutCustomOptions", "paramsRight", "layout", displayOptions.buttonLayoutCustomOptions.paramsRight.layout);
    s3_writeDoc(doc, "buttonLayoutCustomOptions", "paramsRight", "startX", displayOptions.buttonLayoutCustomOptions.paramsRight.common.startX);
    s3_writeDoc(doc, "buttonLayoutCustomOptions", "paramsRight", "startY", displayOptions.buttonLayoutCustomOptions.paramsRight.common.startY);
    s3_writeDoc(doc, "buttonLayoutCustomOptions", "paramsRight", "buttonRadius", displayOptions.buttonLayoutCustomOptions.paramsRight.common.buttonRadius);
    s3_writeDoc(doc, "buttonLayoutCustomOptions", "paramsRight", "buttonPadding", displayOptions.buttonLayoutCustomOptions.paramsRight.common.buttonPadding);

    return s3_serialize(doc);
}

// Mirrors Pico getSplashImage() (src/webconfig.cpp:529-537). copyArray is an
// ArduinoJson utility (same vendored copy the S3 build uses).
static std::string s3_getSplashImage()
{
    const DisplayOptions& displayOptions = Storage::getInstance().getDisplayOptions();
    const size_t capacity = JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(displayOptions.splashImage.size);
    DynamicJsonDocument doc(capacity);
    JsonArray splashImageArray = doc.createNestedArray("splashImage");
    copyArray(displayOptions.splashImage.bytes, displayOptions.splashImage.size, splashImageArray);
    return s3_serialize(doc);
}

// Mirrors Pico setSplashImage() (src/webconfig.cpp:539-556): base64 splash
// image, capped at the 1024-byte proto field, save.
static std::string s3_setSplashImage(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    DisplayOptions& displayOptions = Storage::getInstance().getDisplayOptions();

    std::string decoded;
    std::string base64String = doc["splashImage"];
    Base64::Decode(base64String, decoded);
    const size_t length = std::min(decoded.length(), sizeof(displayOptions.splashImage.bytes));

    memcpy(displayOptions.splashImage.bytes, decoded.data(), length);
    displayOptions.splashImage.size = length;

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return s3_serialize(doc);
}

// Mirrors Pico setAddonOptions() (src/webconfig.cpp:2111-2363): the full
// addon set, same keys, same docToPin/docToValue semantics, save.
static std::string s3_setAddonOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    s3_docToPin(analogOptions.analogAdc1PinX, doc, "analogAdc1PinX");
    s3_docToPin(analogOptions.analogAdc1PinY, doc, "analogAdc1PinY");
    s3_docToValue(analogOptions.analogAdc1Mode, doc, "analogAdc1Mode");
    s3_docToValue(analogOptions.analogAdc1Invert, doc, "analogAdc1Invert");
    s3_docToPin(analogOptions.analogAdc2PinX, doc, "analogAdc2PinX");
    s3_docToPin(analogOptions.analogAdc2PinY, doc, "analogAdc2PinY");
    s3_docToValue(analogOptions.analogAdc2Mode, doc, "analogAdc2Mode");
    s3_docToValue(analogOptions.analogAdc2Invert, doc, "analogAdc2Invert");
    s3_docToValue(analogOptions.forced_circularity, doc, "forced_circularity");
    s3_docToValue(analogOptions.forced_circularity2, doc, "forced_circularity2");
    s3_docToValue(analogOptions.inner_deadzone, doc, "inner_deadzone");
    s3_docToValue(analogOptions.inner_deadzone2, doc, "inner_deadzone2");
    s3_docToValue(analogOptions.outer_deadzone, doc, "outer_deadzone");
    s3_docToValue(analogOptions.outer_deadzone2, doc, "outer_deadzone2");
    s3_docToValue(analogOptions.auto_calibrate, doc, "auto_calibrate");
    s3_docToValue(analogOptions.auto_calibrate2, doc, "auto_calibrate2");
    s3_docToValue(analogOptions.joystick_center_x, doc, "joystickCenterX");
    s3_docToValue(analogOptions.joystick_center_y, doc, "joystickCenterY");
    s3_docToValue(analogOptions.joystick_center_x2, doc, "joystickCenterX2");
    s3_docToValue(analogOptions.joystick_center_y2, doc, "joystickCenterY2");
    s3_docToValue(analogOptions.analog_smoothing, doc, "analog_smoothing");
    s3_docToValue(analogOptions.analog_smoothing2, doc, "analog_smoothing2");
    s3_docToValue(analogOptions.smoothing_factor, doc, "smoothing_factor");
    s3_docToValue(analogOptions.smoothing_factor2, doc, "smoothing_factor2");
    s3_docToValue(analogOptions.analog_error, doc, "analog_error");
    s3_docToValue(analogOptions.analog_error2, doc, "analog_error2");
    s3_docToValue(analogOptions.enabled, doc, "AnalogInputEnabled");

    BootselButtonOptions& bootselButtonOptions = Storage::getInstance().getAddonOptions().bootselButtonOptions;
    s3_docToValue(bootselButtonOptions.buttonMap, doc, "bootselButtonMap");
    s3_docToValue(bootselButtonOptions.enabled, doc, "BootselButtonAddonEnabled");

    BuzzerOptions& buzzerOptions = Storage::getInstance().getAddonOptions().buzzerOptions;
    s3_docToPin(buzzerOptions.pin, doc, "buzzerPin");
    s3_docToValue(buzzerOptions.volume, doc, "buzzerVolume");
    s3_docToValue(buzzerOptions.enablePin, doc, "buzzerEnablePin");
    s3_docToValue(buzzerOptions.enabled, doc, "BuzzerSpeakerAddonEnabled");

    DualDirectionalOptions& dualDirectionalOptions = Storage::getInstance().getAddonOptions().dualDirectionalOptions;
    s3_docToValue(dualDirectionalOptions.dpadMode, doc, "dualDirDpadMode");
    s3_docToValue(dualDirectionalOptions.combineMode, doc, "dualDirCombineMode");
    s3_docToValue(dualDirectionalOptions.fourWayMode, doc, "dualDirFourWayMode");
    s3_docToValue(dualDirectionalOptions.enabled, doc, "DualDirectionalInputEnabled");

    TiltOptions& tiltOptions = Storage::getInstance().getAddonOptions().tiltOptions;
    s3_docToValue(tiltOptions.factorTilt1LeftX, doc, "factorTilt1LeftX");
    s3_docToValue(tiltOptions.factorTilt1LeftY, doc, "factorTilt1LeftY");
    s3_docToValue(tiltOptions.factorTilt1RightX, doc, "factorTilt1RightX");
    s3_docToValue(tiltOptions.factorTilt1RightY, doc, "factorTilt1RightY");
    s3_docToValue(tiltOptions.factorTilt2LeftX, doc, "factorTilt2LeftX");
    s3_docToValue(tiltOptions.factorTilt2LeftY, doc, "factorTilt2LeftY");
    s3_docToValue(tiltOptions.factorTilt2RightX, doc, "factorTilt2RightX");
    s3_docToValue(tiltOptions.factorTilt2RightY, doc, "factorTilt2RightY");
    s3_docToValue(tiltOptions.tiltSOCDMode, doc, "tiltSOCDMode");
    s3_docToValue(tiltOptions.enabled, doc, "TiltInputEnabled");

    FocusModeOptions& focusModeOptions = Storage::getInstance().getAddonOptions().focusModeOptions;
    s3_docToValue(focusModeOptions.buttonLockMask, doc, "focusModeButtonLockMask");
    s3_docToValue(focusModeOptions.buttonLockEnabled, doc, "focusModeButtonLockEnabled");
    s3_docToValue(focusModeOptions.macroLockEnabled, doc, "focusModeMacroLockEnabled");
    s3_docToValue(focusModeOptions.enabled, doc, "FocusModeAddonEnabled");

    AnalogADS1115Options& analogADS1115Options = Storage::getInstance().getAddonOptions().analogADS1115Options;
    s3_docToValue(analogADS1115Options.enabled, doc, "I2CAnalog1115InputEnabled");
    s3_docToValue(analogADS1115Options.lxChannel, doc, "lxChannel");
    s3_docToValue(analogADS1115Options.lyChannel, doc, "lyChannel");
    s3_docToValue(analogADS1115Options.rxChannel, doc, "rxChannel");
    s3_docToValue(analogADS1115Options.ryChannel, doc, "ryChannel");
    s3_docToValue(analogADS1115Options.channel0InnerDeadzone, doc, "channel0InnerDeadzone");
    s3_docToValue(analogADS1115Options.channel1InnerDeadzone, doc, "channel1InnerDeadzone");
    s3_docToValue(analogADS1115Options.channel2InnerDeadzone, doc, "channel2InnerDeadzone");
    s3_docToValue(analogADS1115Options.channel3InnerDeadzone, doc, "channel3InnerDeadzone");
    s3_docToValue(analogADS1115Options.channel0OuterDeadzone, doc, "channel0OuterDeadzone");
    s3_docToValue(analogADS1115Options.channel1OuterDeadzone, doc, "channel1OuterDeadzone");
    s3_docToValue(analogADS1115Options.channel2OuterDeadzone, doc, "channel2OuterDeadzone");
    s3_docToValue(analogADS1115Options.channel3OuterDeadzone, doc, "channel3OuterDeadzone");
    s3_docToValue(analogADS1115Options.left_stick_deadzone_enabled, doc, "leftStickDeadzoneEnable");
    s3_docToValue(analogADS1115Options.right_stick_deadzone_enabled, doc, "rightStickDeadzoneEnable");
    s3_docToValue(analogADS1115Options.leftStickDeadzone, doc, "leftStickDeadzone");
    s3_docToValue(analogADS1115Options.rightStickDeadzone, doc, "rightStickDeadzone");

    AnalogADS1219Options& analogADS1219Options = Storage::getInstance().getAddonOptions().analogADS1219Options;
    s3_docToValue(analogADS1219Options.enabled, doc, "I2CAnalog1219InputEnabled");


    ReverseOptions& reverseOptions = Storage::getInstance().getAddonOptions().reverseOptions;
    s3_docToValue(reverseOptions.enabled, doc, "ReverseInputEnabled");
    s3_docToPin(reverseOptions.ledPin, doc, "reversePinLED");
    s3_docToValue(reverseOptions.actionUp, doc, "reverseActionUp");
    s3_docToValue(reverseOptions.actionDown, doc, "reverseActionDown");
    s3_docToValue(reverseOptions.actionLeft, doc, "reverseActionLeft");
    s3_docToValue(reverseOptions.actionRight, doc, "reverseActionRight");

    SOCDSliderOptions& socdSliderOptions = Storage::getInstance().getAddonOptions().socdSliderOptions;
    s3_docToValue(socdSliderOptions.enabled, doc, "SliderSOCDInputEnabled");
    s3_docToValue(socdSliderOptions.modeDefault, doc, "sliderSOCDModeDefault");

    ProfileSliderOptions& profileSliderOptions = Storage::getInstance().getAddonOptions().profileSliderOptions;
    s3_docToValue(profileSliderOptions.enabled, doc, "SliderProfileInputEnabled");
    s3_docToValue(profileSliderOptions.numPositions, doc, "sliderProfileNumPositions");
    s3_docToValue(profileSliderOptions.defaultProfile, doc, "sliderProfileDefaultProfile");
    // Handle profile assignments array
    if (doc.containsKey("sliderProfileAssignments")) {
        JsonArray profileArray = doc["sliderProfileAssignments"];
        profileSliderOptions.profileAssignments_count = std::min(static_cast<size_t>(8), profileArray.size());
        for (size_t i = 0; i < profileSliderOptions.profileAssignments_count; i++) {
            profileSliderOptions.profileAssignments[i] = profileArray[i];
        }
    }

    OnBoardLedOptions& onBoardLedOptions = Storage::getInstance().getAddonOptions().onBoardLedOptions;
    s3_docToValue(onBoardLedOptions.mode, doc, "onBoardLedMode");
    s3_docToValue(onBoardLedOptions.enabled, doc, "BoardLedAddonEnabled");

    TurboOptions& turboOptions = Storage::getInstance().getAddonOptions().turboOptions;
    s3_docToPin(turboOptions.ledPin, doc, "turboPinLED");
    s3_docToValue(turboOptions.shotCount, doc, "turboShotCount");
    s3_docToValue(turboOptions.shmupModeEnabled, doc, "shmupMode");
    s3_docToValue(turboOptions.shmupMixMode, doc, "shmupMixMode");
    s3_docToValue(turboOptions.shmupAlwaysOn1, doc, "shmupAlwaysOn1");
    s3_docToValue(turboOptions.shmupAlwaysOn2, doc, "shmupAlwaysOn2");
    s3_docToValue(turboOptions.shmupAlwaysOn3, doc, "shmupAlwaysOn3");
    s3_docToValue(turboOptions.shmupAlwaysOn4, doc, "shmupAlwaysOn4");
    s3_docToPin(turboOptions.shmupBtn1Pin, doc, "pinShmupBtn1");
    s3_docToPin(turboOptions.shmupBtn2Pin, doc, "pinShmupBtn2");
    s3_docToPin(turboOptions.shmupBtn3Pin, doc, "pinShmupBtn3");
    s3_docToPin(turboOptions.shmupBtn4Pin, doc, "pinShmupBtn4");
    s3_docToValue(turboOptions.shmupBtnMask1, doc, "shmupBtnMask1");
    s3_docToValue(turboOptions.shmupBtnMask2, doc, "shmupBtnMask2");
    s3_docToValue(turboOptions.shmupBtnMask3, doc, "shmupBtnMask3");
    s3_docToValue(turboOptions.shmupBtnMask4, doc, "shmupBtnMask4");
    s3_docToPin(turboOptions.shmupDialPin, doc, "pinShmupDial");
    s3_docToValue(turboOptions.turboLedType, doc, "turboLedType");
    s3_docToValue(turboOptions.enabled, doc, "TurboInputEnabled");

    WiiOptions& wiiOptions = Storage::getInstance().getAddonOptions().wiiOptions;
    s3_docToValue(wiiOptions.enabled, doc, "WiiExtensionAddonEnabled");

    SNESOptions& snesOptions = Storage::getInstance().getAddonOptions().snesOptions;
    s3_docToValue(snesOptions.enabled, doc, "SNESpadAddonEnabled");
    s3_docToPin(snesOptions.clockPin, doc, "snesPadClockPin");
    s3_docToPin(snesOptions.latchPin, doc, "snesPadLatchPin");
    s3_docToPin(snesOptions.dataPin, doc, "snesPadDataPin");

    KeyboardHostOptions& keyboardHostOptions = Storage::getInstance().getAddonOptions().keyboardHostOptions;
    s3_docToValue(keyboardHostOptions.enabled, doc, "KeyboardHostAddonEnabled");
    s3_docToValue(keyboardHostOptions.mapping.keyDpadUp, doc, "keyboardHostMap", "Up");
    s3_docToValue(keyboardHostOptions.mapping.keyDpadDown, doc, "keyboardHostMap", "Down");
    s3_docToValue(keyboardHostOptions.mapping.keyDpadLeft, doc, "keyboardHostMap", "Left");
    s3_docToValue(keyboardHostOptions.mapping.keyDpadRight, doc, "keyboardHostMap", "Right");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonB1, doc, "keyboardHostMap", "B1");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonB2, doc, "keyboardHostMap", "B2");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonB3, doc, "keyboardHostMap", "B3");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonB4, doc, "keyboardHostMap", "B4");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonL1, doc, "keyboardHostMap", "L1");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonR1, doc, "keyboardHostMap", "R1");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonL2, doc, "keyboardHostMap", "L2");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonR2, doc, "keyboardHostMap", "R2");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonS1, doc, "keyboardHostMap", "S1");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonS2, doc, "keyboardHostMap", "S2");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonL3, doc, "keyboardHostMap", "L3");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonR3, doc, "keyboardHostMap", "R3");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonA1, doc, "keyboardHostMap", "A1");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonA2, doc, "keyboardHostMap", "A2");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonA3, doc, "keyboardHostMap", "A3");
    s3_docToValue(keyboardHostOptions.mapping.keyButtonA4, doc, "keyboardHostMap", "A4");
    s3_docToValue(keyboardHostOptions.mouseLeft, doc, "keyboardHostMouseLeft");
    s3_docToValue(keyboardHostOptions.mouseMiddle, doc, "keyboardHostMouseMiddle");
    s3_docToValue(keyboardHostOptions.mouseRight, doc, "keyboardHostMouseRight");
    s3_docToValue(keyboardHostOptions.mouseSensitivity, doc, "keyboardHostMouseSensitivity");
    s3_docToValue(keyboardHostOptions.movementMode, doc, "keyboardHostMouseMovement");

    GamepadUSBHostOptions& gamepadUSBHostOptions = Storage::getInstance().getAddonOptions().gamepadUSBHostOptions;
    s3_docToValue(gamepadUSBHostOptions.enabled, doc, "GamepadUSBHostAddonEnabled");

    AnalogADS1256Options& ads1256Options = Storage::getInstance().getAddonOptions().analogADS1256Options;
    s3_docToValue(ads1256Options.enabled, doc, "Analog1256Enabled");
    s3_docToValue(ads1256Options.spiBlock, doc, "analog1256Block");
    s3_docToPin(ads1256Options.csPin, doc, "analog1256CsPin");
    s3_docToPin(ads1256Options.drdyPin, doc, "analog1256DrdyPin");
    s3_docToValue(ads1256Options.avdd, doc, "analog1256AnalogMax");
    s3_docToValue(ads1256Options.enableTriggers, doc, "analog1256EnableTriggers");

    RotaryOptions& rotaryOptions = Storage::getInstance().getAddonOptions().rotaryOptions;
    s3_docToValue(rotaryOptions.enabled, doc, "RotaryAddonEnabled");
    s3_docToValue(rotaryOptions.encoderOne.enabled, doc, "encoderOneEnabled");
    s3_docToPin(rotaryOptions.encoderOne.pinA, doc, "encoderOnePinA");
    s3_docToPin(rotaryOptions.encoderOne.pinB, doc, "encoderOnePinB");
    s3_docToValue(rotaryOptions.encoderOne.mode, doc, "encoderOneMode");
    s3_docToValue(rotaryOptions.encoderOne.pulsesPerRevolution, doc, "encoderOnePPR");
    s3_docToValue(rotaryOptions.encoderOne.resetAfter, doc, "encoderOneResetAfter");
    s3_docToValue(rotaryOptions.encoderOne.allowWrapAround, doc, "encoderOneAllowWrapAround");
    s3_docToValue(rotaryOptions.encoderOne.multiplier, doc, "encoderOneMultiplier");
    s3_docToValue(rotaryOptions.encoderTwo.enabled, doc, "encoderTwoEnabled");
    s3_docToPin(rotaryOptions.encoderTwo.pinA, doc, "encoderTwoPinA");
    s3_docToPin(rotaryOptions.encoderTwo.pinB, doc, "encoderTwoPinB");
    s3_docToValue(rotaryOptions.encoderTwo.mode, doc, "encoderTwoMode");
    s3_docToValue(rotaryOptions.encoderTwo.pulsesPerRevolution, doc, "encoderTwoPPR");
    s3_docToValue(rotaryOptions.encoderTwo.resetAfter, doc, "encoderTwoResetAfter");
    s3_docToValue(rotaryOptions.encoderTwo.allowWrapAround, doc, "encoderTwoAllowWrapAround");
    s3_docToValue(rotaryOptions.encoderTwo.multiplier, doc, "encoderTwoMultiplier");

    PCF8575Options& pcf8575Options = Storage::getInstance().getAddonOptions().pcf8575Options;
    s3_docToValue(pcf8575Options.enabled, doc, "PCF8575AddonEnabled");

    ReactiveLEDOptions& reactiveLEDOptions = Storage::getInstance().getAddonOptions().reactiveLEDOptions;
    s3_docToValue(reactiveLEDOptions.enabled, doc, "ReactiveLEDAddonEnabled");

    DRV8833RumbleOptions& drv8833RumbleOptions = Storage::getInstance().getAddonOptions().drv8833RumbleOptions;
    s3_docToValue(drv8833RumbleOptions.enabled, doc, "DRV8833RumbleAddonEnabled");
    s3_docToPin(drv8833RumbleOptions.leftMotorPin, doc, "drv8833RumbleLeftMotorPin");
    s3_docToPin(drv8833RumbleOptions.rightMotorPin, doc, "drv8833RumbleRightMotorPin");
    s3_docToPin(drv8833RumbleOptions.motorSleepPin, doc, "drv8833RumbleMotorSleepPin");
    s3_docToValue(drv8833RumbleOptions.pwmFrequency, doc, "drv8833RumblePWMFrequency");
    s3_docToValue(drv8833RumbleOptions.dutyMin, doc, "drv8833RumbleDutyMin");
    s3_docToValue(drv8833RumbleOptions.dutyMax, doc, "drv8833RumbleDutyMax");

    TG16Options& tg16Options = Storage::getInstance().getAddonOptions().tg16Options;
    s3_docToValue(tg16Options.enabled, doc, "TG16padAddonEnabled");
    s3_docToPin(tg16Options.oePin, doc, "tg16PadOePin");
    s3_docToPin(tg16Options.selectPin, doc, "tg16PadSelectPin");
    s3_docToPin(tg16Options.dataPin0, doc, "tg16PadDataPin0");
    s3_docToPin(tg16Options.dataPin1, doc, "tg16PadDataPin1");
    s3_docToPin(tg16Options.dataPin2, doc, "tg16PadDataPin2");
    s3_docToPin(tg16Options.dataPin3, doc, "tg16PadDataPin3");

    HETriggerOptions& heTriggerOptions = Storage::getInstance().getAddonOptions().heTriggerOptions;
    s3_docToValue(heTriggerOptions.enabled, doc, "HETriggerEnabled");
    s3_docToValue(heTriggerOptions.muxChannels, doc, "muxChannels");
    s3_docToPin(heTriggerOptions.selectPin0, doc, "muxSelectPin0");
    s3_docToPin(heTriggerOptions.selectPin1, doc, "muxSelectPin1");
    s3_docToPin(heTriggerOptions.selectPin2, doc, "muxSelectPin2");
    s3_docToPin(heTriggerOptions.selectPin3, doc, "muxSelectPin3");
    s3_docToPin(heTriggerOptions.muxADCPin0, doc, "muxADCPin0");
    s3_docToPin(heTriggerOptions.muxADCPin1, doc, "muxADCPin1");
    s3_docToPin(heTriggerOptions.muxADCPin2, doc, "muxADCPin2");
    s3_docToPin(heTriggerOptions.muxADCPin3, doc, "muxADCPin3");
    s3_docToValue(heTriggerOptions.emaSmoothing, doc, "heTriggerSmoothing");
    s3_docToValue(heTriggerOptions.smoothingFactor, doc, "heTriggerSmoothingFactor");

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return s3_serialize(doc);
}

// Mirrors Pico getAddonOptions() (src/webconfig.cpp:2603-2846).
static std::string s3_getAddonOptions()
{
    const size_t capacity = JSON_OBJECT_SIZE(500);
    DynamicJsonDocument doc(capacity);

    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    s3_writeDoc(doc, "analogAdc1PinX", s3_cleanPin(analogOptions.analogAdc1PinX));
    s3_writeDoc(doc, "analogAdc1PinY", s3_cleanPin(analogOptions.analogAdc1PinY));
    s3_writeDoc(doc, "analogAdc1Mode", analogOptions.analogAdc1Mode);
    s3_writeDoc(doc, "analogAdc1Invert", analogOptions.analogAdc1Invert);
    s3_writeDoc(doc, "analogAdc2PinX", s3_cleanPin(analogOptions.analogAdc2PinX));
    s3_writeDoc(doc, "analogAdc2PinY", s3_cleanPin(analogOptions.analogAdc2PinY));
    s3_writeDoc(doc, "analogAdc2Mode", analogOptions.analogAdc2Mode);
    s3_writeDoc(doc, "analogAdc2Invert", analogOptions.analogAdc2Invert);
    s3_writeDoc(doc, "forced_circularity", analogOptions.forced_circularity);
    s3_writeDoc(doc, "forced_circularity2", analogOptions.forced_circularity2);
    s3_writeDoc(doc, "inner_deadzone", analogOptions.inner_deadzone);
    s3_writeDoc(doc, "inner_deadzone2", analogOptions.inner_deadzone2);
    s3_writeDoc(doc, "outer_deadzone", analogOptions.outer_deadzone);
    s3_writeDoc(doc, "outer_deadzone2", analogOptions.outer_deadzone2);
    s3_writeDoc(doc, "auto_calibrate", analogOptions.auto_calibrate);
    s3_writeDoc(doc, "auto_calibrate2", analogOptions.auto_calibrate2);
    s3_writeDoc(doc, "joystickCenterX", analogOptions.joystick_center_x);
    s3_writeDoc(doc, "joystickCenterY", analogOptions.joystick_center_y);
    s3_writeDoc(doc, "joystickCenterX2", analogOptions.joystick_center_x2);
    s3_writeDoc(doc, "joystickCenterY2", analogOptions.joystick_center_y2);
    s3_writeDoc(doc, "analog_smoothing", analogOptions.analog_smoothing);
    s3_writeDoc(doc, "analog_smoothing2", analogOptions.analog_smoothing2);
    s3_writeDoc(doc, "smoothing_factor", analogOptions.smoothing_factor);
    s3_writeDoc(doc, "smoothing_factor2", analogOptions.smoothing_factor2);
    s3_writeDoc(doc, "analog_error", analogOptions.analog_error);
    s3_writeDoc(doc, "analog_error2", analogOptions.analog_error2);
    s3_writeDoc(doc, "AnalogInputEnabled", analogOptions.enabled);

    const BootselButtonOptions& bootselButtonOptions = Storage::getInstance().getAddonOptions().bootselButtonOptions;
    s3_writeDoc(doc, "bootselButtonMap", bootselButtonOptions.buttonMap);
    s3_writeDoc(doc, "BootselButtonAddonEnabled", bootselButtonOptions.enabled);

    const BuzzerOptions& buzzerOptions = Storage::getInstance().getAddonOptions().buzzerOptions;
    s3_writeDoc(doc, "buzzerPin", s3_cleanPin(buzzerOptions.pin));
    s3_writeDoc(doc, "buzzerVolume", buzzerOptions.volume);
    s3_writeDoc(doc, "buzzerEnablePin", buzzerOptions.enablePin);
    s3_writeDoc(doc, "BuzzerSpeakerAddonEnabled", buzzerOptions.enabled);

    const DualDirectionalOptions& dualDirectionalOptions = Storage::getInstance().getAddonOptions().dualDirectionalOptions;
    s3_writeDoc(doc, "dualDirDpadMode", dualDirectionalOptions.dpadMode);
    s3_writeDoc(doc, "dualDirCombineMode", dualDirectionalOptions.combineMode);
    s3_writeDoc(doc, "dualDirFourWayMode", dualDirectionalOptions.fourWayMode);
    s3_writeDoc(doc, "DualDirectionalInputEnabled", dualDirectionalOptions.enabled);

    const TiltOptions& tiltOptions = Storage::getInstance().getAddonOptions().tiltOptions;
    s3_writeDoc(doc, "factorTilt1LeftX", tiltOptions.factorTilt1LeftX);
    s3_writeDoc(doc, "factorTilt1LeftY", tiltOptions.factorTilt1LeftY);
    s3_writeDoc(doc, "factorTilt1RightX", tiltOptions.factorTilt1RightX);
    s3_writeDoc(doc, "factorTilt1RightY", tiltOptions.factorTilt1RightY);
    s3_writeDoc(doc, "factorTilt2LeftX", tiltOptions.factorTilt2LeftX);
    s3_writeDoc(doc, "factorTilt2LeftY", tiltOptions.factorTilt2LeftY);
    s3_writeDoc(doc, "factorTilt2RightX", tiltOptions.factorTilt2RightX);
    s3_writeDoc(doc, "factorTilt2RightY", tiltOptions.factorTilt2RightY);
    s3_writeDoc(doc, "tiltSOCDMode", tiltOptions.tiltSOCDMode);
    s3_writeDoc(doc, "TiltInputEnabled", tiltOptions.enabled);

    const AnalogADS1219Options& analogADS1219Options = Storage::getInstance().getAddonOptions().analogADS1219Options;
    s3_writeDoc(doc, "I2CAnalog1219InputEnabled", analogADS1219Options.enabled);

    const AnalogADS1115Options& analogADS1115Options = Storage::getInstance().getAddonOptions().analogADS1115Options;
    s3_writeDoc(doc, "I2CAnalog1115InputEnabled", analogADS1115Options.enabled);

    s3_writeDoc(doc, "lxChannel", analogADS1115Options.lxChannel);
    s3_writeDoc(doc, "lyChannel", analogADS1115Options.lyChannel);
    s3_writeDoc(doc, "rxChannel", analogADS1115Options.rxChannel);
    s3_writeDoc(doc, "ryChannel", analogADS1115Options.ryChannel);
    s3_writeDoc(doc, "channel0InnerDeadzone", analogADS1115Options.channel0InnerDeadzone);
    s3_writeDoc(doc, "channel1InnerDeadzone", analogADS1115Options.channel1InnerDeadzone);
    s3_writeDoc(doc, "channel2InnerDeadzone", analogADS1115Options.channel2InnerDeadzone);
    s3_writeDoc(doc, "channel3InnerDeadzone", analogADS1115Options.channel3InnerDeadzone);
    s3_writeDoc(doc, "channel0OuterDeadzone", analogADS1115Options.channel0OuterDeadzone);
    s3_writeDoc(doc, "channel1OuterDeadzone", analogADS1115Options.channel1OuterDeadzone);
    s3_writeDoc(doc, "channel2OuterDeadzone", analogADS1115Options.channel2OuterDeadzone);
    s3_writeDoc(doc, "channel3OuterDeadzone", analogADS1115Options.channel3OuterDeadzone);
    s3_writeDoc(doc, "leftStickDeadzoneEnable", analogADS1115Options.left_stick_deadzone_enabled);
    s3_writeDoc(doc, "rightStickDeadzoneEnable", analogADS1115Options.right_stick_deadzone_enabled);
    s3_writeDoc(doc, "leftStickDeadzone", analogADS1115Options.leftStickDeadzone);
    s3_writeDoc(doc, "rightStickDeadzone", analogADS1115Options.rightStickDeadzone);

    const ReverseOptions& reverseOptions = Storage::getInstance().getAddonOptions().reverseOptions;
    s3_writeDoc(doc, "reversePinLED", s3_cleanPin(reverseOptions.ledPin));
    s3_writeDoc(doc, "reverseActionUp", reverseOptions.actionUp);
    s3_writeDoc(doc, "reverseActionDown", reverseOptions.actionDown);
    s3_writeDoc(doc, "reverseActionLeft", reverseOptions.actionLeft);
    s3_writeDoc(doc, "reverseActionRight", reverseOptions.actionRight);
    s3_writeDoc(doc, "ReverseInputEnabled", reverseOptions.enabled);

    const SOCDSliderOptions& socdSliderOptions = Storage::getInstance().getAddonOptions().socdSliderOptions;
    s3_writeDoc(doc, "sliderSOCDModeDefault", socdSliderOptions.modeDefault);
    s3_writeDoc(doc, "SliderSOCDInputEnabled", socdSliderOptions.enabled);

    const ProfileSliderOptions& profileSliderOptions = Storage::getInstance().getAddonOptions().profileSliderOptions;
    s3_writeDoc(doc, "SliderProfileInputEnabled", profileSliderOptions.enabled);
    s3_writeDoc(doc, "sliderProfileNumPositions", profileSliderOptions.numPositions);
    s3_writeDoc(doc, "sliderProfileDefaultProfile", profileSliderOptions.defaultProfile);
    JsonArray profileAssignmentsArray = doc.createNestedArray("sliderProfileAssignments");
    for (size_t i = 0; i < profileSliderOptions.profileAssignments_count; i++) {
        profileAssignmentsArray.add(profileSliderOptions.profileAssignments[i]);
    }

    const OnBoardLedOptions& onBoardLedOptions = Storage::getInstance().getAddonOptions().onBoardLedOptions;
    s3_writeDoc(doc, "onBoardLedMode", onBoardLedOptions.mode);
    s3_writeDoc(doc, "BoardLedAddonEnabled", onBoardLedOptions.enabled);

    const TurboOptions& turboOptions = Storage::getInstance().getAddonOptions().turboOptions;
    s3_writeDoc(doc, "turboPinLED", s3_cleanPin(turboOptions.ledPin));
    s3_writeDoc(doc, "turboShotCount", turboOptions.shotCount);
    s3_writeDoc(doc, "shmupMode", turboOptions.shmupModeEnabled);
    s3_writeDoc(doc, "shmupMixMode", turboOptions.shmupMixMode);
    s3_writeDoc(doc, "shmupAlwaysOn1", turboOptions.shmupAlwaysOn1);
    s3_writeDoc(doc, "shmupAlwaysOn2", turboOptions.shmupAlwaysOn2);
    s3_writeDoc(doc, "shmupAlwaysOn3", turboOptions.shmupAlwaysOn3);
    s3_writeDoc(doc, "shmupAlwaysOn4", turboOptions.shmupAlwaysOn4);
    s3_writeDoc(doc, "pinShmupBtn1", s3_cleanPin(turboOptions.shmupBtn1Pin));
    s3_writeDoc(doc, "pinShmupBtn2", s3_cleanPin(turboOptions.shmupBtn2Pin));
    s3_writeDoc(doc, "pinShmupBtn3", s3_cleanPin(turboOptions.shmupBtn3Pin));
    s3_writeDoc(doc, "pinShmupBtn4", s3_cleanPin(turboOptions.shmupBtn4Pin));
    s3_writeDoc(doc, "shmupBtnMask1", turboOptions.shmupBtnMask1);
    s3_writeDoc(doc, "shmupBtnMask2", turboOptions.shmupBtnMask2);
    s3_writeDoc(doc, "shmupBtnMask3", turboOptions.shmupBtnMask3);
    s3_writeDoc(doc, "shmupBtnMask4", turboOptions.shmupBtnMask4);
    s3_writeDoc(doc, "pinShmupDial", s3_cleanPin(turboOptions.shmupDialPin));
    s3_writeDoc(doc, "turboLedType", turboOptions.turboLedType);
    s3_writeDoc(doc, "TurboInputEnabled", turboOptions.enabled);

    const WiiOptions& wiiOptions = Storage::getInstance().getAddonOptions().wiiOptions;
    s3_writeDoc(doc, "WiiExtensionAddonEnabled", wiiOptions.enabled);

    const SNESOptions& snesOptions = Storage::getInstance().getAddonOptions().snesOptions;
    s3_writeDoc(doc, "snesPadClockPin", s3_cleanPin(snesOptions.clockPin));
    s3_writeDoc(doc, "snesPadLatchPin", s3_cleanPin(snesOptions.latchPin));
    s3_writeDoc(doc, "snesPadDataPin", s3_cleanPin(snesOptions.dataPin));
    s3_writeDoc(doc, "SNESpadAddonEnabled", snesOptions.enabled);

    const KeyboardHostOptions& keyboardHostOptions = Storage::getInstance().getAddonOptions().keyboardHostOptions;
    s3_writeDoc(doc, "KeyboardHostAddonEnabled", keyboardHostOptions.enabled);
    s3_writeDoc(doc, "keyboardHostMap", "Up", keyboardHostOptions.mapping.keyDpadUp);
    s3_writeDoc(doc, "keyboardHostMap", "Down", keyboardHostOptions.mapping.keyDpadDown);
    s3_writeDoc(doc, "keyboardHostMap", "Left", keyboardHostOptions.mapping.keyDpadLeft);
    s3_writeDoc(doc, "keyboardHostMap", "Right", keyboardHostOptions.mapping.keyDpadRight);
    s3_writeDoc(doc, "keyboardHostMap", "B1", keyboardHostOptions.mapping.keyButtonB1);
    s3_writeDoc(doc, "keyboardHostMap", "B2", keyboardHostOptions.mapping.keyButtonB2);
    s3_writeDoc(doc, "keyboardHostMap", "B3", keyboardHostOptions.mapping.keyButtonB3);
    s3_writeDoc(doc, "keyboardHostMap", "B4", keyboardHostOptions.mapping.keyButtonB4);
    s3_writeDoc(doc, "keyboardHostMap", "L1", keyboardHostOptions.mapping.keyButtonL1);
    s3_writeDoc(doc, "keyboardHostMap", "R1", keyboardHostOptions.mapping.keyButtonR1);
    s3_writeDoc(doc, "keyboardHostMap", "L2", keyboardHostOptions.mapping.keyButtonL2);
    s3_writeDoc(doc, "keyboardHostMap", "R2", keyboardHostOptions.mapping.keyButtonR2);
    s3_writeDoc(doc, "keyboardHostMap", "S1", keyboardHostOptions.mapping.keyButtonS1);
    s3_writeDoc(doc, "keyboardHostMap", "S2", keyboardHostOptions.mapping.keyButtonS2);
    s3_writeDoc(doc, "keyboardHostMap", "L3", keyboardHostOptions.mapping.keyButtonL3);
    s3_writeDoc(doc, "keyboardHostMap", "R3", keyboardHostOptions.mapping.keyButtonR3);
    s3_writeDoc(doc, "keyboardHostMap", "A1", keyboardHostOptions.mapping.keyButtonA1);
    s3_writeDoc(doc, "keyboardHostMap", "A2", keyboardHostOptions.mapping.keyButtonA2);
    s3_writeDoc(doc, "keyboardHostMap", "A3", keyboardHostOptions.mapping.keyButtonA3);
    s3_writeDoc(doc, "keyboardHostMap", "A4", keyboardHostOptions.mapping.keyButtonA4);
    s3_writeDoc(doc, "keyboardHostMouseLeft", keyboardHostOptions.mouseLeft);
    s3_writeDoc(doc, "keyboardHostMouseMiddle", keyboardHostOptions.mouseMiddle);
    s3_writeDoc(doc, "keyboardHostMouseRight", keyboardHostOptions.mouseRight);
    s3_writeDoc(doc, "keyboardHostMouseSensitivity", keyboardHostOptions.mouseSensitivity);
    s3_writeDoc(doc, "keyboardHostMouseMovement", keyboardHostOptions.movementMode);

    const GamepadUSBHostOptions& gamepadUSBHostOptions = Storage::getInstance().getAddonOptions().gamepadUSBHostOptions;
    s3_writeDoc(doc, "GamepadUSBHostAddonEnabled", gamepadUSBHostOptions.enabled);

    AnalogADS1256Options& ads1256Options = Storage::getInstance().getAddonOptions().analogADS1256Options;
    s3_writeDoc(doc, "Analog1256Enabled", ads1256Options.enabled);
    s3_writeDoc(doc, "analog1256Block", ads1256Options.spiBlock);
    s3_writeDoc(doc, "analog1256CsPin", s3_cleanPin(ads1256Options.csPin));
    s3_writeDoc(doc, "analog1256DrdyPin", s3_cleanPin(ads1256Options.drdyPin));
    s3_writeDoc(doc, "analog1256AnalogMax", ads1256Options.avdd);
    s3_writeDoc(doc, "analog1256EnableTriggers", ads1256Options.enableTriggers);

    const FocusModeOptions& focusModeOptions = Storage::getInstance().getAddonOptions().focusModeOptions;
    s3_writeDoc(doc, "focusModeButtonLockMask", focusModeOptions.buttonLockMask);
    s3_writeDoc(doc, "focusModeButtonLockEnabled", focusModeOptions.buttonLockEnabled);
    s3_writeDoc(doc, "focusModeMacroLockEnabled", focusModeOptions.macroLockEnabled);
    s3_writeDoc(doc, "FocusModeAddonEnabled", focusModeOptions.enabled);

    RotaryOptions& rotaryOptions = Storage::getInstance().getAddonOptions().rotaryOptions;
    s3_writeDoc(doc, "RotaryAddonEnabled", rotaryOptions.enabled);
    s3_writeDoc(doc, "encoderOneEnabled", rotaryOptions.encoderOne.enabled);
    s3_writeDoc(doc, "encoderOnePinA", s3_cleanPin(rotaryOptions.encoderOne.pinA));
    s3_writeDoc(doc, "encoderOnePinB", s3_cleanPin(rotaryOptions.encoderOne.pinB));
    s3_writeDoc(doc, "encoderOneMode", rotaryOptions.encoderOne.mode);
    s3_writeDoc(doc, "encoderOnePPR", rotaryOptions.encoderOne.pulsesPerRevolution);
    s3_writeDoc(doc, "encoderOneResetAfter", rotaryOptions.encoderOne.resetAfter);
    s3_writeDoc(doc, "encoderOneAllowWrapAround", rotaryOptions.encoderOne.allowWrapAround);
    s3_writeDoc(doc, "encoderOneMultiplier", rotaryOptions.encoderOne.multiplier);
    s3_writeDoc(doc, "encoderTwoEnabled", rotaryOptions.encoderTwo.enabled);
    s3_writeDoc(doc, "encoderTwoPinA", s3_cleanPin(rotaryOptions.encoderTwo.pinA));
    s3_writeDoc(doc, "encoderTwoPinB", s3_cleanPin(rotaryOptions.encoderTwo.pinB));
    s3_writeDoc(doc, "encoderTwoMode", rotaryOptions.encoderTwo.mode);
    s3_writeDoc(doc, "encoderTwoPPR", rotaryOptions.encoderTwo.pulsesPerRevolution);
    s3_writeDoc(doc, "encoderTwoResetAfter", rotaryOptions.encoderTwo.resetAfter);
    s3_writeDoc(doc, "encoderTwoAllowWrapAround", rotaryOptions.encoderTwo.allowWrapAround);
    s3_writeDoc(doc, "encoderTwoMultiplier", rotaryOptions.encoderTwo.multiplier);

    PCF8575Options& pcf8575Options = Storage::getInstance().getAddonOptions().pcf8575Options;
    s3_writeDoc(doc, "PCF8575AddonEnabled", pcf8575Options.enabled);

    ReactiveLEDOptions& reactiveLEDOptions = Storage::getInstance().getAddonOptions().reactiveLEDOptions;
    s3_writeDoc(doc, "ReactiveLEDAddonEnabled", reactiveLEDOptions.enabled);

    const DRV8833RumbleOptions& drv8833RumbleOptions = Storage::getInstance().getAddonOptions().drv8833RumbleOptions;
    s3_writeDoc(doc, "DRV8833RumbleAddonEnabled", drv8833RumbleOptions.enabled);
    s3_writeDoc(doc, "drv8833RumbleLeftMotorPin", s3_cleanPin(drv8833RumbleOptions.leftMotorPin));
    s3_writeDoc(doc, "drv8833RumbleRightMotorPin", s3_cleanPin(drv8833RumbleOptions.rightMotorPin));
    s3_writeDoc(doc, "drv8833RumbleMotorSleepPin", s3_cleanPin(drv8833RumbleOptions.motorSleepPin));
    s3_writeDoc(doc, "drv8833RumblePWMFrequency", drv8833RumbleOptions.pwmFrequency);
    s3_writeDoc(doc, "drv8833RumbleDutyMin", drv8833RumbleOptions.dutyMin);
    s3_writeDoc(doc, "drv8833RumbleDutyMax", drv8833RumbleOptions.dutyMax);

    TG16Options& tg16Options = Storage::getInstance().getAddonOptions().tg16Options;
    s3_writeDoc(doc, "TG16padAddonEnabled", tg16Options.enabled);
    s3_writeDoc(doc, "tg16PadOePin", s3_cleanPin(tg16Options.oePin));
    s3_writeDoc(doc, "tg16PadSelectPin", s3_cleanPin(tg16Options.selectPin));
    s3_writeDoc(doc, "tg16PadDataPin0", s3_cleanPin(tg16Options.dataPin0));
    s3_writeDoc(doc, "tg16PadDataPin1", s3_cleanPin(tg16Options.dataPin1));
    s3_writeDoc(doc, "tg16PadDataPin2", s3_cleanPin(tg16Options.dataPin2));
    s3_writeDoc(doc, "tg16PadDataPin3", s3_cleanPin(tg16Options.dataPin3));

    const HETriggerOptions& heTriggerOptions = Storage::getInstance().getAddonOptions().heTriggerOptions;
    s3_writeDoc(doc, "HETriggerEnabled", heTriggerOptions.enabled);
    s3_writeDoc(doc, "muxChannels", heTriggerOptions.muxChannels);
    s3_writeDoc(doc, "muxSelectPin0", s3_cleanPin(heTriggerOptions.selectPin0));
    s3_writeDoc(doc, "muxSelectPin1", s3_cleanPin(heTriggerOptions.selectPin1));
    s3_writeDoc(doc, "muxSelectPin2", s3_cleanPin(heTriggerOptions.selectPin2));
    s3_writeDoc(doc, "muxSelectPin3", s3_cleanPin(heTriggerOptions.selectPin3));
    s3_writeDoc(doc, "muxADCPin0", s3_cleanPin(heTriggerOptions.muxADCPin0));
    s3_writeDoc(doc, "muxADCPin1", s3_cleanPin(heTriggerOptions.muxADCPin1));
    s3_writeDoc(doc, "muxADCPin2", s3_cleanPin(heTriggerOptions.muxADCPin2));
    s3_writeDoc(doc, "muxADCPin3", s3_cleanPin(heTriggerOptions.muxADCPin3));
    s3_writeDoc(doc, "heTriggerSmoothing", heTriggerOptions.emaSmoothing);
    s3_writeDoc(doc, "heTriggerSmoothingFactor", heTriggerOptions.smoothingFactor);

    return s3_serialize(doc);
}

// Mirrors Pico setWiiControls() (src/webconfig.cpp:2438-2519): save,
// {"success":true}.
static std::string s3_setWiiControls(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);
    WiiOptions& wiiOptions = Storage::getInstance().getAddonOptions().wiiOptions;

    s3_readDoc(wiiOptions.controllers.nunchuk.buttonC, doc, "nunchuk.buttonC");
    s3_readDoc(wiiOptions.controllers.nunchuk.buttonZ, doc, "nunchuk.buttonZ");
    s3_readDoc(wiiOptions.controllers.nunchuk.stick.x.axisType, doc, "nunchuk.analogStick.x.axisType");
    s3_readDoc(wiiOptions.controllers.nunchuk.stick.y.axisType, doc, "nunchuk.analogStick.y.axisType");

    s3_readDoc(wiiOptions.controllers.classic.buttonA, doc, "classic.buttonA");
    s3_readDoc(wiiOptions.controllers.classic.buttonB, doc, "classic.buttonB");
    s3_readDoc(wiiOptions.controllers.classic.buttonX, doc, "classic.buttonX");
    s3_readDoc(wiiOptions.controllers.classic.buttonY, doc, "classic.buttonY");
    s3_readDoc(wiiOptions.controllers.classic.buttonL, doc, "classic.buttonL");
    s3_readDoc(wiiOptions.controllers.classic.buttonZL, doc, "classic.buttonZL");
    s3_readDoc(wiiOptions.controllers.classic.buttonR, doc, "classic.buttonR");
    s3_readDoc(wiiOptions.controllers.classic.buttonZR, doc, "classic.buttonZR");
    s3_readDoc(wiiOptions.controllers.classic.buttonMinus, doc, "classic.buttonMinus");
    s3_readDoc(wiiOptions.controllers.classic.buttonPlus, doc, "classic.buttonPlus");
    s3_readDoc(wiiOptions.controllers.classic.buttonHome, doc, "classic.buttonHome");
    s3_readDoc(wiiOptions.controllers.classic.buttonUp, doc, "classic.buttonUp");
    s3_readDoc(wiiOptions.controllers.classic.buttonDown, doc, "classic.buttonDown");
    s3_readDoc(wiiOptions.controllers.classic.buttonLeft, doc, "classic.buttonLeft");
    s3_readDoc(wiiOptions.controllers.classic.buttonRight, doc, "classic.buttonRight");
    s3_readDoc(wiiOptions.controllers.classic.leftStick.x.axisType, doc, "classic.analogLeftStick.x.axisType");
    s3_readDoc(wiiOptions.controllers.classic.leftStick.y.axisType, doc, "classic.analogLeftStick.y.axisType");
    s3_readDoc(wiiOptions.controllers.classic.rightStick.x.axisType, doc, "classic.analogRightStick.x.axisType");
    s3_readDoc(wiiOptions.controllers.classic.rightStick.y.axisType, doc, "classic.analogRightStick.y.axisType");
    s3_readDoc(wiiOptions.controllers.classic.leftTrigger.axisType, doc, "classic.analogLeftTrigger.axisType");
    s3_readDoc(wiiOptions.controllers.classic.rightTrigger.axisType, doc, "classic.analogRightTrigger.axisType");

    s3_readDoc(wiiOptions.controllers.taiko.buttonKatLeft, doc, "taiko.buttonKatLeft");
    s3_readDoc(wiiOptions.controllers.taiko.buttonKatRight, doc, "taiko.buttonKatRight");
    s3_readDoc(wiiOptions.controllers.taiko.buttonDonLeft, doc, "taiko.buttonDonLeft");
    s3_readDoc(wiiOptions.controllers.taiko.buttonDonRight, doc, "taiko.buttonDonRight");

    s3_readDoc(wiiOptions.controllers.guitar.buttonRed, doc, "guitar.buttonRed");
    s3_readDoc(wiiOptions.controllers.guitar.buttonGreen, doc, "guitar.buttonGreen");
    s3_readDoc(wiiOptions.controllers.guitar.buttonYellow, doc, "guitar.buttonYellow");
    s3_readDoc(wiiOptions.controllers.guitar.buttonBlue, doc, "guitar.buttonBlue");
    s3_readDoc(wiiOptions.controllers.guitar.buttonOrange, doc, "guitar.buttonOrange");
    s3_readDoc(wiiOptions.controllers.guitar.buttonPedal, doc, "guitar.buttonPedal");
    s3_readDoc(wiiOptions.controllers.guitar.buttonMinus, doc, "guitar.buttonMinus");
    s3_readDoc(wiiOptions.controllers.guitar.buttonPlus, doc, "guitar.buttonPlus");
    s3_readDoc(wiiOptions.controllers.guitar.strumUp, doc, "guitar.buttonStrumUp");
    s3_readDoc(wiiOptions.controllers.guitar.strumDown, doc, "guitar.buttonStrumDown");
    s3_readDoc(wiiOptions.controllers.guitar.stick.x.axisType, doc, "guitar.analogStick.x.axisType");
    s3_readDoc(wiiOptions.controllers.guitar.stick.y.axisType, doc, "guitar.analogStick.y.axisType");
    s3_readDoc(wiiOptions.controllers.guitar.whammyBar.axisType, doc, "guitar.analogWhammyBar.axisType");

    s3_readDoc(wiiOptions.controllers.drum.buttonRed, doc, "drum.buttonRed");
    s3_readDoc(wiiOptions.controllers.drum.buttonGreen, doc, "drum.buttonGreen");
    s3_readDoc(wiiOptions.controllers.drum.buttonYellow, doc, "drum.buttonYellow");
    s3_readDoc(wiiOptions.controllers.drum.buttonBlue, doc, "drum.buttonBlue");
    s3_readDoc(wiiOptions.controllers.drum.buttonOrange, doc, "drum.buttonOrange");
    s3_readDoc(wiiOptions.controllers.drum.buttonPedal, doc, "drum.buttonPedal");
    s3_readDoc(wiiOptions.controllers.drum.buttonMinus, doc, "drum.buttonMinus");
    s3_readDoc(wiiOptions.controllers.drum.buttonPlus, doc, "drum.buttonPlus");
    s3_readDoc(wiiOptions.controllers.drum.stick.x.axisType, doc, "drum.analogStick.x.axisType");
    s3_readDoc(wiiOptions.controllers.drum.stick.y.axisType, doc, "drum.analogStick.y.axisType");

    s3_readDoc(wiiOptions.controllers.turntable.buttonLeftRed, doc, "turntable.buttonLeftRed");
    s3_readDoc(wiiOptions.controllers.turntable.buttonLeftGreen, doc, "turntable.buttonLeftGreen");
    s3_readDoc(wiiOptions.controllers.turntable.buttonLeftBlue, doc, "turntable.buttonLeftBlue");
    s3_readDoc(wiiOptions.controllers.turntable.buttonRightRed, doc, "turntable.buttonRightRed");
    s3_readDoc(wiiOptions.controllers.turntable.buttonRightGreen, doc, "turntable.buttonRightGreen");
    s3_readDoc(wiiOptions.controllers.turntable.buttonRightBlue, doc, "turntable.buttonRightBlue");
    s3_readDoc(wiiOptions.controllers.turntable.buttonMinus, doc, "turntable.buttonMinus");
    s3_readDoc(wiiOptions.controllers.turntable.buttonPlus, doc, "turntable.buttonPlus");
    s3_readDoc(wiiOptions.controllers.turntable.buttonEuphoria, doc, "turntable.buttonEuphoria");
    s3_readDoc(wiiOptions.controllers.turntable.stick.x.axisType, doc, "turntable.analogStick.x.axisType");
    s3_readDoc(wiiOptions.controllers.turntable.stick.y.axisType, doc, "turntable.analogStick.y.axisType");
    s3_readDoc(wiiOptions.controllers.turntable.leftTurntable.axisType, doc, "turntable.analogLeftTurntable.axisType");
    s3_readDoc(wiiOptions.controllers.turntable.rightTurntable.axisType, doc, "turntable.analogRightTurntable.axisType");
    s3_readDoc(wiiOptions.controllers.turntable.effects.axisType, doc, "turntable.analogEffects.axisType");
    s3_readDoc(wiiOptions.controllers.turntable.fader.axisType, doc, "turntable.analogFader.axisType");

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return "{\"success\":true}";
}

// Mirrors Pico getWiiControls() (src/webconfig.cpp:2521-2601), including the
// inherited Pico quirk at src/webconfig.cpp:2594 where stick.y overwrites the
// "turntable.analogStick.x.axisType" key (kept verbatim so S3 matches Pico).
static std::string s3_getWiiControls()
{
    const size_t capacity = JSON_OBJECT_SIZE(100);
    DynamicJsonDocument doc(capacity);
    WiiOptions& wiiOptions = Storage::getInstance().getAddonOptions().wiiOptions;

    s3_writeDoc(doc, "nunchuk.buttonC", wiiOptions.controllers.nunchuk.buttonC);
    s3_writeDoc(doc, "nunchuk.buttonZ", wiiOptions.controllers.nunchuk.buttonZ);
    s3_writeDoc(doc, "nunchuk.analogStick.x.axisType", wiiOptions.controllers.nunchuk.stick.x.axisType);
    s3_writeDoc(doc, "nunchuk.analogStick.y.axisType", wiiOptions.controllers.nunchuk.stick.y.axisType);

    s3_writeDoc(doc, "classic.buttonA", wiiOptions.controllers.classic.buttonA);
    s3_writeDoc(doc, "classic.buttonB", wiiOptions.controllers.classic.buttonB);
    s3_writeDoc(doc, "classic.buttonX", wiiOptions.controllers.classic.buttonX);
    s3_writeDoc(doc, "classic.buttonY", wiiOptions.controllers.classic.buttonY);
    s3_writeDoc(doc, "classic.buttonL", wiiOptions.controllers.classic.buttonL);
    s3_writeDoc(doc, "classic.buttonZL", wiiOptions.controllers.classic.buttonZL);
    s3_writeDoc(doc, "classic.buttonR", wiiOptions.controllers.classic.buttonR);
    s3_writeDoc(doc, "classic.buttonZR", wiiOptions.controllers.classic.buttonZR);
    s3_writeDoc(doc, "classic.buttonMinus", wiiOptions.controllers.classic.buttonMinus);
    s3_writeDoc(doc, "classic.buttonPlus", wiiOptions.controllers.classic.buttonPlus);
    s3_writeDoc(doc, "classic.buttonHome", wiiOptions.controllers.classic.buttonHome);
    s3_writeDoc(doc, "classic.buttonUp", wiiOptions.controllers.classic.buttonUp);
    s3_writeDoc(doc, "classic.buttonDown", wiiOptions.controllers.classic.buttonDown);
    s3_writeDoc(doc, "classic.buttonLeft", wiiOptions.controllers.classic.buttonLeft);
    s3_writeDoc(doc, "classic.buttonRight", wiiOptions.controllers.classic.buttonRight);
    s3_writeDoc(doc, "classic.analogLeftStick.x.axisType", wiiOptions.controllers.classic.leftStick.x.axisType);
    s3_writeDoc(doc, "classic.analogLeftStick.y.axisType", wiiOptions.controllers.classic.leftStick.y.axisType);
    s3_writeDoc(doc, "classic.analogRightStick.x.axisType", wiiOptions.controllers.classic.rightStick.x.axisType);
    s3_writeDoc(doc, "classic.analogRightStick.y.axisType", wiiOptions.controllers.classic.rightStick.y.axisType);
    s3_writeDoc(doc, "classic.analogLeftTrigger.axisType", wiiOptions.controllers.classic.leftTrigger.axisType);
    s3_writeDoc(doc, "classic.analogRightTrigger.axisType", wiiOptions.controllers.classic.rightTrigger.axisType);

    s3_writeDoc(doc, "taiko.buttonKatLeft", wiiOptions.controllers.taiko.buttonKatLeft);
    s3_writeDoc(doc, "taiko.buttonKatRight", wiiOptions.controllers.taiko.buttonKatRight);
    s3_writeDoc(doc, "taiko.buttonDonLeft", wiiOptions.controllers.taiko.buttonDonLeft);
    s3_writeDoc(doc, "taiko.buttonDonRight", wiiOptions.controllers.taiko.buttonDonRight);

    s3_writeDoc(doc, "guitar.buttonRed", wiiOptions.controllers.guitar.buttonRed);
    s3_writeDoc(doc, "guitar.buttonGreen", wiiOptions.controllers.guitar.buttonGreen);
    s3_writeDoc(doc, "guitar.buttonYellow", wiiOptions.controllers.guitar.buttonYellow);
    s3_writeDoc(doc, "guitar.buttonBlue", wiiOptions.controllers.guitar.buttonBlue);
    s3_writeDoc(doc, "guitar.buttonOrange", wiiOptions.controllers.guitar.buttonOrange);
    s3_writeDoc(doc, "guitar.buttonPedal", wiiOptions.controllers.guitar.buttonPedal);
    s3_writeDoc(doc, "guitar.buttonMinus", wiiOptions.controllers.guitar.buttonMinus);
    s3_writeDoc(doc, "guitar.buttonPlus", wiiOptions.controllers.guitar.buttonPlus);
    s3_writeDoc(doc, "guitar.buttonStrumUp", wiiOptions.controllers.guitar.strumUp);
    s3_writeDoc(doc, "guitar.buttonStrumDown", wiiOptions.controllers.guitar.strumDown);
    s3_writeDoc(doc, "guitar.analogStick.x.axisType", wiiOptions.controllers.guitar.stick.x.axisType);
    s3_writeDoc(doc, "guitar.analogStick.y.axisType", wiiOptions.controllers.guitar.stick.y.axisType);
    s3_writeDoc(doc, "guitar.analogWhammyBar.axisType", wiiOptions.controllers.guitar.whammyBar.axisType);

    s3_writeDoc(doc, "drum.buttonRed", wiiOptions.controllers.drum.buttonRed);
    s3_writeDoc(doc, "drum.buttonGreen", wiiOptions.controllers.drum.buttonGreen);
    s3_writeDoc(doc, "drum.buttonYellow", wiiOptions.controllers.drum.buttonYellow);
    s3_writeDoc(doc, "drum.buttonBlue", wiiOptions.controllers.drum.buttonBlue);
    s3_writeDoc(doc, "drum.buttonOrange", wiiOptions.controllers.drum.buttonOrange);
    s3_writeDoc(doc, "drum.buttonPedal", wiiOptions.controllers.drum.buttonPedal);
    s3_writeDoc(doc, "drum.buttonMinus", wiiOptions.controllers.drum.buttonMinus);
    s3_writeDoc(doc, "drum.buttonPlus", wiiOptions.controllers.drum.buttonPlus);
    s3_writeDoc(doc, "drum.analogStick.x.axisType", wiiOptions.controllers.drum.stick.x.axisType);
    s3_writeDoc(doc, "drum.analogStick.y.axisType", wiiOptions.controllers.drum.stick.y.axisType);

    s3_writeDoc(doc, "turntable.buttonLeftRed", wiiOptions.controllers.turntable.buttonLeftRed);
    s3_writeDoc(doc, "turntable.buttonLeftGreen", wiiOptions.controllers.turntable.buttonLeftGreen);
    s3_writeDoc(doc, "turntable.buttonLeftBlue", wiiOptions.controllers.turntable.buttonLeftBlue);
    s3_writeDoc(doc, "turntable.buttonRightRed", wiiOptions.controllers.turntable.buttonRightRed);
    s3_writeDoc(doc, "turntable.buttonRightGreen", wiiOptions.controllers.turntable.buttonRightGreen);
    s3_writeDoc(doc, "turntable.buttonRightBlue", wiiOptions.controllers.turntable.buttonRightBlue);
    s3_writeDoc(doc, "turntable.buttonMinus", wiiOptions.controllers.turntable.buttonMinus);
    s3_writeDoc(doc, "turntable.buttonPlus", wiiOptions.controllers.turntable.buttonPlus);
    s3_writeDoc(doc, "turntable.buttonEuphoria", wiiOptions.controllers.turntable.buttonEuphoria);
    s3_writeDoc(doc, "turntable.analogStick.x.axisType", wiiOptions.controllers.turntable.stick.x.axisType);
    s3_writeDoc(doc, "turntable.analogStick.x.axisType", wiiOptions.controllers.turntable.stick.y.axisType);
    s3_writeDoc(doc, "turntable.analogLeftTurntable.axisType", wiiOptions.controllers.turntable.leftTurntable.axisType);
    s3_writeDoc(doc, "turntable.analogRightTurntable.axisType", wiiOptions.controllers.turntable.rightTurntable.axisType);
    s3_writeDoc(doc, "turntable.analogEffects.axisType", wiiOptions.controllers.turntable.effects.axisType);
    s3_writeDoc(doc, "turntable.analogFader.axisType", wiiOptions.controllers.turntable.fader.axisType);

    return s3_serialize(doc);
}

// Mirrors Pico setMacroAddonOptions() (src/webconfig.cpp:2848-2889): save.
static std::string s3_setMacroAddonOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    MacroOptions& macroOptions = Storage::getInstance().getAddonOptions().macroOptions;
    s3_docToValue(macroOptions.macroBoardLedEnabled, doc, "macroBoardLedEnabled");

    JsonObject options = doc.as<JsonObject>();
    JsonArray macros = options["macroList"];
    int macrosIndex = 0;

    for (JsonObject macro : macros) {
        size_t macroLabelSize = sizeof(macroOptions.macroList[macrosIndex].macroLabel);
        strncpy(macroOptions.macroList[macrosIndex].macroLabel, macro["macroLabel"], macroLabelSize - 1);
        macroOptions.macroList[macrosIndex].macroLabel[macroLabelSize - 1] = '\0';
        macroOptions.macroList[macrosIndex].macroType = macro["macroType"].as<MacroType>();
        macroOptions.macroList[macrosIndex].useMacroTriggerButton = macro["useMacroTriggerButton"].as<bool>();
        macroOptions.macroList[macrosIndex].macroTriggerButton = macro["macroTriggerButton"].as<uint32_t>();
        macroOptions.macroList[macrosIndex].enabled = macro["enabled"] == true;
        macroOptions.macroList[macrosIndex].exclusive = macro["exclusive"] == true;
        macroOptions.macroList[macrosIndex].interruptible = macro["interruptible"] == true;
        macroOptions.macroList[macrosIndex].showFrames = macro["showFrames"] == true;
        JsonArray macroInputs = macro["macroInputs"];
        int macroInputsIndex = 0;

        for (JsonObject input: macroInputs) {
            macroOptions.macroList[macrosIndex].macroInputs[macroInputsIndex].duration = input["duration"].as<uint32_t>();
            macroOptions.macroList[macrosIndex].macroInputs[macroInputsIndex].waitDuration = input["waitDuration"].as<uint32_t>();
            macroOptions.macroList[macrosIndex].macroInputs[macroInputsIndex].buttonMask = input["buttonMask"].as<uint32_t>();
            if (++macroInputsIndex >= MAX_MACRO_INPUT_LIMIT) break;
        }
        macroOptions.macroList[macrosIndex].macroInputs_count = macroInputsIndex;

        if (++macrosIndex >= MAX_MACRO_LIMIT)
            break;
    }

    macroOptions.macroList_count = MAX_MACRO_LIMIT;

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));
    return s3_serialize(doc);
}

// Mirrors Pico getMacroAddonOptions() (src/webconfig.cpp:2891-2922).
static std::string s3_getMacroAddonOptions()
{
    const size_t capacity = JSON_OBJECT_SIZE(500);
    DynamicJsonDocument doc(capacity);

    MacroOptions& macroOptions = Storage::getInstance().getAddonOptions().macroOptions;
    JsonArray macroList = doc.createNestedArray("macroList");

    s3_writeDoc(doc, "macroBoardLedEnabled", macroOptions.macroBoardLedEnabled);

    for (int i = 0; i < MAX_MACRO_LIMIT; i++) {
        JsonObject macro = macroList.createNestedObject();
        macro["enabled"] = macroOptions.macroList[i].enabled ? 1 : 0;
        macro["exclusive"] = macroOptions.macroList[i].exclusive ? 1 : 0;
        macro["interruptible"] = macroOptions.macroList[i].interruptible ? 1 : 0;
        macro["showFrames"] = macroOptions.macroList[i].showFrames ? 1 : 0;
        macro["macroType"] = macroOptions.macroList[i].macroType;
        macro["useMacroTriggerButton"] = macroOptions.macroList[i].useMacroTriggerButton ? 1 : 0;
        macro["macroTriggerButton"] = macroOptions.macroList[i].macroTriggerButton;
        macro["macroLabel"] = macroOptions.macroList[i].macroLabel;

        JsonArray macroInputs = macro.createNestedArray("macroInputs");
        for (int j = 0; j < macroOptions.macroList[i].macroInputs_count; j++) {
            JsonObject macroInput = macroInputs.createNestedObject();
            macroInput["buttonMask"] = macroOptions.macroList[i].macroInputs[j].buttonMask;
            macroInput["duration"] = macroOptions.macroList[i].macroInputs[j].duration;
            macroInput["waitDuration"] = macroOptions.macroList[i].macroInputs[j].waitDuration;
        }
    }

    return s3_serialize(doc);
}

// Mirrors Pico getPeripheralOptions() (src/webconfig.cpp:1710-1744).
static std::string s3_getPeripheralOptions()
{
    const size_t capacity = JSON_OBJECT_SIZE(100);
    DynamicJsonDocument doc(capacity);
    const PeripheralOptions& peripheralOptions = Storage::getInstance().getPeripheralOptions();

    s3_writeDoc(doc, "peripheral", "i2c0", "enabled", peripheralOptions.blockI2C0.enabled);
    s3_writeDoc(doc, "peripheral", "i2c0", "sda",     peripheralOptions.blockI2C0.sda);
    s3_writeDoc(doc, "peripheral", "i2c0", "scl",     peripheralOptions.blockI2C0.scl);
    s3_writeDoc(doc, "peripheral", "i2c0", "speed",   peripheralOptions.blockI2C0.speed);

    s3_writeDoc(doc, "peripheral", "i2c1", "enabled", peripheralOptions.blockI2C1.enabled);
    s3_writeDoc(doc, "peripheral", "i2c1", "sda",     peripheralOptions.blockI2C1.sda);
    s3_writeDoc(doc, "peripheral", "i2c1", "scl",     peripheralOptions.blockI2C1.scl);
    s3_writeDoc(doc, "peripheral", "i2c1", "speed",   peripheralOptions.blockI2C1.speed);

    s3_writeDoc(doc, "peripheral", "spi0", "enabled", peripheralOptions.blockSPI0.enabled);
    s3_writeDoc(doc, "peripheral", "spi0", "rx",      peripheralOptions.blockSPI0.rx);
    s3_writeDoc(doc, "peripheral", "spi0", "cs",      peripheralOptions.blockSPI0.cs);
    s3_writeDoc(doc, "peripheral", "spi0", "sck",     peripheralOptions.blockSPI0.sck);
    s3_writeDoc(doc, "peripheral", "spi0", "tx",      peripheralOptions.blockSPI0.tx);

    s3_writeDoc(doc, "peripheral", "spi1", "enabled", peripheralOptions.blockSPI1.enabled);
    s3_writeDoc(doc, "peripheral", "spi1", "rx",      peripheralOptions.blockSPI1.rx);
    s3_writeDoc(doc, "peripheral", "spi1", "cs",      peripheralOptions.blockSPI1.cs);
    s3_writeDoc(doc, "peripheral", "spi1", "sck",     peripheralOptions.blockSPI1.sck);
    s3_writeDoc(doc, "peripheral", "spi1", "tx",      peripheralOptions.blockSPI1.tx);

    s3_writeDoc(doc, "peripheral", "usb0", "enabled", peripheralOptions.blockUSB0.enabled);
    s3_writeDoc(doc, "peripheral", "usb0", "dp",      peripheralOptions.blockUSB0.dp);
    s3_writeDoc(doc, "peripheral", "usb0", "enable5v",peripheralOptions.blockUSB0.enable5v);
    s3_writeDoc(doc, "peripheral", "usb0", "order",   peripheralOptions.blockUSB0.order);

    return s3_serialize(doc);
}

// Mirrors Pico getI2CPeripheralMap() (src/webconfig.cpp:1746-1767): live scan
// of the enabled I2C blocks via PeripheralManager (S3 I2C backend in SRCS).
static std::string s3_getI2CPeripheralMap() {
    const size_t capacity = JSON_OBJECT_SIZE(500);
    DynamicJsonDocument doc(capacity);

    PeripheralOptions& peripheralOptions = Storage::getInstance().getPeripheralOptions();

    if (peripheralOptions.blockI2C0.enabled && PeripheralManager::getInstance().isI2CEnabled(0)) {
        std::map<uint8_t,bool> result = PeripheralManager::getInstance().getI2C(0)->scan();
        for (std::map<uint8_t,bool>::iterator it = result.begin(); it != result.end(); ++it) {
            s3_writeDoc(doc, "i2c0", std::to_string(it->first), it->second);
        }
    }

    if (peripheralOptions.blockI2C1.enabled && PeripheralManager::getInstance().isI2CEnabled(1)) {
        std::map<uint8_t,bool> result = PeripheralManager::getInstance().getI2C(1)->scan();
        for (std::map<uint8_t,bool>::iterator it = result.begin(); it != result.end(); ++it) {
            s3_writeDoc(doc, "i2c1", std::to_string(it->first), it->second);
        }
    }

    return s3_serialize(doc);
}

// Mirrors Pico setPeripheralOptions() (src/webconfig.cpp:1769-1828) with one
// deliberate S3 exception: Pico's USB D+/- adjacent-pin reservation block is
// SKIPPED (no USB-PIO init exists on S3). The dp value itself is still stored
// via s3_docToPin. Save semantics unchanged.
static std::string s3_setPeripheralOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    PeripheralOptions& peripheralOptions = Storage::getInstance().getPeripheralOptions();

    s3_docToValue(peripheralOptions.blockI2C0.enabled, doc, "peripheral", "i2c0", "enabled");
    s3_docToPin(peripheralOptions.blockI2C0.sda, doc, "peripheral", "i2c0", "sda");
    s3_docToPin(peripheralOptions.blockI2C0.scl, doc, "peripheral", "i2c0", "scl");
    s3_docToValue(peripheralOptions.blockI2C0.speed, doc, "peripheral", "i2c0", "speed");

    s3_docToValue(peripheralOptions.blockI2C1.enabled, doc, "peripheral", "i2c1", "enabled");
    s3_docToPin(peripheralOptions.blockI2C1.sda, doc, "peripheral", "i2c1", "sda");
    s3_docToPin(peripheralOptions.blockI2C1.scl, doc, "peripheral", "i2c1", "scl");
    s3_docToValue(peripheralOptions.blockI2C1.speed, doc, "peripheral", "i2c1", "speed");

    s3_docToValue(peripheralOptions.blockSPI0.enabled, doc,  "peripheral", "spi0", "enabled");
    s3_docToPin(peripheralOptions.blockSPI0.rx, doc,  "peripheral", "spi0", "rx");
    s3_docToPin(peripheralOptions.blockSPI0.cs, doc,  "peripheral", "spi0", "cs");
    s3_docToPin(peripheralOptions.blockSPI0.sck, doc, "peripheral", "spi0", "sck");
    s3_docToPin(peripheralOptions.blockSPI0.tx, doc,  "peripheral", "spi0", "tx");

    s3_docToValue(peripheralOptions.blockSPI1.enabled, doc,  "peripheral", "spi1", "enabled");
    s3_docToPin(peripheralOptions.blockSPI1.rx, doc,  "peripheral", "spi1", "rx");
    s3_docToPin(peripheralOptions.blockSPI1.cs, doc,  "peripheral", "spi1", "cs");
    s3_docToPin(peripheralOptions.blockSPI1.sck, doc, "peripheral", "spi1", "sck");
    s3_docToPin(peripheralOptions.blockSPI1.tx, doc,  "peripheral", "spi1", "tx");

    s3_docToValue(peripheralOptions.blockUSB0.enabled, doc, "peripheral", "usb0", "enabled");
    s3_docToValue(peripheralOptions.blockUSB0.enable5v, doc, "peripheral", "usb0", "enable5v");
    s3_docToValue(peripheralOptions.blockUSB0.order, doc, "peripheral", "usb0", "order");

    s3_docToPin(peripheralOptions.blockUSB0.dp, doc, "peripheral", "usb0", "dp");
    // NOTE (S3): Pico reserves the D- neighbor pin here for its PIO-USB
    // init; the S3 USB device runs on the native peripheral, so no pin
    // reservation exists to maintain.

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return s3_serialize(doc);
}

// Mirrors Pico getExpansionPins() (src/webconfig.cpp:1830-1868).
static std::string s3_getExpansionPins()
{
    const size_t capacity = JSON_OBJECT_SIZE(100);
    DynamicJsonDocument doc(capacity);
    GpioMappingInfo* gpioMappings = Storage::getInstance().getAddonOptions().pcf8575Options.pins;
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin00", "option", gpioMappings[0].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin00", "direction", gpioMappings[0].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin01", "option", gpioMappings[1].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin01", "direction", gpioMappings[1].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin02", "option", gpioMappings[2].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin02", "direction", gpioMappings[2].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin03", "option", gpioMappings[3].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin03", "direction", gpioMappings[3].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin04", "option", gpioMappings[4].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin04", "direction", gpioMappings[4].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin05", "option", gpioMappings[5].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin05", "direction", gpioMappings[5].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin06", "option", gpioMappings[6].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin06", "direction", gpioMappings[6].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin07", "option", gpioMappings[7].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin07", "direction", gpioMappings[7].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin08", "option", gpioMappings[8].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin08", "direction", gpioMappings[8].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin09", "option", gpioMappings[9].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin09", "direction", gpioMappings[9].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin10", "option", gpioMappings[10].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin10", "direction", gpioMappings[10].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin11", "option", gpioMappings[11].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin11", "direction", gpioMappings[11].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin12", "option", gpioMappings[12].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin12", "direction", gpioMappings[12].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin13", "option", gpioMappings[13].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin13", "direction", gpioMappings[13].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin14", "option", gpioMappings[14].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin14", "direction", gpioMappings[14].direction);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin15", "option", gpioMappings[15].action);
    s3_writeDoc(doc, "pins", "pcf8575", 0, "pin15", "direction", gpioMappings[15].direction);
    return s3_serialize(doc);
}

// Mirrors Pico setExpansionPins() (src/webconfig.cpp:1870-1893): save.
static std::string s3_setExpansionPins(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    GpioMappingInfo* gpioMappings = Storage::getInstance().getAddonOptions().pcf8575Options.pins;

    char pinName[6];
    for (uint16_t pin = 0; pin < 16; pin++) {
        snprintf(pinName, 6, "pin%0*d", 2, pin);
        // setting a pin shouldn't change a new existing addon/reserved pin
        if (gpioMappings[pin].action != GpioAction::RESERVED &&
                gpioMappings[pin].action != GpioAction::ASSIGNED_TO_ADDON &&
                (GpioAction)doc["pins"]["pcf8575"][0][pinName]["option"] != GpioAction::RESERVED &&
                (GpioAction)doc["pins"]["pcf8575"][0][pinName]["option"] != GpioAction::ASSIGNED_TO_ADDON) {
            gpioMappings[pin].action = (GpioAction)doc["pins"]["pcf8575"][0][pinName]["option"];
            gpioMappings[pin].direction = (GpioDirection)doc["pins"]["pcf8575"][0][pinName]["direction"];
        }
    }
    Storage::getInstance().getAddonOptions().pcf8575Options.pins_count = 16;

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return s3_serialize(doc);
}

// HE trigger calibration globals (mirrors Pico src/webconfig.cpp:1895-1901).
// RAM-only: set by setHETriggerOptions, consumed by getHETriggerVoltage.
static uint32_t s3_calibrationMuxChannels = 0;
static Pin_t s3_calibrationSelectPins[4];
static Pin_t s3_calibrationADCPins[4];
static bool s3_calibrationSmoothing = false;
static uint32_t s3_calibrationSmoothingFactor = 0;
static float s3_ema_smoothing;
static uint32_t s3_smoothingRead = 0;

// 12-bit ADC scale shared with the S3 oneshot backend (ATTEN_DB_11,
// BITWIDTH_12 in hal_adc_s3.cpp); mirrors Pico's ADC_MAX (4095).
static const uint16_t S3_HE_ADC_MAX = 4095;

// Mirrors Pico setHETriggerOptions() (src/webconfig.cpp:1904-1946): RAM-only
// calibration globals, no save. S3 difference: select pins become HAL GPIO
// outputs; ADC pins need no per-pin init (oneshot configures per read).
static std::string s3_setHETriggerOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);
    s3_calibrationMuxChannels = doc["muxChannels"];
    s3_calibrationSelectPins[0] = doc["muxSelectPin0"];
    s3_calibrationSelectPins[1] = doc["muxSelectPin1"];
    s3_calibrationSelectPins[2] = doc["muxSelectPin2"];
    s3_calibrationSelectPins[3] = doc["muxSelectPin3"];

    s3_calibrationADCPins[0] = doc["muxADCPin0"];
    s3_calibrationADCPins[1] = doc["muxADCPin1"];
    s3_calibrationADCPins[2] = doc["muxADCPin2"];
    s3_calibrationADCPins[3] = doc["muxADCPin3"];

    s3_calibrationSmoothing = doc["heTriggerSmoothing"];
    s3_calibrationSmoothingFactor = doc["heTriggerSmoothingFactor"];
    s3_ema_smoothing = (float)s3_calibrationSmoothingFactor / 100.f; // 99 = max smoothing factor

    for (int i = 0; i < 4; i++) {
        if (isValidPin(s3_calibrationSelectPins[i])) {
            hal::gpioSetOutput((uint8_t)s3_calibrationSelectPins[i]);
            hal::gpioPut((uint8_t)s3_calibrationSelectPins[i], false);
        }
    }

    return s3_serialize(doc);
}

// Mirrors Pico emaCalculation() (src/webconfig.cpp:1949-1953).
static uint16_t s3_heEmaCalculation(uint16_t value, uint16_t previous) {
    float ema_value = (float)value / S3_HE_ADC_MAX;
    float ema_previous = (float)previous / S3_HE_ADC_MAX;
    return (uint16_t)(((s3_ema_smoothing*ema_value) + ((1.0f-s3_ema_smoothing) * ema_previous)) * S3_HE_ADC_MAX);
}

// Mirrors Pico getHETriggerVoltage() (src/webconfig.cpp:1956-2027):
// {targetId} -> {voltage} or {error}, live mux/ADC read, no save. S3
// differences: mux select via hal::gpioPut, ADC via the ADC1 oneshot backend
// (pins without an ADC1 channel report "adc pin out of range").
static std::string s3_getHETriggerVoltage(const char *body, size_t len)
{
    DynamicJsonDocument postDoc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(postDoc, body, len);
    uint32_t id = postDoc["targetId"];
    const size_t capacity = JSON_OBJECT_SIZE(20);
    DynamicJsonDocument doc(capacity);
    uint32_t adcSelectPin = 0;

    // Mux Channels determines how many select pins we use
    if (s3_calibrationMuxChannels == 1) {
        if ( id > 3 ) {
            doc["error"] = "id out of range";
            return s3_serialize(doc);
        }
        adcSelectPin = s3_calibrationADCPins[id];
    } else if ( s3_calibrationMuxChannels == 4) {
        uint32_t adcNum = id / 4;
        uint32_t channel = (id % 4);
        if ( adcNum > 3 ) {
            doc["error"] = "id out of 4-channel mux range";
            return s3_serialize(doc);
        }
        adcSelectPin = s3_calibrationADCPins[adcNum];
        hal::gpioPut((uint8_t)s3_calibrationSelectPins[0], channel & 0x01);
        hal::gpioPut((uint8_t)s3_calibrationSelectPins[1], (channel >> 1) & 0x01);
    } else if (s3_calibrationMuxChannels == 8) {
        uint32_t adcNum = id / 8;
        uint32_t channel = (id % 8);
        if ( adcNum > 2 ) {
            doc["error"] = "id out of 8-channel mux range";
            return s3_serialize(doc);
        }
        adcSelectPin = s3_calibrationADCPins[adcNum];
        hal::gpioPut((uint8_t)s3_calibrationSelectPins[0], channel & 0x01);
        hal::gpioPut((uint8_t)s3_calibrationSelectPins[1], (channel >> 1) & 0x01);
        hal::gpioPut((uint8_t)s3_calibrationSelectPins[2], (channel >> 2) & 0x01);
    } else if (s3_calibrationMuxChannels == 16) {
        uint32_t adcNum = id / 16;
        uint32_t channel = (id % 16);
        if ( adcNum > 1 ) {
            doc["error"] = "id out of 16-channel mux range";
            return s3_serialize(doc);
        }
        adcSelectPin = s3_calibrationADCPins[adcNum];
        hal::gpioPut((uint8_t)s3_calibrationSelectPins[0], channel & 0x01);
        hal::gpioPut((uint8_t)s3_calibrationSelectPins[1], (channel >> 1) & 0x01);
        hal::gpioPut((uint8_t)s3_calibrationSelectPins[2], (channel >> 2) & 0x01);
        hal::gpioPut((uint8_t)s3_calibrationSelectPins[3], (channel >> 3) & 0x01);
    } else {
        doc["error"] = "mux channels incorrect";
        return s3_serialize(doc);
    }

    int adcChannel = s3_adcChannelForGpio((Pin_t)adcSelectPin);
    if (adcChannel < 0) {
        doc["error"] = "adc pin out of range";
        return s3_serialize(doc);
    }
    s3_adcInitOnce();
    // Web-Config triggers getHECalibration every 50ms, game controller triggers <1ms
    if ( s3_calibrationSmoothing ) {
        uint16_t read = 0;
        for(int i = 0; i < 50; i++) {
            read = halAdcRead((uint8_t)adcSelectPin, (adc_channel_t)adcChannel);
            read = s3_heEmaCalculation(read, s3_smoothingRead);
            s3_smoothingRead = read;
        }
        doc["voltage"] = read;
    } else {
        doc["voltage"] = halAdcRead((uint8_t)adcSelectPin, (adc_channel_t)adcChannel);
    }
    return s3_serialize(doc);
}

// Mirrors Pico getHETriggerCalibrations() (src/webconfig.cpp:2029-2050).
static std::string s3_getHETriggerCalibrations()
{
    const size_t capacity = JSON_OBJECT_SIZE(500);
    DynamicJsonDocument doc(capacity);

    HETriggerInfo * heTriggers = Storage::getInstance().getAddonOptions().heTriggerOptions.triggers;

    JsonArray triggerList = doc.createNestedArray("triggers");
    for(int i = 0; i < 32; i++) {
        JsonObject trigger = triggerList.createNestedObject();
        trigger["action"] = heTriggers[i].action;
        trigger["idle"] = heTriggers[i].idle;
        trigger["active"] = heTriggers[i].active;
        trigger["pressed"] = heTriggers[i].pressed;
        trigger["is_polarized"] = heTriggers[i].is_polarized;
        trigger["release"] = heTriggers[i].release;
        trigger["noise"] = heTriggers[i].noise;
        trigger["rapidTrigger"] = heTriggers[i].rapidTrigger;
    }

    return s3_serialize(doc);
}

// Mirrors Pico setHETriggerCalibrations() (src/webconfig.cpp:2053-2073): save.
static std::string s3_setHETriggerCalibrations(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);
    HETriggerInfo * heTriggers = Storage::getInstance().getAddonOptions().heTriggerOptions.triggers;

    for(int i = 0; i < 32; i++) {
        heTriggers[i].action = doc["triggers"][i]["action"];
        heTriggers[i].idle = doc["triggers"][i]["idle"];
        heTriggers[i].active = doc["triggers"][i]["active"];
        heTriggers[i].pressed = doc["triggers"][i]["pressed"];
        heTriggers[i].is_polarized = doc["triggers"][i]["is_polarized"];
        heTriggers[i].release = doc["triggers"][i]["release"];
        heTriggers[i].noise = doc["triggers"][i]["noise"];
        heTriggers[i].rapidTrigger = doc["triggers"][i]["rapidTrigger"];
    }

    Storage::getInstance().getAddonOptions().heTriggerOptions.triggers_count = 32;
    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return s3_serialize(doc);
}

// Mirrors Pico getReactiveLEDs() (src/webconfig.cpp:2076-2090).
static std::string s3_getReactiveLEDs()
{
    const size_t capacity = JSON_OBJECT_SIZE(100);
    DynamicJsonDocument doc(capacity);
    ReactiveLEDInfo* ledInfo = Storage::getInstance().getAddonOptions().reactiveLEDOptions.leds;

    for (uint16_t led = 0; led < 10; led++) {
        s3_writeDoc(doc, "leds", led, "pin", ledInfo[led].pin);
        s3_writeDoc(doc, "leds", led, "action", ledInfo[led].action);
        s3_writeDoc(doc, "leds", led, "modeDown", ledInfo[led].modeDown);
        s3_writeDoc(doc, "leds", led, "modeUp", ledInfo[led].modeUp);
    }

    return s3_serialize(doc);
}

// Mirrors Pico setReactiveLEDs() (src/webconfig.cpp:2092-2109): save.
static std::string s3_setReactiveLEDs(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    ReactiveLEDInfo* ledInfo = Storage::getInstance().getAddonOptions().reactiveLEDOptions.leds;

    for (uint16_t led = 0; led < 10; led++) {
        ledInfo[led].pin = doc["leds"][led]["pin"];
        ledInfo[led].action = doc["leds"][led]["action"];
        ledInfo[led].modeDown = doc["leds"][led]["modeDown"];
        ledInfo[led].modeUp = doc["leds"][led]["modeUp"];
    }
    Storage::getInstance().getAddonOptions().reactiveLEDOptions.leds_count = 10;

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return s3_serialize(doc);
}

// Mirrors Pico helperGetProfileFromJsonObject() (src/webconfig.cpp:1215-1303).
static void s3_helperGetProfileFromJsonObject(AnimationProfile* Profile, JsonObject* JsonData)
{
    Profile->bEnabled = (*JsonData)["bEnabled"].as<bool>();
    if(Profile->baseNonPressedEffect != (AnimationNonPressedEffects)((*JsonData)["baseNonPressedEffect"].as<uint32_t>()))
    {
        Profile->baseNonPressedEffect = (AnimationNonPressedEffects)((*JsonData)["baseNonPressedEffect"].as<uint32_t>());
    }
    if(Profile->basePressedEffect != (AnimationPressedEffects)((*JsonData)["basePressedEffect"].as<uint32_t>()))
    {
        Profile->basePressedEffect = (AnimationPressedEffects)((*JsonData)["basePressedEffect"].as<uint32_t>());
    }
    if(Profile->baseCaseEffect != (AnimationNonPressedEffects)((*JsonData)["baseCaseEffect"].as<uint32_t>()))
    {
        Profile->baseCaseEffect = (AnimationNonPressedEffects)((*JsonData)["baseCaseEffect"].as<uint32_t>());
    }

    Profile->buttonPressHoldTimeInMs = (*JsonData)["buttonPressHoldTimeInMs"].as<uint32_t>();
    Profile->buttonPressFadeOutTimeInMs = (*JsonData)["buttonPressFadeOutTimeInMs"].as<uint32_t>();
    Profile->nonPressedSpecialColor = (*JsonData)["nonPressedSpecialColor"].as<uint32_t>();
    Profile->bUseCaseLightsInPressedAnimations = (*JsonData)["bUseCaseLightsInPressedAnimations"].as<bool>();
    Profile->pressedSpecialColor = (*JsonData)["pressedSpecialColor"].as<uint32_t>();
    Profile->caseSpecialColor = (*JsonData)["caseSpecialColor"].as<uint32_t>();

    Profile->baseCycleTime = (*JsonData)["baseCycleTime"].as<uint32_t>() - 1;
    Profile->basePressedCycleTime = (*JsonData)["basePressedCycleTime"].as<uint32_t>() - 1;
    Profile->baseCaseCycleTime = (*JsonData)["baseCaseCycleTime"].as<uint32_t>() - 1;

    Profile->effectContextParam = (*JsonData)["nonPressedContextParam"].as<uint32_t>() & 0xFF;
    Profile->effectContextParam += ((*JsonData)["pressedContextParam"].as<uint32_t>() & 0xFF) << 8;
    Profile->effectContextParam += ((*JsonData)["caseContextParam"].as<uint32_t>() & 0xFF) << 16;

    Profile->bNonPressedSpecialColorIsRainbow = (*JsonData)["bNonPressedSpecialColorIsRainbow"].as<bool>();
    Profile->bPressedSpecialColorIsRainbow = (*JsonData)["bPressedSpecialColorIsRainbow"].as<bool>();
    Profile->bCaseSpecialColorIsRainbow = (*JsonData)["bCaseSpecialColorIsRainbow"].as<bool>();

    JsonArray notPressedStaticColorsList = (*JsonData)["notPressedStaticColors"];
    Profile->notPressedStaticColors_count = 0;
    for(unsigned int packedPinIndex = 0; packedPinIndex < (NUM_BANK0_GPIOS + 3) / 4; ++packedPinIndex)
    {
        unsigned int pinIndex = packedPinIndex * 4;
        if(pinIndex < notPressedStaticColorsList.size())
            Profile->notPressedStaticColors[packedPinIndex] = notPressedStaticColorsList[pinIndex].as<uint32_t>() & 0xFF;
        else
            break;
        if(pinIndex+1 < notPressedStaticColorsList.size())
            Profile->notPressedStaticColors[packedPinIndex] += ((notPressedStaticColorsList[pinIndex+1].as<uint32_t>() & 0xFF) << 8);
        if(pinIndex+2 < notPressedStaticColorsList.size())
            Profile->notPressedStaticColors[packedPinIndex] += ((notPressedStaticColorsList[pinIndex+2].as<uint32_t>() & 0xFF) << 16);
        if(pinIndex+3 < notPressedStaticColorsList.size())
            Profile->notPressedStaticColors[packedPinIndex] += ((notPressedStaticColorsList[pinIndex+3].as<uint32_t>() & 0xFF) << 24);
        Profile->notPressedStaticColors_count = packedPinIndex+1;
    }

    JsonArray pressedStaticColorsList = (*JsonData)["pressedStaticColors"];
    Profile->pressedStaticColors_count = 0;
    for(unsigned int packedPinIndex = 0; packedPinIndex < (NUM_BANK0_GPIOS + 3) / 4; ++packedPinIndex)
    {
        unsigned int pinIndex = packedPinIndex * 4;
        if(pinIndex < pressedStaticColorsList.size())
            Profile->pressedStaticColors[packedPinIndex] = pressedStaticColorsList[pinIndex].as<uint32_t>() & 0xFF;
        else
            break;
        if(pinIndex+1 < pressedStaticColorsList.size())
            Profile->pressedStaticColors[packedPinIndex] += ((pressedStaticColorsList[pinIndex+1].as<uint32_t>() & 0xFF) << 8);
        if(pinIndex+2 < pressedStaticColorsList.size())
            Profile->pressedStaticColors[packedPinIndex] += ((pressedStaticColorsList[pinIndex+2].as<uint32_t>() & 0xFF) << 16);
        if(pinIndex+3 < pressedStaticColorsList.size())
            Profile->pressedStaticColors[packedPinIndex] += ((pressedStaticColorsList[pinIndex+3].as<uint32_t>() & 0xFF) << 24);
        Profile->pressedStaticColors_count = packedPinIndex+1;
    }

    JsonArray nonButtonStaticColorsList = (*JsonData)["nonButtonStaticColors"];
    Profile->nonButtonStaticColors_count = 0;
    for(unsigned int packedPinIndex = 0; packedPinIndex < (MAX_NON_BUTTON_LIGHT_COLOR_INDEXES/4)+1; ++packedPinIndex)
    {
        unsigned int pinIndex = packedPinIndex * 4;
        if(pinIndex < nonButtonStaticColorsList.size())
            Profile->nonButtonStaticColors[packedPinIndex] = nonButtonStaticColorsList[pinIndex].as<uint32_t>() & 0xFF;
        else
            break;
        if(pinIndex+1 < nonButtonStaticColorsList.size())
            Profile->nonButtonStaticColors[packedPinIndex] += ((nonButtonStaticColorsList[pinIndex+1].as<uint32_t>() & 0xFF) << 8);
        if(pinIndex+2 < nonButtonStaticColorsList.size())
            Profile->nonButtonStaticColors[packedPinIndex] += ((nonButtonStaticColorsList[pinIndex+2].as<uint32_t>() & 0xFF) << 16);
        if(pinIndex+3 < nonButtonStaticColorsList.size())
            Profile->nonButtonStaticColors[packedPinIndex] += ((nonButtonStaticColorsList[pinIndex+3].as<uint32_t>() & 0xFF) << 24);
        Profile->nonButtonStaticColors_count = packedPinIndex+1;
    }
}

// Mirrors Pico setAnimationButtonTestMode() (src/webconfig.cpp:1305-1336):
// AnimationStation test call, no save. S3 AnimationStation exists in SRCS.
static std::string s3_setAnimationButtonTestMode(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    JsonObject docJson = doc.as<JsonObject>();
    JsonObject testOptions = docJson["TestData"];

    AnimationStationTestMode testMode = (AnimationStationTestMode)(testOptions["testMode"].as<uint32_t>());

    //Get current max brightness
    const LEDOptions& ledOptions = Storage::getInstance().getLedOptions();
    uint32_t overrideMaxBrightness = ledOptions.brightnessMaximum;
    AnimationOptions& animOptions = Storage::getInstance().getAnimationOptions();
    uint32_t overrideBrightness = animOptions.brightness;

    AnimationProfile testAnimProfile;
    if(testMode == AnimationStationTestMode::AnimationStation_TestModeProfilePreview)
    {
        JsonObject testProfile = testOptions["testProfile"];
        s3_helperGetProfileFromJsonObject(&testAnimProfile, &testProfile);

        //Allow instant testing of max brightness or brightness changes without saving
        uint32_t checkedBrightnessMax = std::clamp<uint32_t>(testOptions["overrideMaxBrightness"].as<uint8_t>(), 0, 100);
        overrideMaxBrightness = int(((float)checkedBrightnessMax * 2.55f) +  + 0.5f); //+0.5 to cause it to round to nearest number
        overrideMaxBrightness = std::clamp<uint32_t>(overrideMaxBrightness, 0, 255);
        overrideBrightness = std::clamp<uint32_t>(testOptions["overrideBrightness"].as<uint8_t>(), 0, AnimationStation::brightnessSteps);
    }

    AnimationStation::SetTestMode(testMode, &testAnimProfile, overrideBrightness, overrideMaxBrightness);

    return s3_serialize(doc);
}

// Mirrors Pico setAnimationButtonTestState() (src/webconfig.cpp:1338-1350).
static std::string s3_setAnimationButtonTestState(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    JsonObject docJson = doc.as<JsonObject>();
    JsonObject testOptions = docJson["TestLight"];
    int testButton = testOptions["testID"].as<uint32_t>();
    bool testIsNonButtonLight = testOptions["testIsNonButtonLight"].as<bool>();

    AnimationStation::SetTestPinState(testButton, testIsNonButtonLight);

    return s3_serialize(doc);
}

// Mirrors Pico clearAnimationButtonTestMode() (src/webconfig.cpp:1352-1359).
static std::string s3_clearAnimationButtonTestMode(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    AnimationStation::ClearTestMode();

    return s3_serialize(doc);
}

// Mirrors Pico setAnimationProtoOptions() (src/webconfig.cpp:1361-1399):
// RestartLedSystem() + save.
static std::string s3_setAnimationProtoOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    AnimationOptions& options = Storage::getInstance().getAnimationOptions();

    JsonObject docJson = doc.as<JsonObject>();
    JsonObject AnimOptions = docJson["AnimationOptions"];

    options.brightness = AnimOptions["brightness"].as<uint32_t>();
    options.brightness = std::clamp<uint32_t>(options.brightness, 0, AnimationStation::brightnessSteps);
    options.autoDisableTime = AnimOptions["idletimeout"].as<uint32_t>() * 1000;
    options.baseProfileIndex = AnimOptions["baseProfileIndex"].as<uint32_t>();
    JsonArray customColorsList = AnimOptions["customColors"];
    options.customColors_count = 0;
    for(unsigned int customColorsIndex = 0; customColorsIndex < customColorsList.size() && customColorsIndex < MAX_CUSTOM_COLORS; ++customColorsIndex)
    {
        options.customColors[customColorsIndex] = customColorsList[customColorsIndex];
        options.customColors_count = customColorsIndex+1;
    }

    JsonArray profilesList = AnimOptions["profiles"];
    int profilesIndex = 0;
    options.profiles_count = 0;
    for (JsonObject profile : profilesList)
    {
        s3_helperGetProfileFromJsonObject(&(options.profiles[profilesIndex]), &profile);

        options.profiles_count = profilesIndex+1;

        if (++profilesIndex >= MAX_ANIMATION_PROFILES)
            break;
    }

    NeoPicoLEDAddon::RestartLedSystem();

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));
    return s3_serialize(doc);
}

// Mirrors Pico getAnimationProtoOptions() (src/webconfig.cpp:1401-1472).
static std::string s3_getAnimationProtoOptions()
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    const AnimationOptions& options = Storage::getInstance().getAnimationOptions();

    uint32_t checkedBrightness = std::clamp<uint32_t>(options.brightness, 0, AnimationStation::brightnessSteps);

    JsonObject AnimOptions = doc.createNestedObject("AnimationOptions");
    AnimOptions["brightness"] = checkedBrightness;
    AnimOptions["baseProfileIndex"] = options.baseProfileIndex;
    AnimOptions["idletimeout"] = (options.autoDisableTime / 1000);
    JsonArray customColorsList = AnimOptions.createNestedArray("customColors");
    for (int customColorsIndex = 0; customColorsIndex < options.customColors_count; ++customColorsIndex)
    {
        customColorsList.add(options.customColors[customColorsIndex]);
    }

    JsonArray profileList = AnimOptions.createNestedArray("profiles");
    for (int profilesIndex = 0; profilesIndex < options.profiles_count; ++profilesIndex)
    {
        JsonObject profile = profileList.createNestedObject();
        profile["bEnabled"] = options.profiles[profilesIndex].bEnabled ? 1 : 0;
        profile["baseNonPressedEffect"] = options.profiles[profilesIndex].baseNonPressedEffect;
        profile["basePressedEffect"] = options.profiles[profilesIndex].basePressedEffect;
        profile["buttonPressHoldTimeInMs"] = options.profiles[profilesIndex].buttonPressHoldTimeInMs;
        profile["buttonPressFadeOutTimeInMs"] = options.profiles[profilesIndex].buttonPressFadeOutTimeInMs;
        profile["nonPressedSpecialColor"] = options.profiles[profilesIndex].nonPressedSpecialColor;
        profile["bUseCaseLightsInPressedAnimations"] = options.profiles[profilesIndex].bUseCaseLightsInPressedAnimations ? 1 : 0;
        profile["baseCaseEffect"] = options.profiles[profilesIndex].baseCaseEffect;
        profile["pressedSpecialColor"] = options.profiles[profilesIndex].pressedSpecialColor;
        profile["caseSpecialColor"] = options.profiles[profilesIndex].caseSpecialColor;

        profile["baseCycleTime"] = options.profiles[profilesIndex].baseCycleTime + 1;
        profile["basePressedCycleTime"] = options.profiles[profilesIndex].basePressedCycleTime + 1;
        profile["baseCaseCycleTime"] = options.profiles[profilesIndex].baseCaseCycleTime + 1;

        profile["bNonPressedSpecialColorIsRainbow"] = options.profiles[profilesIndex].bNonPressedSpecialColorIsRainbow ? 1 : 0;
        profile["bPressedSpecialColorIsRainbow"] = options.profiles[profilesIndex].bPressedSpecialColorIsRainbow ? 1 : 0;
        profile["bCaseSpecialColorIsRainbow"] = options.profiles[profilesIndex].bCaseSpecialColorIsRainbow ? 1 : 0;

        profile["nonPressedContextParam"] = options.profiles[profilesIndex].effectContextParam & 0xFF;
        profile["pressedContextParam"] = (options.profiles[profilesIndex].effectContextParam >> 8) & 0xFF;
        profile["caseContextParam"] = (options.profiles[profilesIndex].effectContextParam >> 16) & 0xFF;

        JsonArray notPressedStaticColorsList = profile.createNestedArray("notPressedStaticColors");
        for (int notPressedStaticColorsIndex = 0; notPressedStaticColorsIndex < options.profiles[profilesIndex].notPressedStaticColors_count; ++notPressedStaticColorsIndex)
        {
            notPressedStaticColorsList.add(options.profiles[profilesIndex].notPressedStaticColors[notPressedStaticColorsIndex] & 0xFF);
            notPressedStaticColorsList.add((options.profiles[profilesIndex].notPressedStaticColors[notPressedStaticColorsIndex] >> 8) & 0xFF);
            notPressedStaticColorsList.add((options.profiles[profilesIndex].notPressedStaticColors[notPressedStaticColorsIndex] >> 16) & 0xFF);
            notPressedStaticColorsList.add((options.profiles[profilesIndex].notPressedStaticColors[notPressedStaticColorsIndex] >> 24) & 0xFF);
        }
        JsonArray pressedStaticColorsList = profile.createNestedArray("pressedStaticColors");
        for (int pressedStaticColorsIndex = 0; pressedStaticColorsIndex < options.profiles[profilesIndex].pressedStaticColors_count; ++pressedStaticColorsIndex)
        {
            pressedStaticColorsList.add(options.profiles[profilesIndex].pressedStaticColors[pressedStaticColorsIndex] & 0xFF);
            pressedStaticColorsList.add((options.profiles[profilesIndex].pressedStaticColors[pressedStaticColorsIndex] >> 8) & 0xFF);
            pressedStaticColorsList.add((options.profiles[profilesIndex].pressedStaticColors[pressedStaticColorsIndex] >> 16) & 0xFF);
            pressedStaticColorsList.add((options.profiles[profilesIndex].pressedStaticColors[pressedStaticColorsIndex] >> 24) & 0xFF);
        }
        JsonArray nonButtonStaticColorsList = profile.createNestedArray("nonButtonStaticColors");
        for (int nonButtonStaticColorsIndex = 0; nonButtonStaticColorsIndex < options.profiles[profilesIndex].nonButtonStaticColors_count; ++nonButtonStaticColorsIndex)
        {
            nonButtonStaticColorsList.add(options.profiles[profilesIndex].nonButtonStaticColors[nonButtonStaticColorsIndex] & 0xFF);
            nonButtonStaticColorsList.add((options.profiles[profilesIndex].nonButtonStaticColors[nonButtonStaticColorsIndex] >> 8) & 0xFF);
            nonButtonStaticColorsList.add((options.profiles[profilesIndex].nonButtonStaticColors[nonButtonStaticColorsIndex] >> 16) & 0xFF);
            nonButtonStaticColorsList.add((options.profiles[profilesIndex].nonButtonStaticColors[nonButtonStaticColorsIndex] >> 24) & 0xFF);
        }
    }

    return s3_serialize(doc);
}

// Mirrors Pico setLightsDataOptions() (src/webconfig.cpp:957-988):
// RestartLedSystem() + save. FRAME_MAX arrives via addons/neopicoleds.h.
static std::string s3_setLightsDataOptions(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    LEDOptions& options = Storage::getInstance().getLedOptions();

    JsonObject docJson = doc.as<JsonObject>();
    JsonObject AnimOptions = docJson["LightData"];
    JsonArray lightsList = AnimOptions["Lights"];
    options.lightClusterData_count = 0;
    options.lightClusterDataInitialised = true;
    for (JsonObject light : lightsList)
    {
        int thisEntryIndex = options.lightClusterData_count;
        options.lightClusterData[thisEntryIndex].lightLocationData = light["firstLedIndex"].as<uint8_t>();
        options.lightClusterData[thisEntryIndex].lightLocationData += ((int)light["numLedsOnLight"].as<uint8_t>()) << 8;
        options.lightClusterData[thisEntryIndex].lightLocationData += ((int)light["xCoord"].as<uint8_t>()) << 16;
        options.lightClusterData[thisEntryIndex].lightLocationData += ((int)light["yCoord"].as<uint8_t>()) << 24;
        options.lightClusterData[thisEntryIndex].lightTypeData = light["GPIOPinOrNonButtonIndex"].as<uint8_t>();
        options.lightClusterData[thisEntryIndex].lightTypeData += ((int)light["lightType"].as<uint8_t>()) << 8;

        options.lightClusterData_count++;

        if(options.lightClusterData_count >= FRAME_MAX) //100 entries total
            break;
    }

    NeoPicoLEDAddon::RestartLedSystem();

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));
    return s3_serialize(doc);
}

// Mirrors Pico getLightsDataOptions() (src/webconfig.cpp:990-1013).
static std::string s3_getLightsDataOptions()
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    const LEDOptions& options = Storage::getInstance().getLedOptions();
    const TurboOptions& turboOptions = Storage::getInstance().getAddonOptions().turboOptions;

    JsonObject LedOptions = doc.createNestedObject("LightData");
    JsonArray lightsList = LedOptions.createNestedArray("Lights");
    for (int lightsIndex = 0; lightsIndex < options.lightClusterData_count; ++lightsIndex)
    {
        JsonObject light = lightsList.createNestedObject();
        light["firstLedIndex"] = options.lightClusterData[lightsIndex].lightLocationData & 0xFF;
        light["numLedsOnLight"] = (options.lightClusterData[lightsIndex].lightLocationData >> 8) & 0xFF;
        light["xCoord"] = (options.lightClusterData[lightsIndex].lightLocationData >> 16) & 0xFF;
        light["yCoord"] = (options.lightClusterData[lightsIndex].lightLocationData >> 24) & 0xFF;
        light["GPIOPinOrNonButtonIndex"] = options.lightClusterData[lightsIndex].lightTypeData & 0xFF;
        light["lightType"] = (options.lightClusterData[lightsIndex].lightTypeData >> 8) & 0xFF;
    }

    LedOptions["TurboIsRGB"] = turboOptions.turboLedType == PLED_TYPE_RGB ? 1 : 0;
    LedOptions["PLedIsRGB"] = options.pledType == PLED_TYPE_RGB ? 1 : 0;

    return s3_serialize(doc);
}

// Mirrors Pico getLightsPresetsByIndex() (src/webconfig.cpp:1015-1084). The
// S3 board defines no LIGHT_DATA_* presets (defaults are empty names), so
// these return {} exactly like Pico does on a preset-less board.
static std::string s3_getLightsPresetsByIndex(int presetIdx)
{
    DynamicJsonDocument outDoc(S3_POST_MAX_PAYLOAD_LEN);
    bool found = false;

    auto addPreset = [&](const char* name, const unsigned char* data, int32_t dataSize)
    {
        if (strcmp(name, "") != 0) {
            found = true;
            JsonObject preset = outDoc.to<JsonObject>();
            preset["name"] = name;

            JsonObject lightDataObj = preset.createNestedObject("lightData");
            JsonArray lightsList = lightDataObj.createNestedArray("Lights");

            for (int lightsIndex = 0; lightsIndex < dataSize; ++lightsIndex)
            {
                int thisEntryIndex = lightsIndex * 6;
                JsonObject light = lightsList.createNestedObject();
                light["firstLedIndex"] = data[thisEntryIndex];
                light["numLedsOnLight"] = data[thisEntryIndex+1];
                light["xCoord"] = data[thisEntryIndex+2];
                light["yCoord"] = data[thisEntryIndex+3];
                light["GPIOPinOrNonButtonIndex"] = data[thisEntryIndex+4];
                light["lightType"] = data[thisEntryIndex+5];
            }
        }
    };

    if(presetIdx == 0 && strcmp(LIGHT_DATA_NAME_DEFAULT, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_DEFAULT };
        addPreset(LIGHT_DATA_NAME_DEFAULT, lightData, LIGHT_DATA_SIZE_DEFAULT);
    }
    else if(presetIdx == 1 && strcmp(LIGHT_DATA_NAME_1, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_1 };
        addPreset(LIGHT_DATA_NAME_1, lightData, LIGHT_DATA_SIZE_1);
    }
    else if(presetIdx == 2 && strcmp(LIGHT_DATA_NAME_2, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_2 };
        addPreset(LIGHT_DATA_NAME_2, lightData, LIGHT_DATA_SIZE_2);
    }
    else if(presetIdx == 3 && strcmp(LIGHT_DATA_NAME_3, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_3 };
        addPreset(LIGHT_DATA_NAME_3, lightData, LIGHT_DATA_SIZE_3);
    }
    else if(presetIdx == 4 && strcmp(LIGHT_DATA_NAME_4, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_4 };
        addPreset(LIGHT_DATA_NAME_4, lightData, LIGHT_DATA_SIZE_4);
    }
    else if(presetIdx == 5 && strcmp(LIGHT_DATA_NAME_5, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_5 };
        addPreset(LIGHT_DATA_NAME_5, lightData, LIGHT_DATA_SIZE_5);
    }
    else if(presetIdx == 6 && strcmp(LIGHT_DATA_NAME_6, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_6 };
        addPreset(LIGHT_DATA_NAME_6, lightData, LIGHT_DATA_SIZE_6);
    }
    else if(presetIdx == 7 && strcmp(LIGHT_DATA_NAME_7, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_7 };
        addPreset(LIGHT_DATA_NAME_7, lightData, LIGHT_DATA_SIZE_7);
    }

    if (!found) {
        DynamicJsonDocument emptyDoc(16);
        emptyDoc.to<JsonObject>();
        return s3_serialize(emptyDoc);
    }

    return s3_serialize(outDoc);
}

static std::string s3_getLightsPresets0() { return s3_getLightsPresetsByIndex(0); }
static std::string s3_getLightsPresets1() { return s3_getLightsPresetsByIndex(1); }
static std::string s3_getLightsPresets2() { return s3_getLightsPresetsByIndex(2); }
static std::string s3_getLightsPresets3() { return s3_getLightsPresetsByIndex(3); }
static std::string s3_getLightsPresets4() { return s3_getLightsPresetsByIndex(4); }
static std::string s3_getLightsPresets5() { return s3_getLightsPresetsByIndex(5); }
static std::string s3_getLightsPresets6() { return s3_getLightsPresetsByIndex(6); }
static std::string s3_getLightsPresets7() { return s3_getLightsPresetsByIndex(7); }

// Mirrors Pico getLightsDataPresets() (src/webconfig.cpp:1095-1158).
static std::string s3_getLightsDataPresets()
{
    //DynamicJsonDocument outDoc(S3_POST_MAX_PAYLOAD_LEN);
    DynamicJsonDocument outDoc((1024 * 32)); //Set a bigger value here as the preset data is quite large but it should be fine for a get call
    JsonArray presetsArray = outDoc.to<JsonArray>();

    auto addPreset = [&](const char* name, const unsigned char* data, int32_t dataSize)
    {
        if (strcmp(name, "") != 0) {
            JsonObject preset = presetsArray.createNestedObject();
            preset["name"] = name;

            JsonObject lightDataObj = preset.createNestedObject("lightData");
            JsonArray lightsList = lightDataObj.createNestedArray("Lights");

            for (int lightsIndex = 0; lightsIndex < dataSize; ++lightsIndex)
            {
                int thisEntryIndex = lightsIndex * 6;
                JsonObject light = lightsList.createNestedObject();
                light["firstLedIndex"] = data[thisEntryIndex];
                light["numLedsOnLight"] = data[thisEntryIndex+1];
                light["xCoord"] = data[thisEntryIndex+2];
                light["yCoord"] = data[thisEntryIndex+3];
                light["GPIOPinOrNonButtonIndex"] = data[thisEntryIndex+4];
                light["lightType"] = data[thisEntryIndex+5];
            }
        }
    };

    if(strcmp(LIGHT_DATA_NAME_DEFAULT, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_DEFAULT };
        addPreset(LIGHT_DATA_NAME_DEFAULT, lightData, LIGHT_DATA_SIZE_DEFAULT);
    }
    if(strcmp(LIGHT_DATA_NAME_1, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_1 };
        addPreset(LIGHT_DATA_NAME_1, lightData, LIGHT_DATA_SIZE_1);
    }
    if(strcmp(LIGHT_DATA_NAME_2, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_2 };
        addPreset(LIGHT_DATA_NAME_2, lightData, LIGHT_DATA_SIZE_2);
    }
    if(strcmp(LIGHT_DATA_NAME_3, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_3 };
        addPreset(LIGHT_DATA_NAME_3, lightData, LIGHT_DATA_SIZE_3);
    }
    if(strcmp(LIGHT_DATA_NAME_4, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_4 };
        addPreset(LIGHT_DATA_NAME_4, lightData, LIGHT_DATA_SIZE_4);
    }
    if(strcmp(LIGHT_DATA_NAME_5, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_5 };
        addPreset(LIGHT_DATA_NAME_5, lightData, LIGHT_DATA_SIZE_5);
    }
    if(strcmp(LIGHT_DATA_NAME_6, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_6 };
        addPreset(LIGHT_DATA_NAME_6, lightData, LIGHT_DATA_SIZE_6);
    }
    if(strcmp(LIGHT_DATA_NAME_7, "") != 0) {
        const unsigned char lightData[] = { LIGHT_DATA_7 };
        addPreset(LIGHT_DATA_NAME_7, lightData, LIGHT_DATA_SIZE_7);
    }

    return s3_serialize(outDoc);
}

// Mirrors Pico setLightsToDefault() (src/webconfig.cpp:1160-1213): preset
// assign + RestartLedSystem() + save.
static std::string s3_setLightsToDefault(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);

    JsonObject docJson = doc.as<JsonObject>();
    const char*  resetName = docJson["ResetName"];

    if(strcmp(resetName, LIGHT_DATA_NAME_DEFAULT) == 0)
    {
        const unsigned char lightData[] = { LIGHT_DATA_DEFAULT };
        NeoPicoLEDAddon::AssignLedPreset(lightData, sizeof(lightData));
    }
    else if(strcmp(resetName, LIGHT_DATA_NAME_1) == 0)
    {
        const unsigned char lightData[] = { LIGHT_DATA_1 };
        NeoPicoLEDAddon::AssignLedPreset(lightData, sizeof(lightData));
    }
    else if(strcmp(resetName, LIGHT_DATA_NAME_2) == 0)
    {
        const unsigned char lightData[] = { LIGHT_DATA_2 };
        NeoPicoLEDAddon::AssignLedPreset(lightData, sizeof(lightData));
    }
    else if(strcmp(resetName, LIGHT_DATA_NAME_3) == 0)
    {
        const unsigned char lightData[] = { LIGHT_DATA_3 };
        NeoPicoLEDAddon::AssignLedPreset(lightData, sizeof(lightData));
    }
    else if(strcmp(resetName, LIGHT_DATA_NAME_4) == 0)
    {
        const unsigned char lightData[] = { LIGHT_DATA_4 };
        NeoPicoLEDAddon::AssignLedPreset(lightData, sizeof(lightData));
    }
    else if(strcmp(resetName, LIGHT_DATA_NAME_5) == 0)
    {
        const unsigned char lightData[] = { LIGHT_DATA_5 };
        NeoPicoLEDAddon::AssignLedPreset(lightData, sizeof(lightData));
    }
    else if(strcmp(resetName, LIGHT_DATA_NAME_6) == 0)
    {
        const unsigned char lightData[] = { LIGHT_DATA_6 };
        NeoPicoLEDAddon::AssignLedPreset(lightData, sizeof(lightData));
    }
    else if(strcmp(resetName, LIGHT_DATA_NAME_7) == 0)
    {
        const unsigned char lightData[] = { LIGHT_DATA_7 };
        NeoPicoLEDAddon::AssignLedPreset(lightData, sizeof(lightData));
    }

    NeoPicoLEDAddon::RestartLedSystem();

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return s3_serialize(doc);
}

// Board definition for the S3 port (brief shape): flat minPin/maxPin over the
// S3 GPIO window, analogPins from the ADC1 channel map above, availablePins
// as the routable set minus the native-USB pair 19/20, usedPins from the pin
// mappings. Pico instead wraps this in a "pico" object (src/webconfig.cpp:3177).
static std::string s3_getBoardDefinition() {
    const size_t capacity = JSON_OBJECT_SIZE(100);
    DynamicJsonDocument doc(capacity);

    GpioMappings& gpioMappings = Storage::getInstance().getGpioMappings();

    s3_writeDoc(doc, "minPin", 0);
    s3_writeDoc(doc, "maxPin", 48);

    JsonArray analogPins = doc.createNestedArray("analogPins");
    for (Pin_t pin = 0; pin <= 48; pin++) {
        if (s3_adcChannelForGpio(pin) >= 0) analogPins.add(pin);
    }

    JsonArray availablePins = doc.createNestedArray("availablePins");
    for (Pin_t pin = 0; pin <= 48; pin++) {
        if (pin == 19 || pin == 20) continue; // native USB D-/D+
        availablePins.add(pin);
    }

    char pinName[6];
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        snprintf(pinName, 6, "pin%02d", (int)pin);
        doc["usedPins"][pinName] = gpioMappings.pins[pin].action;
    }

    return s3_serialize(doc);
}

// Memory report with S3 sources (brief): flash sizes from the JEDEC-ID chip
// size + Storage equivalents, heap from the esp free/minimum-free APIs,
// staticAllocs = 0 (Pico SRAM-layout-specific).
static std::string s3_getMemoryReport()
{
    const size_t capacity = JSON_OBJECT_SIZE(10);
    DynamicJsonDocument doc(capacity);
    uint32_t freeHeap = (uint32_t)esp_get_free_heap_size();
    uint32_t minFreeHeap = (uint32_t)esp_get_minimum_free_heap_size();
    s3_writeDoc(doc, "totalFlash", (uint32_t)spi_flash_get_chip_size());
    s3_writeDoc(doc, "usedFlash", System::getUsedFlash());
    s3_writeDoc(doc, "physicalFlash", Storage::getInstance().GetFlashSize());
    s3_writeDoc(doc, "staticAllocs", 0);
    s3_writeDoc(doc, "totalHeap", freeHeap);
    s3_writeDoc(doc, "usedHeap", (freeHeap >= minFreeHeap) ? (freeHeap - minFreeHeap) : 0);
    return s3_serialize(doc);
}

// Mirrors Pico getUsedPins() (src/webconfig.cpp:422-428).
static std::string s3_getUsedPins()
{
    const size_t capacity = JSON_OBJECT_SIZE(100);
    DynamicJsonDocument doc(capacity);
    s3_addUsedPinsArray(doc);
    return s3_serialize(doc);
}

static bool s3_abortGetHeldPinsFlag = false;

// Mirrors Pico getHeldPins() (src/webconfig.cpp:2953-3011): up to 5 s GPIO
// scan with 5 ms debounce and an abort flag, same contract. S3 differences:
// polling via hal::gpioGet over the 30-pin window (S3 GPIOs >= 30 are
// invisible to the mask, same window the core loop iterates) with
// hal::millis() timing; unassigned pins get input+pullup like Pico's
// gpio_init/pull_up; no deinit exists in the S3 HAL so pins stay inputs.
// P3 (accepted for POC): the S3 HAL has no direction query, so a pin driven
// LOW as an output during the scan (e.g. by an addon peripheral) reads LOW
// and false-positives as held — Pico's SIO-input-only guard could not be
// ported.
static std::string s3_getHeldPins()
{
    s3_abortGetHeldPinsFlag = false;
    DynamicJsonDocument doc(JSON_OBJECT_SIZE(100));

    // Initialize unassigned pins for reading
    GpioMappings& gpioMappings = Storage::getInstance().getGpioMappings();
    for (uint32_t pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
        if (gpioMappings.pins[pin].action == GpioAction::NONE) {
            hal::gpioSetInput((uint8_t)pin, true);
        }
    }

    std::set<uint32_t> heldPinsSet;
    uint32_t startTime = hal::millis();
    // Active-low buttons with pullups: a held pin reads LOW.
    uint32_t oldState = 0;
    for (uint32_t pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
        if (!hal::gpioGet((uint8_t)pin)) {
            oldState |= (1u << pin);
        }
    }
    uint32_t debounceTime = 0;
    bool isAnyPinHeld = false;

    // Monitor pins for 5 seconds
    while (!s3_abortGetHeldPinsFlag && (hal::millis() - startTime) < 5000) {
        uint32_t newState = 0;
        for (uint32_t pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
            if (!hal::gpioGet((uint8_t)pin)) {
                newState |= (1u << pin);
            }
        }

        uint32_t changedPins = newState & ~oldState;
        if (isAnyPinHeld && changedPins == 0) break; // Pins released
        if (changedPins == 0) debounceTime = 0;
        uint32_t currentTime = hal::millis();

        for (uint32_t pin = 0; pin < NUM_BANK0_GPIOS; pin++) {
            if (changedPins & (1u << pin)) {
                if (debounceTime == 0) debounceTime = currentTime;
                if ((currentTime - debounceTime) > 5) { // 5ms debounce
                    heldPinsSet.insert(pin);
                    isAnyPinHeld = true;
                }
            }
        }
    }

    if (s3_abortGetHeldPinsFlag) {
        s3_abortGetHeldPinsFlag = false;
        return {};
    }

    auto heldPins = doc.createNestedArray("heldPins");
    for (uint32_t pin : heldPinsSet) heldPins.add(pin);

    return s3_serialize(doc);
}

// Mirrors Pico abortGetHeldPins() (src/webconfig.cpp:3013-3017).
static std::string s3_abortGetHeldPins(const char *body, size_t len)
{
    (void)body;
    (void)len;
    s3_abortGetHeldPinsFlag = true;
    return {};
}

// Mirrors Pico getJoystickCenter() (src/webconfig.cpp:3090-3133): live raw
// ADC reads via the S3 ADC1 backend; pins without an ADC1 channel keep
// Pico's zero-fill behavior. {success,x,y} or {success:false,error}.
static std::string s3_getJoystickCenter() {
    const size_t capacity = JSON_OBJECT_SIZE(10);
    DynamicJsonDocument doc(capacity);
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;

    uint16_t x = 0, y = 0;
    bool success = true;
    std::string error_msg = "";

    // Check if analog input is enabled
    if (!analogOptions.enabled) {
        success = false;
        error_msg = "Analog input is not enabled";
    } else {
        // Read first stick X/Y
        if (isValidPin(analogOptions.analogAdc1PinX)) {
            uint16_t v = 0;
            if (s3_adcReadGpio(analogOptions.analogAdc1PinX, v)) {
                x = v;
            }
        }
        if (isValidPin(analogOptions.analogAdc1PinY)) {
            uint16_t v = 0;
            if (s3_adcReadGpio(analogOptions.analogAdc1PinY, v)) {
                y = v;
            }
        }
    }

    JsonObject o = doc.to<JsonObject>();
    o["success"] = success;
    if (!success) {
        o["error"] = error_msg;
    } else {
        o["x"] = x;
        o["y"] = y;
    }
    return s3_serialize(doc);
}

// Mirrors Pico getJoystickCenter2() (src/webconfig.cpp:3136-3175): stick 2.
static std::string s3_getJoystickCenter2() {
    const size_t capacity = JSON_OBJECT_SIZE(10);
    DynamicJsonDocument doc(capacity);
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;

    uint16_t x = 0, y = 0;
    bool success = true;
    std::string error_msg = "";

    // Check if analog input is enabled
    if (!analogOptions.enabled) {
        success = false;
        error_msg = "Analog input is not enabled";
    } else {
        // Read second stick X/Y
        if (isValidPin(analogOptions.analogAdc2PinX)) {
            uint16_t v = 0;
            if (s3_adcReadGpio(analogOptions.analogAdc2PinX, v)) {
                x = v;
            }
        }
        if (isValidPin(analogOptions.analogAdc2PinY)) {
            uint16_t v = 0;
            if (s3_adcReadGpio(analogOptions.analogAdc2PinY, v)) {
                y = v;
            }
        }
    }

    JsonObject o = doc.to<JsonObject>();
    o["success"] = success;
    if (!success) {
        o["error"] = error_msg;
    } else {
        o["x"] = x;
        o["y"] = y;
    }
    return s3_serialize(doc);
}

// Mirrors Pico setPS4Options() (src/webconfig.cpp:2365-2436): N,E,P,Q,serial,
// signature base64 blobs persisted to the PS4 option fields, deprecated RSA
// fields zapped, {"success":true}; storage only, no host needed.
static std::string s3_setPS4Options(const char *body, size_t len)
{
    DynamicJsonDocument doc(S3_POST_MAX_PAYLOAD_LEN);
    deserializeJson(doc, body, len);
    PS4Options& ps4Options = Storage::getInstance().getAddonOptions().ps4Options;
    std::string encoded;
    std::string decoded;

    const auto readEncoded = [&](const char* key) -> bool
    {
        if (doc.containsKey(key))
        {
            const char* str = nullptr;
            s3_readDoc(str, doc, key);
            encoded = str;
            return true;
        }
        else
        {
            return false;
        }
    };

    // RSA Context
    if ( readEncoded("N") ) {
        if ( Base64::Decode(encoded, decoded) && (decoded.length() == sizeof(ps4Options.rsaN.bytes)) ) {
            memcpy(ps4Options.rsaN.bytes, decoded.data(), decoded.length());
            ps4Options.rsaN.size = decoded.length();
        }
    }
    if ( readEncoded("E") ) {
        if ( Base64::Decode(encoded, decoded) && (decoded.length() == sizeof(ps4Options.rsaE.bytes)) ) {
            memcpy(ps4Options.rsaE.bytes, decoded.data(), decoded.length());
            ps4Options.rsaE.size = decoded.length();
        }
    }
    if ( readEncoded("P") ) {
        if ( Base64::Decode(encoded, decoded) && (decoded.length() == sizeof(ps4Options.rsaP.bytes)) ) {
            memcpy(ps4Options.rsaP.bytes, decoded.data(), decoded.length());
            ps4Options.rsaP.size = decoded.length();
        }
    }
    if ( readEncoded("Q") ) {
        if ( Base64::Decode(encoded, decoded) && (decoded.length() == sizeof(ps4Options.rsaQ.bytes)) ) {
            memcpy(ps4Options.rsaQ.bytes, decoded.data(), decoded.length());
            ps4Options.rsaQ.size = decoded.length();
        }
    }
    // Serial & Signature
    if ( readEncoded("serial") ) {
        if ( Base64::Decode(encoded, decoded) && (decoded.length() == sizeof(ps4Options.serial.bytes)) ) {
            memcpy(ps4Options.serial.bytes, decoded.data(), decoded.length());
            ps4Options.serial.size = decoded.length();
        }
    }
    if ( readEncoded("signature") ) {
        if ( Base64::Decode(encoded, decoded) && (decoded.length() == sizeof(ps4Options.signature.bytes)) ) {
            memcpy(ps4Options.signature.bytes, decoded.data(), decoded.length());
            ps4Options.signature.size = decoded.length();
        }
    }

    // Zap deprecated fields
    if (ps4Options.rsaD.size != 0) ps4Options.rsaD.size = 0;
    if (ps4Options.rsaDP.size != 0) ps4Options.rsaDP.size = 0;
    if (ps4Options.rsaDQ.size != 0) ps4Options.rsaDQ.size = 0;
    if (ps4Options.rsaQP.size != 0) ps4Options.rsaQP.size = 0;
    if (ps4Options.rsaRN.size != 0) ps4Options.rsaRN.size = 0;

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));

    return "{\"success\":true}";
}

// ---- Static file serving (React bundle on the SPIFFS www partition) ----

// SPA routes served with /www/index.html (must match web navigation).
static const char *s3_spa_paths[] = {
    "/animation", "/backup", "/display-config", "/led-config", "/pin-mapping",
    "/settings", "/reset-settings", "/add-ons", "/macro", "/peripheral-mapping",
    "/boot-mode-mapping",
};

static const char *s3_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == nullptr)
    {
        return "application/octet-stream";
    }
    if (strcmp(ext, ".html") == 0)
    {
        return "text/html";
    }
    if (strcmp(ext, ".css") == 0)
    {
        return "text/css";
    }
    if (strcmp(ext, ".js") == 0)
    {
        return "application/javascript";
    }
    if (strcmp(ext, ".json") == 0)
    {
        return "application/json";
    }
    if (strcmp(ext, ".png") == 0)
    {
        return "image/png";
    }
    if (strcmp(ext, ".ico") == 0)
    {
        return "image/x-icon";
    }
    if (strcmp(ext, ".svg") == 0)
    {
        return "image/svg+xml";
    }
    return "application/octet-stream";
}

// Catch-all GET: URI -> /www + path, except the SPA routes (and /) which
// map to /www/index.html. Mirrors the Pico fs_open_custom spaPaths /
// excludePaths routing (src/webconfig.cpp:3348): /css /images /js /static
// and every other bundle asset resolve by exact path.
static esp_err_t s3_static_get(httpd_req_t *req)
{
    std::string uri = req->uri;
    size_t q = uri.find('?');
    if (q != std::string::npos)
    {
        uri.resize(q);
    }
    if (uri.empty() || uri[0] != '/' || uri.find("..") != std::string::npos)
    {
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    std::string path;
    if (uri == "/")
    {
        path = "/www/index.html";
    }
    else
    {
        bool spa = false;
        for (const char *spaPath : s3_spa_paths)
        {
            if (uri == spaPath)
            {
                spa = true;
                break;
            }
        }
        path = spa ? "/www/index.html" : ("/www" + uri);
    }
    FILE *f = fopen(path.c_str(), "r");
    if (f == nullptr)
    {
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    httpd_resp_set_type(req, s3_mime_type(path.c_str()));
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char chunk[1024];
    size_t n = 0;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
    {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK)
        {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// ---- URI handler adapters ----

static esp_err_t s3_handle_getFirmwareVersion(httpd_req_t *req)
{
    return s3_json_get(req, s3_getFirmwareVersion);
}

static esp_err_t s3_handle_getConfig(httpd_req_t *req)
{
    return s3_json_get(req, s3_getConfig);
}

static esp_err_t s3_handle_setConfig(httpd_req_t *req)
{
    return s3_json_post_status(req, s3_setConfig);
}

static esp_err_t s3_handle_reboot(httpd_req_t *req)
{
    return s3_json_post(req, s3_reboot);
}

static esp_err_t s3_handle_resetSettings(httpd_req_t *req)
{
    return s3_json_post(req, s3_resetSettings);
}

static esp_err_t s3_handle_getGamepadOptions(httpd_req_t *req)
{
    return s3_json_get(req, s3_getGamepadOptions);
}

static esp_err_t s3_handle_setGamepadOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setGamepadOptions);
}

static esp_err_t s3_handle_getPinMappings(httpd_req_t *req)
{
    return s3_json_get(req, s3_getPinMappings);
}

static esp_err_t s3_handle_setPinMappings(httpd_req_t *req)
{
    return s3_json_post(req, s3_setPinMappings);
}

static esp_err_t s3_handle_getProfileOptions(httpd_req_t *req)
{
    return s3_json_get(req, s3_getProfileOptions);
}

static esp_err_t s3_handle_setProfileOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setProfileOptions);
}

static esp_err_t s3_handle_getKeyMappings(httpd_req_t *req)
{
    return s3_json_get(req, s3_getKeyMappings);
}

static esp_err_t s3_handle_setKeyMappings(httpd_req_t *req)
{
    return s3_json_post(req, s3_setKeyMappings);
}

static esp_err_t s3_handle_getBootModeOptions(httpd_req_t *req)
{
    return s3_json_get(req, s3_getBootModeOptions);
}

static esp_err_t s3_handle_setBootModeOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setBootModeOptions);
}

// ---- Task 6 URI handler adapters ----

static esp_err_t s3_handle_getLedOptions(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLedOptions);
}

static esp_err_t s3_handle_setLedOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setLedOptions);
}

static esp_err_t s3_handle_getDisplayOptions(httpd_req_t *req)
{
    return s3_json_get(req, s3_getDisplayOptions);
}

static esp_err_t s3_handle_setDisplayOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setDisplayOptions);
}

static esp_err_t s3_handle_setPreviewDisplayOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setPreviewDisplayOptions);
}

static esp_err_t s3_handle_getSplashImage(httpd_req_t *req)
{
    return s3_json_get(req, s3_getSplashImage);
}

static esp_err_t s3_handle_setSplashImage(httpd_req_t *req)
{
    return s3_json_post(req, s3_setSplashImage);
}

static esp_err_t s3_handle_getAddonsOptions(httpd_req_t *req)
{
    return s3_json_get(req, s3_getAddonOptions);
}

static esp_err_t s3_handle_setAddonsOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setAddonOptions);
}

static esp_err_t s3_handle_getWiiControls(httpd_req_t *req)
{
    return s3_json_get(req, s3_getWiiControls);
}

static esp_err_t s3_handle_setWiiControls(httpd_req_t *req)
{
    return s3_json_post(req, s3_setWiiControls);
}

static esp_err_t s3_handle_getMacroAddonOptions(httpd_req_t *req)
{
    return s3_json_get(req, s3_getMacroAddonOptions);
}

static esp_err_t s3_handle_setMacroAddonOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setMacroAddonOptions);
}

static esp_err_t s3_handle_getPeripheralOptions(httpd_req_t *req)
{
    return s3_json_get(req, s3_getPeripheralOptions);
}

static esp_err_t s3_handle_setPeripheralOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setPeripheralOptions);
}

static esp_err_t s3_handle_getI2CPeripheralMap(httpd_req_t *req)
{
    return s3_json_get(req, s3_getI2CPeripheralMap);
}

static esp_err_t s3_handle_getExpansionPins(httpd_req_t *req)
{
    return s3_json_get(req, s3_getExpansionPins);
}

static esp_err_t s3_handle_setExpansionPins(httpd_req_t *req)
{
    return s3_json_post(req, s3_setExpansionPins);
}

static esp_err_t s3_handle_getHETriggerCalibrations(httpd_req_t *req)
{
    return s3_json_get(req, s3_getHETriggerCalibrations);
}

static esp_err_t s3_handle_setHETriggerCalibrations(httpd_req_t *req)
{
    return s3_json_post(req, s3_setHETriggerCalibrations);
}

static esp_err_t s3_handle_setHETriggerOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setHETriggerOptions);
}

static esp_err_t s3_handle_getHETriggerVoltage(httpd_req_t *req)
{
    return s3_json_post(req, s3_getHETriggerVoltage);
}

static esp_err_t s3_handle_getReactiveLEDs(httpd_req_t *req)
{
    return s3_json_get(req, s3_getReactiveLEDs);
}

static esp_err_t s3_handle_setReactiveLEDs(httpd_req_t *req)
{
    return s3_json_post(req, s3_setReactiveLEDs);
}

static esp_err_t s3_handle_getAnimationProtoOptions(httpd_req_t *req)
{
    return s3_json_get(req, s3_getAnimationProtoOptions);
}

static esp_err_t s3_handle_setAnimationProtoOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setAnimationProtoOptions);
}

static esp_err_t s3_handle_setAnimationButtonTestMode(httpd_req_t *req)
{
    return s3_json_post(req, s3_setAnimationButtonTestMode);
}

static esp_err_t s3_handle_setAnimationButtonTestState(httpd_req_t *req)
{
    return s3_json_post(req, s3_setAnimationButtonTestState);
}

static esp_err_t s3_handle_clearAnimationButtonTestMode(httpd_req_t *req)
{
    return s3_json_post(req, s3_clearAnimationButtonTestMode);
}

static esp_err_t s3_handle_getLightsDataOptions(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLightsDataOptions);
}

static esp_err_t s3_handle_setLightsDataOptions(httpd_req_t *req)
{
    return s3_json_post(req, s3_setLightsDataOptions);
}

static esp_err_t s3_handle_getLightsPresets0(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLightsPresets0);
}

static esp_err_t s3_handle_getLightsPresets1(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLightsPresets1);
}

static esp_err_t s3_handle_getLightsPresets2(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLightsPresets2);
}

static esp_err_t s3_handle_getLightsPresets3(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLightsPresets3);
}

static esp_err_t s3_handle_getLightsPresets4(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLightsPresets4);
}

static esp_err_t s3_handle_getLightsPresets5(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLightsPresets5);
}

static esp_err_t s3_handle_getLightsPresets6(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLightsPresets6);
}

static esp_err_t s3_handle_getLightsPresets7(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLightsPresets7);
}

static esp_err_t s3_handle_getLightsDataPresets(httpd_req_t *req)
{
    return s3_json_get(req, s3_getLightsDataPresets);
}

static esp_err_t s3_handle_setLightsToDefault(httpd_req_t *req)
{
    return s3_json_post(req, s3_setLightsToDefault);
}

static esp_err_t s3_handle_getBoardDefinition(httpd_req_t *req)
{
    return s3_json_get(req, s3_getBoardDefinition);
}

static esp_err_t s3_handle_getMemoryReport(httpd_req_t *req)
{
    return s3_json_get(req, s3_getMemoryReport);
}

static esp_err_t s3_handle_getUsedPins(httpd_req_t *req)
{
    return s3_json_get(req, s3_getUsedPins);
}

static esp_err_t s3_handle_getHeldPins(httpd_req_t *req)
{
    return s3_json_get(req, s3_getHeldPins);
}

static esp_err_t s3_handle_abortGetHeldPins(httpd_req_t *req)
{
    return s3_json_post(req, s3_abortGetHeldPins);
}

static esp_err_t s3_handle_getJoystickCenter(httpd_req_t *req)
{
    return s3_json_get(req, s3_getJoystickCenter);
}

static esp_err_t s3_handle_getJoystickCenter2(httpd_req_t *req)
{
    return s3_json_get(req, s3_getJoystickCenter2);
}

static esp_err_t s3_handle_setPS4Options(httpd_req_t *req)
{
    return s3_json_post(req, s3_setPS4Options);
}

// ---- WiFi AP lifecycle (Task 7) ----
//
// Credentials come from the Task-1 WebConfigOptions settings (apSSID default
// "GP2040-CE", apPassphrase default DEFAULT_AP_PASSPHRASE, both applied by
// ConfigUtils::initUnsetPropertiesWithDefaults). Bring-up follows the brief's
// exact IDF sequence; the default AP netif runs DHCP (clients lease
// 192.168.4.x). Called once from GP2040::setup() when the AP is requested
// (L1-hold WiFi-config session, saved apEnabled toggle, or CONFIG boot with
// the WIFI transport pref).
//
// Security notes: an empty passphrase means an OPEN network (WPA2 needs
// 8+ chars; a shorter non-empty passphrase fails esp_wifi_set_config and is
// logged, never silently downgraded). The passphrase is NEVER logged (Task 8
// guard: no creds in logs). Bounded copies (not raw strcpy): apSSID is up to
// 32 chars + NUL and ap.ap.ssid is 32 bytes, so strcpy would overrun by one.

static bool s3_ap_started = false;

bool startWifiAP()
{
    if (s3_ap_started)
    {
        return true;
    }
    WebConfigOptions &webConfigOptions = Storage::getInstance().getConfig().webConfigOptions;
    const char *ssid = webConfigOptions.apSSID[0] != '\0' ? webConfigOptions.apSSID : "GP2040-CE";
    const char *pass = webConfigOptions.apPassphrase;
    size_t passLen = strlen(pass);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(S3_WEBCONFIG_TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(S3_WEBCONFIG_TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(S3_WEBCONFIG_TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
        return false;
    }
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK)
    {
        ESP_LOGE(S3_WEBCONFIG_TAG, "esp_wifi_init failed");
        return false;
    }
    wifi_config_t ap = {};
    strncpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid[sizeof(ap.ap.ssid) - 1] = '\0';
    strncpy((char *)ap.ap.password, pass, sizeof(ap.ap.password) - 1);
    ap.ap.password[sizeof(ap.ap.password) - 1] = '\0';
    if (passLen == 0)
    {
        ap.ap.authmode = WIFI_AUTH_OPEN;
    }
    else
    {
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    ap.ap.max_connection = 4;
    // Explicit channel 1 (IDF softAP example default): the brief's snippet
    // leaves the zero-init channel unset, but channel-0 AP behavior is
    // undocumented in the precompiled WiFi lib, so pin it instead of
    // risking an ESP_ERR_INVALID_ARG at first boot.
    ap.ap.channel = 1;
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK)
    {
        ESP_LOGE(S3_WEBCONFIG_TAG, "esp_wifi_set_mode AP failed");
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK)
    {
        ESP_LOGE(S3_WEBCONFIG_TAG, "esp_wifi_set_config AP failed (SSID len %u, passphrase len %u)",
            (unsigned int)strlen(ssid), (unsigned int)passLen);
        return false;
    }
    if (esp_wifi_start() != ESP_OK)
    {
        ESP_LOGE(S3_WEBCONFIG_TAG, "esp_wifi_start failed");
        return false;
    }
    s3_ap_started = true;
    ESP_LOGI(S3_WEBCONFIG_TAG, "WiFi AP started (SSID len %u, %s)",
        (unsigned int)strlen(ssid), passLen == 0 ? "open" : "WPA2");
    return true;
}

void stopWifiAP()
{
    if (!s3_ap_started)
    {
        return;
    }
    esp_wifi_stop();
    s3_ap_started = false;
}

// ---- Server lifecycle (started from GP2040::setup() in Task 7) ----

void startWebconfigServer()
{
    if (s3_httpd != nullptr)
    {
        return;
    }
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/www",
        .partition_label = "www",
        .max_files = 8,
        .format_if_mount_failed = false,
    };
    esp_err_t spiffs_ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (spiffs_ret != ESP_OK && spiffs_ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(S3_WEBCONFIG_TAG, "SPIFFS www mount failed: %s", esp_err_to_name(spiffs_ret));
        return;
    }
    // 12288-byte task stack: 16 KB ArduinoJson docs are heap-allocated
    // (DynamicJsonDocument), but handler call frames must not live on the
    // 4096-byte httpd default.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 12288;
    // 15 core+Task-5 URIs + 49 Task-6 URIs + catch-all = 65; the IDF default
    // max_uri_handlers (8) would reject registration with
    // ESP_ERR_HTTPD_HANDLERS_FULL at server start, so raise it with margin.
    config.max_uri_handlers = 96;
    if (httpd_start(&s3_httpd, &config) != ESP_OK)
    {
        ESP_LOGE(S3_WEBCONFIG_TAG, "httpd_start failed");
        s3_httpd = nullptr;
        esp_vfs_spiffs_unregister("www");
        return;
    }
    // Tasks 5-6 append endpoint URIs here.
    const httpd_uri_t uris[] = {
        { .uri = "/api/getFirmwareVersion", .method = HTTP_GET, .handler = s3_handle_getFirmwareVersion, .user_ctx = nullptr },
        { .uri = "/api/getConfig", .method = HTTP_GET, .handler = s3_handle_getConfig, .user_ctx = nullptr },
        { .uri = "/api/setConfig", .method = HTTP_POST, .handler = s3_handle_setConfig, .user_ctx = nullptr },
        { .uri = "/api/reboot", .method = HTTP_POST, .handler = s3_handle_reboot, .user_ctx = nullptr },
        { .uri = "/api/resetSettings", .method = HTTP_POST, .handler = s3_handle_resetSettings, .user_ctx = nullptr },
        { .uri = "/api/getGamepadOptions", .method = HTTP_GET, .handler = s3_handle_getGamepadOptions, .user_ctx = nullptr },
        { .uri = "/api/setGamepadOptions", .method = HTTP_POST, .handler = s3_handle_setGamepadOptions, .user_ctx = nullptr },
        { .uri = "/api/getPinMappings", .method = HTTP_GET, .handler = s3_handle_getPinMappings, .user_ctx = nullptr },
        { .uri = "/api/setPinMappings", .method = HTTP_POST, .handler = s3_handle_setPinMappings, .user_ctx = nullptr },
        { .uri = "/api/getProfileOptions", .method = HTTP_GET, .handler = s3_handle_getProfileOptions, .user_ctx = nullptr },
        { .uri = "/api/setProfileOptions", .method = HTTP_POST, .handler = s3_handle_setProfileOptions, .user_ctx = nullptr },
        { .uri = "/api/getKeyMappings", .method = HTTP_GET, .handler = s3_handle_getKeyMappings, .user_ctx = nullptr },
        { .uri = "/api/setKeyMappings", .method = HTTP_POST, .handler = s3_handle_setKeyMappings, .user_ctx = nullptr },
        { .uri = "/api/getBootModeOptions", .method = HTTP_GET, .handler = s3_handle_getBootModeOptions, .user_ctx = nullptr },
        { .uri = "/api/setBootModeOptions", .method = HTTP_POST, .handler = s3_handle_setBootModeOptions, .user_ctx = nullptr },
        // Task 6: LED/display/addons/peripherals (49 URIs).
        { .uri = "/api/getLedOptions", .method = HTTP_GET, .handler = s3_handle_getLedOptions, .user_ctx = nullptr },
        { .uri = "/api/setLedOptions", .method = HTTP_POST, .handler = s3_handle_setLedOptions, .user_ctx = nullptr },
        { .uri = "/api/getDisplayOptions", .method = HTTP_GET, .handler = s3_handle_getDisplayOptions, .user_ctx = nullptr },
        { .uri = "/api/setDisplayOptions", .method = HTTP_POST, .handler = s3_handle_setDisplayOptions, .user_ctx = nullptr },
        { .uri = "/api/setPreviewDisplayOptions", .method = HTTP_POST, .handler = s3_handle_setPreviewDisplayOptions, .user_ctx = nullptr },
        { .uri = "/api/getSplashImage", .method = HTTP_GET, .handler = s3_handle_getSplashImage, .user_ctx = nullptr },
        { .uri = "/api/setSplashImage", .method = HTTP_POST, .handler = s3_handle_setSplashImage, .user_ctx = nullptr },
        { .uri = "/api/getAddonsOptions", .method = HTTP_GET, .handler = s3_handle_getAddonsOptions, .user_ctx = nullptr },
        { .uri = "/api/setAddonsOptions", .method = HTTP_POST, .handler = s3_handle_setAddonsOptions, .user_ctx = nullptr },
        { .uri = "/api/getWiiControls", .method = HTTP_GET, .handler = s3_handle_getWiiControls, .user_ctx = nullptr },
        { .uri = "/api/setWiiControls", .method = HTTP_POST, .handler = s3_handle_setWiiControls, .user_ctx = nullptr },
        { .uri = "/api/getMacroAddonOptions", .method = HTTP_GET, .handler = s3_handle_getMacroAddonOptions, .user_ctx = nullptr },
        { .uri = "/api/setMacroAddonOptions", .method = HTTP_POST, .handler = s3_handle_setMacroAddonOptions, .user_ctx = nullptr },
        { .uri = "/api/getPeripheralOptions", .method = HTTP_GET, .handler = s3_handle_getPeripheralOptions, .user_ctx = nullptr },
        { .uri = "/api/setPeripheralOptions", .method = HTTP_POST, .handler = s3_handle_setPeripheralOptions, .user_ctx = nullptr },
        { .uri = "/api/getI2CPeripheralMap", .method = HTTP_GET, .handler = s3_handle_getI2CPeripheralMap, .user_ctx = nullptr },
        { .uri = "/api/getExpansionPins", .method = HTTP_GET, .handler = s3_handle_getExpansionPins, .user_ctx = nullptr },
        { .uri = "/api/setExpansionPins", .method = HTTP_POST, .handler = s3_handle_setExpansionPins, .user_ctx = nullptr },
        { .uri = "/api/getHETriggerCalibrations", .method = HTTP_GET, .handler = s3_handle_getHETriggerCalibrations, .user_ctx = nullptr },
        { .uri = "/api/setHETriggerCalibrations", .method = HTTP_POST, .handler = s3_handle_setHETriggerCalibrations, .user_ctx = nullptr },
        { .uri = "/api/setHETriggerOptions", .method = HTTP_POST, .handler = s3_handle_setHETriggerOptions, .user_ctx = nullptr },
        { .uri = "/api/getHETriggerVoltage", .method = HTTP_POST, .handler = s3_handle_getHETriggerVoltage, .user_ctx = nullptr },
        { .uri = "/api/getReactiveLEDs", .method = HTTP_GET, .handler = s3_handle_getReactiveLEDs, .user_ctx = nullptr },
        { .uri = "/api/setReactiveLEDs", .method = HTTP_POST, .handler = s3_handle_setReactiveLEDs, .user_ctx = nullptr },
        { .uri = "/api/getAnimationProtoOptions", .method = HTTP_GET, .handler = s3_handle_getAnimationProtoOptions, .user_ctx = nullptr },
        { .uri = "/api/setAnimationProtoOptions", .method = HTTP_POST, .handler = s3_handle_setAnimationProtoOptions, .user_ctx = nullptr },
        { .uri = "/api/setAnimationButtonTestMode", .method = HTTP_POST, .handler = s3_handle_setAnimationButtonTestMode, .user_ctx = nullptr },
        { .uri = "/api/setAnimationButtonTestState", .method = HTTP_POST, .handler = s3_handle_setAnimationButtonTestState, .user_ctx = nullptr },
        { .uri = "/api/clearAnimationButtonTestMode", .method = HTTP_POST, .handler = s3_handle_clearAnimationButtonTestMode, .user_ctx = nullptr },
        { .uri = "/api/getLightsDataOptions", .method = HTTP_GET, .handler = s3_handle_getLightsDataOptions, .user_ctx = nullptr },
        { .uri = "/api/setLightsDataOptions", .method = HTTP_POST, .handler = s3_handle_setLightsDataOptions, .user_ctx = nullptr },
        { .uri = "/api/getLightsPresets/0", .method = HTTP_GET, .handler = s3_handle_getLightsPresets0, .user_ctx = nullptr },
        { .uri = "/api/getLightsPresets/1", .method = HTTP_GET, .handler = s3_handle_getLightsPresets1, .user_ctx = nullptr },
        { .uri = "/api/getLightsPresets/2", .method = HTTP_GET, .handler = s3_handle_getLightsPresets2, .user_ctx = nullptr },
        { .uri = "/api/getLightsPresets/3", .method = HTTP_GET, .handler = s3_handle_getLightsPresets3, .user_ctx = nullptr },
        { .uri = "/api/getLightsPresets/4", .method = HTTP_GET, .handler = s3_handle_getLightsPresets4, .user_ctx = nullptr },
        { .uri = "/api/getLightsPresets/5", .method = HTTP_GET, .handler = s3_handle_getLightsPresets5, .user_ctx = nullptr },
        { .uri = "/api/getLightsPresets/6", .method = HTTP_GET, .handler = s3_handle_getLightsPresets6, .user_ctx = nullptr },
        { .uri = "/api/getLightsPresets/7", .method = HTTP_GET, .handler = s3_handle_getLightsPresets7, .user_ctx = nullptr },
        { .uri = "/api/getLightsDataPresets", .method = HTTP_GET, .handler = s3_handle_getLightsDataPresets, .user_ctx = nullptr },
        { .uri = "/api/setLightsToDefault", .method = HTTP_POST, .handler = s3_handle_setLightsToDefault, .user_ctx = nullptr },
        { .uri = "/api/getBoardDefinition", .method = HTTP_GET, .handler = s3_handle_getBoardDefinition, .user_ctx = nullptr },
        { .uri = "/api/getMemoryReport", .method = HTTP_GET, .handler = s3_handle_getMemoryReport, .user_ctx = nullptr },
        { .uri = "/api/getUsedPins", .method = HTTP_GET, .handler = s3_handle_getUsedPins, .user_ctx = nullptr },
        { .uri = "/api/getHeldPins", .method = HTTP_GET, .handler = s3_handle_getHeldPins, .user_ctx = nullptr },
        { .uri = "/api/abortGetHeldPins", .method = HTTP_POST, .handler = s3_handle_abortGetHeldPins, .user_ctx = nullptr },
        { .uri = "/api/getJoystickCenter", .method = HTTP_GET, .handler = s3_handle_getJoystickCenter, .user_ctx = nullptr },
        { .uri = "/api/getJoystickCenter2", .method = HTTP_GET, .handler = s3_handle_getJoystickCenter2, .user_ctx = nullptr },
        { .uri = "/api/setPS4Options", .method = HTTP_POST, .handler = s3_handle_setPS4Options, .user_ctx = nullptr },
        // Catch-all static serving LAST: "/*" matches every GET.
        { .uri = "/*", .method = HTTP_GET, .handler = s3_static_get, .user_ctx = nullptr },
    };
    for (const httpd_uri_t &uri : uris)
    {
        if (httpd_register_uri_handler(s3_httpd, &uri) != ESP_OK)
        {
            ESP_LOGE(S3_WEBCONFIG_TAG, "URI register failed: %s", uri.uri);
            httpd_stop(s3_httpd);
            s3_httpd = nullptr;
            esp_vfs_spiffs_unregister("www");
            return;
        }
    }
    ESP_LOGI(S3_WEBCONFIG_TAG, "webconfig HTTP backend started");
}

void stopWebconfigServer()
{
    if (s3_httpd != nullptr)
    {
        httpd_stop(s3_httpd);
        s3_httpd = nullptr;
    }
    esp_vfs_spiffs_unregister("www");
}

#endif // defined(ESP_PLATFORM)
