#include "system_bootword.h"
static const uint32_t BOOTWORD_MAGIC = 0xB007C0DE;
uint32_t packBootWord(System::BootMode mode) { return BOOTWORD_MAGIC ^ (uint32_t)mode; }
System::BootMode unpackBootWord(uint32_t word) {
    uint32_t mode = word ^ BOOTWORD_MAGIC;
    if (mode == (uint32_t)System::BootMode::GAMEPAD) return System::BootMode::GAMEPAD;
    if (mode == (uint32_t)System::BootMode::WEBCONFIG) return System::BootMode::WEBCONFIG;
    if (mode == (uint32_t)System::BootMode::USB) return System::BootMode::USB;
    return System::BootMode::DEFAULT;
}
