CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Werror -I../../headers/drivers/bluetooth

test: test_ble_report
	./test_ble_report

test_ble_report: test_ble_report.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f test_ble_report

.PHONY: test clean
