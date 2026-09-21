#include <cassert>
#include <cstdio>
#include "system_bootword.h"
int main() {
    assert(unpackBootWord(packBootWord(System::BootMode::WEBCONFIG)) == System::BootMode::WEBCONFIG);
    assert(unpackBootWord(packBootWord(System::BootMode::GAMEPAD)) == System::BootMode::GAMEPAD);
    assert(unpackBootWord(0x00000000) == System::BootMode::DEFAULT);
    assert(unpackBootWord(0xdeadbeef) == System::BootMode::DEFAULT);
    printf("bootword: all assertions passed\n");
    return 0;
}
