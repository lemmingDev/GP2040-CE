#include "drivers/bluetooth/BluetoothDriver.h"
#include "gamepad.h"
#include <cstring>

#if defined(ESP_PLATFORM)
// TBD-Task8-verify: confirm each of these NimBLE include paths exists in the
// ESP-IDF 5.x NimBLE component at the first `idf.py build` on S3 hardware.
#include "nimble/nimble_port.h" // TBD-Task8-verify: header path + nimble_port_init/_run
#include "nimble/nimble_port_freertos.h" // TBD-Task8-verify: header path + nimble_port_freertos_init
#include "host/ble_hs.h" // TBD-Task8-verify: header path + ble_hs_cfg/ble_hs_mbuf_from_flat
#include "host/ble_gap.h" // TBD-Task8-verify: header path + ble_gap_* + BLE_GAP_EVENT_* ids
#include "services/gap/ble_svc_gap.h" // TBD-Task8-verify: header path + ble_svc_gap_device_name_set
#include "services/gatt/ble_svc_gatt.h" // TBD-Task8-verify: header path (GATT service housekeeping)
#include "store/config/ble_store_config.h" // TBD-Task8-verify: header path + ble_store_config_init/_util_delete

// HOGP report map mirroring BleGamepadReport (11 buttons + LT/RT + 4 axes).
// Report layout produced: 11 button bits + 5-bit pad (2 B), LT, RT (2 B),
// leftX, leftY, rightX, rightY as little-endian int16 (8 B) = 12 bytes total,
// i.e. sizeof(BleGamepadReport) with no ID prefix (single-report device).
// Usage-page/style follows headers/drivers/hid/HIDDescriptors.h; triggers use
// Generic Desktop Z/Rz and sticks X/Y/Rx/Ry so the descriptor stays on one page.
static const uint8_t bleHidReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Gamepad)
    0xA1, 0x01,        // Collection (Application)
    // 11 buttons: A,B,X,Y,LB,RB,Back,Start,LS,RS,Guide
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x0B,        //   Usage Maximum (Button 11)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x95, 0x0B,        //   Report Count (11)
    0x75, 0x01,        //   Report Size (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    // 5-bit pad to a byte boundary
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x05,        //   Report Size (5)
    0x81, 0x01,        //   Input (Cnst,Ary,Abs)
    // LT/RT analog triggers: 2 x 8-bit, 0..255
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x32,        //   Usage (Z: left trigger)
    0x09, 0x35,        //   Usage (Rz: right trigger)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    // sticks: 4 x 16-bit, -32768..32767
    0x09, 0x30,        //   Usage (X: left X)
    0x09, 0x31,        //   Usage (Y: left Y)
    0x09, 0x33,        //   Usage (Rx: right X)
    0x09, 0x34,        //   Usage (Ry: right Y)
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0xC0               // End Collection
};

// HID Information value: bcdHID 1.11, country 0, flags.
// TBD-Task8-verify: flags choice (0x02 = NormallyConnectable per HID spec
// 7.11.8); confirm the peer enumerates the gamepad with this value.
static const uint8_t bleHidInfo[4] = { 0x11, 0x01, 0x00, 0x02 };
// Report Reference descriptor body for the single input report: ID 0 (no ID
// prefix on the wire), type Input.
static const uint8_t bleHidReportRef[2] = { 0x00, 0x01 }; // TBD-Task8-verify: Report Reference layout accepted by hosts

static uint16_t bleConnHandle = 0xFFFF; // TBD-Task8-verify: BLE_HS_CONN_HANDLE_NONE value/name
static uint16_t bleInputReportHandle = 0;
static bool bleInputNotify = false;

static int bleHidGattAccess(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg); // TBD-Task8-verify: ble_gatt_access_ctxt shape + access-cb signature
static int bleHidGapEvent(struct ble_gap_event *event, void *arg); // TBD-Task8-verify: ble_gap_event shape + handler signature

// HIDS (0x1812): HID Information + Report Map + Control Point + one input
// Report (Report Reference descriptor; CCCD auto-added for NOTIFY).
// Deliberately HIDS-only (no BAS/DIS yet — YAGNI for Phase 0; add if hosts
// refuse to enumerate without them).
static const struct ble_gatt_svc_def bleHidSvcs[] = { // TBD-Task8-verify: ble_gatt_svc_def field names/order
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY, // TBD-Task8-verify: enum name
        .uuid = BLE_UUID16_DECLARE(0x1812), // TBD-Task8-verify: HIDS UUID macro name
        .characteristics = (struct ble_gatt_chr_def[]) { // TBD-Task8-verify: chr table field + def struct name
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4A), // TBD-Task8-verify: HID Information UUID + macro
                .access_cb = bleHidGattAccess, // TBD-Task8-verify: access_cb field
                .arg = (void *)0x2A4A, // tag: which characteristic (compared in access cb)
                .flags = BLE_GATT_CHR_F_READ, // TBD-Task8-verify: flag name
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4B), // TBD-Task8-verify: Report Map UUID
                .access_cb = bleHidGattAccess, // TBD-Task8-verify
                .arg = (void *)0x2A4B,
                .flags = BLE_GATT_CHR_F_READ, // TBD-Task8-verify
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4C), // TBD-Task8-verify: HID Control Point UUID
                .access_cb = bleHidGattAccess, // TBD-Task8-verify
                .arg = (void *)0x2A4C,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP, // TBD-Task8-verify: flag name
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D), // TBD-Task8-verify: Report UUID
                .access_cb = bleHidGattAccess, // TBD-Task8-verify
                .arg = (void *)0x2A4D,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, // TBD-Task8-verify: flags + auto-CCCD behavior
                .val_handle = &bleInputReportHandle, // TBD-Task8-verify: val_handle back-pointer field
                .descriptors = (struct ble_gatt_dsc_def[]) { // TBD-Task8-verify: descriptor table field + struct name
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908), // TBD-Task8-verify: Report Reference UUID
                        .att_flags = BLE_GATT_DSC_F_READ, // TBD-Task8-verify: dsc flags field/name
                        .access_cb = bleHidGattAccess, // TBD-Task8-verify
                        .arg = (void *)0x2908,
                    },
                    { 0 }, // TBD-Task8-verify: terminator convention
                },
            },
            { 0 }, // TBD-Task8-verify: characteristic terminator convention
        },
    },
    { 0 }, // TBD-Task8-verify: service terminator convention
};

static int bleHidGattAccess(uint16_t conn_handle, uint16_t attr_handle, // TBD-Task8-verify: signature + ctxt->op constants
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle;
    uint16_t tag = (uint16_t)(uintptr_t)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) { // TBD-Task8-verify: op constant
        if (tag == 0x2A4A) {
            int rc = os_mbuf_append(ctxt->om, bleHidInfo, sizeof(bleHidInfo)); // TBD-Task8-verify: ctxt->om + os_mbuf_append
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES; // TBD-Task8-verify: ATT error constant
        }
        if (tag == 0x2A4B) {
            int rc = os_mbuf_append(ctxt->om, bleHidReportMap, sizeof(bleHidReportMap)); // TBD-Task8-verify
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES; // TBD-Task8-verify
        }
        if (tag == 0x2908) {
            int rc = os_mbuf_append(ctxt->om, bleHidReportRef, sizeof(bleHidReportRef)); // TBD-Task8-verify
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES; // TBD-Task8-verify
        }
        return BLE_ATT_ERR_UNLIKELY; // TBD-Task8-verify: error constant for input-report read (host reads lastSent? keep minimal: reject)
    }
    // Writes (Control Point suspend/exit-suspend): accept and ignore (no
    // suspend behavior in Phase 0).
    return 0;
}

static void bleHidStartAdvertising() {
    struct ble_hs_adv_fields fields = {}; // TBD-Task8-verify: adv-fields struct + zero-init pattern
    static const ble_uuid16_t hidUuid = BLE_UUID16_INIT(0x1812); // TBD-Task8-verify: uuid type + INIT macro
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP; // TBD-Task8-verify: flag names
    fields.uuids16 = (ble_uuid16_t *)&hidUuid; // TBD-Task8-verify: uuid list field type
    fields.num_uuids16 = 1; // TBD-Task8-verify: count field
    fields.uuids16_is_complete = 1; // TBD-Task8-verify: completeness flag field
    fields.appearance = 0x03C4; // TBD-Task8-verify: appearance field (0x03C4 = HID Gamepad)
    fields.appearance_is_present = 1; // TBD-Task8-verify: presence flag field
    int rc = ble_gap_adv_set_fields(&fields); // TBD-Task8-verify: fn name/signature
    if (rc != 0) { return; }
    // Scan response carries the complete local name ("GP2040-CE-BLE", set via
    // ble_svc_gap_device_name_set in initialize() before sync starts adv).
    struct ble_hs_adv_fields rsp = {}; // TBD-Task8-verify: scan-rsp fields struct
    const char *devName = ble_svc_gap_device_name(); // TBD-Task8-verify: GAP-name getter name/constness
    rsp.name = (uint8_t *)devName; // TBD-Task8-verify: name field type
    rsp.name_len = (uint8_t)strlen(devName); // TBD-Task8-verify: length field/type
    rsp.name_is_complete = 1; // TBD-Task8-verify: completeness flag field
    rc = ble_gap_adv_rsp_set_fields(&rsp); // TBD-Task8-verify: scan-rsp setter name/signature
    if (rc != 0) { return; }
    struct ble_gap_adv_params params = {}; // TBD-Task8-verify: adv-params struct
    params.conn_mode = BLE_GAP_CONN_MODE_UND; // TBD-Task8-verify: connectable-undirected constant
    params.disc_mode = BLE_GAP_DISC_MODE_GEN; // TBD-Task8-verify: general-discoverable constant
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, // TBD-Task8-verify: addr type/forever constants + signature
                           &params, bleHidGapEvent, NULL); // TBD-Task8-verify: gap-event-cb param position
    (void)rc;
}

static int bleHidGapEvent(struct ble_gap_event *event, void *arg) { // TBD-Task8-verify: event struct + type ids
    (void)arg;
    switch (event->type) { // TBD-Task8-verify: type field + case constant names
    case BLE_GAP_EVENT_CONNECT: // TBD-Task8-verify
        if (event->connect.status == 0) { // TBD-Task8-verify: connect status/conn_handle fields
            bleConnHandle = event->connect.conn_handle; // TBD-Task8-verify
        } else {
            bleHidStartAdvertising(); // retry advertising when connect failed
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT: // TBD-Task8-verify
        bleConnHandle = 0xFFFF; // TBD-Task8-verify: NONE handle value (see static init)
        bleInputNotify = false;
        bleHidStartAdvertising(); // re-arm provisioning advertising
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE: // TBD-Task8-verify: subscribe event + field names
        if (event->subscribe.attr_handle == bleInputReportHandle) { // TBD-Task8-verify
            bleInputNotify = event->subscribe.cur_notify; // TBD-Task8-verify: cur_notify field
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: // TBD-Task8-verify: repeat-pairing event id
        // Peer deleted its bond: drop ours and retry pairing.
        ble_store_util_delete(event->repeat_pairing.conn_handle, NULL); // TBD-Task8-verify: fn name/signature
        return BLE_GAP_REPEAT_PAIRING_RETRY; // TBD-Task8-verify: retry constant
    default:
        return 0;
    }
}

static void bleHidOnReset(int reason) { // TBD-Task8-verify: reset-cb signature
    (void)reason;
}

static void bleHidOnSync() { // TBD-Task8-verify: sync-cb signature
    ble_hs_id_infer_auto(0, NULL); // TBD-Task8-verify: addr setup call
    bleHidStartAdvertising();
}

static void bleHidHostTask(void *param) { // TBD-Task8-verify: FreeRTOS host-task shape
    (void)param;
    nimble_port_run(); // TBD-Task8-verify: run call (returns only on stop)
}

#endif // ESP_PLATFORM

void BluetoothDriver::initialize() {
    lastReport = {};
    bleConnected = false;
#if defined(ESP_PLATFORM)
    nimble_port_init(); // TBD-Task8-verify: init call (IDF: esp_nimble_init vs nimble_port_init)
    ble_hs_cfg.reset_cb = bleHidOnReset; // TBD-Task8-verify: cfg field names
    ble_hs_cfg.sync_cb = bleHidOnSync; // TBD-Task8-verify
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr; // TBD-Task8-verify: store-status cb name
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO; // TBD-Task8-verify: NoIO pairing constant
    ble_hs_cfg.sm_bonding = 1; // TBD-Task8-verify: bonding cfg field
    ble_hs_cfg.sm_mitm = 0; // TBD-Task8-verify: MITM cfg field (gamepad: Just Works, no MITM)
    ble_hs_cfg.sm_sc = 1; // TBD-Task8-verify: Secure Connections cfg field
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC; // TBD-Task8-verify: key-dist constants
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC; // TBD-Task8-verify
    ble_store_config_init(); // TBD-Task8-verify: NVS-backed store init (needs CONFIG_BT_NIMBLE_NVS_PERSIST=y)
    int rc = ble_gatts_count_cfg(bleHidSvcs); // TBD-Task8-verify: count-cfg call
    if (rc == 0) { rc = ble_gatts_add_svcs(bleHidSvcs); } // TBD-Task8-verify: add-svcs call
    (void)rc;
    ble_svc_gap_device_name_set("GP2040-CE-BLE"); // TBD-Task8-verify: GAP name call (name in adv/scan-rsp)
    nimble_port_freertos_init(bleHidHostTask); // TBD-Task8-verify: host-task spawn call
#else
    // Non-ESP builds (Pico firmware without BLE controller / host checks):
    // transport stays down; process() still stages lastReport/lastSent.
#endif
}

bool BluetoothDriver::process(Gamepad * gamepad) {
    BleGamepadReport r = buildBleGamepadReport(
        static_cast<uint16_t>(gamepad->state.buttons & 0x07FFu),
        gamepad->state.lt, gamepad->state.rt,
        bleAxisFromRaw(gamepad->state.lx), bleAxisFromRaw(gamepad->state.ly),
        bleAxisFromRaw(gamepad->state.rx), bleAxisFromRaw(gamepad->state.ry));
    lastReport = r;
    return pushReport(r);
}

uint16_t BluetoothDriver::get_report(uint8_t report_id, hid_report_type_t report_type,
                                     uint8_t *buffer, uint16_t reqlen) {
    (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

bool BluetoothDriver::pushReport(const BleGamepadReport & report) {
#if defined(ESP_PLATFORM)
    // TBD-Task8-verify: `ble_gatts_notify_custom` server-notify signature and
    // the `ble_hs_mbuf_from_flat` mbuf pattern on ESP-IDF 5.x NimBLE.
    // Connection state lives in the TU-static GAP state above (the static GAP
    // handler cannot reach this member); mirror it here so `bleConnected`
    // stays a truthful connection flag for callers/debuggers.
    bleConnected = (bleConnHandle != 0xFFFF);
    if (!bleConnected || !bleInputNotify) { return false; } // no peer / CCCD off: stay silent
    if (memcmp(&report, &lastSent, sizeof(report)) == 0) { return false; } // change-only: ~133 Hz BLE ceiling
    lastSent = report;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&report, sizeof(report)); // TBD-Task8-verify
    if (om == NULL) { return false; }
    int rc = ble_gatts_notify_custom(bleConnHandle, bleInputReportHandle, om); // TBD-Task8-verify
    (void)rc;
    // Note: lastSent is updated before notify; a failed notify drops that
    // delta (next input change re-syncs). Accepted for Phase 0.
    return true;
#else
    if (memcmp(&report, &lastSent, sizeof(report)) == 0) { return false; }
    lastSent = report;
    return true;
#endif
}
