CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Werror -I../../headers/drivers/bluetooth

test: test_ble_report test_bootword test_wifiscan test_pinpolicy test_usbnet
	./test_ble_report
	./test_bootword
	./test_wifiscan
	./test_pinpolicy
	./test_usbnet

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

clean:
	rm -f test_ble_report test_bootword test_wifiscan test_pinpolicy test_usbnet

.PHONY: test clean
