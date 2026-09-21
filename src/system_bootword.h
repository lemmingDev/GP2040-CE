#pragma once
#include <stdint.h>
#include "system.h"
uint32_t packBootWord(System::BootMode mode);
System::BootMode unpackBootWord(uint32_t word);
