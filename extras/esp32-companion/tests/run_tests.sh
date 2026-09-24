#!/bin/sh
set -e
CXX="${CXX:-g++}"
FLAGS="-std=c++17 -Wall -Werror -fno-exceptions -fno-rtti -I extras/gp-link -I extras/esp32-companion/src"
$CXX $FLAGS extras/esp32-companion/tests/test_companion_pins.cpp extras/esp32-companion/src/companion_pins.cpp -o /tmp/test_companion_pins
/tmp/test_companion_pins
