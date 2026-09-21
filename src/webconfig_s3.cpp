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
//                                POST /api/resetSettings
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
