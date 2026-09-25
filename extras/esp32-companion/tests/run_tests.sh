#!/bin/sh
set -e
CXX="${CXX:-g++}"
FLAGS="-std=c++17 -Wall -Werror -fno-exceptions -fno-rtti -I extras/gp-link -I extras/esp32-companion/src -I lib/CRC32/src"
$CXX $FLAGS extras/esp32-companion/tests/test_companion_pins.cpp extras/esp32-companion/src/companion_pins.cpp extras/gp-link/gplink.cpp lib/CRC32/src/CRC32.cpp -o /tmp/test_companion_pins
/tmp/test_companion_pins
$CXX $FLAGS -DCOMPANION_BOARD_R32 extras/esp32-companion/tests/test_companion_pins.cpp extras/esp32-companion/src/companion_pins.cpp extras/gp-link/gplink.cpp lib/CRC32/src/CRC32.cpp -o /tmp/test_companion_pins_r32
/tmp/test_companion_pins_r32
