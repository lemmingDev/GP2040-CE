CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Werror -I../../headers/drivers/bluetooth

test: test_ble_report test_bootword test_wifiscan
	./test_ble_report
	./test_bootword
	./test_wifiscan

test_ble_report: test_ble_report.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_bootword: test_bootword.cpp ../../src/system_bootword.cpp
	$(CXX) $(CXXFLAGS) -I../../headers -I../../src -o $@ $^

test_wifiscan: test_wifiscan.cpp
	$(CXX) $(CXXFLAGS) -I../../src -o $@ $<

clean:
	rm -f test_ble_report test_bootword test_wifiscan

.PHONY: test clean
