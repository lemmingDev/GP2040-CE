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

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_system.h"

#include <ArduinoJson.h>

#include "config.pb.h"
#include "config_utils.h"
#include "storagemanager.h"
#include "system.h"
#include "eventmanager.h"
#include "GPRestartEvent.h"
#include "version.h" // esp32-s3/generated/version.h (S3 include dir, not Pico's)

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

// ---- Server lifecycle (Task 7 wires the start/stop calls) ----

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
