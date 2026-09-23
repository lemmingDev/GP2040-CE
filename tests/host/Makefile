CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Werror -I../../headers/drivers/bluetooth

test: test_ble_report test_bootword test_wifiscan test_pinpolicy test_usbnet test_usbdesc
	./test_ble_report
	./test_bootword
	./test_wifiscan
	./test_pinpolicy
	./test_usbnet
	./test_usbdesc

test_ble_report: test_ble_report.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_bootword: test_bootword.cpp ../../src/system_bootword.cpp
	$(CXX) $(CXXFLAGS) -I../../headers -I../../src -o $@ $^

test_wifiscan: test_wifiscan.cpp
	$(CXX) $(CXXFLAGS) -I../../src -o $@ $<

# BoardConfig.h pulls class/hid/hid.h (needs CFG_TUSB_MCU, normally set by
# the firmware build); define the S3 MCU so the header stays includable here.
test_pinpolicy: test_pinpolicy.cpp
	$(CXX) $(CXXFLAGS) -I../../headers -I../../src -I../../esp32-s3/generated -I../../lib/nanopb -I../../lib/tinyusb/src -I../../configs/ESP32S3DevKitC1 -DCFG_TUSB_MCU=OPT_MCU_ESP32S3 -o $@ $<

test_usbnet: test_usbnet.cpp ../../src/usbnet_s3.cpp
	$(CXX) $(CXXFLAGS) -I../../src -o $@ $^

# S3 USB-webconfig Task 6: HID-family RNDIS descriptor byte tests. Mirror the
# test_pinpolicy rule (TinyUSB headers need CFG_TUSB_MCU; tusb_config.h comes
# from ../../headers). HIDDescriptors.h must be included first in the TU (see
# test_usbdesc.cpp header note); that is a source-order property, not a flag.
# CFG_TUSB_OS=OPT_OS_NONE: headers/tusb_config.h falls back to OPT_OS_PICO on
# non-firmware platforms, whose osal_pico.h needs the Pico SDK; the None OSAL
# is plain C and sufficient for descriptor byte checks.
test_usbdesc: test_usbdesc.cpp
	$(CXX) $(CXXFLAGS) -I../../headers -I../../src -I../../esp32-s3/generated -I../../lib/nanopb -I../../lib/tinyusb/src -I../../configs/ESP32S3DevKitC1 -DCFG_TUSB_MCU=OPT_MCU_ESP32S3 -DCFG_TUSB_OS=OPT_OS_NONE -o $@ $<

clean:
	rm -f test_ble_report test_bootword test_wifiscan test_pinpolicy test_usbnet test_usbdesc

.PHONY: test clean
