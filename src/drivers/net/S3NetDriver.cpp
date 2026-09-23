#if defined(ESP_PLATFORM)

#include "drivers/net/S3NetDriver.h"
#include "drivers/net/S3NetDriverDescriptors.h"
#include "class/net/net_device.h"
#include "storagemanager.h"

static uint16_t _s3_desc_str[32];

void S3NetDriver::initialize() {
	class_driver = {
		.init             = netd_init,
		.reset            = netd_reset,
		.open             = netd_open,
		.control_xfer_cb  = netd_control_xfer_cb,
		.xfer_cb          = netd_xfer_cb,
		.sof              = NULL,
	};
}

// No pump needed on S3: TinyUSB runs in the USB task and esp_netif is
// timer/callback driven (contrast Pico, which runs rndis_task() here).
bool S3NetDriver::process(Gamepad * gamepad) {
	(void) gamepad;
	return false;
}

uint16_t S3NetDriver::get_report(uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
	(void) report_id; (void) report_type; (void) buffer; (void) reqlen;
	return 0;
}

void S3NetDriver::set_report(uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
	(void) report_id; (void) report_type; (void) buffer; (void) bufsize;
}

bool S3NetDriver::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
	(void) rhport; (void) stage; (void) request;
	return false;
}

const uint16_t * S3NetDriver::get_descriptor_string_cb(uint8_t index, uint16_t langid) {
	(void) langid;
	unsigned int chr_count = 0;
	if (S3_STRID_LANGID == index) {
		memcpy(&_s3_desc_str[1], "\x09\x04", 2);
		chr_count = 1;
	}
	else if (S3_STRID_MAC == index) {
		// Convert MAC address into UTF-16
		for (unsigned i = 0; i < sizeof(tud_network_mac_address); i++)
		{
			_s3_desc_str[1+chr_count++] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 4) & 0xf];
			_s3_desc_str[1+chr_count++] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 0) & 0xf];
		}
	} else {
		// Positional (not designated) initializer: Xtensa GCC rejects
		// array designators here; order matches S3_STRID_* 0..4.
		static char const* const string_desc_arr[] =
		{
			"",                             // 0 LANGID (handled above)
			"TinyUSB",                      // 1 MANUFACTURER
			"TinyUSB Device",               // 2 PRODUCT
			"123456",                       // 3 SERIAL
			"TinyUSB Network Interface",    // 4 INTERFACE
		};
		if (index >= sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) return NULL;
		const char* str = string_desc_arr[index];

		// Cap at max char
		chr_count = (uint8_t) strlen(str);
		if ( chr_count > 31 ) chr_count = 31;

		// Convert ASCII string into UTF-16
		for (unsigned int i = 0; i < chr_count; i++)
		{
			_s3_desc_str[1+i] = str[i];
		}
	}

	// first byte is length (including header), second byte is string type
	_s3_desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8 ) | (2*chr_count + 2));

	return _s3_desc_str;
}

const uint8_t * S3NetDriver::get_descriptor_device_cb() {
	return s3net_device_descriptor;
}

const uint8_t * S3NetDriver::get_hid_descriptor_report_cb(uint8_t itf) {
	(void) itf;
	return nullptr;
}

const uint8_t * S3NetDriver::get_descriptor_configuration_cb(uint8_t index) {
	(void) index;
	return s3net_configuration_descriptor;
}

const uint8_t * S3NetDriver::get_descriptor_device_qualifier_cb() {
	return nullptr;
}

uint16_t S3NetDriver::GetJoystickMidValue() {
	return GAMEPAD_JOYSTICK_MID;
}

#endif // defined(ESP_PLATFORM)
