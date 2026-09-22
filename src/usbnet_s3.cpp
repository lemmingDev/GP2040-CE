#include "usbnet_s3.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

bool parsePrivateSubnet24(const char *s) {
    if (s == nullptr) {
        return false;
    }
    int octets[4] = {0, 0, 0, 0};
    int end = 0;
    if (std::sscanf(s, "%d.%d.%d.%d%n",
                    &octets[0], &octets[1], &octets[2], &octets[3], &end) != 4) {
        return false;
    }
    if (s[end] != '\0') {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (octets[i] < 0 || octets[i] > 255) {
            return false;
        }
    }
    if (octets[3] != 0) {
        return false;  // host bits must be zero (/24 network address)
    }
    if (octets[0] == 10) {
        return true;
    }
    if (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) {
        return true;
    }
    if (octets[0] == 192 && octets[1] == 168) {
        return true;
    }
    return false;
}

}  // namespace

bool s3_validateSubnets(const char *apSubnet, const char *usbSubnet) {
    if (apSubnet == nullptr || usbSubnet == nullptr) {
        return false;
    }
    if (!parsePrivateSubnet24(apSubnet) || !parsePrivateSubnet24(usbSubnet)) {
        return false;
    }
    return std::strcmp(apSubnet, usbSubnet) != 0;
}
