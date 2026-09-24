#!/bin/sh
set -e
CXX="${CXX:-g++}"
FLAGS="-std=c++17 -Wall -Werror -fno-exceptions -fno-rtti -I extras/gp-link -I lib/CRC32/src"
$CXX $FLAGS extras/gp-link/tests/test_gplink_codec.cpp extras/gp-link/gplink.cpp lib/CRC32/src/CRC32.cpp -o /tmp/test_gplink_codec
/tmp/test_gplink_codec
$CXX $FLAGS extras/gp-link/tests/test_gplink_messages.cpp extras/gp-link/gplink.cpp lib/CRC32/src/CRC32.cpp -o /tmp/test_gplink_messages
/tmp/test_gplink_messages
